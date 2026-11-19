#pragma once

#include "AudioEngine.h"
#include "ui/MeterModules.h"

#include <array>
#include <vector>

/// The one UI clock (30 Hz). Each tick it drains every meter's analyser
/// FIFOs once and hands the samples to every open window, so several windows
/// and slots can show the same source without stealing each other's samples.
class MeterHub final : private juce::Timer
{
public:
    explicit MeterHub (AudioEngine&);
    ~MeterHub() override;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void meterTick (MeterHub&) = 0;
    };

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    AudioEngine& getEngine() noexcept { return engine; }

    /// Context for Prism's meter modules, for one engine meter (see
    /// AudioEngine::meterFor), valid during a tick.
    ModuleContext contextFor (int meter);

    const std::vector<float>& getLeftSamples (int meter) const { return feeds[(size_t) meter].left; }
    const std::vector<float>& getRightSamples (int meter) const { return feeds[(size_t) meter].right; }
    const MeterState& getState (int meter) { return engine.getProcessor (meter).getMeterState(); }

    bool spectrumTilt = true;

    /// One UI tick: drain the FIFOs and update every listener. The timer calls
    /// this; the snapshot tool calls it directly.
    void tick();

private:
    void timerCallback() override { tick(); }

    struct Feed
    {
        std::vector<float> spectrum, left, right;
        std::vector<float> scopeLeft = std::vector<float> (4096), scopeRight = std::vector<float> (4096);
        int numScope = 0;
    };

    AudioEngine& engine;
    std::array<Feed, AudioEngine::maxMeters> feeds;
    std::vector<float> scratch = std::vector<float> (8192), scratchRight = std::vector<float> (8192);
    juce::ListenerList<Listener> listeners;
    int tickCount = 0;
};
