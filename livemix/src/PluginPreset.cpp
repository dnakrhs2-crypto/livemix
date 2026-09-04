#include "PluginPreset.h"

#include "model/ProjectSerializer.h"
#include "model/SafeFileWrite.h"

#include <algorithm>

namespace gocue::livemix
{

namespace
{
    juce::String k (const char* utf8) { return juce::String::fromUTF8 (utf8); }
}

juce::String PluginPreset::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "LiveMix");
    root->setProperty ("kind", "pluginPreset");
    root->setProperty ("version", currentVersion);
    root->setProperty ("name", name);

    std::vector<PluginSlotState> capped = plugins;

    if ((int) capped.size() > maxPlugins)
        capped.resize ((size_t) maxPlugins);

    root->setProperty ("plugins", ProjectSerializer::pluginSlotsToVar (capped));
    return juce::JSON::toString (juce::var (root), false);
}

juce::Result PluginPreset::fromJson (const juce::String& json, PluginPreset& out)
{
    const auto root = juce::JSON::parse (json);

    if (root.getDynamicObject() == nullptr)
        return juce::Result::fail (k ("플러그인 프리셋 파일이 아닙니다 (JSON이 아닙니다)"));

    if (root.getProperty ("app", "").toString() != "LiveMix" || root.getProperty ("kind", "").toString() != "pluginPreset")
        return juce::Result::fail (k ("LiveMix 플러그인 프리셋 파일이 아닙니다"));

    const int version = (int) root.getProperty ("version", 1);

    if (version > currentVersion)
        return juce::Result::fail (k ("더 새로운 LiveMix로 저장한 프리셋입니다 (파일 버전 ") + juce::String (version) + k ("). LiveMix를 업데이트하세요."));

    if (ProjectSerializer::hasTrailingJsonData (json))
        return juce::Result::fail (k ("프리셋 파일 뒤에 알 수 없는 내용이 붙어 있습니다"));

    PluginPreset p;
    p.name = root.getProperty ("name", "").toString().trim();
    p.plugins = ProjectSerializer::pluginSlotsFromVar (root.getProperty ("plugins", juce::var()));

    if ((int) p.plugins.size() > maxPlugins)
        p.plugins.resize ((size_t) maxPlugins);

    out = std::move (p);
    return juce::Result::ok();
}

juce::Result PluginPreset::save (const juce::File& target) const
{
    const auto json = toJson();
    const auto bytes = (juce::int64) json.getNumBytesAsUTF8();

    if (bytes > maxFileBytes)
        return juce::Result::fail (k ("프리셋이 너무 커서 저장하지 않았습니다 (") + juce::File::descriptionOfSizeInBytes (bytes) + ")");

    target.getParentDirectory().createDirectory();
    return SafeFileWrite::writeTextVerified (target, json, [] (const juce::String& readBack) -> juce::Result
    {
        PluginPreset check;
        return fromJson (readBack, check);
    });
}

juce::Result PluginPreset::load (const juce::File& file, PluginPreset& out)
{
    if (! file.existsAsFile())
        return juce::Result::fail (k ("파일이 없습니다: ") + file.getFullPathName());

    if (file.getSize() > maxFileBytes)
        return juce::Result::fail (k ("프리셋 파일이 너무 큽니다 (") + juce::File::descriptionOfSizeInBytes (file.getSize()) + k ("): 프리셋 파일이 아닙니다"));

    const auto result = fromJson (file.loadFileAsString(), out);

    if (result.wasOk())
    {
        out.file = file;

        if (out.name.isEmpty())
            out.name = file.getFileNameWithoutExtension();   // an unnamed preset goes by its file
    }

    return result;
}

juce::File PluginPreset::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("LiveMix").getChildFile (k ("플러그인 프리셋"));
}

juce::String PluginPreset::fileNameFor (const juce::String& name)
{
    auto legal = juce::File::createLegalFileName (name.trim());

    if (legal.isEmpty() || legal.startsWithChar ('.'))
        legal = k ("프리셋") + legal;

    return legal + fileExtension;
}

juce::File PluginPreset::fileFor (const juce::String& name, const juce::File& folder)
{
    return folder.getChildFile (fileNameFor (name));
}

bool PluginPreset::isPresetFileName (const juce::String& fileName)
{
    return fileName.endsWithIgnoreCase (fileExtension);
}

std::vector<PluginPreset> PluginPreset::listFolder (const juce::File& folder, juce::StringArray* problems)
{
    std::vector<PluginPreset> found;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*" + juce::String (fileExtension)))
    {
        PluginPreset p;

        if (const auto result = load (file, p); result.failed())
        {
            if (problems != nullptr)
                problems->add (file.getFileName() + ": " + result.getErrorMessage());

            continue;
        }

        found.push_back (std::move (p));
    }

    std::sort (found.begin(), found.end(), [] (const PluginPreset& a, const PluginPreset& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return found;
}

juce::String PluginPreset::summary() const
{
    juce::StringArray parts;

    for (size_t i = 0; i < plugins.size(); ++i)
        parts.add (juce::String ((int) i + 1) + ". " + plugins[i].name);

    return parts.joinIntoString (juce::String::fromUTF8 (" \xE2\x86\x92 "));   // →
}

} // namespace gocue::livemix
