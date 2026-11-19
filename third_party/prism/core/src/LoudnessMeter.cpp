#include "prism/LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace prism
{

double energyToLufs (double energy) noexcept
{
    if (energy <= 0.0)
        return kSilenceDb;

    return -0.691 + 10.0 * std::log10 (energy);
}

//==============================================================================
LoudnessHistogram::LoudnessHistogram()
    : counts (static_cast<size_t> (numBins), 0u),
      energies (static_cast<size_t> (numBins), 0.0)
{
}

void LoudnessHistogram::clear() noexcept
{
    std::fill (counts.begin(), counts.end(), 0u);
    std::fill (energies.begin(), energies.end(), 0.0);
    totalCount = 0;
    totalEnergy = 0.0;
}

int LoudnessHistogram::binForLufs (double lufs) const noexcept
{
    const auto bin = static_cast<int> (std::floor ((lufs - minLufs) / binWidth));
    return std::clamp (bin, 0, numBins - 1);
}

double LoudnessHistogram::lufsForBinCentre (int bin) const noexcept
{
    return minLufs + (bin + 0.5) * binWidth;
}

int LoudnessHistogram::firstBinAbove (double gateLufs) const noexcept
{
    const auto bin = static_cast<int> (std::ceil ((gateLufs - minLufs) / binWidth));
    return std::clamp (bin, 0, numBins);
}

void LoudnessHistogram::add (double energy) noexcept
{
    const double lufs = energyToLufs (energy);

    if (! (lufs > minLufs)) // absolute gate: keep blocks above -70 LUFS
        return;

    const auto bin = static_cast<size_t> (binForLufs (lufs));
    ++counts[bin];
    energies[bin] += energy;
    ++totalCount;
    totalEnergy += energy;
}

double LoudnessHistogram::integrated() const noexcept
{
    if (totalCount == 0)
        return kSilenceDb;

    const double relativeGate = energyToLufs (totalEnergy / static_cast<double> (totalCount)) - 10.0;

    std::uint64_t n = 0;
    double sum = 0.0;

    for (int bin = firstBinAbove (relativeGate); bin < numBins; ++bin)
    {
        n += counts[static_cast<size_t> (bin)];
        sum += energies[static_cast<size_t> (bin)];
    }

    return n > 0 ? energyToLufs (sum / static_cast<double> (n)) : kSilenceDb;
}

double LoudnessHistogram::loudnessRange() const noexcept
{
    if (totalCount == 0)
        return 0.0;

    const double relativeGate = energyToLufs (totalEnergy / static_cast<double> (totalCount)) - 20.0;
    const int start = firstBinAbove (relativeGate);

    std::uint64_t n = 0;
    for (int bin = start; bin < numBins; ++bin)
        n += counts[static_cast<size_t> (bin)];

    if (n == 0)
        return 0.0;

    // Nearest-rank percentiles on the sorted gated values, as in Tech 3342.
    const auto rankLow = static_cast<std::uint64_t> (std::llround (0.10 * static_cast<double> (n - 1)));
    const auto rankHigh = static_cast<std::uint64_t> (std::llround (0.95 * static_cast<double> (n - 1)));

    double low = 0.0, high = 0.0;
    bool foundLow = false;
    std::uint64_t cumulative = 0;

    for (int bin = start; bin < numBins; ++bin)
    {
        cumulative += counts[static_cast<size_t> (bin)];

        if (! foundLow && cumulative > rankLow)
        {
            low = lufsForBinCentre (bin);
            foundLow = true;
        }

        if (cumulative > rankHigh)
        {
            high = lufsForBinCentre (bin);
            break;
        }
    }

    return std::max (0.0, high - low);
}

//==============================================================================
void LoudnessMeter::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channels = std::clamp (numChannels, 1, maxChannels);
    subBlockLength = std::max (1, static_cast<int> (std::lround (sampleRate * 0.1)));

    for (auto& f : filters)
        f.prepare (sampleRate);

    weights.fill (1.0);

    if (channels == 6) // L R C LFE Ls Rs
    {
        weights[3] = 0.0;
        weights[4] = 1.41;
        weights[5] = 1.41;
    }

    reset();
}

void LoudnessMeter::setChannelWeight (int channel, double weight) noexcept
{
    if (channel >= 0 && channel < maxChannels)
        weights[static_cast<size_t> (channel)] = weight;
}

void LoudnessMeter::reset() noexcept
{
    for (auto& f : filters)
        f.reset();

    ring.fill (0.0);
    ringPos = 0;
    blocksSeen = 0;
    subBlockCount = 0;
    subBlockSum = 0.0;
    momentary = kSilenceDb;
    shortTerm = kSilenceDb;
    resetIntegrated();
}

void LoudnessMeter::resetIntegrated() noexcept
{
    integratedHistogram.clear();
    rangeHistogram.clear();
    blocksSeenSinceReset = 0;
    integrated = kSilenceDb;
    loudnessRange = 0.0;
    momentaryMax = kSilenceDb;
    shortTermMax = kSilenceDb;
}

void LoudnessMeter::process (const float* const* channelData, int numChannels, int numSamples) noexcept
{
    const int usable = std::min (numChannels, channels);

    for (int i = 0; i < numSamples; ++i)
    {
        double weighted = 0.0;

        for (int ch = 0; ch < usable; ++ch)
        {
            const double y = filters[static_cast<size_t> (ch)].processSample (channelData[ch][i]);
            weighted += weights[static_cast<size_t> (ch)] * y * y;
        }

        subBlockSum += weighted;

        if (++subBlockCount >= subBlockLength)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock() noexcept
{
    ring[static_cast<size_t> (ringPos)] = subBlockSum / static_cast<double> (subBlockLength);
    ringPos = (ringPos + 1) % shortTermBlocks;
    subBlockSum = 0.0;
    subBlockCount = 0;
    ++blocksSeen;
    ++blocksSeenSinceReset;

    auto windowEnergy = [this] (int numBlocks)
    {
        double sum = 0.0;
        for (int k = 1; k <= numBlocks; ++k)
            sum += ring[static_cast<size_t> ((ringPos - k + shortTermBlocks) % shortTermBlocks)];
        return sum / static_cast<double> (numBlocks);
    };

    const double momentaryEnergy = windowEnergy (momentaryBlocks);
    const double shortTermEnergy = windowEnergy (shortTermBlocks);

    momentary = energyToLufs (momentaryEnergy);
    shortTerm = energyToLufs (shortTermEnergy);

    // Gated measurements only use windows that lie entirely after the last reset.
    if (blocksSeenSinceReset >= static_cast<std::uint64_t> (momentaryBlocks))
    {
        integratedHistogram.add (momentaryEnergy);
        integrated = integratedHistogram.integrated();
        momentaryMax = std::max (momentaryMax, momentary);
    }

    if (blocksSeenSinceReset >= static_cast<std::uint64_t> (shortTermBlocks))
    {
        rangeHistogram.add (shortTermEnergy);
        loudnessRange = rangeHistogram.loudnessRange();
        shortTermMax = std::max (shortTermMax, shortTerm);
    }
}

} // namespace prism
