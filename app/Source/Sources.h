#pragma once

#include <juce_graphics/juce_graphics.h>

/// A source in the library: one input to meter, for example a stereo keyboard
/// on two Dante Virtual Soundcard channels or a mono vocal mic. The library
/// can hold any number of sources; up to four are active in the view slots
/// A to D at a time.
struct SourceDef
{
    juce::String id;            ///< stable, saved
    juce::String name;
    juce::Colour colour;
    bool stereo = true;
    int left = 0;               ///< 0-based device input channel (the only one when mono)
    int right = 1;

    int rightChannel() const noexcept { return stereo ? right : left; }

    juce::String channelText() const
    {
        return stereo ? juce::String (left + 1) + "-" + juce::String (right + 1) : juce::String (left + 1);
    }
};

/// What one view slot is showing right now (a copy, safe to keep).
struct SourceConfig
{
    bool enabled = false;       ///< a source is assigned to this slot
    juce::String sourceId;
    juce::String name;
    juce::Colour colour { 0xff8d96a0 };
    bool stereo = true;
    int meter = -1;             ///< the engine meter that measures this source

    juce::String displayName (int slot) const;
};

namespace Sources
{
    /// Number of view slots (A to D).
    inline constexpr int count = 4;

    inline juce::String letter (int slot)
    {
        return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + slot));
    }

    struct NamedColour { const char* name; juce::uint32 argb; };

    /// Colours a source can take; new sources cycle through them.
    inline const std::vector<NamedColour>& palette()
    {
        static const std::vector<NamedColour> colours {
            { "Sky", 0xff4fc3f7 }, { "Amber", 0xffffa726 }, { "Violet", 0xffd96bff }, { "Leaf", 0xff7ddc5a },
            { "Rose", 0xffff5c9d }, { "Aqua", 0xff2ee6c4 }, { "Gold", 0xfff7d23b }, { "Coral", 0xffff7a45 },
        };
        return colours;
    }

    inline juce::Colour paletteColour (int index)
    {
        const auto& p = palette();
        return juce::Colour (p[(size_t) (index % (int) p.size())].argb);
    }
}

inline juce::String SourceConfig::displayName (int slot) const
{
    return enabled ? (name.isNotEmpty() ? name : "Source") : "Slot " + Sources::letter (slot) + " (empty)";
}
