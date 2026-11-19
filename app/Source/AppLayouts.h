#pragma once

#include "ui/Layouts.h"

/// The views a Meter Bridge window can show. Grid views reuse Prism's meter
/// slots (each slot keeps Prism's own tool menu); the overlay is our own.
namespace AppViews
{
    inline const juce::String quad = "quad", strip = "strip4", panel = "panel", arc = "arc", overlay = "overlay";

    inline const juce::StringArray& ids()
    {
        static const juce::StringArray list { quad, strip, panel, arc, overlay };
        return list;
    }

    inline juce::String name (const juce::String& id)
    {
        if (id == quad)    return "Quad (2 x 2)";
        if (id == strip)   return "Row of four";
        if (id == panel)   return "Meter panel";
        if (id == arc)     return "Arched LED only";
        if (id == overlay) return "Spectrum overlay";
        return id;
    }

    /// Grid layouts. Quad and Row of four are sized for the 12" display
    /// (1920 x 720), the meter panels for the 8" display (1920 x 480).
    inline DisplayPreset grid (const juce::String& id)
    {
        using K = MeterKind;

        if (id == strip)
            return { strip, name (strip), {}, 1920, 720, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f },
                     { { 0, 0, 1, 1, K::loudness }, { 1, 0, 1, 1, K::spectrum },
                       { 2, 0, 1, 1, K::peakAverage }, { 3, 0, 1, 1, K::vu } } };

        if (id == panel)
            return { panel, name (panel), {}, 1920, 480, { 2.4f, 1.0f }, { 1.0f },
                     { { 0, 0, 1, 1, K::arcStereo }, { 1, 0, 1, 1, K::loudness } } };

        if (id == arc)
            return { arc, name (arc), {}, 1920, 480, { 1.0f }, { 1.0f },
                     { { 0, 0, 1, 1, K::arcStereo } } };

        return { quad, name (quad), {}, 1920, 720, { 1.0f, 1.0f }, { 1.0f, 1.0f },
                 { { 0, 0, 1, 1, K::loudness }, { 1, 0, 1, 1, K::spectrum },
                   { 0, 1, 1, 1, K::barStereo }, { 1, 1, 1, 1, K::phase } } };
    }
}
