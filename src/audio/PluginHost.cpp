#include "audio/PluginHost.h"

namespace gocue
{

namespace
{
    struct NameComparator
    {
        static int compareElements (const juce::PluginDescription& a, const juce::PluginDescription& b)
        {
            return a.name.compareIgnoreCase (b.name);
        }
    };
}

PluginHost::PluginHost()
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

   #if JUCE_PLUGINHOST_VST
    formatManager.addFormat (std::make_unique<juce::VSTPluginFormat>());   // offered only while vst2Enabled
   #endif

    knownPlugins.addChangeListener (this);
}

bool PluginHost::hasVst2Support() noexcept
{
   #if JUCE_PLUGINHOST_VST
    return true;
   #else
    return false;
   #endif
}

juce::AudioPluginFormat* PluginHost::getFormat (const juce::String& name) const
{
    for (auto* format : formatManager.getFormats())
        if (format->getName() == name)
            return format;

    return nullptr;
}

juce::String PluginHost::keyFor (const juce::PluginDescription& d)
{
    return d.pluginFormatName + "|" + juce::String (d.uniqueId) + "|" + d.fileOrIdentifier;
}

void PluginHost::setDisabledPlugins (const juce::StringArray& keys)
{
    disabledPlugins = keys;
    disabledPlugins.removeEmptyStrings();
    disabledPlugins.removeDuplicates (false);
}

void PluginHost::setPluginEnabled (const juce::PluginDescription& d, bool enabled)
{
    const auto key = keyFor (d);

    if (enabled)
        disabledPlugins.removeString (key);
    else if (! disabledPlugins.contains (key))
        disabledPlugins.add (key);
}

bool PluginHost::isPluginEnabled (const juce::PluginDescription& d) const
{
    if (d.pluginFormatName == "VST" && ! vst2Enabled)
        return false;

    return ! disabledPlugins.contains (keyFor (d));
}

PluginHost::~PluginHost()
{
    knownPlugins.removeChangeListener (this);
}

juce::AudioPluginFormat* PluginHost::getVST3Format() const
{
    for (auto* format : formatManager.getFormats())
        if (format->getName() == "VST3")
            return format;

    return nullptr;
}

juce::Array<juce::PluginDescription> PluginHost::getEffectTypes() const
{
    juce::Array<juce::PluginDescription> result;

    for (const auto& type : knownPlugins.getTypes())
        if (! type.isInstrument && isPluginEnabled (type))
            result.add (type);

    NameComparator comparator;
    result.sort (comparator);
    return result;
}

juce::Array<juce::PluginDescription> PluginHost::getAllEffectTypes() const
{
    juce::Array<juce::PluginDescription> result;

    for (const auto& type : knownPlugins.getTypes())
        if (! type.isInstrument)
            result.add (type);

    NameComparator comparator;
    result.sort (comparator);
    return result;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& description,
                                                                       double sampleRate, int blockSize, juce::String& error)
{
    if (safeMode)
    {
        error = juce::String::fromUTF8 ("안전 모드: 플러그인을 불러오지 않습니다 (") + description.name + ")";
        return nullptr;
    }

    if (description.pluginFormatName == "VST" && ! vst2Enabled)
    {
        error = juce::String::fromUTF8 ("VST2 사용이 꺼져 있습니다 (플러그인 관리에서 켜세요): ") + description.name;
        return nullptr;
    }

    std::unique_ptr<juce::AudioPluginInstance> instance;

    try
    {
        instance = formatManager.createPluginInstance (description, sampleRate, blockSize, error);
    }
    catch (...)
    {
        instance = nullptr;
        error = juce::String::fromUTF8 ("플러그인을 만드는 중 오류가 났습니다: ") + description.name;
    }

    if (instance == nullptr && error.isEmpty())
        error = "Could not create plugin \"" + description.name + "\"";

    return instance;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const PluginSlotState& state,
                                                                       double sampleRate, int blockSize, juce::String& error)
{
    juce::PluginDescription description;
    bool haveDescription = false;

    if (state.descriptionXml.isNotEmpty())
        if (const auto xml = juce::parseXML (state.descriptionXml))
            haveDescription = description.loadFromXml (*xml);

    if (! haveDescription)
    {
        description.pluginFormatName = state.format;
        description.name = state.name;
        description.fileOrIdentifier = state.fileOrIdentifier;
        description.uniqueId = state.uniqueId;
    }

    if (description.uniqueId != 0)
    {
        for (const auto& known : knownPlugins.getTypes())
        {
            if (known.uniqueId == description.uniqueId && known.pluginFormatName == description.pluginFormatName)
            {
                description = known;
                break;
            }
        }
    }

    if (description.fileOrIdentifier.isEmpty())
    {
        error = "No plugin file recorded for \"" + state.name + "\"";
        return nullptr;
    }

    return createInstance (description, sampleRate, blockSize, error);
}

PluginChain::Factory PluginHost::makeFactory (double sampleRate, int blockSize)
{
    return [this, sampleRate, blockSize] (const PluginSlotState& state, juce::String& error)
    {
        return createInstance (state, sampleRate, blockSize, error);
    };
}

void PluginHost::loadKnownPluginsFromXml (const juce::XmlElement* xml)
{
    if (xml != nullptr)
        knownPlugins.recreateFromXml (*xml);
}

std::unique_ptr<juce::XmlElement> PluginHost::createKnownPluginsXml() const
{
    return knownPlugins.createXml();
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onKnownPluginsChanged)
        onKnownPluginsChanged();
}

} // namespace gocue
