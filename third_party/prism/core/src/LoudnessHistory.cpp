#include "prism/LoudnessHistory.h"

#include <algorithm>

namespace prism
{

LoudnessHistory::LoudnessHistory (int capacity)
    : values (static_cast<size_t> (std::max (1, capacity)))
{
    for (auto& v : values)
        v.store (0.0f, std::memory_order_relaxed);
}

void LoudnessHistory::push (float value) noexcept
{
    const auto n = written.load (std::memory_order_relaxed);
    values[static_cast<size_t> (n % values.size())].store (value, std::memory_order_relaxed);
    written.store (n + 1, std::memory_order_release);
}

void LoudnessHistory::clear() noexcept
{
    runStart.store (written.load (std::memory_order_relaxed), std::memory_order_release);
}

std::uint64_t LoudnessHistory::size() const noexcept
{
    const auto start = runStart.load (std::memory_order_acquire);
    const auto end = written.load (std::memory_order_acquire);
    return end > start ? end - start : 0;
}

int LoudnessHistory::readLatest (float* dest, int maxPoints, std::uint64_t& firstIndex) const noexcept
{
    const auto start = runStart.load (std::memory_order_acquire);
    const auto end = written.load (std::memory_order_acquire);
    const auto cap = static_cast<std::uint64_t> (values.size());

    std::uint64_t available = end > start ? end - start : 0;
    available = std::min (available, cap);
    const auto count = std::min (available, static_cast<std::uint64_t> (std::max (0, maxPoints)));

    const auto from = end - count;
    for (std::uint64_t i = 0; i < count; ++i)
        dest[i] = values[static_cast<size_t> ((from + i) % cap)].load (std::memory_order_relaxed);

    firstIndex = from - start;
    return static_cast<int> (count);
}

} // namespace prism
