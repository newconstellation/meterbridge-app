#pragma once

namespace prism
{

/// Normalised biquad coefficients (a0 == 1).
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

/// Transposed direct form II biquad, double precision state.
/// Real-time safe: no allocation, no locks.
class Biquad
{
public:
    void setCoefficients (const BiquadCoeffs& c) noexcept { coeffs = c; }
    const BiquadCoeffs& getCoefficients() const noexcept { return coeffs; }

    void reset() noexcept { s1 = s2 = 0.0; }

    double processSample (double x) noexcept
    {
        const double y = coeffs.b0 * x + s1;
        s1 = coeffs.b1 * x - coeffs.a1 * y + s2;
        s2 = coeffs.b2 * x - coeffs.a2 * y;
        return y;
    }

private:
    BiquadCoeffs coeffs;
    double s1 = 0.0, s2 = 0.0;
};

} // namespace prism
