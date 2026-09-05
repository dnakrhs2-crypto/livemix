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
            expect (host.getFormat ("VST") == nullptr);          // not registered until switched on: no VST2 scan for an app that never does

            host.setVst2Enabled (true);
            expect (host.isVst2Enabled() == PluginHost::hasVst2Support());   // a build without the SDK stays off

            if (PluginHost::hasVst2Support())
            {
                expect (host.getFormat ("VST") != nullptr);
                expectEquals (host.getEffectTypes().size(), 2);
                expect (host.isPluginEnabled (old));
            }
            else
            {
                expectEquals (host.getEffectTypes().size(), 1);
            }

            host.setPluginEnabled (eq, false);
            expect (host.isPluginSwitchedOff (eq) && ! host.isPluginSwitchedOff (old));
            expect (! host.isPluginEnabled (eq));
            expectEquals (host.getDisabledPlugins().size(), 1);
            expectEquals (host.getDisabledPlugins()[0], PluginHost::keyFor (eq));

            auto moved = eq;
            moved.fileOrIdentifier = "D:\\moved\\EQ.vst3";   // the same plugin from another folder keeps its switch
            expect (host.isPluginSwitchedOff (moved));

            host.setDisabledPlugins ({ PluginHost::keyFor (old), PluginHost::keyFor (old), "" });   // duplicates and blanks dropped
            expectEquals (host.getDisabledPlugins().size(), 1);
            expect (host.isPluginEnabled (eq) && ! host.isPluginEnabled (old));

            host.setVst2Enabled (false);
            host.setPluginEnabled (old, true);
            expect (! host.isPluginEnabled (old));   // enabled, but its format is off
            expect (! host.isPluginSwitchedOff (old));   // ... and its own switch says so
        }
    }
};

static PluginHostFilterTests pluginHostFilterTests;

/** The preset fixes of 0.4.0: a preset file's shape is checked; a saved plugin is resolved by its file, then its name,
    among the known plugins sharing its id. */
class PluginPresetFixesTests : public juce::UnitTest
{
public:
    PluginPresetFixesTests() : juce::UnitTest ("LiveMix preset file shape and plugin resolution", "LiveMix") {}

    void runTest() override
    {
        beginTest ("a preset file's shape: the plugin list an array of objects, the version a number, the state Base64");
        {
            PluginPreset q;
            const juce::String head = "{\"app\": \"LiveMix\", \"kind\": \"pluginPreset\", \"version\": 1, \"name\": \"x\", ";
            expect (PluginPreset::fromJson (head + "\"plugins\": [{\"format\": \"VST3\", \"name\": \"EQ\", \"fileOrIdentifier\": \"C:\\eq.vst3\", \"uniqueId\": 1, \"state\": \"AAECAw==\"}]}", q).wasOk());
            expectEquals ((int) q.plugins.size(), 1);
            expectEquals (q.plugins[0].stateBase64, juce::String ("AAECAw=="));
            expect (PluginPreset::fromJson (head + "\"plugins\": \"EQ\"}", q).failed());                                   // not a list
            expect (PluginPreset::fromJson (head + "\"plugins\": [1, 2]}", q).failed());                                  // not objects
            expect (PluginPreset::fromJson (head + "\"plugins\": [{\"name\": \"EQ\", \"state\": \"@@@@\"}]}", q).failed());   // a state that cannot be decoded
            expect (PluginPreset::fromJson ("{\"app\": \"LiveMix\", \"kind\": \"pluginPreset\", \"version\": \"abc\", \"plugins\": []}", q).failed());   // a version that is not a number
            expect (PluginPreset::fromJson ("{\"app\": \"LiveMix\", \"kind\": \"pluginPreset\", \"name\": \"x\"}", q).failed());   // no list at all
            expect (PluginPreset::fromJson (head + "\"plugins\": []}", q).wasOk());                                       // an empty list is a preset (an honest one)
        }

        beginTest ("a saved plugin is found by its file, then its name, among known plugins sharing an id; two strangers with the id are refused");
        {
            PluginHost host;
            auto desc = [] (const juce::String& name, const juce::String& file, int id)
            {
                juce::PluginDescription d;
                d.pluginFormatName = "VST3";
                d.name = name;
                d.fileOrIdentifier = file;
                d.uniqueId = id;
                d.deprecatedUid = id;
                d.manufacturerName = "Test";
                return d;
            };
            host.getKnownPlugins().addType (desc ("Alpha", "C:\\\\plugins\\\\alpha.vst3", 77));
            host.getKnownPlugins().addType (desc ("Beta", "C:\\\\plugins\\\\beta.vst3", 77));   // the same id, another file: the list allows it

            PluginSlotState s;
            s.format = "VST3";
            s.uniqueId = 77;
            juce::String error;

            s.name = "Beta";
            s.fileOrIdentifier = "C:\\\\plugins\\\\beta.vst3";
            expect (host.createInstance (s, 48000.0, 256, error) == nullptr);   // the file is not really there: the error names the one chosen
            expect (error.contains ("beta.vst3") && ! error.containsIgnoreCase ("alpha"), "resolved by file: " + error);

            s.fileOrIdentifier = "D:\\\\moved\\\\beta.vst3";   // moved: by name
            error.clear();
            expect (host.createInstance (s, 48000.0, 256, error) == nullptr);
            expect (error.contains ("C:\\\\plugins\\\\beta.vst3"), "resolved by name: " + error);

            s.name = "Gamma";
            s.fileOrIdentifier = "D:\\\\moved\\\\gamma.vst3";   // neither the file nor the name is known and two share the id: refused, not the first one
            error.clear();
            expect (host.createInstance (s, 48000.0, 256, error) == nullptr);
            expect (error.contains (juce::String::fromUTF8 ("여러 개")), "ambiguous: " + error);

            PluginHost one;
            one.getKnownPlugins().addType (desc ("Alpha", "C:\\\\plugins\\\\alpha.vst3", 78));
            s.uniqueId = 78;   // the one plugin with the id: taken (it moved and was renamed)
            error.clear();
            expect (one.createInstance (s, 48000.0, 256, error) == nullptr);
            expect (error.contains ("alpha.vst3"), "the single id match: " + error);
        }
    }
};

static PluginPresetFixesTests pluginPresetFixesTests;

} // namespace gocue::tests
