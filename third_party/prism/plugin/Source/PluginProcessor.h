#pragma once

#include "MeterState.h"

#include <prism/Ballistics.h>
#include <prism/LoudnessHistory.h>
#include <prism/LoudnessMeter.h>
#include <prism/StereoAnalyzer.h>
#include <prism/TruePeakMeter.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace ParamIds
{
    // Parameter IDs are permanent once released: never rename or reuse them.
    inline constexpr const char* vuReference = "vuRef";
    inline constexpr const char* ppmType = "ppmType";
    inline constexpr const char* kScale = "kScale";
    inline constexpr const char* loudnessTarget = "loudnessTarget";
    inline constexpr const char* truePeakCeiling = "tpCeiling";
    inline constexpr const char* radarTurn = "radarTurn";
}

/// Choice lists shared by the processor and editor. Order is part of saved state.
namespace Choices
{
    inline const juce::StringArray vuReferences { "-20 dBFS", "-18 dBFS", "-14 dBFS", "-12 dBFS", "-10 dBFS" };
    inline constexpr std::array<float, 5> vuReferenceValues { -20.0f, -18.0f, -14.0f, -12.0f, -10.0f };

    inline const juce::StringArray ppmTypes { "EBU", "BBC", "DIN", "Nordic" };

    inline const juce::StringArray kScales { "K-20", "K-14", "K-12" };
    inline constexpr std::array<float, 3> kScaleValues { 20.0f, 14.0f, 12.0f };

    inline const juce::StringArray loudnessTargets { "-14 LUFS Streaming", "-16 LUFS Podcast", "-23 LUFS EBU R128", "-24 LKFS ATSC A/85" };
    inline constexpr std::array<float, 4> loudnessTargetValues { -14.0f, -16.0f, -23.0f, -24.0f };

    inline const juce::StringArray truePeakCeilings { "-0.1 dBTP", "-1.0 dBTP", "-2.0 dBTP" };
    inline constexpr std::array<float, 3> truePeakCeilingValues { -0.1f, -1.0f, -2.0f };

    /// Time for one turn of the loudness radar.
    inline const juce::StringArray radarTurns { "1 min", "2 min", "4 min", "10 min", "30 min", "60 min" };
    inline constexpr std::array<float, 6> radarTurnMinutes { 1.0f, 2.0f, 4.0f, 10.0f, 30.0f, 60.0f };
}

class PrismMeterProcessor final : public juce::AudioProcessor
{
public:
    PrismMeterProcessor();
    ~PrismMeterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    MeterState& getMeterState() noexcept { return meterState; }

    /// Short-term loudness, one point per 100 ms, for the radar. Written by the
    /// audio thread, safe to read from the UI.
    const prism::LoudnessHistory& getShortTermHistory() const noexcept { return shortTermHistory; }

    /// Safe to call from any thread; the audio thread performs the reset.
    void requestReset() noexcept { resetRequested.store (true); }

    /// Mono (mid) samples for the spectrum view. Single producer (audio thread),
    /// single consumer (editor timer).
    int readSpectrumSamples (float* dest, int maxSamples) noexcept;
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    /// Left/right sample pairs for the vectorscope. Same threading rules as the spectrum FIFO.
    int readScopeSamples (float* left, float* right, int maxSamples) noexcept;

private:
    void pushSpectrumSamples (const float* const* channels, int numChannels, int numSamples) noexcept;
    void pushScopeSamples (const float* const* channels, int numChannels, int numSamples) noexcept;
    void publishReadings (int numChannels) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* ppmTypeParam = nullptr;

    MeterState meterState;
    std::atomic<bool> resetRequested { false };
    std::atomic<double> currentSampleRate { 48000.0 };

    prism::LoudnessMeter loudness;
    prism::LoudnessHistory shortTermHistory;
    std::uint64_t lastLoudnessStep = 0;
    prism::StereoAnalyzer stereo;
    std::array<prism::TruePeakMeter, MeterState::maxMeterChannels> truePeak;
    std::array<prism::VuMeter, MeterState::maxMeterChannels> vu;
    std::array<prism::PpmMeter, MeterState::maxMeterChannels> ppm;
    std::array<prism::RmsMeter, MeterState::maxMeterChannels> rms;
    std::array<prism::DualNeedleMeter, MeterState::maxMeterChannels> needles;
    prism::DualNeedleMeter monoNeedle;

    // Recent true peak per channel, held so the UI (30 Hz) never misses an over.
    std::array<float, MeterState::maxMeterChannels> recentPeakGain {};
    std::array<int, MeterState::maxMeterChannels> recentPeakAge {};
    int peakHoldSamples = 14400;
    int lastPpmType = -1;

    static constexpr int spectrumFifoSize = 1 << 15;
    juce::AbstractFifo spectrumFifo { spectrumFifoSize };
    std::vector<float> spectrumBuffer = std::vector<float> (spectrumFifoSize, 0.0f);
    std::vector<float> monoScratch;

    static constexpr int scopeFifoSize = 1 << 14;
    juce::AbstractFifo scopeFifo { scopeFifoSize };
    std::vector<float> scopeLeft = std::vector<float> (scopeFifoSize, 0.0f);
    std::vector<float> scopeRight = std::vector<float> (scopeFifoSize, 0.0f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismMeterProcessor)
};
