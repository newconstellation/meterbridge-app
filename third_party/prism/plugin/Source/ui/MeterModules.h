#pragma once

#include "Layouts.h"
#include "MeterViews.h"
#include "RadarView.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

struct MeterState;

namespace prism
{
    class LoudnessHistory;
}

/// Everything a meter needs for one UI tick. Built once per tick by the editor
/// and shared by every slot, so several slots can show the same tool.
struct ModuleContext
{
    const MeterState& state;
    juce::AudioProcessorValueTreeState& params;
    const float* spectrumSamples = nullptr;
    int numSpectrumSamples = 0;
    const float* scopeLeft = nullptr;
    const float* scopeRight = nullptr;
    int numScopeSamples = 0;
    double sampleRate = 48000.0;
    bool historyTick = false;   ///< true at 10 Hz
    bool spectrumTilt = true;
    const prism::LoudnessHistory* shortTermHistory = nullptr;   ///< 10 points a second, kept by the processor
};

/// A meter tool placed in a layout slot.
class MeterModule : public juce::Component
{
public:
    ~MeterModule() override = default;

    virtual void update (const ModuleContext&) = 0;

    /// Clears peak holds and histories (the Reset button).
    virtual void resetDisplay() {}

    static std::unique_ptr<MeterModule> create (MeterKind kind);
};

/// A layout cell: hosts one module and a small menu button in its title strip
/// for choosing which tool the cell shows.
class MeterSlot final : public juce::Component
{
public:
    MeterSlot();

    void setKind (MeterKind newKind);
    MeterKind getKind() const noexcept { return kind; }
    MeterModule* getModule() const noexcept { return module.get(); }

    /// Called after the person picks a different tool for this slot.
    std::function<void (MeterKind)> onKindChosen;

    void resized() override;

private:
    void showKindMenu();

    MeterKind kind = MeterKind::loudness;
    std::unique_ptr<MeterModule> module;
    juce::TextButton menuButton { juce::String::fromUTF8 ("\xe2\x96\xbe") };
};
