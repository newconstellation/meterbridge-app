#pragma once

#include <complex>
#include <vector>

namespace prism
{

/// In-place iterative radix-2 complex FFT. Tables are built in prepare().
class Fft
{
public:
    /// order = log2(size). Allocates.
    void prepare (int order);
    int getSize() const noexcept { return size; }

    /// Forward transform, in place. data must hold getSize() values.
    void forward (std::complex<float>* data) const noexcept;

private:
    int size = 0;
    std::vector<int> bitReverse;
    std::vector<std::complex<float>> twiddles;
};

/// FFT spectrum analyser for one signal.
///
/// Feed it frames of getFftSize() samples (the caller handles hop and overlap).
/// Uses a Hann window, scaled so a full-scale sine centred on a bin reads 0 dB.
/// Results are exponentially averaged across frames and a peak-hold curve is kept.
class SpectrumAnalyzer
{
public:
    /// Allocates. order 10..15 (1024..32768 points).
    void prepare (int fftOrder, double sampleRate);
    void reset() noexcept;

    /// 0 = no averaging, values near 1 = heavy averaging.
    void setAveraging (float amount) noexcept { averaging = amount; }

    /// Analyse one frame of getFftSize() samples.
    void processFrame (const float* frame) noexcept;

    int getFftSize() const noexcept { return fft.getSize(); }
    int getNumBins() const noexcept { return fft.getSize() / 2 + 1; }
    double getBinFrequency (int bin) const noexcept;

    /// Averaged magnitude per bin in dBFS.
    const std::vector<float>& getMagnitudesDb() const noexcept { return averagedDb; }
    const std::vector<float>& getPeakHoldDb() const noexcept { return peakDb; }

    /// Interpolated averaged level at an arbitrary frequency, dBFS.
    float getLevelAtFrequency (double hz) const noexcept;

private:
    Fft fft;
    double sampleRate = 48000.0;
    float averaging = 0.7f;
    float windowGain = 1.0f;
    std::vector<float> window;
    std::vector<std::complex<float>> buffer;
    std::vector<float> averagedDb;
    std::vector<float> peakDb;
};

} // namespace prism
