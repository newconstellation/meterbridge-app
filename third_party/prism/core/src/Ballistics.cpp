#include "prism/Ballistics.h"
#include "prism/TruePeakMeter.h"

#include <cmath>
#include <numbers>

namespace prism
{

double onePoleCoefficient (double timeConstantSeconds, double sampleRate) noexcept
{
    if (timeConstantSeconds <= 0.0 || sampleRate <= 0.0)
        return 1.0;

    return 1.0 - std::exp (-1.0 / (timeConstantSeconds * sampleRate));
}

namespace
{
    double fallMultiplier (double dbPerSecond, double sampleRate) noexcept
    {
        return std::pow (10.0, -dbPerSecond / (20.0 * sampleRate));
    }
} // namespace

//==============================================================================
// A critically damped 2nd-order step response is 1 - (1 + t/tau) e^(-t/tau).
// It reaches 0.99 at t/tau = 6.638, so tau = 300 ms / 6.638.
static constexpr double vuTimeConstant = 0.300 / 6.638;

void VuMeter::prepare (double sampleRate) noexcept
{
    coeff = onePoleCoefficient (vuTimeConstant, sampleRate);
    reset();
}

void VuMeter::reset() noexcept
{
    stage1 = stage2 = 0.0;
}

void VuMeter::process (const float* samples, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        stage1 += coeff * (std::abs (static_cast<double> (samples[i])) - stage1);
        stage2 += coeff * (stage1 - stage2);
    }
}

double VuMeter::getReadingDbfs() const noexcept
{
    // Rectified average of a sine is 2/pi of its peak.
    return gainToDb (stage2 * std::numbers::pi * 0.5);
}

//==============================================================================
double PpmMeter::attackTimeConstant (Type t) noexcept
{
    switch (t)
    {
        case Type::dinTypeI:
        case Type::nordicTypeI: return 0.0017;
        case Type::ebuTypeIIb:
        case Type::bbcTypeIIa:  break;
    }
    return 0.0052;
}

double PpmMeter::steadyStateCorrectionDb (Type t) noexcept
{
    // The peak-charging envelope settles slightly below the crest of a steady
    // tone (independent of frequency). Meters are aligned on steady tone, so
    // these offsets were measured from this model and are added back.
    switch (t)
    {
        case Type::dinTypeI:
        case Type::nordicTypeI: return 0.207;
        case Type::ebuTypeIIb:
        case Type::bbcTypeIIa:  break;
    }
    return 0.317;
}

double PpmMeter::fallRateDbPerSecond (Type t) noexcept
{
    switch (t)
    {
        case Type::dinTypeI:
        case Type::nordicTypeI: return 20.0 / 1.5;
        case Type::ebuTypeIIb:
        case Type::bbcTypeIIa:  break;
    }
    return 24.0 / 2.8;
}

void PpmMeter::setType (Type newType) noexcept
{
    type = newType;
    updateCoefficients();
}

void PpmMeter::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    updateCoefficients();
    reset();
}

void PpmMeter::updateCoefficients() noexcept
{
    attackCoeff = onePoleCoefficient (attackTimeConstant (type), sampleRate);
    releaseMultiplier = fallMultiplier (fallRateDbPerSecond (type), sampleRate);
}

void PpmMeter::reset() noexcept
{
    envelope = 0.0;
}

void PpmMeter::process (const float* samples, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double x = std::abs (static_cast<double> (samples[i]));

        if (x > envelope)
            envelope += attackCoeff * (x - envelope);
        else
            envelope *= releaseMultiplier;
    }
}

double PpmMeter::getReadingDbfs() const noexcept
{
    return gainToDb (envelope) + steadyStateCorrectionDb (type);
}

//==============================================================================
void RmsMeter::setOptions (double timeConstantSeconds, bool aes17) noexcept
{
    timeConstant = timeConstantSeconds;
    useAes17 = aes17;
    coeff = onePoleCoefficient (timeConstant, sampleRate);
}

void RmsMeter::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    coeff = onePoleCoefficient (timeConstant, sampleRate);
    reset();
}

void RmsMeter::reset() noexcept
{
    meanSquare = 0.0;
}

void RmsMeter::process (const float* samples, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double x = samples[i];
        meanSquare += coeff * (x * x - meanSquare);
    }
}

double RmsMeter::getReadingDbfs() const noexcept
{
    const double rms = std::sqrt (meanSquare) * (useAes17 ? std::numbers::sqrt2 : 1.0);
    return gainToDb (rms);
}

//==============================================================================
void PeakFollower::setFallRate (double dbPerSecond) noexcept
{
    fallRate = dbPerSecond;
    releaseMultiplier = fallMultiplier (fallRate, sampleRate);
}

void PeakFollower::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    releaseMultiplier = fallMultiplier (fallRate, sampleRate);
    reset();
}

void PeakFollower::reset() noexcept
{
    envelope = 0.0;
}

void PeakFollower::process (const float* samples, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double x = std::abs (static_cast<double> (samples[i]));
        envelope = x > envelope ? x : envelope * releaseMultiplier;
    }
}

double PeakFollower::getReadingDbfs() const noexcept
{
    return gainToDb (envelope);
}

//==============================================================================
void DualNeedleMeter::prepare (double sampleRate) noexcept
{
    peak.setFallRate (13.3);
    peak.prepare (sampleRate);
    average.setOptions (0.6, true);
    average.prepare (sampleRate);
}

void DualNeedleMeter::reset() noexcept
{
    peak.reset();
    average.reset();
}

void DualNeedleMeter::process (const float* samples, int numSamples) noexcept
{
    peak.process (samples, numSamples);
    average.process (samples, numSamples);
}

} // namespace prism
