#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace prism
{

/// Fixed-length record of one loudness value per 100 ms step, kept by the audio
/// thread so that a history display (the loudness radar) still shows the whole
/// run after its window was closed and opened again.
///
/// Threading: one writer (push / clear, the audio thread) and any number of
/// readers (the UI). Nothing allocates or locks after construction. A reader
/// that races the writer can at worst see a value that was just overwritten,
/// which a display does not notice.
class LoudnessHistory
{
public:
    /// 60 minutes at 10 points a second.
    static constexpr int defaultCapacity = 60 * 60 * 10;

    explicit LoudnessHistory (int capacity = defaultCapacity);

    int capacity() const noexcept { return static_cast<int> (values.size()); }

    /// Appends one point. Writer thread only.
    void push (float value) noexcept;

    /// Starts a new run: earlier points are no longer returned. Writer thread only.
    void clear() noexcept;

    /// Points pushed since the last clear (may exceed capacity).
    std::uint64_t size() const noexcept;

    /// Copies up to maxPoints of the newest points into dest, oldest first, and
    /// returns how many were copied. firstIndex receives the run index of
    /// dest[0], counted from 0 at the last clear.
    int readLatest (float* dest, int maxPoints, std::uint64_t& firstIndex) const noexcept;

private:
    std::vector<std::atomic<float>> values;
    std::atomic<std::uint64_t> written { 0 };    // total pushes, never reset
    std::atomic<std::uint64_t> runStart { 0 };   // value of written at the last clear
};

} // namespace prism
