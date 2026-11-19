#pragma once

#include "Biquad.h"

namespace prism
{

/// K-weighting filter from ITU-R BS.1770: a high-shelf "pre-filter" followed by
/// the RLB high-pass. Coefficients are computed for any sample rate from the
/// analogue prototype that reproduces the 48 kHz coefficients printed in the
/// recommendation.
class KWeighting
{
public:
    static BiquadCoeffs makeShelf (double sampleRate) noexcept;
    static BiquadCoeffs makeHighPass (double sampleRate) noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    double processSample (double x) noexcept { return highPass.processSample (shelf.processSample (x)); }

private:
    Biquad shelf, highPass;
};

} // namespace prism
