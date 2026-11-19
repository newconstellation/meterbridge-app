#pragma once

#include "KWeighting.h"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace prism
{

/// Value used for "no measurement yet" or digital silence.
inline constexpr double kSilenceDb = -std::numeric_limits<double>::infinity();

/// Converts a channel-weighted mean square (K-weighted) to LUFS per BS.1770.
double energyToLufs (double energy) noexcept;

/// Fixed-resolution histogram of block loudness used for the gated measurements.
/// Storage is allocated once in the constructor, so add() is real-time safe.
/// Resolution is 0.01 LU from -70 to +10 LUFS, far finer than any meter display.
class LoudnessHistogram
{
public:
    static constexpr double minLufs = -70.0;
    static constexpr double maxLufs = 10.0;
    static constexpr double binWidth = 0.01;
    static constexpr int numBins = static_cast<int> ((maxLufs - minLufs) / binWidth);

    LoudnessHistogram();

    void clear() noexcept;

    /// Adds one block. Blocks at or below the -70 LUFS absolute gate are ignored.
    void add (double energy) noexcept;

    /// Gated integrated loudness (BS.1770-5, relative gate -10 LU).
    double integrated() const noexcept;

    /// Loudness range (EBU Tech 3342, relative gate -20 LU, 10th to 95th percentile).
    double loudnessRange() const noexcept;

    std::uint64_t blockCount() const noexcept { return totalCount; }

private:
    int binForLufs (double lufs) const noexcept;
    double lufsForBinCentre (int bin) const noexcept;
    int firstBinAbove (double gateLufs) const noexcept;

    std::vector<std::uint32_t> counts;
    std::vector<double> energies;
    std::uint64_t totalCount = 0;
    double totalEnergy = 0.0;
};

/// Loudness meter per ITU-R BS.1770-5 / EBU R128 (Tech 3341 "EBU mode").
///
/// - Momentary: 400 ms sliding window
/// - Short-term: 3 s sliding window
/// - Integrated: gated over the whole run, 400 ms blocks with 75 % overlap
/// - Loudness range (LRA): EBU Tech 3342 from short-term values
///
/// Windows advance every 100 ms. Before a window has filled, the missing part
/// counts as silence, which matches a meter that was running on silence.
class LoudnessMeter
{
public:
    static constexpr int maxChannels = 8;

    LoudnessMeter() = default;

    /// Call from prepareToPlay. Not real-time safe only in the sense that it resets state.
    void prepare (double sampleRate, int numChannels);

    /// Channel weight G_i. Defaults: 1.0, except channels 4 and 5 of a 6 channel
    /// layout (Ls, Rs) at 1.41 and channel 3 (LFE) at 0, as in BS.1770.
    void setChannelWeight (int channel, double weight) noexcept;

    /// Clears everything.
    void reset() noexcept;

    /// Clears integrated, LRA and maxima but keeps the sliding windows running.
    void resetIntegrated() noexcept;

    void process (const float* const* channelData, int numChannels, int numSamples) noexcept;

    double getMomentary() const noexcept { return momentary; }
    double getShortTerm() const noexcept { return shortTerm; }
    double getIntegrated() const noexcept { return integrated; }
    double getLoudnessRange() const noexcept { return loudnessRange; }
    double getMomentaryMax() const noexcept { return momentaryMax; }
    double getShortTermMax() const noexcept { return shortTermMax; }

    /// Number of 100 ms steps completed since prepare() or reset(). Each step
    /// updates the momentary and short-term values.
    std::uint64_t getStepCount() const noexcept { return blocksSeen; }

private:
    static constexpr int momentaryBlocks = 4;   // 4 x 100 ms
    static constexpr int shortTermBlocks = 30;  // 30 x 100 ms

    void finishSubBlock() noexcept;

    double sampleRate = 48000.0;
    int channels = 2;
    int subBlockLength = 4800;
    int subBlockCount = 0;
    double subBlockSum = 0.0;

    std::array<KWeighting, maxChannels> filters {};
    std::array<double, maxChannels> weights {};

    std::array<double, shortTermBlocks> ring {};
    int ringPos = 0;
    std::uint64_t blocksSeen = 0;
    std::uint64_t blocksSeenSinceReset = 0;

    LoudnessHistogram integratedHistogram;
    LoudnessHistogram rangeHistogram;

    double momentary = kSilenceDb;
    double shortTerm = kSilenceDb;
    double integrated = kSilenceDb;
    double loudnessRange = 0.0;
    double momentaryMax = kSilenceDb;
    double shortTermMax = kSilenceDb;
};

} // namespace prism
