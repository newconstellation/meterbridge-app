#include "AppSettings.h"

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Meter Bridge";
    options.folderName = "Meter Bridge";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    properties.setStorageParameters (options);
}

juce::PropertiesFile& AppSettings::file() { return *properties.getUserSettings(); }
const juce::PropertiesFile& AppSettings::file() const
{
    return *const_cast<juce::ApplicationProperties&> (properties).getUserSettings();
}

juce::ValueTree AppSettings::loadTree (const juce::String& key, const juce::Identifier& type) const
{
    if (auto xml = file().getXmlValue (key))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (type))
            return tree;
    }
    return juce::ValueTree (type);
}

void AppSettings::saveTree (const juce::String& key, const juce::ValueTree& tree)
{
    if (auto xml = tree.createXml())
        file().setValue (key, xml.get());
}

std::vector<SourceDef> AppSettings::loadLibrary() const
{
    std::vector<SourceDef> library;
    const auto tree = loadTree ("library", "Library");
    for (const auto& child : tree)
    {
        SourceDef d;
        d.id = child.getProperty ("id").toString();
        d.name = child.getProperty ("name").toString();
        d.colour = juce::Colour::fromString (child.getProperty ("colour", "ff8d96a0").toString());
        d.stereo = child.getProperty ("stereo", true);
        d.left = child.getProperty ("left", 0);
        d.right = child.getProperty ("right", 1);
        if (d.id.isNotEmpty())
            library.push_back (d);
    }

    if (library.empty())
    {
        // First run: the first eight inputs as four stereo pairs.
        for (int i = 0; i < 4; ++i)
            library.push_back ({ juce::Uuid().toString(), "Inputs " + juce::String (i * 2 + 1) + "-" + juce::String (i * 2 + 2),
                                 Sources::paletteColour (i), true, i * 2, i * 2 + 1 });
    }
    return library;
}

void AppSettings::saveLibrary (const std::vector<SourceDef>& library)
{
    juce::ValueTree tree ("Library");
    for (const auto& d : library)
    {
        juce::ValueTree child ("Source");
        child.setProperty ("id", d.id, nullptr);
        child.setProperty ("name", d.name, nullptr);
        child.setProperty ("colour", d.colour.toString(), nullptr);
        child.setProperty ("stereo", d.stereo, nullptr);
        child.setProperty ("left", d.left, nullptr);
        child.setProperty ("right", d.right, nullptr);
        tree.appendChild (child, nullptr);
    }
    saveTree ("library", tree);
}

std::array<juce::String, Sources::count> AppSettings::loadSlots() const
{
    std::array<juce::String, Sources::count> slots;
    const auto tree = loadTree ("slots", "Slots");
    for (int i = 0; i < Sources::count; ++i)
        slots[(size_t) i] = tree.getProperty ("slot" + juce::String (i)).toString();
    return slots;
}

void AppSettings::saveSlots (const std::array<juce::String, Sources::count>& slots)
{
    juce::ValueTree tree ("Slots");
    for (int i = 0; i < Sources::count; ++i)
        tree.setProperty ("slot" + juce::String (i), slots[(size_t) i], nullptr);
    saveTree ("slots", tree);
}

std::unique_ptr<juce::XmlElement> AppSettings::loadAudioDeviceState() const
{
    return file().getXmlValue ("audioDevice");
}

void AppSettings::saveAudioDeviceState (const juce::XmlElement* xml)
{
    if (xml != nullptr)
        file().setValue ("audioDevice", xml);
}

bool AppSettings::getFullScreenOnLaunch() const { return file().getBoolValue ("fullScreenOnLaunch", false); }
void AppSettings::setFullScreenOnLaunch (bool on) { file().setValue ("fullScreenOnLaunch", on); }

juce::ValueTree AppSettings::loadScales() const { return loadTree ("scales", "Scales"); }
void AppSettings::saveScales (const juce::ValueTree& tree) { saveTree ("scales", tree); }

juce::ValueTree AppSettings::loadWindows() const { return loadTree ("windows", "Windows"); }
void AppSettings::saveWindows (const juce::ValueTree& tree) { saveTree ("windows", tree); }

void AppSettings::flush() { properties.saveIfNeeded(); }
