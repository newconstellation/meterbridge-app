#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

/// Prism Meter's visual language.
///
/// Hardware-console feel: near-black instrument faces set into charcoal panels.
/// Every tool has its own accent colour, shown in its title strip and in its
/// traces, so the eye finds an area by colour before reading a label.
/// Level meters use the studio convention of green / yellow / red segments.
namespace Theme
{
    // Chassis
    inline const juce::Colour background { 0xff0b0c0e };
    inline const juce::Colour panel { 0xff1b1d21 };
    inline const juce::Colour panelEdge { 0xff31353c };
    inline const juce::Colour face { 0xff050607 };        // instrument glass
    inline const juce::Colour grid { 0xff1f252c };
    inline const juce::Colour gridStrong { 0xff2e3640 };
    inline const juce::Colour textPrimary { 0xffeef1f4 };
    inline const juce::Colour textSecondary { 0xff8d96a0 };

    // Level zones (segment meters, numbers)
    inline const juce::Colour levelGreen { 0xff2fe05a };
    inline const juce::Colour levelYellow { 0xfff7d23b };
    inline const juce::Colour levelRed { 0xffff3b30 };

    // Per-tool accents
    inline const juce::Colour loudnessAccent { 0xffffa726 };  // amber
    inline const juce::Colour needleAccent { 0xffe8d7a8 };    // ivory
    inline const juce::Colour spectrumAccent { 0xff4fc3f7 };  // sky
    inline const juce::Colour phaseAccent { 0xffd96bff };     // violet
    inline const juce::Colour scopeTrace { 0xff6dff8a };      // phosphor green
    inline const juce::Colour vuAccent { 0xff7ddc5a };        // leaf green
    inline const juce::Colour ppmAccent { 0xffff7a45 };       // orange
    inline const juce::Colour kAccent { 0xff5b8cff };         // blue
    inline const juce::Colour radarAccent { 0xffff5c9d };     // rose
    inline const juce::Colour arcAccent { 0xff2ee6c4 };       // aqua (arc LED peak + average)

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font mono (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline constexpr float titleHeight = 22.0f;

    /// Panel with a tinted title strip in the tool's accent colour.
    /// Returns the content area below the strip.
    inline juce::Rectangle<float> drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title,
                                             juce::Colour accent)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r, 5.0f);

        auto strip = r.withHeight (titleHeight);
        juce::Path stripPath;
        stripPath.addRoundedRectangle (strip.getX(), strip.getY(), strip.getWidth(), strip.getHeight(), 5.0f, 5.0f,
                                       true, true, false, false);
        g.setColour (accent.withAlpha (0.18f));
        g.fillPath (stripPath);
        g.setColour (accent);
        g.fillRect (strip.removeFromLeft (4.0f).withTrimmedTop (5.0f));

        g.setColour (accent.brighter (0.2f));
        g.setFont (font (11.5f, true));
        g.drawText (title.toUpperCase(), r.withHeight (titleHeight).withTrimmedLeft (12.0f), juce::Justification::centredLeft);

        g.setColour (panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        return r.withTrimmedTop (titleHeight);
    }

    /// Recessed black instrument face.
    inline void drawFace (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (face);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    /// Small black readout box with coloured digits, like a console display.
    inline void drawReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& caption,
                             const juce::String& value, juce::Colour valueColour, float valueSize = 18.0f)
    {
        drawFace (g, r);
        auto inner = r.reduced (6.0f, 3.0f);
        g.setColour (textSecondary);
        g.setFont (font (9.5f, true));
        g.drawText (caption.toUpperCase(), inner.removeFromTop (12.0f), juce::Justification::left);
        g.setColour (valueColour);
        g.setFont (mono (valueSize, true));
        g.drawText (value, inner, juce::Justification::right);
    }

    inline juce::String formatDb (float value, int decimals = 1)
    {
        if (! std::isfinite (value) || value <= -149.0f)
            return "-inf";
        return juce::String (value, decimals);
    }

    /// Spectrum colour by frequency: deep red lows through orange, yellow,
    /// green and cyan to violet highs.
    inline juce::Colour frequencyColour (float hz)
    {
        const float pos = juce::jlimit (0.0f, 1.0f, std::log (hz / 20.0f) / std::log (1000.0f));
        return juce::Colour::fromHSV (0.0f + pos * 0.78f, 0.85f, 1.0f, 1.0f);
    }

    /// Spectrogram heat map: black, navy, blue, cyan, green, yellow, red, white.
    inline juce::Colour heat (float norm)
    {
        static const juce::Colour stops[] = {
            juce::Colour (0xff000000), juce::Colour (0xff0b1040), juce::Colour (0xff1640c0), juce::Colour (0xff10b8d8),
            juce::Colour (0xff30e060), juce::Colour (0xfff0e030), juce::Colour (0xffff4020), juce::Colour (0xffffffff)
        };
        constexpr int last = static_cast<int> (std::size (stops)) - 1;
        const float x = juce::jlimit (0.0f, 1.0f, norm) * static_cast<float> (last);
        const int i = juce::jmin (last - 1, static_cast<int> (x));
        return stops[i].interpolatedWith (stops[i + 1], x - static_cast<float> (i));
    }
}
