#pragma once

#include "MixDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gocue::livemix
{

/** 플러그인 그룹: a mic channel's groups of chain plugins that switch off together. Up to five groups, each picked
    from the channel's VST3 chain as it is now; the card shows a number a group (lit while it is off). A window of
    its own, showing one channel at a time (hidden on close, kept). */
class PluginGroupsWindow : public juce::DocumentWindow
{
public:
    explicit PluginGroupsWindow (MixDocument& document);
    ~PluginGroupsWindow() override;

    /** Shows the groups of that channel. */
    void open (const juce::Uuid& channelId);
    /** The session or a chain changed: what is shown follows (the channel gone: the window closes). */
    void refresh();
    void closeButtonPressed() override;

private:
    class Content;
    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginGroupsWindow)
};

} // namespace gocue::livemix
