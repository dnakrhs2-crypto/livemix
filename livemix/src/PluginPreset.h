#pragma once

#include "MixModel.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace gocue::livemix
{

/** A plugin preset: an ordered chain of plugins - with the settings they had when the preset was saved from a live
    chain, or the plugins' own defaults when it was put together in the plugin manager - that goes into any mic, FX
    or master chain. A file of its own (Documents/LiveMix/플러그인 프리셋/<name>.livemixpreset, JSON), so it can be
    copied between PCs and backed up online next to the sessions. */
struct PluginPreset
{
    juce::String name;
    std::vector<PluginSlotState> plugins;
    juce::File file;   // where it was loaded from (not part of the JSON)

    static constexpr const char* fileExtension = ".livemixpreset";
    static constexpr int currentVersion = 1;
    static constexpr int maxPlugins = MixSession::maxChainSlots;
    static constexpr juce::int64 maxFileBytes = 8 * 1024 * 1024;

    juce::String toJson() const;
    /** Refuses anything that is not a LiveMix preset (another app's file, a future version); caps the plugins. */
    static juce::Result fromJson (const juce::String& json, PluginPreset& out);

    /** A verified, atomic write; refuses a preset that load() could not read back (the size cap). */
    juce::Result save (const juce::File& target) const;
    static juce::Result load (const juce::File& file, PluginPreset& out);

    /** Documents/LiveMix/플러그인 프리셋 */
    static juce::File defaultFolder();
    /** A file name for a preset name (illegal characters dropped; an empty name becomes "프리셋"). */
    static juce::String fileNameFor (const juce::String& name);
    static juce::File fileFor (const juce::String& name, const juce::File& folder);
    /** Every preset in the folder, in name order; unreadable files are skipped and named in 'problems'. */
    static std::vector<PluginPreset> listFolder (const juce::File& folder, juce::StringArray* problems = nullptr);

    /** "1. EQ → 2. Comp → 3. Gate" */
    juce::String summary() const;

    /** True for a file name a preset would be saved under (by extension). */
    static bool isPresetFileName (const juce::String& fileName);
};

} // namespace gocue::livemix
