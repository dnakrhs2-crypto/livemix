#include "ui/LoudnessWindow.h"

#include "Widgets.h"

#include <cmath>
#include <limits>
#include <vector>

namespace gocue::livemix
{

namespace
{
    struct Target
    {
        double lufs;
        const char* label;
    };

    // the platforms' normalisation levels and the broadcast standards; the first is the default
    const Target targets[] = {
        { -14.0, "-14 LUFS   \xEC\x9C\xA0\xED\x8A\x9C\xEB\xB8\x8C \xC2\xB7 \xEC\x8A\xA4\xED\x8F\xAC\xED\x8B\xB0\xED\x8C\x8C\xEC\x9D\xB4" },        // 유튜브 · 스포티파이
        { -16.0, "-16 LUFS   \xEC\x95\xA0\xED\x94\x8C \xEB\xAE\xA4\xEC\xA7\x81 \xC2\xB7 \xED\x8C\x9F\xEC\xBA\x90\xEC\x8A\xA4\xED\x8A\xB8" },   // 애플 뮤직 · 팟캐스트
        { -18.0, "-18 LUFS" },
        { -20.0, "-20 LUFS" },
        { -23.0, "-23 LUFS   \xEB\xB0\xA9\xEC\x86\xA1 (EBU R128)" },                                                                       // 방송 (EBU R128)
        { -24.0, "-24 LUFS   \xEB\xB0\xA9\xEC\x86\xA1 (ATSC A/85)" },                                                                      // 방송 (ATSC A/85)
    };

    constexpr double scaleMin = -60.0, scaleMax = 0.0;   // the bar and the graph
    constexpr int historyLength = 1800;                 // three minutes of short-term values, one every 100 ms
    constexpr double truePeakCeiling = -1.0;            // dBTP: the usual limit (EBU R128, the platforms)
    const float nothing = std::numeric_limits<float>::quiet_NaN();

    juce::String valueText (const LoudnessValue& v, int decimals = 1)
    {
        return v.valid ? juce::String (v.value, decimals) : juce::String ("-");
    }

    juce::String signedText (double value)
    {
        return (value >= 0.0 ? "+" : "") + juce::String (value, 1);
    }
}

//==============================================================================
class LoudnessWindow::Content : public juce::Component,
                                private juce::Timer
{
public:
    Content (MixEngine& e, LiveMixSettings& s) : engine (e), settings (s)
    {
        history.assign ((size_t) historyLength, nothing);

        resetButton.setButtonText (ko ("초기화"));
        resetButton.setTooltip (ko ("통합 라우드니스, 범위, 최대값, 트루 피크를 지금부터 다시 잽니다"));
        resetButton.setWantsKeyboardFocus (false);
        resetButton.setColour (juce::TextButton::buttonColourId, Palette::accent);
        resetButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        resetButton.onClick = [this]
        {
            engine.getLoudnessMeter().reset();
            clearHistory();
            repaint();
        };
        addAndMakeVisible (resetButton);

        styleCaption (targetCaption, ko ("목표"));
        targetCaption.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (targetCaption);

        for (size_t i = 0; i < std::size (targets); ++i)
            targetCombo.addItem (juce::String::fromUTF8 (targets[i].label), (int) i + 1);

        targetCombo.setWantsKeyboardFocus (false);
        selectTarget (settings.getLufsTarget());
        targetCombo.onChange = [this]
        {
            const int id = targetCombo.getSelectedId();

            if (id >= 1 && id <= (int) std::size (targets))
            {
                target = targets[(size_t) id - 1].lufs;
                settings.setLufsTarget (target);
                repaint();
            }
        };
        addAndMakeVisible (targetCombo);

        onTopToggle.setButtonText (ko ("항상 위"));
        onTopToggle.setTooltip (ko ("이 창을 다른 창 위에 둡니다 (방송 화면 옆에 두고 볼 때)"));
        onTopToggle.setWantsKeyboardFocus (false);
        onTopToggle.setToggleState (settings.getLufsAlwaysOnTop(), juce::dontSendNotification);
        onTopToggle.onClick = [this]
        {
            settings.setLufsAlwaysOnTop (onTopToggle.getToggleState());

            if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
                window->setAlwaysOnTop (onTopToggle.getToggleState());
        };
        addAndMakeVisible (onTopToggle);

        setSize (780, 580);
        startTimerHz (10);
    }

    ~Content() override
    {
        stopTimer();
    }

    bool isAlwaysOnTopWanted() const { return onTopToggle.getToggleState(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20, 14);
        auto top = area.removeFromTop (30);
        resetButton.setBounds (top.removeFromRight (96));
        top.removeFromRight (10);
        onTopToggle.setBounds (top.removeFromRight (96));
        top.removeFromRight (6);
        targetCombo.setBounds (top.removeFromRight (juce::jlimit (150, 300, top.getWidth() / 2)));
        top.removeFromRight (6);
        targetCaption.setBounds (top.removeFromRight (40));
        headerArea = top;
        area.removeFromTop (4);
        noteArea = area.removeFromTop (18);
        area.removeFromTop (10);

        const int bigH = juce::jlimit (96, 130, area.getHeight() / 4);
        tilesArea = area.removeFromTop (bigH);
        area.removeFromTop (10);
        smallArea = area.removeFromTop (66);
        area.removeFromTop (12);
        barArea = area.removeFromTop (66);   // the caption row with the legend, the bar, the scale
        area.removeFromTop (14);
        graphArea = area;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::background);

        const auto& meter = engine.getLoudnessMeter();
        const auto& st = meter.getStats();
        const auto m = st.momentary(), s = st.shortTerm(), i = st.integrated(), lra = st.loudnessRange(), tp = meter.truePeakMax();

        paintHeader (g, st, meter);
        paintTiles (g, m, s, i);
        paintSmallTiles (g, i, lra, tp, st);
        paintBar (g, m, s, i);
        paintGraph (g, i);
    }

private:
    void timerCallback() override
    {
        const auto& st = engine.getLoudnessMeter().getStats();   // polled by the main window's timer
        const auto blocks = st.getSubBlockCount();

        if (blocks < lastBlocks)   // a reset
            clearHistory();

        if (blocks != lastBlocks)
        {
            const auto s = st.shortTerm();
            history[(size_t) historyPos] = s.valid ? (float) s.value : nothing;
            historyPos = (historyPos + 1) % historyLength;
            lastBlocks = blocks;
        }

        repaint();
    }

    void clearHistory()
    {
        std::fill (history.begin(), history.end(), nothing);
        historyPos = 0;
        lastBlocks = 0;
    }

    void selectTarget (double lufs)
    {
        int id = 1;

        for (size_t k = 0; k < std::size (targets); ++k)
            if (std::abs (targets[k].lufs - lufs) < 0.01)
                id = (int) k + 1;

        target = targets[(size_t) id - 1].lufs;
        targetCombo.setSelectedId (id, juce::dontSendNotification);
    }

    /** Green within 1 LU of the target, red above it (too loud), yellow below (too quiet); dim without a value. */
    juce::Colour colourFor (const LoudnessValue& v) const
    {
        if (! v.valid)
            return Palette::dimText;

        const double d = v.value - target;
        return d > 1.0 ? Palette::danger : d < -1.0 ? Palette::meterYellow : Palette::lampOn;
    }

    static float xFor (juce::Rectangle<float> r, double lufs)
    {
        return r.getX() + r.getWidth() * (float) juce::jlimit (0.0, 1.0, (lufs - scaleMin) / (scaleMax - scaleMin));
    }

    static float yFor (juce::Rectangle<float> r, double lufs)
    {
        return r.getY() + r.getHeight() * (float) juce::jlimit (0.0, 1.0, (scaleMax - lufs) / (scaleMax - scaleMin));
    }

    void paintHeader (juce::Graphics& g, const LoudnessStats& st, const LoudnessMeter& meter)
    {
        g.setColour (Palette::text);
        g.setFont (titleFont (16.0f));
        g.drawText (ko ("마스터 출력 라우드니스"), headerArea, juce::Justification::centredLeft, true);
        g.setColour (Palette::dimText);
        g.setFont (bodyFont (12.5f));
        juce::String note = ko ("측정 시간 ") + formatTimeMs (st.elapsedSeconds(), false) + "   " + ko ("(초기화 후)") + "   " + ko ("ITU-R BS.1770-4 / EBU R128");

        if (! engine.isDeviceRunning())
            note += "   " + ko ("- 오디오 멈춤: 장치가 돌면 잽니다");

        if (meter.getDroppedSubBlocks() > 0)
            note += "   " + ko ("- 끊긴 구간 있음");

        g.drawText (note, noteArea, juce::Justification::centredLeft, true);
    }

    void paintTile (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& caption, const juce::String& value, const juce::String& unit,
                    juce::Colour valueColour, const juce::String& sub, float valuePt)
    {
        g.setColour (Palette::card);
        g.fillRoundedRectangle (r.toFloat(), Palette::controlRadius);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), Palette::controlRadius, 1.0f);

        auto inner = r.reduced (14, 8);
        g.setColour (Palette::dimText);
        g.setFont (captionFont());
        g.drawText (caption, inner.removeFromTop (18), juce::Justification::centredLeft, true);

        if (sub.isNotEmpty())
        {
            g.setFont (bodyFont (12.0f));
            g.drawText (sub, inner.removeFromBottom (16), juce::Justification::centredLeft, true);
        }

        const juce::Font valueFont (juce::FontOptions (pt (valuePt), juce::Font::bold));
        g.setFont (valueFont);
        g.setColour (valueColour);
        const int valueW = juce::jmin (inner.getWidth(), (int) std::ceil (juce::GlyphArrangement::getStringWidth (valueFont, value)) + 2);
        g.drawText (value, inner.removeFromLeft (valueW), juce::Justification::centredLeft, false);

        if (unit.isNotEmpty() && inner.getWidth() > 24)
        {
            inner.removeFromLeft (6);
            g.setColour (Palette::dimText);
            g.setFont (bodyFont (12.5f));
            g.drawText (unit, inner.withTrimmedTop (inner.getHeight() / 2 - 4), juce::Justification::centredLeft, true);
        }
    }

    void paintTiles (juce::Graphics& g, const LoudnessValue& m, const LoudnessValue& s, const LoudnessValue& i)
    {
        const auto& st = engine.getLoudnessMeter().getStats();
        auto area = tilesArea;
        const int gap = 10;
        const int w = (area.getWidth() - 2 * gap) / 3;
        const float big = area.getHeight() >= 120 ? 34.0f : 28.0f;

        paintTile (g, area.removeFromLeft (w), ko ("순간 (M, 400 ms)"), valueText (m), "LUFS", m.valid ? Palette::text : Palette::dimText,
                   ko ("최대 ") + valueText (st.maxMomentary()), big);
        area.removeFromLeft (gap);
        paintTile (g, area.removeFromLeft (w), ko ("단기 (S, 3초)"), valueText (s), "LUFS", colourFor (s),
                   ko ("최대 ") + valueText (st.maxShortTerm()), big);
        area.removeFromLeft (gap);
        paintTile (g, area, ko ("통합 (I, 초기화 후 전체)"), valueText (i), "LUFS", colourFor (i),
                   i.valid ? ko ("목표 대비 ") + signedText (i.value - target) + " LU" : ko ("소리가 들어오면 잽니다"), big);
    }

    void paintSmallTiles (juce::Graphics& g, const LoudnessValue& i, const LoudnessValue& lra, const LoudnessValue& tp, const LoudnessStats& st)
    {
        auto area = smallArea;
        const int gap = 10;
        const int w = (area.getWidth() - 2 * gap) / 3;
        juce::ignoreUnused (i, st);

        paintTile (g, area.removeFromLeft (w), ko ("범위 (LRA)"), valueText (lra), "LU", lra.valid ? Palette::text : Palette::dimText, {}, 20.0f);
        area.removeFromLeft (gap);
        paintTile (g, area.removeFromLeft (w), ko ("트루 피크 (최대)"), valueText (tp), "dBTP",
                   ! tp.valid ? Palette::dimText : tp.value > truePeakCeiling ? Palette::danger : Palette::text, {}, 20.0f);
        area.removeFromLeft (gap);
        paintTile (g, area, ko ("목표"), juce::String (target, 0), "LUFS", Palette::text, {}, 20.0f);
    }

    void paintBar (juce::Graphics& g, const LoudnessValue& m, const LoudnessValue& s, const LoudnessValue& i)
    {
        auto area = barArea;
        auto caption = area.removeFromTop (18);
        g.setColour (Palette::dimText);
        g.setFont (captionFont());
        g.drawText (ko ("라우드니스 막대"), caption.removeFromLeft (110), juce::Justification::centredLeft, true);
        g.setFont (bodyFont (12.0f));
        g.drawText (caption.getWidth() >= 470 ? ko ("파란 막대 순간(M)  ·  흰 줄 단기(S)  ·  노란 줄 통합(I)  ·  초록 점선 목표 ±1 LU")
                                              : ko ("막대 M · 흰 줄 S · 노란 줄 I · 점선 목표"), caption, juce::Justification::centredRight, true);
        auto scale = area.removeFromBottom (14);
        const auto track = area.toFloat();
        g.setColour (Palette::meterBg);
        g.fillRoundedRectangle (track, 6.0f);

        if (m.valid)
        {
            g.setColour (Palette::accent.withAlpha (0.85f));
            g.fillRoundedRectangle (track.withRight (xFor (track, m.value)), 6.0f);
        }

        // the ±1 LU band around the target, then its line
        g.setColour (Palette::lampOn.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (xFor (track, target - 1.0), track.getY(), xFor (track, target + 1.0) - xFor (track, target - 1.0), track.getHeight()));
        g.setColour (Palette::lampOn);
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (xFor (track, target), track.getY(), xFor (track, target), track.getBottom()), dashes, 2, 2.0f);

        if (s.valid)
        {
            g.setColour (Palette::text);
            g.fillRect (juce::Rectangle<float> (xFor (track, s.value) - 1.5f, track.getY(), 3.0f, track.getHeight()));
        }

        if (i.valid)
        {
            g.setColour (Palette::meterYellow);
            g.fillRect (juce::Rectangle<float> (xFor (track, i.value) - 1.5f, track.getY() - 3.0f, 3.0f, track.getHeight() + 6.0f));
        }

        g.setColour (Palette::dimText);
        g.setFont (juce::Font (juce::FontOptions (pt (10.5f))));

        for (int db = (int) scaleMin; db <= (int) scaleMax; db += 10)
        {
            const float x = xFor (track, db);
            const auto justification = db == (int) scaleMin ? juce::Justification::centredLeft : db == (int) scaleMax ? juce::Justification::centredRight : juce::Justification::centred;
            g.drawText (juce::String (db), juce::Rectangle<int> ((int) x - 18, scale.getY(), 36, scale.getHeight()), justification, false);
        }

    }

    void paintGraph (juce::Graphics& g, const LoudnessValue& i)
    {
        auto area = graphArea;

        if (area.getHeight() < 60)
            return;

        auto caption = area.removeFromTop (18);
        g.setColour (Palette::dimText);
        g.setFont (captionFont());
        g.drawText (ko ("최근 3분 - 단기 라우드니스 (S)"), caption, juce::Justification::centredLeft, true);

        auto labels = area.removeFromLeft (34);
        const auto plot = area.toFloat();
        g.setColour (Palette::meterBg);
        g.fillRoundedRectangle (plot, 6.0f);

        g.setFont (juce::Font (juce::FontOptions (pt (10.5f))));

        for (int db = (int) scaleMin; db <= (int) scaleMax; db += 10)
        {
            const float y = yFor (plot, db);
            g.setColour (Palette::line.withAlpha (0.7f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (Palette::dimText);
            g.drawText (juce::String (db), juce::Rectangle<int> (labels.getX(), (int) y - 7, labels.getWidth() - 4, 14), juce::Justification::centredRight, false);
        }

        g.setColour (Palette::lampOn.withAlpha (0.15f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (plot, target + 1.0), plot.getWidth(), yFor (plot, target - 1.0) - yFor (plot, target + 1.0)));
        g.setColour (Palette::lampOn.withAlpha (0.8f));
        g.drawHorizontalLine ((int) yFor (plot, target), plot.getX(), plot.getRight());

        // the short-term curve: oldest at the left, now at the right; a gap where there was no value
        juce::Path path;
        bool drawing = false;

        for (int k = 0; k < historyLength; ++k)
        {
            const float v = history[(size_t) ((historyPos + k) % historyLength)];
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) (historyLength - 1);

            if (std::isnan (v))
            {
                drawing = false;
                continue;
            }

            const float y = yFor (plot, v);

            if (drawing)
                path.lineTo (x, y);
            else
                path.startNewSubPath (x, y);

            drawing = true;
        }

        g.setColour (Palette::accent);
        g.strokePath (path, juce::PathStrokeType (1.8f));

        if (i.valid)
        {
            g.setColour (Palette::meterYellow.withAlpha (0.9f));
            const float dashes[] = { 6.0f, 4.0f };
            g.drawDashedLine (juce::Line<float> (plot.getX(), yFor (plot, i.value), plot.getRight(), yFor (plot, i.value)), dashes, 2, 1.5f);
        }

        g.setColour (Palette::line);
        g.drawRoundedRectangle (plot.reduced (0.5f), 6.0f, 1.0f);
    }

    MixEngine& engine;
    LiveMixSettings& settings;
    double target = -14.0;
    std::vector<float> history;
    int historyPos = 0;
    juce::int64 lastBlocks = 0;

    juce::Rectangle<int> headerArea, noteArea, tilesArea, smallArea, barArea, graphArea;
    juce::Label targetCaption;
    juce::ComboBox targetCombo;
    juce::TextButton resetButton;
    juce::ToggleButton onTopToggle;
};

//==============================================================================
LoudnessWindow::LoudnessWindow (MixEngine& engine, LiveMixSettings& settings)
    : DocumentWindow (ko ("LUFS 미터 - 마스터 출력"), Palette::background, DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    auto* c = new Content (engine, settings);
    content = c;
    setContentOwned (c, true);
    setResizable (true, false);
    setResizeLimits (540, 440, 10000, 10000);
    setAlwaysOnTop (c->isAlwaysOnTopWanted());
    centreWithSize (getWidth(), getHeight());   // the owner then centres it on its own display (centreAroundComponent)
}

LoudnessWindow::~LoudnessWindow()
{
    clearContentComponent();
}

void LoudnessWindow::open()
{
    setVisible (true);
    toFront (true);
}

void LoudnessWindow::closeButtonPressed()
{
    setVisible (false);   // kept: the numbers go on, reopening is instant
}

} // namespace gocue::livemix
