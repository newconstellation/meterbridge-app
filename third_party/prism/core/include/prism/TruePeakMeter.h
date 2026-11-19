#pragma once

#include <vector>

namespace prism
{

/// Sample-peak and true-peak detector for one channel, per ITU-R BS.1770 Annex 2.
///
/// The signal is oversampled (4x below 96 kHz, 2x below 192 kHz, 1x above) with
/// a polyphase windowed-sinc interpolator and the absolute maximum is held.
/// The interpolator is our own design (Kaiser window, 32 taps per phase) rather
/// than the example filter in the annex; it is tighter than the annex example.
class TruePeakMeter
{
public:
    static constexpr int tapsPerPhase = 32;

    /// Allocates. Call from prepareToPlay.
    void prepare (double sampleRate);
    void reset() noexcept;

    /// Clears the held maxima but keeps the interpolator history.
    void resetPeaks() noexcept;

    void process (const float* samples, int numSamples) noexcept;

    int getOversamplingFactor() const noexcept { return factor; }

    /// Highest true peak since the last reset, linear.
    float getTruePeak() const noexcept { return truePeak; }
    /// Highest sample peak since the last reset, linear.
    float getSamplePeak() const noexcept { return samplePeak; }

    /// Highest true peak inside the most recent process() call, linear.
    float getBlockTruePeak() const noexcept { return blockTruePeak; }

private:
    int factor = 4;
    std::vector<float> coeffs;   // [phase * tapsPerPhase + k]
    std::vector<float> history;  // doubled ring buffer, 2 * tapsPerPhase
    int writePos = 0;

    float truePeak = 0.0f;
    float samplePeak = 0.0f;
    float blockTruePeak = 0.0f;
};

/// Linear gain to decibels, with a floor for silence.
double gainToDb (double gain, double floorDb = -150.0) noexcept;

} // namespace prism
