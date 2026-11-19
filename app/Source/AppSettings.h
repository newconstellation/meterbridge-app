#pragma once

#include "Sources.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <memory>
#include <vector>

/// Everything Meter Bridge remembers between launches, in one properties file
/// (~/Library/Application Support/Meter Bridge/Meter Bridge.settings on a Mac).
class AppSettings
{
public:
    AppSettings();

    /// The source library (any number of sources).
    std::vector<SourceDef> loadLibrary() const;
    void saveLibrary (const std::vector<SourceDef>&);

    /// Source id in each view slot A to D (empty = slot unused).
    std::array<juce::String, Sources::count> loadSlots() const;
    void saveSlots (const std::array<juce::String, Sources::count>&);

    std::unique_ptr<juce::XmlElement> loadAudioDeviceState() const;
    void saveAudioDeviceState (const juce::XmlElement*);

    bool getFullScreenOnLaunch() const;
    void setFullScreenOnLaunch (bool);

    /// Meter scale choices shared by every source (indices into Prism's choice lists).
    juce::ValueTree loadScales() const;
    void saveScales (const juce::ValueTree&);

    /// One child per open display window.
    juce::ValueTree loadWindows() const;
    void saveWindows (const juce::ValueTree&);

    void flush();

private:
    juce::ValueTree loadTree (const juce::String& key, const juce::Identifier& type) const;
    void saveTree (const juce::String& key, const juce::ValueTree&);

    juce::ApplicationProperties properties;
    juce::PropertiesFile& file();
    const juce::PropertiesFile& file() const;
};
