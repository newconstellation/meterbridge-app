#include "RadarView.h"
#include "Theme.h"

#include <cmath>

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    bool isAudible (float lufs) { return std::isfinite (lufs) && lufs > -149.0f; }

    juce::String signedLu (float lu)
    {
        return (lu > 0.05f ? "+" : "") + juce::String (lu, 1);
    }

    // Zone edges in LU against the target; each wedge is built from the zones it
    // passes through so the rings read as bands of colour.
    constexpr std::array<float, 6> zoneEdges { RadarView::minLu, -20.0f, -10.0f, 0.0f, 6.0f, RadarView::maxLu };
}

juce::Colour RadarView::zoneColour (float lu)
{
    if (lu < -20.0f) return juce::Colour (0xff2f4fb8);   // quiet: deep blue
    if (lu < -10.0f) return juce::Colour (0xff22b3d6);   // low: cyan
    if (lu < 0.0f)   return Theme::levelGreen;           // on target
    if (lu < 6.0f)   return Theme::levelYellow;          // above target
    return Theme::levelRed;                               // well above
}

RadarView::RadarView()
{
    turnButton.setTooltip ("Time for one turn of the radar");
    turnButton.setColour (juce::TextButton::buttonColourId, Theme::face);
    turnButton.setColour (juce::TextButton::textColourOffId, Theme::radarAccent);
    turnButton.onClick = [this]
    {
        if (onTurnButtonClicked)
            onTurnButtonClicked();
    };
    addAndMakeVisible (turnButton);
}

void RadarView::setReadings (const Readings& r)
{
    readings = r;
}

void RadarView::setTurnLabel (const juce::String& text)
{
    turnButton.setButtonText (text);
}

void RadarView::setHistory (const float* values, int numValues, std::uint64_t firstIndex, int pointsPerTurn)
{
    binUsed.fill (false);
    handBin = -1;

    if (numValues <= 0 || pointsPerTurn <= 0)
        return;

    const auto perTurn = static_cast<std::uint64_t> (pointsPerTurn);
    const auto newest = firstIndex + static_cast<std::uint64_t> (numValues - 1);
    const int start = juce::jmax (0, numValues - pointsPerTurn);

    for (int i = start; i < numValues; ++i)
    {
        const auto index = firstIndex + static_cast<std::uint64_t> (i);
        const auto bin = static_cast<int> ((index % perTurn) * static_cast<std::uint64_t> (numBins) / perTurn);
        const auto b = static_cast<size_t> (bin);
        const float age = static_cast<float> (newest - index) / static_cast<float> (pointsPerTurn);

        const float lufs = values[i];
        const float lu = isAudible (lufs) ? lufs - readings.target : minLu - 1.0f;

        // Points come oldest first: the first point of a bin in this turn starts
        // it afresh, later ones keep the peak.
        if (! binUsed[b])
        {
            binUsed[b] = true;
            binLu[b] = lu;
        }
        else
        {
            binLu[b] = juce::jmax (binLu[b], lu);
        }
        binAge[b] = age;
    }

    handFraction = static_cast<float> (newest % perTurn) / static_cast<float> (perTurn);
    handBin = static_cast<int> ((newest % perTurn) * static_cast<std::uint64_t> (numBins) / perTurn);
}

juce::Rectangle<float> RadarView::dialBounds() const
{
    auto area = content;
    if (readoutsBeside)
        area = area.withTrimmedRight (readoutArea.getWidth() + 8.0f);
    else
        area = area.withTrimmedBottom (readoutArea.getHeight() + 6.0f);

    const float size = juce::jmin (area.getWidth(), area.getHeight());
    return juce::Rectangle<float> (size, size).withCentre (area.getCentre());
}

void RadarView::resized()
{
    content = getLocalBounds().toFloat().withTrimmedTop (Theme::titleHeight).reduced (8.0f);

    // Readouts sit beside the dial on wide slots and under it on tall ones.
    readoutsBeside = content.getWidth() >= content.getHeight() * 1.2f;
    if (readoutsBeside)
        readoutArea = content.withLeft (content.getRight() - juce::jlimit (130.0f, 220.0f, content.getWidth() * 0.3f));
    else
        readoutArea = content.withTop (content.getBottom() - juce::jlimit (44.0f, 64.0f, content.getHeight() * 0.18f));

    const auto dial = dialBounds();
    turnButton.setBounds (juce::Rectangle<float> (68.0f, 20.0f)
                              .withPosition (dial.getX(), dial.getBottom() - 20.0f)
                              .toNearestInt());
}

void RadarView::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat(), "Loudness Radar  LU", Theme::radarAccent);
    paintDial (g, dialBounds());
    paintReadouts (g, readoutArea, readoutsBeside);
}

void RadarView::paintDial (juce::Graphics& g, juce::Rectangle<float> dial)
{
    const auto c = dial.getCentre();
    const float rOuter = dial.getWidth() * 0.5f;
    const float ringWidth = juce::jlimit (4.0f, 9.0f, rOuter * 0.045f);
    const float labelSpace = rOuter > 120.0f ? 18.0f : 0.0f;
    const float rRing = rOuter - labelSpace - ringWidth * 0.5f;
    const float rMax = rRing - ringWidth - 4.0f;
    const float rMin = rMax * 0.2f;

    if (rMax <= 10.0f)
        return;

    auto radiusOf = [&] (float lu) { return rMin + (rMax - rMin) * juce::jlimit (0.0f, 1.0f, (lu - minLu) / (maxLu - minLu)); };
    auto circle = [&] (float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c); };
    auto pointAt = [&] (float r, float a) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };

    // Glass
    g.setColour (Theme::face);
    g.fillEllipse (circle (rMax + 2.0f));

    // Sweep
    const float wedge = twoPi / static_cast<float> (numBins);
    for (int i = 0; i < numBins; ++i)
    {
        const auto b = static_cast<size_t> (i);
        if (! binUsed[b] || binLu[b] <= minLu)
            continue;

        const float a0 = wedge * static_cast<float> (i);
        const float alpha = 0.9f - 0.55f * juce::jlimit (0.0f, 1.0f, binAge[b]);

        for (size_t z = 0; z + 1 < zoneEdges.size(); ++z)
        {
            const float lo = zoneEdges[z];
            const float hi = juce::jmin (zoneEdges[z + 1], binLu[b]);
            if (hi <= lo)
                break;

            const float r0 = radiusOf (lo), r1 = radiusOf (hi);
            juce::Path p;
            p.addPieSegment (circle (r1), a0, a0 + wedge * 1.08f, r0 / r1);
            g.setColour (zoneColour (0.5f * (lo + zoneEdges[z + 1])).withAlpha (alpha));
            g.fillPath (p);
        }
    }

    // Grid: rings every 10 LU, spokes every 30 degrees.
    g.setColour (Theme::gridStrong.withAlpha (0.8f));
    for (float lu : { -30.0f, -20.0f, -10.0f, 10.0f })
        g.drawEllipse (circle (radiusOf (lu)), 0.8f);
    for (int k = 0; k < 12; ++k)
    {
        const float a = twoPi * static_cast<float> (k) / 12.0f;
        g.drawLine ({ pointAt (rMin, a), pointAt (rMax, a) }, 0.6f);
    }

    // Target ring
    g.setColour (Theme::radarAccent.withAlpha (0.9f));
    g.drawEllipse (circle (radiusOf (0.0f)), 1.6f);

    // Ring labels along the 12 o'clock spoke
    if (rMax > 70.0f)
    {
        g.setFont (Theme::font (9.0f));
        for (float lu : { -20.0f, -10.0f, 0.0f, 10.0f })
        {
            g.setColour (lu == 0.0f ? Theme::radarAccent : Theme::textSecondary);
            const auto p = pointAt (radiusOf (lu), 0.0f);
            g.drawText (lu > 0.0f ? "+" + juce::String (juce::roundToInt (lu)) : juce::String (juce::roundToInt (lu)),
                        juce::Rectangle<float> (26.0f, 11.0f).withPosition (p.x + 3.0f, p.y - 1.0f), juce::Justification::left);
        }
    }

    // Hand
    if (handBin >= 0)
    {
        const float a = twoPi * handFraction;
        g.setColour (Theme::radarAccent.withAlpha (0.25f));
        g.drawLine ({ pointAt (rMin, a), pointAt (rMax, a) }, 4.0f);
        g.setColour (Theme::radarAccent);
        g.drawLine ({ pointAt (rMin, a), pointAt (rMax, a) }, 1.4f);
    }

    // Centre: programme loudness against the target.
    g.setColour (Theme::panel);
    g.fillEllipse (circle (rMin));
    g.setColour (Theme::radarAccent.withAlpha (0.6f));
    g.drawEllipse (circle (rMin), 1.0f);
    {
        const bool hasI = isAudible (readings.integrated);
        const float iLu = readings.integrated - readings.target;
        const auto colour = ! hasI ? Theme::textSecondary
                          : std::abs (iLu) <= 1.0f ? Theme::levelGreen
                          : std::abs (iLu) <= 2.0f ? Theme::levelYellow
                                                   : Theme::levelRed;
        auto box = circle (rMin).reduced (rMin * 0.22f);
        g.setColour (colour);
        g.setFont (Theme::mono (juce::jlimit (9.0f, 34.0f, rMin * 0.55f), true));
        g.drawText (hasI ? signedLu (iLu) : juce::String ("--"), box.withTrimmedBottom (box.getHeight() * 0.25f),
                    juce::Justification::centred);
        if (rMin > 26.0f)
        {
            g.setColour (Theme::textSecondary);
            g.setFont (Theme::font (juce::jlimit (8.0f, 11.0f, rMin * 0.2f), true));
            g.drawText ("PROGRAMME", box.withTrimmedTop (box.getHeight() * 0.62f), juce::Justification::centredTop);
        }
    }

    // Outer ring: momentary loudness, 0 LU at the top, 5 degrees per LU.
    const float m = isAudible (readings.momentary) ? readings.momentary - readings.target : minLu - 1.0f;
    auto angleOf = [] (float lu) { return juce::jlimit (minLu, maxLu, lu) * 5.0f * juce::MathConstants<float>::pi / 180.0f; };
    for (int k = static_cast<int> (minLu); k < static_cast<int> (maxLu); ++k)
    {
        const float a0 = angleOf (static_cast<float> (k)) + 0.012f;
        const float a1 = angleOf (static_cast<float> (k + 1)) - 0.012f;
        juce::Path seg;
        seg.addCentredArc (c.x, c.y, rRing, rRing, 0.0f, a0, a1, true);
        const bool lit = static_cast<float> (k) < m;
        g.setColour (lit ? zoneColour (static_cast<float> (k) + 0.5f) : Theme::grid);
        g.strokePath (seg, juce::PathStrokeType (ringWidth));
    }

    if (labelSpace > 0.0f)
    {
        g.setFont (Theme::font (10.0f));
        for (int k = static_cast<int> (minLu); k <= static_cast<int> (maxLu); k += 6)
        {
            g.setColour (k == 0 ? Theme::radarAccent : Theme::textSecondary);
            const auto p = pointAt (rRing + ringWidth * 0.5f + 9.0f, angleOf (static_cast<float> (k)));
            g.drawText ((k > 0 ? "+" : "") + juce::String (k), juce::Rectangle<float> (30.0f, 12.0f).withCentre (p),
                        juce::Justification::centred);
        }
    }
}

void RadarView::paintReadouts (juce::Graphics& g, juce::Rectangle<float> area, bool vertical)
{
    const auto& r = readings;
    const bool hasI = isAudible (r.integrated);
    const bool tpOver = r.truePeakMax > r.truePeakCeiling;

    const int secs = static_cast<int> (r.elapsedSeconds);
    const auto elapsed = juce::String (secs / 3600) + ":" + juce::String ((secs / 60) % 60).paddedLeft ('0', 2) + ":"
                       + juce::String (secs % 60).paddedLeft ('0', 2);

    struct Item { const char* caption; juce::String value; juce::Colour colour; };
    const Item items[] = {
        { "Short-term LU", isAudible (r.shortTerm) ? signedLu (r.shortTerm - r.target) : juce::String ("--"),
          zoneColour (isAudible (r.shortTerm) ? r.shortTerm - r.target : minLu) },
        { "Integrated LUFS", hasI ? juce::String (r.integrated, 1) : juce::String ("--"), Theme::textPrimary },
        { "Range LU", juce::String (r.loudnessRange, 1), Theme::radarAccent },
        { "True peak dBTP", Theme::formatDb (r.truePeakMax), tpOver ? Theme::levelRed : Theme::levelGreen },
        { "Time", elapsed, Theme::textSecondary },
    };
    constexpr int count = static_cast<int> (std::size (items));

    if (vertical)
    {
        const float h = juce::jmin (52.0f, (area.getHeight() - 6.0f * (count - 1)) / static_cast<float> (count));
        if (h < 26.0f)
            return;
        for (const auto& it : items)
        {
            Theme::drawReadout (g, area.removeFromTop (h), it.caption, it.value, it.colour, juce::jlimit (12.0f, 22.0f, h * 0.45f));
            area.removeFromTop (6.0f);
        }
    }
    else
    {
        // Under the dial: the first four in a row, time left out when narrow.
        const int shown = area.getWidth() > 420.0f ? count : count - 1;
        const float w = (area.getWidth() - 6.0f * static_cast<float> (shown - 1)) / static_cast<float> (shown);
        if (w < 56.0f || area.getHeight() < 26.0f)
            return;
        for (int i = 0; i < shown; ++i)
        {
            const auto& it = items[i];
            Theme::drawReadout (g, area.removeFromLeft (w), it.caption, it.value, it.colour, juce::jlimit (11.0f, 18.0f, area.getHeight() * 0.4f));
            area.removeFromLeft (6.0f);
        }
    }
}
