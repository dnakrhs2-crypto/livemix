#include "PluginSearch.h"

#include <juce_core/juce_core.h>

namespace gocue::tests
{

using namespace gocue::livemix;

/** The plugin manager's search box (and the preset builder's): name, maker, format, every word, any case. */
class PluginSearchTests : public juce::UnitTest
{
public:
    PluginSearchTests() : juce::UnitTest ("LiveMix plugin search", "LiveMix") {}

    static juce::PluginDescription plugin (const juce::String& name, const juce::String& maker, const juce::String& format)
    {
        juce::PluginDescription d;
        d.name = name;
        d.manufacturerName = maker;
        d.pluginFormatName = format;
        d.fileOrIdentifier = "C:\\plugins\\" + name + ".vst3";
        return d;
    }

    void runTest() override
    {
        juce::Array<juce::PluginDescription> all;
        all.add (plugin ("C6 Compressor", "Waves", "VST3"));
        all.add (plugin ("Pro-Q 3", "FabFilter", "VST3"));
        all.add (plugin ("TDR Nova", "Tokyo Dawn Labs", "VST"));
        all.add (plugin (juce::String::fromUTF8 ("보컬 리버브"), "Gomtwigim", "VST3"));

        beginTest ("an empty query shows every plugin, in the order they came");
        {
            const auto shown = filterPlugins (all, "");
            expectEquals (shown.size(), 4);
            expectEquals (shown[0].name, juce::String ("C6 Compressor"));
            expectEquals (shown[3].name, juce::String::fromUTF8 ("보컬 리버브"));
            expectEquals (filterPlugins (all, "   ").size(), 4);
        }

        beginTest ("a word matches the name, the maker or the format label, in any case");
        {
            expectEquals (filterPlugins (all, "comp").size(), 1);
            expectEquals (filterPlugins (all, "COMP")[0].name, juce::String ("C6 Compressor"));
            expectEquals (filterPlugins (all, "fabfilter").size(), 1);
            expectEquals (filterPlugins (all, "waves")[0].name, juce::String ("C6 Compressor"));
            expectEquals (filterPlugins (all, juce::String::fromUTF8 ("리버브")).size(), 1);
            expectEquals (filterPlugins (all, "vst2").size(), 1);         // JUCE's "VST" format shows as VST2
            expectEquals (filterPlugins (all, "vst2")[0].name, juce::String ("TDR Nova"));
            expectEquals (filterPlugins (all, "vst3").size(), 3);
            expectEquals (filterPlugins (all, "vst").size(), 4);
            expectEquals (filterPlugins (all, "nothing here").size(), 0);
        }

        beginTest ("every word has to match somewhere: 'waves comp' is the Waves compressor, 'waves q' nothing");
        {
            expectEquals (filterPlugins (all, "waves comp").size(), 1);
            expectEquals (filterPlugins (all, "comp waves").size(), 1);
            expectEquals (filterPlugins (all, "waves q").size(), 0);
            expectEquals (filterPlugins (all, "tokyo vst2").size(), 1);
            expectEquals (filterPlugins (all, "tokyo vst3").size(), 0);
            expect (pluginMatchesSearch (all[1], "pro-q"));
            expect (! pluginMatchesSearch (all[1], "pro q 4"));
        }

        beginTest ("the format label");
        {
            expectEquals (pluginFormatLabel ("VST"), juce::String ("VST2"));
            expectEquals (pluginFormatLabel ("VST3"), juce::String ("VST3"));
            expectEquals (pluginFormatLabel ("AudioUnit"), juce::String ("AudioUnit"));
        }
    }
};

static PluginSearchTests pluginSearchTests;

} // namespace gocue::tests
