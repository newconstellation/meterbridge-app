#include "MeterHub.h"

MeterHub::MeterHub (AudioEngine& e) : engine (e)
{
    for (auto& f : feeds)
    {
        f.spectrum.reserve (16384);
        f.left.reserve (16384);
        f.right.reserve (16384);
    }
    startTimerHz (30);
}

MeterHub::~MeterHub()
{
    stopTimer();
}

ModuleContext MeterHub::contextFor (int source)
{
    auto& processor = engine.getProcessor (source);
    auto& feed = feeds[(size_t) source];

    ModuleContext context { processor.getMeterState(), processor.getParameters() };
    context.spectrumSamples = feed.spectrum.data();
    context.numSpectrumSamples = (int) feed.spectrum.size();
    context.scopeLeft = feed.scopeLeft.data();
    context.scopeRight = feed.scopeRight.data();
    context.numScopeSamples = feed.numScope;
    context.sampleRate = processor.getCurrentSampleRate();
    context.historyTick = (tickCount % 3) == 0;
    context.spectrumTilt = spectrumTilt;
    context.shortTermHistory = &processor.getShortTermHistory();
    return context;
}

void MeterHub::tick()
{
    ++tickCount;

    for (int s = 0; s < Sources::count; ++s)
    {
        auto& processor = engine.getProcessor (s);
        auto& feed = feeds[(size_t) s];

        feed.spectrum.clear();
        for (;;)
        {
            const int n = processor.readSpectrumSamples (scratch.data(), (int) scratch.size());
            if (n <= 0)
                break;
            feed.spectrum.insert (feed.spectrum.end(), scratch.begin(), scratch.begin() + n);
        }

        feed.left.clear();
        feed.right.clear();
        for (;;)
        {
            const int n = engine.readStereoSamples (s, scratch.data(), scratchRight.data(), (int) scratch.size());
            if (n <= 0)
                break;
            feed.left.insert (feed.left.end(), scratch.begin(), scratch.begin() + n);
            feed.right.insert (feed.right.end(), scratchRight.begin(), scratchRight.begin() + n);
        }

        feed.numScope = 0;
        for (;;)
        {
            const int got = processor.readScopeSamples (feed.scopeLeft.data(), feed.scopeRight.data(),
                                                        (int) feed.scopeLeft.size());
            if (got <= 0)
                break;
            feed.numScope = got; // keep only the newest chunk
        }
    }

    listeners.call ([this] (Listener& l) { l.meterTick (*this); });
}
