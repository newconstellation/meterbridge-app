#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

/// Applies the saved meter scale choices (loudness target, ceiling, VU
/// reference, PPM type, K scale) to every slot's processor.
void applyMeterScales (AudioEngine&, const juce::ValueTree& scales);

/// Settings, kept out of the meter windows: audio device, the source library
/// (any number of mono or stereo sources), which four are in view slots A-D,
/// meter scales, and launch options.
class SettingsContent final : public juce::Component
{
public:
    SettingsContent (AudioEngine&, AppSettings&);
    ~SettingsContent() override;

    /// Device channels changed: rebuild the channel menus.
    void refreshChannels();
    /// A slot changed elsewhere (for example from the overlay): update the slot menus.
    void refreshSlots();

    std::function<void()> onNewWindow;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LibraryRow;
    class LibraryList;

    struct ScaleRow
    {
        const char* paramId;
        juce::Label label;
        juce::ComboBox box;
    };

    void rebuildLibraryRows();
    void libraryEdited();
    void addSource();
    void removeSource (int index);
    void scaleEdited();

    AudioEngine& engine;
    AppSettings& settings;

    juce::AudioDeviceSelectorComponent deviceSelector;

    std::unique_ptr<LibraryList> libraryList;
    juce::Viewport libraryViewport;
    juce::TextButton addSourceButton { "+ Add source" };

    std::array<juce::ComboBox, Sources::count> slotBoxes;

    std::array<std::unique_ptr<ScaleRow>, 5> scales;
    juce::ToggleButton fullScreenOnLaunch { "Full screen on launch (each window on its own display)" };
    juce::TextButton newWindowButton { "New display window" };

    juce::Rectangle<int> devicePanel, metersPanel, libraryPanel, slotsPanel;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (AudioEngine&, AppSettings&);
    SettingsContent& getContent() noexcept { return *content; }
    void closeButtonPressed() override { setVisible (false); }

private:
    SettingsContent* content = nullptr;
};
