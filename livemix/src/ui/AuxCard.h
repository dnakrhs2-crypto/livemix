#pragma once

#include "ChannelCard.h"   // CardLayout
#include "Localisation.h"
#include "MixDocument.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace gocue::livemix
{

/** One aux channel: a send-only device input through its own VST3 chain and ON/OFF switch. It has no outputs or FX
    sends: the mic channels that pick it as their input (the chips on the right, or their own input box) receive it.
    The columns line up with the mic cards' (name, input, chain), so the two kinds read as one list. */
class AuxCard : public juce::Component
{
public:
    AuxCard (MixDocument& document, const juce::Uuid& auxId);
    ~AuxCard() override;

    const juce::Uuid& getAuxId() const noexcept { return auxId; }

    void refresh();
    void setDeviceChannels (const juce::StringArray& inputNames);
    void setLayout (CardLayout layout);
    int getPreferredHeight (int width) const;
    void pushMeter (MixEngine::Meter meter, bool paint = true) { meter_.push (meter, paint); }

    std::function<void (const juce::Uuid&)> onOpenChain;
    std::function<void (const juce::Uuid&)> onAddPlugin;
    juce::Component& getAddPluginButton() noexcept { return addPluginButton; }
    std::function<void (const juce::Uuid&)> onRemove;
    std::function<void (const juce::Uuid&, int slotIndex)> onOpenPluginEditor;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct ReceiverChip;

    const MixAux* aux() const;
    int chainWidthForWidth (int width) const;
    int chainHeightForWidth (int width) const;
    int receiversWidthForWidth (int width) const;
    int receiversHeightForWidth (int width) const;
    void rebuildChain();
    void rebuildReceivers();
    void commitInput();
    void showMenu();

    MixDocument& document;
    juce::Uuid auxId;
    CardLayout layout = CardLayout::wide;
    juce::StringArray inputNames;

    juce::Label badge;
    NameLabel name;
    juce::TextButton menuButton { juce::String::fromUTF8 ("\xE2\x8B\xAF") };   // ···
    LampButton onButton;
    juce::Label inputCaption, meterCaption, chainCaption, receiversCaption;
    juce::ComboBox inputCombo;
    juce::ToggleButton stereoToggle;
    MeterBar meter_;
    std::vector<std::unique_ptr<ChainChip>> chips;
    juce::TextButton openChainButton, addPluginButton;
    std::vector<std::unique_ptr<ReceiverChip>> receivers;   // one per mic channel, in card order
    static constexpr int chainFooter = 1 + 30;               // the chain actions under the chip rows
    bool refreshing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuxCard)
};

} // namespace gocue::livemix
