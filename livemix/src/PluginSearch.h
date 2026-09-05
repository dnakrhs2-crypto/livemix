#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace gocue::livemix
{

/** The format as the operator sees it: JUCE's "VST" is VST2 here. */
inline juce::String pluginFormatLabel (const juce::String& formatName)
{
    return formatName == "VST" ? "VST2" : formatName;
}

/** The search box of the plugin manager and of the preset builder: every word of the query (split at spaces) has to
    appear in the plugin's name, its maker or its format label, case-insensitively; an empty query matches every
    plugin. "waves comp" finds "Waves C6 Compressor", "vst2" the VST2 plugins. */
inline bool pluginMatchesSearch (const juce::PluginDescription& d, const juce::String& query)
{
    juce::StringArray words;
    words.addTokens (query, " \t", "");
    words.removeEmptyStrings();

    if (words.isEmpty())
        return true;

    const auto haystack = d.name + " " + d.manufacturerName + " " + pluginFormatLabel (d.pluginFormatName);

    for (const auto& word : words)
        if (! haystack.containsIgnoreCase (word))
            return false;

    return true;
}

/** The plugins of 'all' the query matches, in the order they came. */
inline juce::Array<juce::PluginDescription> filterPlugins (const juce::Array<juce::PluginDescription>& all, const juce::String& query)
{
    juce::Array<juce::PluginDescription> shown;

    for (const auto& d : all)
        if (pluginMatchesSearch (d, query))
            shown.add (d);

    return shown;
}

} // namespace gocue::livemix
