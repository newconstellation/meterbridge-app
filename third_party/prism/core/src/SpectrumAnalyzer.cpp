#include "prism/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism
{

namespace
{
    constexpr float floorDb = -160.0f;
}

void Fft::prepare (int order)
{
    size = 1 << order;
    bitReverse.resize (static_cast<size_t> (size));

    for (int i = 0; i < size; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitReverse[static_cast<size_t> (i)] = r;
    }

    twiddles.resize (static_cast<size_t> (size / 2));
    for (int k = 0; k < size / 2; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * k / size;
        twiddles[static_cast<size_t> (k)] = { static_cast<float> (std::cos (angle)), static_cast<float> (std::sin (angle)) };
    }
}

void Fft::forward (std::complex<float>* data) const noexcept
{
    for (int i = 0; i < size; ++i)
    {
        const int j = bitReverse[static_cast<size_t> (i)];
        if (j > i)
            std::swap (data[i], data[j]);
    }

    for (int len = 2; len <= size; len <<= 1)
    {
        const int half = len / 2;
        const int step = size / len;

        for (int start = 0; start < size; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const auto w = twiddles[static_cast<size_t> (k * step)];
                const auto a = data[start + k];
                const auto b = data[start + k + half] * w;
                data[start + k] = a + b;
                data[start + k + half] = a - b;
            }
        }
    }
}

//==============================================================================
void SpectrumAnalyzer::prepare (int fftOrder, double newSampleRate)
{
    sampleRate = newSampleRate;
    fft.prepare (std::clamp (fftOrder, 6, 16));

    const int n = fft.getSize();
    window.resize (static_cast<size_t> (n));
    double sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
        window[static_cast<size_t> (i)] = static_cast<float> (w);
        sum += w;
    }

    // A sine of amplitude A on a bin gives |X| = A * sum(w) / 2.
    windowGain = static_cast<float> (2.0 / sum);

    buffer.resize (static_cast<size_t> (n));
    averagedDb.assign (static_cast<size_t> (getNumBins()), floorDb);
    peakDb.assign (static_cast<size_t> (getNumBins()), floorDb);
}

void SpectrumAnalyzer::reset() noexcept
{
    std::fill (averagedDb.begin(), averagedDb.end(), floorDb);
    std::fill (peakDb.begin(), peakDb.end(), floorDb);
}

double SpectrumAnalyzer::getBinFrequency (int bin) const noexcept
{
    return bin * sampleRate / fft.getSize();
}

void SpectrumAnalyzer::processFrame (const float* frame) noexcept
{
    const int n = fft.getSize();

    for (int i = 0; i < n; ++i)
        buffer[static_cast<size_t> (i)] = { frame[i] * window[static_cast<size_t> (i)], 0.0f };

    fft.forward (buffer.data());

    const int bins = getNumBins();
    for (int k = 0; k < bins; ++k)
    {
        const float mag = std::abs (buffer[static_cast<size_t> (k)]) * windowGain;
        const float db = mag > 0.0f ? std::max (floorDb, 20.0f * std::log10 (mag)) : floorDb;

        auto& avg = averagedDb[static_cast<size_t> (k)];
        avg = averaging * avg + (1.0f - averaging) * db;

        auto& pk = peakDb[static_cast<size_t> (k)];
        pk = std::max (pk, db);
    }
}

float SpectrumAnalyzer::getLevelAtFrequency (double hz) const noexcept
{
    if (averagedDb.empty())
        return floorDb;

    const double pos = std::clamp (hz * fft.getSize() / sampleRate, 0.0, static_cast<double> (getNumBins() - 1));
    const auto i0 = static_cast<size_t> (pos);
    const auto i1 = std::min (i0 + 1, averagedDb.size() - 1);
    const auto frac = static_cast<float> (pos - static_cast<double> (i0));
    return averagedDb[i0] + frac * (averagedDb[i1] - averagedDb[i0]);
}

} // namespace prism
