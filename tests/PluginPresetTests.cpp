#include "PluginPreset.h"
#include "audio/PluginHost.h"

#include <juce_core/juce_core.h>

namespace gocue::tests
{

using namespace gocue::livemix;

class PluginPresetTests : public juce::UnitTest
{
public:
    PluginPresetTests() : juce::UnitTest ("LiveMix plugin presets", "LiveMix") {}

    static PluginSlotState slot (const juce::String& name, int id)
    {
        PluginSlotState s;
        s.format = "VST3";
        s.name = name;
        s.fileOrIdentifier = "C:\\plugins\\" + name + ".vst3";
        s.uniqueId = id;
        return s;
    }

    void runTest() override
    {
        beginTest ("a preset round-trips through JSON, keeps the order and the states, and refuses foreign files");
        {
            PluginPreset p;
            p.name = juce::String::fromUTF8 ("보컬 체인");
            p.plugins = { slot ("EQ", 1), slot ("Comp", 2), slot ("Gate", 3) };
            p.plugins[1].stateBase64 = "AAECAw==";
            p.plugins[2].bypassed = true;

            PluginPreset q;
            expect (PluginPreset::fromJson (p.toJson(), q).wasOk());
            expectEquals (q.name, p.name);
            expectEquals ((int) q.plugins.size(), 3);
            expectEquals (q.plugins[0].name, juce::String ("EQ"));
            expectEquals (q.plugins[1].stateBase64, juce::String ("AAECAw=="));
            expect (q.plugins[2].bypassed);
            expectEquals (q.plugins[2].uniqueId, 3);
            expectEquals (q.summary(), juce::String::fromUTF8 ("1. EQ \xE2\x86\x92 2. Comp \xE2\x86\x92 3. Gate"));

            expect (PluginPreset::fromJson ("", q).failed());
            expect (PluginPreset::fromJson ("[1,2]", q).failed());
            expect (PluginPreset::fromJson ("{\"app\": \"LiveMix\", \"kind\": \"session\", \"version\": 1}", q).failed());     // a session, not a preset
            expect (PluginPreset::fromJson ("{\"app\": \"Enqueue\", \"kind\": \"pluginPreset\", \"version\": 1}", q).failed());
            expect (PluginPreset::fromJson ("{\"app\": \"LiveMix\", \"kind\": \"pluginPreset\", \"version\": 99}", q).failed());   // the future
            expect (PluginPreset::fromJson (p.toJson() + " {\"x\": 1}", q).failed());                                             // trailing data

            PluginPreset big;
            big.name = "big";

            for (int i = 0; i < PluginPreset::maxPlugins + 5; ++i)
                big.plugins.push_back (slot ("p" + juce::String (i), i + 1));

            expect (PluginPreset::fromJson (big.toJson(), q).wasOk());
            expectEquals ((int) q.plugins.size(), PluginPreset::maxPlugins);   // capped on the way in, like a session's chain
        }

        beginTest ("file names, the folder listing, save and load");
        {
            expectEquals (PluginPreset::fileNameFor ("Vocal"), juce::String ("Vocal.livemixpreset"));
            expectEquals (PluginPreset::fileNameFor ("  a/b:c?  "), juce::String ("abc.livemixpreset"));
            expectEquals (PluginPreset::fileNameFor (""), juce::String::fromUTF8 ("프리셋.livemixpreset"));
            expectEquals (PluginPreset::fileNameFor (juce::String::fromUTF8 ("보컬 체인")), juce::String::fromUTF8 ("보컬 체인.livemixpreset"));
            expect (PluginPreset::isPresetFileName ("x.livemixpreset") && PluginPreset::isPresetFileName ("X.LIVEMIXPRESET"));
            expect (! PluginPreset::isPresetFileName ("x.livemix"));

            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LiveMixPresetTests_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            folder.createDirectory();

            PluginPreset a;
            a.name = "Alpha";
            a.plugins = { slot ("EQ", 1) };
            PluginPreset b;
            b.name = "beta";
            b.plugins = { slot ("Comp", 2), slot ("Gate", 3) };
            expect (a.save (PluginPreset::fileFor (a.name, folder)).wasOk());
            expect (b.save (PluginPreset::fileFor (b.name, folder)).wasOk());
            folder.getChildFile ("broken.livemixpreset").replaceWithText ("not json");
            folder.getChildFile ("session.livemix").replaceWithText ("{}");   // another kind of file: not listed

            juce::StringArray problems;
            const auto listed = PluginPreset::listFolder (folder, &problems);
            expectEquals ((int) listed.size(), 2);
            expectEquals (listed[0].name, juce::String ("Alpha"));   // name order, case-insensitive
            expectEquals (listed[1].name, juce::String ("beta"));
            expectEquals ((int) listed[1].plugins.size(), 2);
            expectEquals (problems.size(), 1);
            expect (problems[0].startsWith ("broken.livemixpreset"));

            PluginPreset loaded;
            expect (PluginPreset::load (PluginPreset::fileFor ("Alpha", folder), loaded).wasOk());
            expectEquals (loaded.name, juce::String ("Alpha"));
            expect (PluginPreset::load (folder.getChildFile ("nope.livemixpreset"), loaded).failed());

            // a file whose JSON carries no name goes by its file name
            folder.getChildFile ("Named by file.livemixpreset").replaceWithText ("{\"app\": \"LiveMix\", \"kind\": \"pluginPreset\", \"version\": 1, \"plugins\": []}");
            expect (PluginPreset::load (folder.getChildFile ("Named by file.livemixpreset"), loaded).wasOk());
            expectEquals (loaded.name, juce::String ("Named by file"));

            folder.deleteRecursively();
        }
    }
};

static PluginPresetTests pluginPresetTests;

/** The host's menus offer only what is enabled: not switched off, and of a format that is on. */
class PluginHostFilterTests : public juce::UnitTest
{
public:
    PluginHostFilterTests() : juce::UnitTest ("LiveMix plugin host filters", "LiveMix") {}

    void runTest() override
    {
        beginTest ("a plugin switched off leaves the menus; VST2 plugins are offered only while VST2 is on");
        {
            PluginHost host;
            juce::PluginDescription eq;
            eq.name = "EQ";
            eq.pluginFormatName = "VST3";
            eq.uniqueId = 11;
            eq.fileOrIdentifier = "C:\\plugins\\EQ.vst3";
            juce::PluginDescription old;
            old.name = "OldComp";
            old.pluginFormatName = "VST";
            old.uniqueId = 22;
            old.fileOrIdentifier = "C:\\plugins\\OldComp.dll";
            juce::PluginDescription synth;
            synth.name = "Synth";
            synth.pluginFormatName = "VST3";
            synth.uniqueId = 33;
            synth.fileOrIdentifier = "C:\\plugins\\Synth.vst3";
            synth.isInstrument = true;
            host.getKnownPlugins().addType (eq);
            host.getKnownPlugins().addType (old);
            host.getKnownPlugins().addType (synth);

            expectEquals (host.getEffectTypes().size(), 1);      // the VST2 one is off by default, the instrument never
            expectEquals (host.getAllEffectTypes().size(), 2);   // the manager lists the VST2 one too
            expect (host.isPluginEnabled (eq));
            expect (! host.isPluginEnabled (old));

            host.setVst2Enabled (true);
            expectEquals (host.getEffectTypes().size(), 2);
            expect (host.isPluginEnabled (old));

            host.setPluginEnabled (eq, false);
            expectEquals (host.getEffectTypes().size(), 1);
            expectEquals (host.getEffectTypes()[0].name, juce::String ("OldComp"));
            expectEquals (host.getDisabledPlugins().size(), 1);
            expectEquals (host.getDisabledPlugins()[0], PluginHost::keyFor (eq));

            host.setDisabledPlugins ({ PluginHost::keyFor (old), PluginHost::keyFor (old), "" });   // duplicates and blanks dropped
            expectEquals (host.getDisabledPlugins().size(), 1);
            expect (host.isPluginEnabled (eq) && ! host.isPluginEnabled (old));

            host.setVst2Enabled (false);
            host.setPluginEnabled (old, true);
            expect (! host.isPluginEnabled (old));   // enabled, but its format is off
        }
    }
};

static PluginHostFilterTests pluginHostFilterTests;

} // namespace gocue::tests
