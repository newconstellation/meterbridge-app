#include "prism/TruePeakMeter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism
{

namespace
{
    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double halfX = x * 0.5;

        for (int k = 1; k < 50; ++k)
        {
            term *= (halfX / k) * (halfX / k);
            sum += term;
            if (term < 1.0e-12 * sum)
                break;
        }
        return sum;
    }
} // namespace

double gainToDb (double gain, double floorDb) noexcept
{
    return gain > 0.0 ? std::max (floorDb, 20.0 * std::log10 (gain)) : floorDb;
}

void TruePeakMeter::prepare (double sampleRate)
{
    factor = sampleRate < 96000.0 ? 4 : (sampleRate < 192000.0 ? 2 : 1);

    const int totalTaps = factor * tapsPerPhase;
    coeffs.assign (static_cast<size_t> (totalTaps), 0.0f);
    history.assign (static_cast<size_t> (2 * tapsPerPhase), 0.0f);

    if (factor > 1)
    {
        constexpr double beta = 8.0;
        const double centre = (totalTaps - 1) * 0.5;
        const double i0Beta = besselI0 (beta);
        std::vector<double> h (static_cast<size_t> (totalTaps));

        for (int n = 0; n < totalTaps; ++n)
        {
            const double m = (n - centre) / factor;
            const double sinc = std::abs (m) < 1.0e-12 ? 1.0 : std::sin (std::numbers::pi * m) / (std::numbers::pi * m);
            const double r = (n - centre) / centre;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
            h[static_cast<size_t> (n)] = sinc * window;
        }

        // Split into phases and normalise each phase to unity DC gain.
        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k)
                sum += h[static_cast<size_t> (p + k * factor)];

            for (int k = 0; k < tapsPerPhase; ++k)
                coeffs[static_cast<size_t> (p * tapsPerPhase + k)] =
                    static_cast<float> (h[static_cast<size_t> (p + k * factor)] / sum);
        }
    }

    reset();
}

void TruePeakMeter::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    writePos = 0;
    resetPeaks();
}

void TruePeakMeter::resetPeaks() noexcept
{
    truePeak = 0.0f;
    samplePeak = 0.0f;
    blockTruePeak = 0.0f;
}

void TruePeakMeter::process (const float* samples, int numSamples) noexcept
{
    float blockPeak = 0.0f;
    float blockSample = 0.0f;

    if (factor == 1 || history.empty())
    {
        for (int i = 0; i < numSamples; ++i)
            blockSample = std::max (blockSample, std::abs (samples[i]));

        blockPeak = blockSample;
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = samples[i];
            blockSample = std::max (blockSample, std::abs (x));

            // Newest sample sits at index writePos; the doubled buffer lets us
            // read tapsPerPhase contiguous values going back in time.
            writePos = (writePos == 0 ? tapsPerPhase : writePos) - 1;
            history[static_cast<size_t> (writePos)] = x;
            history[static_cast<size_t> (writePos + tapsPerPhase)] = x;
            const float* hist = history.data() + writePos;

            for (int p = 0; p < factor; ++p)
            {
                const float* c = coeffs.data() + p * tapsPerPhase;
                float y = 0.0f;

                for (int k = 0; k < tapsPerPhase; ++k)
                    y += c[k] * hist[k];

                blockPeak = std::max (blockPeak, std::abs (y));
            }
        }

        blockPeak = std::max (blockPeak, blockSample);
    }

    blockTruePeak = blockPeak;
    truePeak = std::max (truePeak, blockPeak);
    samplePeak = std::max (samplePeak, blockSample);
}

} // namespace prism
