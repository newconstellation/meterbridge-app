#pragma once

namespace prism
{

/// One-pole smoothing coefficient for a time constant in seconds.
double onePoleCoefficient (double timeConstantSeconds, double sampleRate) noexcept;

//==============================================================================
/// Volume unit meter (IEC 60268-17 style ballistics).
///
/// Full-wave rectified signal through a critically damped second-order
/// integrator, tuned so a step reaches 99 % of its final reading in 300 ms.
/// A real moving-coil VU overshoots by 1 to 1.5 %; this model does not (our design).
/// The reading is scaled so a steady sine reads its own peak level, which makes
/// "0 VU = -18 dBFS" mean a sine whose peaks sit at -18 dBFS.
class VuMeter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* samples, int numSamples) noexcept;

    /// Reading in dBFS (sine-peak equivalent). Subtract the reference level to get VU.
    double getReadingDbfs() const noexcept;

private:
    double coeff = 0.0;
    double stage1 = 0.0, stage2 = 0.0;
};

//==============================================================================
/// Quasi-peak programme meter (IEC 60268-10 families).
///
/// Attack time constants are our calibration so a 5 kHz tone burst reads close
/// to the IEC targets (Type I: 10 ms burst -1 dB; Type II: 10 ms burst -4 dB),
/// with a fixed offset so a steady tone reads its peak level.
/// Fall-back is linear in dB at the IEC rates. Scale markings (BBC 1-7, EBU
/// +/-12, DIN -50..+5) are a display concern; this class reports dBFS.
class PpmMeter
{
public:
    enum class Type
    {
        ebuTypeIIb,  ///< 10 ms integration, 24 dB fall in 2.8 s
        bbcTypeIIa,  ///< same ballistics as EBU, different scale
        dinTypeI,    ///< 5 ms integration, 20 dB fall in 1.5 s
        nordicTypeI  ///< same ballistics as DIN, different scale
    };

    void setType (Type newType) noexcept;
    Type getType() const noexcept { return type; }

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* samples, int numSamples) noexcept;

    double getReadingDbfs() const noexcept;

    static double attackTimeConstant (Type t) noexcept;
    static double fallRateDbPerSecond (Type t) noexcept;
    static double steadyStateCorrectionDb (Type t) noexcept;

private:
    void updateCoefficients() noexcept;

    Type type = Type::ebuTypeIIb;
    double sampleRate = 48000.0;
    double attackCoeff = 0.0;
    double releaseMultiplier = 1.0;
    double envelope = 0.0;
};

//==============================================================================
/// RMS meter with an exponential integrator. Used for the K-System scales and
/// as the average needle of the dual-needle meter.
class RmsMeter
{
public:
    /// timeConstant in seconds. aes17 adds +3.01 dB so a sine reads its peak level.
    void setOptions (double timeConstantSeconds, bool aes17) noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* samples, int numSamples) noexcept;

    double getReadingDbfs() const noexcept;

private:
    double timeConstant = 0.3;
    bool useAes17 = true;
    double sampleRate = 48000.0;
    double coeff = 0.0;
    double meanSquare = 0.0;
};

//==============================================================================
/// Instant-attack peak follower with a linear-in-dB fall-back.
class PeakFollower
{
public:
    void setFallRate (double dbPerSecond) noexcept;
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* samples, int numSamples) noexcept;

    double getReadingDbfs() const noexcept;

private:
    double fallRate = 13.3;
    double sampleRate = 48000.0;
    double releaseMultiplier = 1.0;
    double envelope = 0.0;
};

//==============================================================================
/// Dual-needle peak plus average meter (our design).
///
/// Shows the transient peak and the perceived density on one scale, so the gap
/// between the two needles is the crest factor at a glance. The peak needle
/// has instant attack and falls at 13.3 dB/s; the average needle is an AES17
/// RMS reading with a 600 ms time constant, so a steady sine puts both needles
/// in the same place.
class DualNeedleMeter
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (const float* samples, int numSamples) noexcept;

    double getPeakDbfs() const noexcept { return peak.getReadingDbfs(); }
    double getAverageDbfs() const noexcept { return average.getReadingDbfs(); }

private:
    PeakFollower peak;
    RmsMeter average;
};

} // namespace prism
