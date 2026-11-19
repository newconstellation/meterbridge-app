#pragma once

#include "PluginProcessor.h"
#include "ui/Layouts.h"
#include "ui/MeterModules.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

class PrismMeterEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit PrismMeterEditor (PrismMeterProcessor&);
    ~PrismMeterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /// Switches to a display preset: resizes the window to the preset's size
    /// and restores the preset's default meters in every slot.
    void applyPreset (const juce::String& presetId);

    const DisplayPreset& getCurrentPreset() const noexcept { return *currentPreset; }

private:
    void timerCallback() override;

    /// Builds the slots for the current preset with the given meters.
    void buildSlots (const std::vector<MeterKind>& kinds);
    void layoutSlots (juce::Rectangle<int> area);

    void saveLayoutToState();
    void loadLayoutFromState();

    struct LabelledCombo
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void setUpCombo (LabelledCombo& c, const juce::String& name, const juce::StringArray& items, const char* paramId);
    void styleCombo (juce::ComboBox& box);

    PrismMeterProcessor& processor;

    const DisplayPreset* currentPreset = &DisplayPresets::all().front();
    std::vector<std::unique_ptr<MeterSlot>> slots;

    juce::ComboBox displayBox;
    juce::TextButton resetButton { "Reset" };
    juce::ToggleButton tiltButton { "Tilt" };
    LabelledCombo targetCombo, ceilingCombo, vuCombo, ppmCombo, kCombo;

    std::vector<float> spectrumScratch = std::vector<float> (8192);
    std::vector<float> spectrumCollected;
    std::vector<float> scopeLeft = std::vector<float> (4096), scopeRight = std::vector<float> (4096);
    int tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismMeterEditor)
};
