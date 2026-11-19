#pragma once

#include "Sources.h"
#include "ui/Theme.h"

/// The Meter Bridge mark and wordmark.
///
/// Mark: a small instrument face with four level bars in the source colours,
/// spanned by a thin arch: four sources, one bridge. Wordmark: METER BRIDGE in
/// spaced capitals, with the New Constellation line above it.
namespace Brand
{
    inline void drawMark (juce::Graphics& g, juce::Rectangle<float> r)
    {
        r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        const float corner = r.getWidth() * 0.22f;

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1d22), r.getX(), r.getY(),
                                                 juce::Colour (0xff050607), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        // Four level bars (the sources) standing under one arch (the bridge),
        // whose feet rest on the same floor as the bars.
        auto inner = r.reduced (r.getWidth() * 0.16f);
        const float floorY = inner.getBottom();
        const float archTop = inner.getY() + inner.getHeight() * 0.10f;
        auto archHeightAt = [&] (float x)   // parabola: 0 at the feet, full at the centre
        {
            const float u = (x - inner.getX()) / inner.getWidth() * 2.0f - 1.0f;
            return (floorY - archTop) * (1.0f - u * u);
        };

        const float heights[] { 0.55f, 0.72f, 0.60f, 0.70f };
        const float span = inner.getWidth() * 0.62f;
        const float gap = span * 0.09f;
        const float barWidth = (span - gap * 3.0f) / 4.0f;
        const float x0 = inner.getCentreX() - span * 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            const float x = x0 + (float) i * (barWidth + gap);
            const float room = juce::jmin (archHeightAt (x), archHeightAt (x + barWidth)) - r.getWidth() * 0.08f;
            const float h = room * heights[i] / 0.72f * 0.9f;
            g.setColour (Sources::paletteColour (i));
            g.fillRoundedRectangle (juce::Rectangle<float> (x, floorY - h, barWidth, h), barWidth * 0.3f);
        }

        juce::Path arch;
        arch.startNewSubPath (inner.getX(), floorY);
        const float lift = (floorY - archTop) * 0.333f;
        arch.cubicTo (inner.getX() + inner.getWidth() * 0.02f, archTop - lift,
                      inner.getRight() - inner.getWidth() * 0.02f, archTop - lift, inner.getRight(), floorY);
        g.setColour (Theme::textPrimary.withAlpha (0.92f));
        g.strokePath (arch, juce::PathStrokeType (juce::jmax (1.3f, r.getWidth() * 0.06f), juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour (Theme::textSecondary);
        g.drawLine (inner.getX() - 1.0f, floorY, inner.getRight() + 1.0f, floorY, juce::jmax (1.0f, r.getWidth() * 0.04f));
    }

    /// Mark plus two-line wordmark, left-aligned in the given area.
    inline void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float markSize = juce::jmin (area.getHeight(), 40.0f);
        drawMark (g, area.removeFromLeft (markSize).withSizeKeepingCentre (markSize, markSize));
        area.removeFromLeft (markSize * 0.35f);

        const float lineH = area.getHeight();
        auto top = area.removeFromTop (lineH * 0.40f);
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (lineH * 0.24f, true).withExtraKerningFactor (0.26f));
        g.drawText ("NEW CONSTELLATION", top, juce::Justification::bottomLeft, false);

        auto font = Theme::font (lineH * 0.50f, true).withExtraKerningFactor (0.10f);
        g.setFont (font);
        const juce::String meter ("METER"), bridge (" BRIDGE");
        const float meterWidth = juce::GlyphArrangement::getStringWidth (font, meter);
        g.setColour (Theme::textPrimary);
        g.drawText (meter, area.withWidth (meterWidth + 2.0f), juce::Justification::topLeft, false);
        g.setColour (Theme::spectrumAccent);
        g.drawText (bridge, area.withTrimmedLeft (meterWidth), juce::Justification::topLeft, false);
    }
}
