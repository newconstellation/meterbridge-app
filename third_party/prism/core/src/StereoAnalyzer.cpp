#include "prism/StereoAnalyzer.h"
#include "prism/Ballistics.h"

#include <algorithm>
#include <cmath>

namespace prism
{

namespace
{
    constexpr double tiny = 1.0e-20;
}

void StereoAnalyzer::setTimeConstant (double seconds) noexcept
{
    timeConstant = seconds;
    coeff = onePoleCoefficient (timeConstant, sampleRate);
}

void StereoAnalyzer::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    coeff = onePoleCoefficient (timeConstant, sampleRate);
    reset();
}

void StereoAnalyzer::reset() noexcept
{
    ll = rr = lr = 0.0;
}

void StereoAnalyzer::process (const float* left, const float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i];
        const double r = right[i];
        ll += coeff * (l * l - ll);
        rr += coeff * (r * r - rr);
        lr += coeff * (l * r - lr);
    }
}

double StereoAnalyzer::getCorrelation() const noexcept
{
    const double denom = std::sqrt (ll * rr);
    if (denom < tiny)
        return 0.0;

    return std::clamp (lr / denom, -1.0, 1.0);
}

double StereoAnalyzer::getBalanceDb() const noexcept
{
    if (ll < tiny && rr < tiny)
        return 0.0;

    return 10.0 * std::log10 ((ll + tiny) / (rr + tiny));
}

double StereoAnalyzer::getWidth() const noexcept
{
    // mid = (l + r) / 2, side = (l - r) / 2
    const double mid = 0.25 * (ll + rr + 2.0 * lr);
    const double side = 0.25 * (ll + rr - 2.0 * lr);

    if (mid < tiny)
        return side < tiny ? 0.0 : 1.0e6;

    return std::max (0.0, side) / mid;
}

} // namespace prism
