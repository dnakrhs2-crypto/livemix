#include "Localisation.h"
#include "AuxCard.h"

namespace gocue::livemix
{

/** A mic channel this aux can feed: lit (accent edge) while that mic takes the aux as its input. */
struct AuxCard::ReceiverChip : public Chip
{
    explicit ReceiverChip (const juce::Uuid& id) : channelId (id) {}
    juce::Uuid channelId;
};

//==============================================================================
AuxCard::~AuxCard() = default;

AuxCard::AuxCard (MixDocument& doc, const juce::Uuid& id) : document (doc), auxId (id)
{
    badge.setFont (juce::Font (juce::FontOptions (pt (12.5f), juce::Font::bold)));
    badge.setJustificationType (juce::Justification::centred);
    badge.setColour (juce::Label::textColourId, Palette::accent);
    badge.setColour (juce::Label::backgroundColourId, Palette::card2);
    badge.setColour (juce::Label::outlineColourId, Palette::accent);
    badge.setMinimumHorizontalScale (1.0f);
    badge.setTooltip (tr ("억스 채널: 마스터로 나가지 않고, 받는 마이크 채널의 입력으로만 쓰입니다"));
    addAndMakeVisible (badge);

    name.onRenamed = [this] (const juce::String& text) { document.renameAux (auxId, text); };
    name.setTwoLineFont (juce::Font (juce::FontOptions (pt (13.0f), juce::Font::bold)));
    addAndMakeVisible (name);

    menuButton.setWantsKeyboardFocus (false);
    menuButton.setTooltip (tr ("채널 메뉴"));
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);

    onButton.setTexts (tr ("억스 ON"), tr ("억스 OFF"), {});
    onButton.onClick = [this] { document.setAuxOn (auxId, ! onButton.isOn()); };
    addAndMakeVisible (onButton);

    styleCaption (inputCaption, tr ("입력"));
    addAndMakeVisible (inputCaption);
    inputCombo.setWantsKeyboardFocus (false);
    inputCombo.onChange = [this] { commitInput(); };
    addAndMakeVisible (inputCombo);
    stereoToggle.setButtonText (tr ("스테레오"));
    stereoToggle.setWantsKeyboardFocus (false);
    stereoToggle.setTooltip (tr ("켜면 입력 두 개(왼쪽·오른쪽)를 한 채널로 씁니다"));
    stereoToggle.onClick = [this] { commitInput(); };
    addAndMakeVisible (stereoToggle);
    styleCaption (meterCaption, tr ("입력 미터"));
    addAndMakeVisible (meterCaption);
    addAndMakeVisible (meter_);

    styleCaption (chainCaption, tr ("VST3 체인   순서대로 통과"));
    addAndMakeVisible (chainCaption);
    openChainButton.setButtonText (tr ("체인 열기"));
    openChainButton.setWantsKeyboardFocus (false);
    openChainButton.onClick = [this] { if (onOpenChain) onOpenChain (auxId); };
    addAndMakeVisible (openChainButton);
    addPluginButton.setButtonText (tr ("+ 추가"));
    addPluginButton.setWantsKeyboardFocus (false);
    addPluginButton.onClick = [this] { if (onAddPlugin) onAddPlugin (auxId); };
    addAndMakeVisible (addPluginButton);

    styleCaption (receiversCaption, tr ("받는 마이크 채널"));
    addAndMakeVisible (receiversCaption);

    refresh();
}

const MixAux* AuxCard::aux() const
{
    return document.getSession().findAux (auxId);
}

void AuxCard::setDeviceChannels (const juce::StringArray& ins)
{
    inputNames = ins;
    refresh();
}

void AuxCard::refresh()
{
    const auto* a = aux();

    if (a == nullptr)
        return;

    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto& session = document.getSession();
    int index = 0;

    for (size_t i = 0; i < session.auxes.size(); ++i)
        if (session.auxes[i].id == auxId)
            index = (int) i;

    badge.setText ("AUX" + juce::String (index + 1), juce::dontSendNotification);

    if (! name.isBeingEdited())
        name.setText (a->name, juce::dontSendNotification);

    name.setTooltip (trf ("{0}\n더블클릭해서 이름 바꾸기", a->name));
    onButton.setOn (a->on);

    fillChannelCombo (inputCombo, inputNames, a->stereo, MixSession::maxDeviceChannels);
    selectSavedChannel (inputCombo, inputNames, a->inputFirst, a->stereo);
    stereoToggle.setToggleState (a->stereo, juce::dontSendNotification);
    meter_.setStereo (a->stereo);

    rebuildChain();
    rebuildReceivers();

    for (auto* child : getChildren())
        child->setAlpha (a->on || child == &onButton ? 1.0f : 0.62f);

    resized();
    repaint();
}

void AuxCard::rebuildChain()
{
    chips.clear();
    auto* chain = document.getEngine().getAuxChain (auxId);

    if (chain == nullptr)
        return;

    for (int i = 0; i < chain->getNumSlots(); ++i)
    {
        const auto& slot = chain->getSlot (i);
        const auto text = slot.plugin != nullptr ? slot.plugin->getName() : trf ("{0} (없음)", slot.state.name);
        auto chip = std::make_unique<ChainChip> (i, text, slot.bypassed.load() || slot.plugin == nullptr);
        chip->setTooltip (text);
        chip->onClick = [this, i] { if (onOpenPluginEditor) onOpenPluginEditor (auxId, i); };
        addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    }
}

void AuxCard::rebuildReceivers()
{
    const auto& session = document.getSession();
    std::vector<std::unique_ptr<ReceiverChip>> next;   // reused by mic id: the chip clicked may be the one refreshed

    for (size_t i = 0; i < session.channels.size(); ++i)
    {
        const auto& c = session.channels[i];
        std::unique_ptr<ReceiverChip> chip;

        for (auto& existing : receivers)
            if (existing != nullptr && existing->channelId == c.id)
                chip = std::move (existing);

        if (chip == nullptr)
        {
            chip = std::make_unique<ReceiverChip> (c.id);
            chip->setTooltip (tr ("켜면 이 마이크 채널이 이 억스를 입력으로 받습니다. 끄면 같은 장치 입력을 직접 받습니다"));
            chip->onClick = [this, raw = chip.get()]
            {
                if (refreshing)
                    return;

                // the chip has already flipped: lit = take this aux, unlit = back to the device input
                if (! document.setChannelAux (raw->channelId, raw->getToggleState() ? auxId : juce::Uuid::null()))
                    refresh();
            };
            addAndMakeVisible (*chip);
        }

        chip->setButtonText (juce::String ((int) i + 1) + "  " + c.name);
        chip->setToggleState (c.aux == auxId, juce::dontSendNotification);
        next.push_back (std::move (chip));
    }

    receivers = std::move (next);
}

void AuxCard::commitInput()
{
    if (refreshing)
        return;

    const auto* a = aux();
    const int sel = inputCombo.getSelectedId();
    // no matching item (a mono input mid-toggle, a route the device no longer has): keep the aux's input
    const int first = sel > 0 ? sel - 1 : (a != nullptr ? a->inputFirst : 0);
    document.setAuxInput (auxId, first, stereoToggle.getToggleState());
}

void AuxCard::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, tr ("이름 바꾸기"));
    menu.addItem (2, tr ("체인 열기"));
    menu.addSeparator();
    menu.addItem (3, tr ("이 억스 채널 삭제"));

    juce::Component::SafePointer<AuxCard> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)
            safeThis->name.showEditor();
        else if (result == 2 && safeThis->onOpenChain)
            safeThis->onOpenChain (safeThis->auxId);
        else if (result == 3 && safeThis->onRemove)
            safeThis->onRemove (safeThis->auxId);
    });
}

void AuxCard::setLayout (CardLayout newLayout)
{
    if (layout != newLayout)
    {
        layout = newLayout;
        resized();
    }
}

int AuxCard::chainWidthForWidth (int width) const
{
    // the mic cards' columns: name 194, input 160, chain 361 in the wide layout
    const int inner = width - 28;
    int chainW = inner;

    if (layout == CardLayout::wide)
        chainW = 361;
    else if (layout == CardLayout::medium)
        chainW = inner - 194 - 18 - 160 - 18;

    return juce::jmax (1, chainW);
}

int AuxCard::receiversWidthForWidth (int width) const
{
    const int inner = width - 28;
    return juce::jmax (1, layout == CardLayout::wide ? inner - 194 - 18 - 160 - 18 - 361 - 18 : inner);
}

int AuxCard::chainHeightForWidth (int width) const
{
    const int rows = ChipFlow::layout (chips, juce::Rectangle<int> (0, 0, chainWidthForWidth (width), 1), 44, false);
    return 27 + rows * ChipFlow::rowStep + chainFooter;
}

int AuxCard::receiversHeightForWidth (int width) const
{
    const int rows = ChipFlow::layout (receivers, juce::Rectangle<int> (0, 0, receiversWidthForWidth (width), 1), 28, false);
    return 28 + rows * ChipFlow::rowStep - ChipFlow::gap;
}

int AuxCard::getPreferredHeight (int width) const
{
    const int chainH = chainHeightForWidth (width);
    const int receiversH = receiversHeightForWidth (width);

    switch (layout)
    {
        case CardLayout::wide:   return juce::jmax (132, juce::jmax (chainH, receiversH)) + 24;
        case CardLayout::medium: return juce::jmax (132, chainH) + 16 + receiversH + 24;
        case CardLayout::narrow: return 114 + 14 + 96 + 14 + chainH + 14 + receiversH + 24;
    }

    return 200;
}

void AuxCard::resized()
{
    auto area = getLocalBounds().reduced (14, 12);

    auto layoutHead = [this] (juce::Rectangle<int> r)
    {
        auto row = r.removeFromTop (34);
        badge.setBounds (row.removeFromLeft (juce::jmax (52, labelWidthForText (badge, "AUX4"))).reduced (0, 2));
        row.removeFromLeft (10);
        name.setBounds (row);
        r.removeFromTop (8);
        onButton.setBounds (r.removeFromTop (40));
        r.removeFromTop (8);
        menuButton.setBounds (r.removeFromTop (24).removeFromRight (34));   // where a mic's menu is
    };

    auto layoutInput = [this] (juce::Rectangle<int> r, bool stacked)
    {
        if (stacked)
        {
            auto row = r.removeFromTop (30);
            inputCaption.setBounds (row.removeFromLeft (captionWidth (inputCaption, 34)));
            stereoToggle.setBounds (row.removeFromRight (88));
            row.removeFromRight (4);
            inputCombo.setBounds (row);
            r.removeFromTop (8);
        }
        else
        {
            auto row = r.removeFromTop (26);
            inputCaption.setBounds (row.removeFromLeft (captionWidth (inputCaption, 34)));
            stereoToggle.setBounds (row.removeFromRight (88).expanded (0, 2));
            r.removeFromTop (2);
            inputCombo.setBounds (r.removeFromTop (30));
            r.removeFromTop (16);
        }
        meterCaption.setBounds (r.removeFromTop (18));
        meter_.setBounds (r.removeFromTop (40));
    };

    auto layoutChain = [this] (juce::Rectangle<int> r)
    {
        chainCaption.setBounds (r.removeFromTop (26));
        r.removeFromTop (1);
        const int rows = ChipFlow::layout (chips, r, 44, true);
        r.removeFromTop (rows * ChipFlow::rowStep + 1);
        auto buttons = r.removeFromTop (30);
        openChainButton.setBounds (buttons.removeFromLeft (92));
        buttons.removeFromLeft (8);
        addPluginButton.setBounds (buttons.removeFromLeft (76));
    };

    auto layoutReceivers = [this] (juce::Rectangle<int> r)
    {
        receiversCaption.setBounds (r.removeFromTop (26));
        r.removeFromTop (2);
        ChipFlow::layout (receivers, r, 28, true);
    };

    const int chainH = chainHeightForWidth (getWidth());

    if (layout == CardLayout::wide)
    {
        auto head = area.removeFromLeft (194);
        area.removeFromLeft (18);
        auto input = area.removeFromLeft (160);
        area.removeFromLeft (18);
        auto chain = area.removeFromLeft (361);
        area.removeFromLeft (18);
        layoutHead (head);
        layoutInput (input, false);
        layoutChain (chain.withHeight (chainH));
        layoutReceivers (area);
    }
    else if (layout == CardLayout::medium)
    {
        auto top = area.removeFromTop (juce::jmax (132, chainH));
        area.removeFromTop (16);
        auto head = top.removeFromLeft (194);
        top.removeFromLeft (18);
        auto input = top.removeFromLeft (160);
        top.removeFromLeft (18);
        layoutHead (head);
        layoutInput (input, false);
        layoutChain (top.withHeight (chainH));
        layoutReceivers (area);
    }
    else
    {
        layoutHead (area.removeFromTop (114));
        area.removeFromTop (14);
        layoutInput (area.removeFromTop (96), true);
        area.removeFromTop (14);
        layoutChain (area.removeFromTop (chainH));
        area.removeFromTop (14);
        layoutReceivers (area);
    }
}

void AuxCard::paint (juce::Graphics& g)
{
    const auto* a = aux();
    const bool off = a != nullptr && ! a->on;
    if (off) g.beginTransparencyLayer (0.62f);
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Palette::card);
    g.fillRoundedRectangle (bounds, Palette::cardRadius);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (bounds, Palette::cardRadius, 1.0f);
    if (off) g.endTransparencyLayer();
}

} // namespace gocue::livemix
