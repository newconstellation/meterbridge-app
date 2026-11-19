#pragma once

namespace prism
{

/// Phase correlation, balance and width for a stereo pair, with exponential
/// averaging (default 300 ms).
class StereoAnalyzer
{
public:
    void setTimeConstant (double seconds) noexcept;
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* left, const float* right, int numSamples) noexcept;

    /// -1 (out of phase) .. 0 (uncorrelated) .. +1 (mono). 0 on silence.
    double getCorrelation() const noexcept;

    /// Left minus right level in dB. Positive leans left.
    double getBalanceDb() const noexcept;

    /// Side-to-mid energy ratio: 0 = mono, 1 = equal mid and side, > 1 = wider than uncorrelated.
    double getWidth() const noexcept;

private:
    double timeConstant = 0.3;
    double sampleRate = 48000.0;
    double coeff = 0.0;
    double ll = 0.0, rr = 0.0, lr = 0.0;
};

} // namespace prism
