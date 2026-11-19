#pragma once

#include <array>
#include <atomic>
#include <limits>

/// Readings published by the audio thread and polled by the editor.
/// Every field is a lock-free atomic, so neither side ever blocks.
struct MeterState
{
    static constexpr int maxMeterChannels = 2;
    static constexpr float silence = -std::numeric_limits<float>::infinity();

    // Loudness, LUFS / LU
    std::atomic<float> momentary { silence };
    std::atomic<float> shortTerm { silence };
    std::atomic<float> integrated { silence };
    std::atomic<float> loudnessRange { 0.0f };
    std::atomic<float> momentaryMax { silence };
    std::atomic<float> shortTermMax { silence };

    // Peaks since reset, dBFS / dBTP
    std::atomic<float> truePeakMax { -150.0f };
    std::atomic<float> samplePeakMax { -150.0f };

    struct Channel
    {
        std::atomic<float> vu { -150.0f };        // dBFS, sine-peak equivalent
        std::atomic<float> ppm { -150.0f };       // dBFS
        std::atomic<float> rms { -150.0f };       // dBFS, AES17
        std::atomic<float> needlePeak { -150.0f };
        std::atomic<float> needleAverage { -150.0f };
        std::atomic<float> truePeak { -150.0f };  // dBTP, highest in the last 0.3 s
    };

    std::array<Channel, maxMeterChannels> channels;
    std::atomic<int> numChannels { 2 };

    // Peak + average of the mono sum (L + R) / 2, for the mono arc meter
    std::atomic<float> monoPeak { -150.0f };
    std::atomic<float> monoAverage { -150.0f };

    // Stereo
    std::atomic<float> correlation { 0.0f };
    std::atomic<float> balanceDb { 0.0f };
    std::atomic<float> width { 0.0f };
};
