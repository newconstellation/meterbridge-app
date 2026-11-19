#include "MeterViews.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

//==============================================================================
BarMeterBank::BarMeterBank (juce::String t, juce::Colour a, int numBars)
    : title (std::move (t)),
      accent (a),
      values (static_cast<size_t> (numBars), -150.0f),
      holds (static_cast<size_t> (numBars), -150.0f),
      holdAge (static_cast<size_t> (numBars), 0),
      labels (static_cast<size_t> (numBars)),
      visibleBars (numBars)
{
}

void BarMeterBank::setScale (Scale newScale)
{
    scale = std::move (newScale);
    repaint();
}

void BarMeterBank::setTitle (juce::String newTitle)
{
    title = std::move (newTitle);
    repaint();
}

void BarMeterBank::setBarLabel (int index, juce::String label)
{
    labels[static_cast<size_t> (index)] = std::move (label);
}

void BarMeterBank::setNumBarsVisible (int n)
{
    visibleBars = juce::jlimit (1, static_cast<int> (values.size()), n);
}

void BarMeterBank::setValue (int index, float value)
{
    const auto i = static_cast<size_t> (index);
    values[i] = value;

    // Peak hold: ~1.5 s at 30 fps, then it follows the bar down.
    if (value >= holds[i] || ++holdAge[i] > 45)
    {
        holds[i] = value;
        holdAge[i] = 0;
    }
}

float BarMeterBank::toNorm (float value) const noexcept
{
    return (juce::jlimit (scale.minValue, scale.maxValue, value) - scale.minValue) / (scale.maxValue - scale.minValue);
}

juce::Colour BarMeterBank::zoneColour (float value) const noexcept
{
    if (value >= scale.overFrom)
        return Theme::levelRed;
    if (value >= scale.warnFrom)
        return Theme::levelYellow;
    return Theme::levelGreen;
}

void BarMeterBank::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), title, accent).reduced (8.0f, 8.0f);

    auto readoutRow = area.removeFromTop (30.0f);
    area.removeFromTop (6.0f);
    auto labelRow = area.removeFromBottom (16.0f);
    auto scaleArea = area.removeFromLeft (26.0f);
    area.removeFromLeft (4.0f);

    Theme::drawFace (g, area);
    auto meterArea = area.reduced (4.0f);

    // Scale
    g.setFont (Theme::font (9.5f));
    for (float t : scale.ticks)
    {
        const float y = meterArea.getBottom() - toNorm (t) * meterArea.getHeight();
        g.setColour (Theme::textSecondary);
        g.drawText (juce::String (juce::roundToInt (t)), scaleArea.withY (y - 6.0f).withHeight (12.0f),
                    juce::Justification::centredRight);
        g.setColour (Theme::gridStrong);
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getX() + 3.0f);
    }

    constexpr float segmentPitch = 5.0f; // 4 px lit + 1 px gap
    const int numSegments = juce::jmax (8, static_cast<int> (meterArea.getHeight() / segmentPitch));
    const float pitch = meterArea.getHeight() / static_cast<float> (numSegments);
    const float gap = 6.0f;
    const float barWidth = (meterArea.getWidth() - gap * static_cast<float> (visibleBars - 1)) / static_cast<float> (visibleBars);
    const float range = scale.maxValue - scale.minValue;

    // Reference line at the start of the warn zone (0 VU, alignment, K zero).
    {
        const float y = meterArea.getBottom() - toNorm (scale.warnFrom) * meterArea.getHeight();
        g.setColour (accent.withAlpha (0.55f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }

    for (int b = 0; b < visibleBars; ++b)
    {
        const auto i = static_cast<size_t> (b);
        const float x = meterArea.getX() + static_cast<float> (b) * (barWidth + gap);
        const int litSegments = juce::roundToInt (toNorm (values[i]) * static_cast<float> (numSegments));
        const int holdSegment = juce::jlimit (0, numSegments - 1, juce::roundToInt (toNorm (holds[i]) * static_cast<float> (numSegments)) - 1);

        // Soft glow behind the lit column, like light bleeding through a diffuser.
        if (litSegments > 0)
        {
            const float top = meterArea.getBottom() - static_cast<float> (litSegments) * pitch;
            const float topValue = scale.minValue + static_cast<float> (litSegments) / static_cast<float> (numSegments) * range;
            g.setColour (zoneColour (topValue).withAlpha (0.10f));
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (x - 3.0f, top - 3.0f, x + barWidth + 3.0f, meterArea.getBottom() + 1.0f), 3.0f);
        }

        for (int s = 0; s < numSegments; ++s)
        {
            const float segValue = scale.minValue + (static_cast<float> (s) + 0.5f) / static_cast<float> (numSegments) * range;
            const auto colour = zoneColour (segValue);
            const bool lit = s < litSegments;
            const bool isHold = s == holdSegment && holds[i] > scale.minValue;
            const float y = meterArea.getBottom() - static_cast<float> (s + 1) * pitch;
            const auto seg = juce::Rectangle<float> (x, y + 0.5f, barWidth, pitch - 1.0f);

            if (lit || isHold)
            {
                // Lit LED: brighter centre, darker edges.
                const auto core = isHold ? colour.brighter (0.7f) : colour.brighter (0.15f);
                juce::ColourGradient grad (colour.darker (0.35f), seg.getX(), 0.0f, colour.darker (0.35f), seg.getRight(), 0.0f, false);
                grad.addColour (0.5, core);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (seg, 1.0f);
            }
            else
            {
                g.setColour (colour.withAlpha (0.07f));
                g.fillRoundedRectangle (seg, 1.0f);
            }
        }
    }

    for (int b = 0; b < visibleBars; ++b)
    {
        const auto i = static_cast<size_t> (b);
        const float x = meterArea.getX() + static_cast<float> (b) * (barWidth + gap);

        // Per-bar peak-hold readout
        const auto box = readoutRow.withX (x).withWidth (barWidth);
        Theme::drawFace (g, box);
        g.setColour (holds[i] > scale.minValue ? zoneColour (holds[i]) : Theme::textSecondary);
        g.setFont (Theme::mono (barWidth < 44.0f ? 10.0f : 12.0f, true));
        g.drawFittedText (Theme::formatDb (holds[i]), box.toNearestInt(), juce::Justification::centred, 1, 0.8f);

        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (10.0f, true));
        g.drawText (labels[i], labelRow.withX (x).withWidth (barWidth), juce::Justification::centred);
    }

    g.setColour (accent.withAlpha (0.8f));
    g.setFont (Theme::font (9.5f, true));
    g.drawText (scale.unit, scaleArea.withY (labelRow.getY()).withHeight (16.0f), juce::Justification::centredRight);
}

//==============================================================================
void LoudnessPanel::setReadings (const Readings& r)
{
    readings = r;
    repaint();
}

void LoudnessPanel::pushHistory (float shortTermLufs, float momentaryLufs)
{
    shortHistory[static_cast<size_t> (historyWrite)] = shortTermLufs;
    momentaryHistory[static_cast<size_t> (historyWrite)] = momentaryLufs;
    historyWrite = (historyWrite + 1) % historyLength;
    historyCount = juce::jmin (historyCount + 1, historyLength);
}

void LoudnessPanel::clearHistory()
{
    historyCount = 0;
    historyWrite = 0;
}

void LoudnessPanel::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), "Loudness  LUFS", Theme::loudnessAccent).reduced (8.0f);
    const auto& r = readings;

    // Readouts grow on large slots so the numbers read from across the room.
    const float k = juce::jlimit (1.0f, 2.4f, juce::jmin (area.getWidth() / 300.0f, area.getHeight() / 520.0f));

    auto colourForLoudness = [&] (float lufs)
    {
        if (! std::isfinite (lufs) || lufs <= -149.0f)
            return Theme::textSecondary;
        const float off = std::abs (lufs - r.target);
        return off <= 1.0f ? Theme::levelGreen : off <= 2.0f ? Theme::levelYellow : Theme::levelRed;
    };

    // Integrated, large
    auto hero = area.removeFromTop (58.0f * k);
    Theme::drawFace (g, hero);
    {
        auto inner = hero.reduced (8.0f, 4.0f);
        auto captionCol = inner.removeFromRight (inner.getWidth() * 0.36f);
        g.setColour (colourForLoudness (r.integrated));
        g.setFont (Theme::mono (36.0f * k, true));
        g.drawText (Theme::formatDb (r.integrated), inner, juce::Justification::centredLeft);
        g.setColour (Theme::loudnessAccent);
        g.setFont (Theme::font (10.0f * juce::jmin (k, 1.6f), true));
        g.drawText ("INTEGRATED", captionCol.removeFromTop (captionCol.getHeight() * 0.5f), juce::Justification::bottomRight);
        g.setColour (Theme::textSecondary);
        g.drawText ("TARGET " + juce::String (r.target, 0), captionCol, juce::Justification::topRight);
    }

    area.removeFromTop (6.0f);

    // 2 x 2 readouts
    auto grid = area.removeFromTop (84.0f * k);
    const float cellW = (grid.getWidth() - 6.0f) * 0.5f;
    auto rowA = grid.removeFromTop (39.0f * k);
    grid.removeFromTop (6.0f);
    auto rowB = grid;

    const bool tpOver = r.truePeakMax > r.truePeakCeiling;
    Theme::drawReadout (g, rowA.removeFromLeft (cellW), "Momentary", Theme::formatDb (r.momentary), colourForLoudness (r.momentary), 18.0f * k);
    Theme::drawReadout (g, rowA.withTrimmedLeft (6.0f), "Short-term", Theme::formatDb (r.shortTerm), colourForLoudness (r.shortTerm), 18.0f * k);
    Theme::drawReadout (g, rowB.removeFromLeft (cellW), "Range LU", juce::String (r.loudnessRange, 1), Theme::loudnessAccent, 18.0f * k);
    Theme::drawReadout (g, rowB.withTrimmedLeft (6.0f), "True peak dBTP", Theme::formatDb (r.truePeakMax),
                        tpOver ? Theme::levelRed : Theme::levelGreen, 18.0f * k);

    area.removeFromTop (6.0f);

    // Stats line at the bottom
    auto stats = area.removeFromBottom (16.0f);
    area.removeFromBottom (4.0f);
    {
        const bool hasI = std::isfinite (r.integrated) && r.integrated > -149.0f && r.truePeakMax > -149.0f;
        const juce::String text = "M max " + Theme::formatDb (r.momentaryMax) + "   S max " + Theme::formatDb (r.shortTermMax)
                                + "   PLR " + (hasI ? juce::String (r.truePeakMax - r.integrated, 1) : juce::String ("-"));
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (10.5f));
        g.drawText (text, stats, juce::Justification::centredLeft);
    }

    // History graph
    Theme::drawFace (g, area);
    auto plot = area.reduced (4.0f).withTrimmedLeft (22.0f);
    constexpr float lo = -42.0f, hi = -2.0f;
    auto yFor = [&] (float lufs) { return plot.getBottom() - (juce::jlimit (lo, hi, lufs) - lo) / (hi - lo) * plot.getHeight(); };

    g.setFont (Theme::font (9.0f));
    for (float v = -40.0f; v <= -5.0f; v += 5.0f)
    {
        g.setColour (Theme::grid);
        g.drawHorizontalLine (juce::roundToInt (yFor (v)), plot.getX(), plot.getRight());
        g.setColour (Theme::textSecondary);
        g.drawText (juce::String (juce::roundToInt (v)), juce::Rectangle<float> (area.getX() + 2.0f, yFor (v) - 5.0f, 20.0f, 10.0f),
                    juce::Justification::centredLeft);
    }

    // Target band +/- 1 LU
    g.setColour (Theme::loudnessAccent.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yFor (r.target + 1.0f), plot.getRight(), yFor (r.target - 1.0f)));
    g.setColour (Theme::loudnessAccent.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (yFor (r.target)), plot.getX(), plot.getRight());

    if (historyCount > 1)
    {
        juce::Path shortPath, fillPath, momentaryPath;
        const float dx = plot.getWidth() / static_cast<float> (historyLength - 1);
        const int first = (historyWrite - historyCount + historyLength) % historyLength;
        const float x0 = plot.getRight() - static_cast<float> (historyCount - 1) * dx;

        for (int i = 0; i < historyCount; ++i)
        {
            const auto idx = static_cast<size_t> ((first + i) % historyLength);
            const float x = x0 + static_cast<float> (i) * dx;
            const float ys = yFor (std::isfinite (shortHistory[idx]) ? shortHistory[idx] : lo);
            const float ym = yFor (std::isfinite (momentaryHistory[idx]) ? momentaryHistory[idx] : lo);

            if (i == 0)
            {
                shortPath.startNewSubPath (x, ys);
                momentaryPath.startNewSubPath (x, ym);
                fillPath.startNewSubPath (x, plot.getBottom());
            }
            else
            {
                shortPath.lineTo (x, ys);
                momentaryPath.lineTo (x, ym);
            }
            fillPath.lineTo (x, ys);
        }

        fillPath.lineTo (plot.getRight(), plot.getBottom());
        fillPath.closeSubPath();

        g.setGradientFill (juce::ColourGradient (Theme::loudnessAccent.withAlpha (0.45f), 0.0f, plot.getY(),
                                                 Theme::loudnessAccent.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
        g.fillPath (fillPath);
        g.setColour (Theme::needleAccent.withAlpha (0.35f));
        g.strokePath (momentaryPath, juce::PathStrokeType (1.0f));
        g.setColour (Theme::loudnessAccent);
        g.strokePath (shortPath, juce::PathStrokeType (1.8f));
    }

    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (9.0f));
    g.drawText ("short-term, last 60 s", plot.reduced (4.0f, 2.0f), juce::Justification::topRight);
}

//==============================================================================
void NeedleGauge::setValues (float peakDb, float averageDb)
{
    stereo = false;
    peak[0] = peak[1] = peakDb;
    average[0] = average[1] = averageDb;
    updateHolds();
    repaint();
}

void NeedleGauge::setStereoValues (float peakL, float averageL, float peakR, float averageR)
{
    stereo = true;
    peak[0] = peakL;
    average[0] = averageL;
    peak[1] = peakR;
    average[1] = averageR;
    updateHolds();
    repaint();
}

void NeedleGauge::updateHolds()
{
    for (int c = 0; c < 2; ++c)
    {
        if (peak[c] >= hold[c]) { hold[c] = peak[c]; holdFrames[c] = 90; }   // ~3 s at 30 Hz
        else if (--holdFrames[c] <= 0) hold[c] = juce::jmax (peak[c], hold[c] - 0.6f);
    }
}

void NeedleGauge::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), stereo ? "Peak + Average  stereo" : "Peak + Average",
                                  Theme::needleAccent).reduced (10.0f);

    constexpr float aspect = 1.45f;
    constexpr float gap = 12.0f;

    if (! stereo)
    {
        drawDial (g, area, 0, {});
        return;
    }

    const float sideH = juce::jmin (area.getHeight(), (area.getWidth() - gap) * 0.5f / aspect);
    const float stackH = juce::jmin ((area.getHeight() - gap) * 0.5f, area.getWidth() / aspect);
    if (sideH >= stackH)
    {
        auto left = area.removeFromLeft ((area.getWidth() - gap) * 0.5f);
        area.removeFromLeft (gap);
        drawDial (g, left, 0, "L");
        drawDial (g, area, 1, "R");
    }
    else
    {
        auto top = area.removeFromTop ((area.getHeight() - gap) * 0.5f);
        area.removeFromTop (gap);
        drawDial (g, top, 0, "L");
        drawDial (g, area, 1, "R");
    }
}

/// Dual-needle peak + average meter in the same hardware dress as the VU
/// (our design): dark bezel, a cool backlit ivory face behind glass, a dBFS
/// scale with a red zone from -6 to 0, a black average needle and a red peak
/// needle on one pivot, a peak-hold flag on the scale, an OVER lamp and
/// console readouts for peak, average and crest in the bezel.
void NeedleGauge::drawDial (juce::Graphics& g, juce::Rectangle<float> area, int channel, const juce::String& label) const
{
    constexpr float aspect = 1.45f;
    const float h = juce::jmin (area.getHeight(), area.getWidth() / aspect);
    auto bezel = area.withSizeKeepingCentre (h * aspect, h);
    const float s = juce::jlimit (0.55f, 2.4f, h / 260.0f);

    const float peakDb = peak[channel], averageDb = average[channel], holdDb = hold[channel];

    // Bezel
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), bezel.getX(), bezel.getY(),
                                             juce::Colour (0xff141518), bezel.getX(), bezel.getBottom(), false));
    g.fillRoundedRectangle (bezel, 8.0f * s);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (bezel.reduced (1.0f), 8.0f * s, 1.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bezel, 8.0f * s, 1.2f);

    const float lip = juce::jmax (6.0f, 11.0f * s);
    auto inner = bezel.reduced (lip);
    auto readoutStrip = inner.removeFromBottom (juce::jmax (16.0f, 24.0f * s));
    inner.removeFromBottom (lip * 0.5f);
    auto window = inner;

    g.saveState();
    juce::Path windowPath;
    windowPath.addRoundedRectangle (window, 4.0f * s);
    g.reduceClipRegion (windowPath);

    // Cool ivory backlight, brightest behind the scale
    const auto faceLight = juce::Colour (0xfffbf6e9);
    const auto faceEdge = juce::Colour (0xffc9bfa4);
    g.setGradientFill (juce::ColourGradient (faceLight, window.getCentreX(), window.getY() + window.getHeight() * 0.50f,
                                             faceEdge, window.getX(), window.getY(), true));
    g.fillRect (window);

    const auto ink = juce::Colour (0xff15130f);
    const auto red = juce::Colour (0xffc3241b);

    const juce::Point<float> pivot (window.getCentreX(), window.getBottom() + window.getHeight() * 0.20f);
    const float radius = window.getHeight() * 0.92f;
    constexpr float span = juce::MathConstants<float>::pi * 0.25f;
    constexpr float minDb = -40.0f, maxDb = 0.0f;

    auto angleFor = [&] (float db)
    {
        // Slightly expanded top end so -10 to 0 gets more room, as on broadcast peak meters.
        const float norm = (juce::jlimit (minDb - 2.0f, maxDb + 1.0f, db) - minDb) / (maxDb - minDb);
        const float shaped = std::pow (juce::jlimit (0.0f, 1.03f, norm), 1.35f);
        return -span + shaped * 2.0f * span;
    };
    auto pointAt = [&] (float angle, float r)
    {
        return pivot + juce::Point<float> (std::sin (angle) * r, -std::cos (angle) * r);
    };

    // Scale arc and the red zone
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (minDb), angleFor (-6.0f), true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.6f * s));
    juce::Path redBand;
    const float bandR = radius + 4.0f * s;
    redBand.addCentredArc (pivot.x, pivot.y, bandR, bandR, 0.0f, angleFor (-6.0f), angleFor (0.0f), true);
    g.setColour (red);
    g.strokePath (redBand, juce::PathStrokeType (8.0f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    g.setFont (Theme::font (14.0f * s, true));
    for (float t = minDb; t <= maxDb; t += 1.0f)
    {
        const bool numbered = t == -40.0f || t == -30.0f || t == -20.0f || t == -15.0f || t == -10.0f
                              || t == -6.0f || t == -3.0f || t == 0.0f;
        const bool minor = t > -10.0f || std::fmod (t, 5.0f) == 0.0f;
        if (! numbered && ! minor) continue;
        const float a = angleFor (t);
        const bool hot = t >= -6.0f;
        g.setColour (hot ? red : ink);
        const float len = (numbered ? 12.0f : 6.0f) * s;
        g.drawLine ({ pointAt (a, radius), pointAt (a, radius + len) }, (numbered ? 1.6f : 1.0f) * s);
        if (numbered)
            g.drawText (juce::String (juce::roundToInt (t)),
                        juce::Rectangle<float> (34.0f * s, 16.0f * s).withCentre (pointAt (a, radius + 24.0f * s)),
                        juce::Justification::centred);
    }

    // Inner crest scale hint: average zone shading from -24 to -12 (typical programme)
    const float zoneR = radius - 12.0f * s;
    juce::Path zone;
    zone.addCentredArc (pivot.x, pivot.y, zoneR, zoneR, 0.0f, angleFor (-24.0f), angleFor (-12.0f), true);
    g.setColour (juce::Colour (0xff3c7a46).withAlpha (0.55f));
    g.strokePath (zone, juce::PathStrokeType (4.0f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    // Legend: big dBFS plus a needle key
    g.setColour (ink.withAlpha (0.85f));
    g.setFont (Theme::font (22.0f * s, true));
    const float legendY = window.getBottom() - window.getHeight() * 0.33f;
    g.drawText ("dBFS", juce::Rectangle<float> (100.0f * s, 26.0f * s).withCentre ({ pivot.x, legendY }), juce::Justification::centred);
    g.setFont (Theme::font (9.5f * s, true));
    auto key = juce::Rectangle<float> (150.0f * s, 12.0f * s).withPosition (window.getRight() - 160.0f * s, window.getY() + 9.0f * s);
    auto keyL = key.removeFromLeft (key.getWidth() * 0.5f);
    g.setColour (red);
    g.fillRect (keyL.removeFromLeft (14.0f * s).withSizeKeepingCentre (14.0f * s, 2.0f * s));
    g.drawText (" PEAK", keyL, juce::Justification::centredLeft);
    g.setColour (ink);
    g.fillRect (key.removeFromLeft (14.0f * s).withSizeKeepingCentre (14.0f * s, 3.0f * s));
    g.drawText (" AVERAGE", key, juce::Justification::centredLeft);

    g.setFont (Theme::font (8.5f * s, true));
    g.setColour (ink.withAlpha (0.5f));
    g.drawText ("NEW CONSTELLATION", window.reduced (8.0f * s, 6.0f * s), juce::Justification::bottomRight);
    if (label.isNotEmpty())
    {
        g.setColour (ink.withAlpha (0.8f));
        g.setFont (Theme::font (16.0f * s, true));
        g.drawText (label, window.reduced (9.0f * s, 5.0f * s), juce::Justification::bottomLeft);
    }

    // OVER lamp
    const bool lit = holdDb > -0.5f;
    const auto lamp = juce::Rectangle<float> (10.0f * s, 10.0f * s).withPosition (window.getX() + 10.0f * s, window.getY() + 10.0f * s);
    if (lit)
    {
        g.setColour (juce::Colour (0xffff3020).withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (5.0f * s));
    }
    g.setColour (lit ? juce::Colour (0xffff4a30) : juce::Colour (0xff5a1a12));
    g.fillEllipse (lamp);
    g.setColour (ink.withAlpha (0.6f));
    g.drawEllipse (lamp, 1.0f);
    g.setFont (Theme::font (9.0f * s, true));
    g.setColour (ink.withAlpha (0.75f));
    g.drawText ("OVER", juce::Rectangle<float> (lamp.getRight() + 4.0f * s, lamp.getY() - 2.0f * s, 40.0f * s, lamp.getHeight() + 4.0f * s),
                juce::Justification::centredLeft);

    // Peak-hold flag on the scale
    {
        const float a = angleFor (holdDb);
        juce::Path flag;
        const auto tip = pointAt (a, radius + 1.0f * s);
        const auto b1 = pointAt (a - 0.025f, radius + 11.0f * s);
        const auto b2 = pointAt (a + 0.025f, radius + 11.0f * s);
        flag.addTriangle (tip, b1, b2);
        g.setColour (red);
        g.fillPath (flag);
    }

    // Needles: average (black, heavier) under peak (red, fine)
    auto needle = [&] (float db, juce::Colour c, float thickness, float extra)
    {
        const auto tip = pointAt (angleFor (db), radius + extra * s);
        const auto off = juce::Point<float> (2.5f * s, 3.0f * s);
        g.setColour (juce::Colours::black.withAlpha (0.20f));
        g.drawLine ({ pivot + off, tip + off }, thickness);
        g.setColour (c);
        g.drawLine ({ pivot, tip }, thickness);
    };
    needle (averageDb, juce::Colour (0xff111111), 2.4f * s, 6.0f);
    needle (peakDb, juce::Colour (0xffe0281e), 2.0f * s, 12.0f);

    // Mechanism cover
    const float coverR = window.getHeight() * 0.36f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2d31), pivot.x, pivot.y - coverR,
                                             juce::Colour (0xff121315), pivot.x, pivot.y, false));
    g.fillEllipse (juce::Rectangle<float> (coverR * 2.0f, coverR * 2.0f).withCentre (pivot));
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (juce::Rectangle<float> (coverR * 2.0f, coverR * 2.0f).withCentre (pivot), 1.0f);

    // Glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.30f), window.getX(), window.getY(),
                                             juce::Colours::transparentBlack, window.getX(), window.getY() + 14.0f * s, false));
    g.fillRect (window);
    juce::Path glare;
    glare.startNewSubPath (window.getX(), window.getY());
    glare.lineTo (window.getX() + window.getWidth() * 0.62f, window.getY());
    glare.lineTo (window.getX() + window.getWidth() * 0.30f, window.getBottom());
    glare.lineTo (window.getX(), window.getBottom());
    glare.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), window.getX(), window.getY(),
                                             juce::Colours::white.withAlpha (0.0f), window.getCentreX(), window.getBottom(), false));
    g.fillPath (glare);
    g.restoreState();

    // Console readouts in the bezel: PK, AVG, CREST
    const float crest = (peakDb > -149.0f && averageDb > -149.0f) ? peakDb - averageDb : 0.0f;
    const float cellW = juce::jmin (readoutStrip.getWidth() / 3.0f, 96.0f * s);
    auto cells = readoutStrip.withSizeKeepingCentre (cellW * 3.0f, readoutStrip.getHeight());
    struct Cell { juce::String cap, val; juce::Colour col; };
    const Cell items[] = {
        { "PK", Theme::formatDb (peakDb), peakDb > -1.0f ? Theme::levelRed : Theme::levelYellow },
        { "AVG", Theme::formatDb (averageDb), Theme::levelGreen },
        { "CR", juce::String (crest, 1), Theme::needleAccent },
    };
    for (const auto& it : items)
    {
        auto c = cells.removeFromLeft (cellW).reduced (2.0f * s, 0.0f);
        g.setColour (Theme::face);
        g.fillRoundedRectangle (c, 3.0f);
        g.setFont (Theme::font (juce::jmax (7.5f, 8.5f * s), true));
        g.setColour (Theme::textSecondary);
        g.drawText (it.cap, c.reduced (5.0f * s, 0.0f), juce::Justification::centredLeft);
        g.setFont (Theme::mono (juce::jmax (10.0f, 13.0f * s), true));
        g.setColour (it.col);
        g.drawText (it.val, c.reduced (5.0f * s, 0.0f), juce::Justification::centredRight);
    }
}

//==============================================================================
float AnalogVuMeter::needlePosition (float vu) noexcept
{
    // VU scales are linear in voltage: -20 VU is 0.1 of the 0 VU voltage.
    const float lo = 0.1f;
    const float hi = std::pow (10.0f, 3.0f / 20.0f);
    const float v = std::pow (10.0f, juce::jlimit (-60.0f, 6.0f, vu) / 20.0f);
    return juce::jlimit (-0.03f, 1.04f, (v - lo) / (hi - lo));
}

void AnalogVuMeter::setTitle (juce::String newTitle)
{
    if (newTitle != title)
    {
        title = std::move (newTitle);
        repaint();
    }
}

void AnalogVuMeter::setNumChannels (int n)
{
    numChannels = juce::jlimit (1, 2, n);
}

void AnalogVuMeter::setValue (int channel, float vu, bool peakOver)
{
    if (! juce::isPositiveAndBelow (channel, 2))
        return;

    const auto i = static_cast<size_t> (channel);
    values[i] = vu;
    // The lamp stays lit for half a second after an over, like a hardware lamp.
    lampFrames[i] = peakOver ? 15 : juce::jmax (0, lampFrames[i] - 1);
}

void AnalogVuMeter::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), title, Theme::vuAccent).reduced (10.0f);

    constexpr float aspect = 1.45f; // width / height of one meter
    constexpr float gap = 12.0f;

    if (numChannels < 2)
    {
        drawMeter (g, area, 0, {});
        return;
    }

    // Side by side or stacked, whichever gives the bigger meters.
    const float sideH = juce::jmin (area.getHeight(), (area.getWidth() - gap) * 0.5f / aspect);
    const float stackH = juce::jmin ((area.getHeight() - gap) * 0.5f, area.getWidth() / aspect);

    if (sideH >= stackH)
    {
        auto left = area.removeFromLeft ((area.getWidth() - gap) * 0.5f);
        area.removeFromLeft (gap);
        drawMeter (g, left, 0, "L");
        drawMeter (g, area, 1, "R");
    }
    else
    {
        auto top = area.removeFromTop ((area.getHeight() - gap) * 0.5f);
        area.removeFromTop (gap);
        drawMeter (g, top, 0, "L");
        drawMeter (g, area, 1, "R");
    }
}

void AnalogVuMeter::drawMeter (juce::Graphics& g, juce::Rectangle<float> area, int channel, const juce::String& label) const
{
    constexpr float aspect = 1.45f;
    const float h = juce::jmin (area.getHeight(), area.getWidth() / aspect);
    auto bezel = area.withSizeKeepingCentre (h * aspect, h);
    const float s = juce::jlimit (0.55f, 2.4f, h / 260.0f); // type and line scale

    // --- Bezel: brushed dark metal with a bevel
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), bezel.getX(), bezel.getY(),
                                             juce::Colour (0xff141518), bezel.getX(), bezel.getBottom(), false));
    g.fillRoundedRectangle (bezel, 8.0f * s);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (bezel.reduced (1.0f), 8.0f * s, 1.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bezel, 8.0f * s, 1.2f);

    const float lip = juce::jmax (6.0f, 11.0f * s);
    auto inner = bezel.reduced (lip);
    auto readoutStrip = inner.removeFromBottom (juce::jmax (14.0f, 22.0f * s));
    inner.removeFromBottom (lip * 0.5f);
    auto window = inner;

    // --- Backlit face: warm, brightest where the lamp sits behind the scale
    g.saveState();
    juce::Path windowPath;
    windowPath.addRoundedRectangle (window, 4.0f * s);
    g.reduceClipRegion (windowPath);

    const auto faceWarm = juce::Colour (0xfff7e3a6);
    const auto faceEdge = juce::Colour (0xffd9a94e);
    juce::ColourGradient glow (faceWarm.brighter (0.08f), window.getCentreX(), window.getY() + window.getHeight() * 0.55f,
                               faceEdge, window.getX(), window.getY(), true);
    g.setGradientFill (glow);
    g.fillRect (window);

    const auto ink = juce::Colour (0xff1c140a);
    const auto red = juce::Colour (0xffc3241b);

    const juce::Point<float> pivot (window.getCentreX(), window.getBottom() + window.getHeight() * 0.20f);
    const float radius = window.getHeight() * 0.92f;
    constexpr float span = juce::MathConstants<float>::pi * 0.25f; // +/- 45 degrees

    auto angleFor = [&] (float vu) { return -span + needlePosition (vu) * 2.0f * span; };
    auto pointAt = [&] (float angle, float r)
    {
        return pivot + juce::Point<float> (std::sin (angle) * r, -std::cos (angle) * r);
    };

    // Main scale arc, then the red band from 0 to +3 VU
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.6f * s));

    juce::Path redBand;
    const float bandR = radius + 4.0f * s;
    redBand.addCentredArc (pivot.x, pivot.y, bandR, bandR, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
    g.setColour (red);
    g.strokePath (redBand, juce::PathStrokeType (8.0f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    // Ticks: majors are numbered, minors fill the gaps
    struct Mark { float vu; bool numbered; };
    static const Mark marks[] = {
        { -20.0f, true }, { -15.0f, false }, { -10.0f, true }, { -8.0f, false }, { -7.0f, true }, { -6.0f, false },
        { -5.0f, true }, { -4.0f, false }, { -3.0f, true }, { -2.0f, true }, { -1.0f, true }, { -0.5f, false },
        { 0.0f, true }, { 0.5f, false }, { 1.0f, true }, { 1.5f, false }, { 2.0f, true }, { 2.5f, false }, { 3.0f, true }
    };

    g.setFont (Theme::font (14.0f * s, true));
    for (const auto& m : marks)
    {
        const float a = angleFor (m.vu);
        const bool hot = m.vu > 0.0f;
        g.setColour (hot ? red : ink);
        const float len = (m.numbered ? 12.0f : 7.0f) * s;
        g.drawLine ({ pointAt (a, radius), pointAt (a, radius + len) }, (m.numbered ? 1.6f : 1.0f) * s);

        if (m.numbered)
        {
            const auto text = m.vu > 0.0f ? "+" + juce::String (juce::roundToInt (m.vu))
                                          : juce::String (juce::roundToInt (std::abs (m.vu)));
            g.drawText (text, juce::Rectangle<float> (30.0f * s, 16.0f * s).withCentre (pointAt (a, radius + 24.0f * s)),
                        juce::Justification::centred);
        }
    }

    // "-" and "+" end markers, out of the way of the numbers
    g.setFont (Theme::font (18.0f * s, true));
    g.setColour (ink);
    g.drawText ("-", juce::Rectangle<float> (20.0f * s, 20.0f * s).withCentre (pointAt (angleFor (-20.0f) - 0.10f, radius + 6.0f * s)),
                juce::Justification::centred);
    g.setColour (red);
    g.drawText ("+", juce::Rectangle<float> (20.0f * s, 20.0f * s).withCentre (pointAt (angleFor (3.0f) + 0.10f, radius + 6.0f * s)),
                juce::Justification::centred);

    // Secondary percent scale inside the arc (100 % = 0 VU)
    const float pctR = radius - 14.0f * s;
    juce::Path pctArc;
    pctArc.addCentredArc (pivot.x, pivot.y, pctR, pctR, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
    g.setColour (ink.withAlpha (0.55f));
    g.strokePath (pctArc, juce::PathStrokeType (0.9f * s));
    g.setFont (Theme::font (9.5f * s));
    for (int pct : { 20, 40, 60, 80, 100 })
    {
        const float vu = 20.0f * std::log10 (static_cast<float> (pct) / 100.0f);
        const float a = angleFor (vu);
        g.drawLine ({ pointAt (a, pctR), pointAt (a, pctR - 5.0f * s) }, 0.9f * s);
        g.drawText (juce::String (pct), juce::Rectangle<float> (26.0f * s, 12.0f * s).withCentre (pointAt (a, pctR - 13.0f * s)),
                    juce::Justification::centred);
    }

    // Legend
    g.setColour (ink.withAlpha (0.85f));
    g.setFont (Theme::font (26.0f * s, true));
    g.drawText ("VU", juce::Rectangle<float> (80.0f * s, 30.0f * s).withCentre ({ pivot.x, window.getBottom() - window.getHeight() * 0.30f }),
                juce::Justification::centred);

    g.setFont (Theme::font (8.5f * s, true));
    g.setColour (ink.withAlpha (0.5f));
    g.drawText ("NEW CONSTELLATION", window.reduced (8.0f * s, 6.0f * s), juce::Justification::bottomRight);

    if (label.isNotEmpty())
    {
        g.setColour (ink.withAlpha (0.8f));
        g.setFont (Theme::font (16.0f * s, true));
        g.drawText (label, window.reduced (9.0f * s, 5.0f * s), juce::Justification::bottomLeft);
    }

    // PEAK lamp, top left
    const auto idx = static_cast<size_t> (channel);
    const bool lit = lampFrames[idx] > 0;
    const auto lamp = juce::Rectangle<float> (10.0f * s, 10.0f * s).withPosition (window.getX() + 10.0f * s, window.getY() + 10.0f * s);
    if (lit)
    {
        g.setColour (juce::Colour (0xffff3020).withAlpha (0.35f));
        g.fillEllipse (lamp.expanded (5.0f * s));
    }
    g.setColour (lit ? juce::Colour (0xffff4a30) : juce::Colour (0xff5a1a12));
    g.fillEllipse (lamp);
    g.setColour (ink.withAlpha (0.6f));
    g.drawEllipse (lamp, 1.0f);
    g.setFont (Theme::font (9.0f * s, true));
    g.setColour (ink.withAlpha (0.75f));
    g.drawText ("PEAK", juce::Rectangle<float> (lamp.getRight() + 4.0f * s, lamp.getY() - 2.0f * s, 40.0f * s, lamp.getHeight() + 4.0f * s),
                juce::Justification::centredLeft);

    // Needle with a soft shadow on the face
    const float vu = values[idx];
    const float a = angleFor (vu);
    const auto tip = pointAt (a, radius + 10.0f * s);
    const auto shadowOffset = juce::Point<float> (2.5f * s, 3.0f * s);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.drawLine ({ pivot + shadowOffset, tip + shadowOffset }, 2.2f * s);
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ({ pivot, tip }, 1.8f * s);

    // Mechanism cover across the bottom of the window hides the pivot
    const float coverR = window.getHeight() * 0.36f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2d31), pivot.x, pivot.y - coverR,
                                             juce::Colour (0xff121315), pivot.x, pivot.y, false));
    g.fillEllipse (juce::Rectangle<float> (coverR * 2.0f, coverR * 2.0f).withCentre (pivot));
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (juce::Rectangle<float> (coverR * 2.0f, coverR * 2.0f).withCentre (pivot), 1.0f);

    // Glass: a soft top shadow and a diagonal glare
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.30f), window.getX(), window.getY(),
                                             juce::Colours::transparentBlack, window.getX(), window.getY() + 14.0f * s, false));
    g.fillRect (window);
    juce::Path glare;
    glare.startNewSubPath (window.getX(), window.getY());
    glare.lineTo (window.getX() + window.getWidth() * 0.62f, window.getY());
    glare.lineTo (window.getX() + window.getWidth() * 0.30f, window.getBottom());
    glare.lineTo (window.getX(), window.getBottom());
    glare.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), window.getX(), window.getY(),
                                             juce::Colours::white.withAlpha (0.0f), window.getCentreX(), window.getBottom(), false));
    g.fillPath (glare);

    g.restoreState();

    // Numeric reading in the bezel under the window
    g.setColour (Theme::face);
    g.fillRoundedRectangle (readoutStrip.withSizeKeepingCentre (juce::jmin (readoutStrip.getWidth(), 120.0f * s), readoutStrip.getHeight()), 3.0f);
    g.setFont (Theme::mono (juce::jmax (10.0f, 14.0f * s), true));
    g.setColour (vu > 0.0f ? Theme::levelRed : Theme::vuAccent);
    g.drawText ((vu <= -40.0f ? juce::String ("-inf") : juce::String (vu, 1)) + " VU", readoutStrip, juce::Justification::centred);
}

//==============================================================================
PhasePanel::PhasePanel()
{
    trace = juce::Image (juce::Image::RGB, 256, 256, true);
}

void PhasePanel::resized()
{
    trace.clear (trace.getBounds(), juce::Colours::black);
}

void PhasePanel::setValues (float c, float b, float w)
{
    correlation = c;
    balance = b;
    width = w;
}

void PhasePanel::fadeTrace()
{
    juce::Graphics g (trace);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillAll();
}

void PhasePanel::pushSamples (const float* left, const float* right, int numSamples)
{
    fadeTrace();

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));

    // Auto-gain so quiet material still fills the scope; fast up, slow down.
    const float wanted = peak > 1.0e-4f ? 0.85f / (peak * juce::MathConstants<float>::sqrt2) : autoGain;
    autoGain = wanted < autoGain ? wanted : autoGain + 0.05f * (wanted - autoGain);
    autoGain = juce::jlimit (0.5f, 200.0f, autoGain);

    juce::Graphics g (trace);
    const float half = static_cast<float> (trace.getWidth()) * 0.5f;
    const float k = half * autoGain / juce::MathConstants<float>::sqrt2;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = half + (right[i] - left[i]) * k;
        const float y = half - (left[i] + right[i]) * k;
        g.setColour (Theme::scopeTrace.withAlpha (0.35f));
        g.fillRect (x, y, 1.6f, 1.6f);
    }
}

void PhasePanel::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), "Phase  Vectorscope", Theme::phaseAccent).reduced (8.0f);

    // Square scope on the left
    const float side = juce::jmin (area.getHeight(), area.getWidth() * 0.45f);
    auto scopeArea = area.removeFromLeft (side).withSizeKeepingCentre (side, side);
    area.removeFromLeft (10.0f);

    Theme::drawFace (g, scopeArea);
    g.drawImage (trace, scopeArea.reduced (2.0f), juce::RectanglePlacement::stretchToFit);

    // Graticule: L and R diagonals, M vertical, S horizontal
    const auto c = scopeArea.getCentre();
    const float r = side * 0.5f - 4.0f;
    g.setColour (Theme::phaseAccent.withAlpha (0.25f));
    g.drawLine (c.x, scopeArea.getY() + 4.0f, c.x, scopeArea.getBottom() - 4.0f, 1.0f);
    g.drawLine (scopeArea.getX() + 4.0f, c.y, scopeArea.getRight() - 4.0f, c.y, 1.0f);
    g.setColour (Theme::phaseAccent.withAlpha (0.15f));
    g.drawLine (c.x - r * 0.7f, c.y - r * 0.7f, c.x + r * 0.7f, c.y + r * 0.7f, 1.0f);
    g.drawLine (c.x + r * 0.7f, c.y - r * 0.7f, c.x - r * 0.7f, c.y + r * 0.7f, 1.0f);
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);

    g.setColour (Theme::phaseAccent.withAlpha (0.8f));
    g.setFont (Theme::font (9.5f, true));
    g.drawText ("M", juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ c.x + 9.0f, scopeArea.getY() + 10.0f }), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ c.x - r * 0.7f - 4.0f, c.y - r * 0.7f - 4.0f }), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ c.x + r * 0.7f + 4.0f, c.y - r * 0.7f - 4.0f }), juce::Justification::centred);
    g.drawText ("S", juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ scopeArea.getRight() - 9.0f, c.y - 8.0f }), juce::Justification::centred);

    // Correlation bar, multi-coloured: red below 0, yellow 0..0.3, green above
    auto corrBox = area.removeFromTop (44.0f);
    Theme::drawFace (g, corrBox);
    auto inner = corrBox.reduced (8.0f, 5.0f);
    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (9.5f, true));
    g.drawText ("CORRELATION", inner.removeFromTop (12.0f), juce::Justification::left);
    auto bar = inner.removeFromTop (12.0f);

    juce::ColourGradient grad (Theme::levelRed, bar.getX(), 0.0f, Theme::levelGreen, bar.getRight(), 0.0f, false);
    grad.addColour (0.5, Theme::levelYellow);
    g.setGradientFill (grad);
    g.setOpacity (0.18f);
    g.fillRect (bar);
    g.setOpacity (1.0f);

    const float cx = bar.getCentreX();
    const float x = cx + juce::jlimit (-1.0f, 1.0f, correlation) * bar.getWidth() * 0.5f;
    const auto barColour = correlation < 0.0f ? Theme::levelRed : correlation < 0.3f ? Theme::levelYellow : Theme::levelGreen;
    g.setColour (barColour);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), bar.getY(), juce::jmax (cx, x), bar.getBottom()));
    g.setColour (Theme::textPrimary);
    g.fillRect (juce::Rectangle<float> (2.0f, bar.getHeight() + 4.0f).withCentre ({ x, bar.getCentreY() }));
    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (9.0f));
    auto ticks = inner;
    g.drawText ("-1", ticks, juce::Justification::left);
    g.drawText ("0", ticks, juce::Justification::centred);
    g.drawText ("+1", ticks, juce::Justification::right);

    area.removeFromTop (6.0f);
    const float cellW = (area.getWidth() - 12.0f) / 3.0f;
    auto row = area.removeFromTop (juce::jmin (40.0f, area.getHeight()));
    Theme::drawReadout (g, row.removeFromLeft (cellW), "Corr", juce::String (correlation, 2), barColour, 15.0f);
    row.removeFromLeft (6.0f);
    Theme::drawReadout (g, row.removeFromLeft (cellW), "Bal dB", juce::String (balance, 1), Theme::phaseAccent, 15.0f);
    row.removeFromLeft (6.0f);
    Theme::drawReadout (g, row, "Width", juce::String (width, 2), Theme::phaseAccent, 15.0f);
}

//==============================================================================
SpectrumView::SpectrumView()
{
    prepare (48000.0);
}

void SpectrumView::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    analyzer.prepare (fftOrder, sampleRate);
    analyzer.setAveraging (0.75f);
    ring.assign (static_cast<size_t> (analyzer.getFftSize()), 0.0f);
    frame.assign (ring.size(), 0.0f);
    ringWrite = 0;
    samplesSinceFrame = 0;
    spectrogram = juce::Image (juce::Image::RGB, 480, spectrogramRows, true);
}

void SpectrumView::resized() {}

void SpectrumView::pushSamples (const float* samples, int numSamples)
{
    const int size = static_cast<int> (ring.size());

    for (int i = 0; i < numSamples; ++i)
    {
        ring[static_cast<size_t> (ringWrite)] = samples[i];
        ringWrite = (ringWrite + 1) % size;

        if (++samplesSinceFrame >= hopSize)
        {
            samplesSinceFrame = 0;
            for (int k = 0; k < size; ++k)
                frame[static_cast<size_t> (k)] = ring[static_cast<size_t> ((ringWrite + k) % size)];
            analyzer.processFrame (frame.data());
            addSpectrogramColumn();
        }
    }
}

float SpectrumView::tiltAt (float hz) const noexcept
{
    return tilt ? 4.5f * std::log2 (hz / 1000.0f) : 0.0f;
}

float SpectrumView::levelBetween (float hzLo, float hzHi, bool peak) const noexcept
{
    const auto& data = peak ? analyzer.getPeakHoldDb() : analyzer.getMagnitudesDb();
    const int numBins = analyzer.getNumBins();
    const float binHz = static_cast<float> (sampleRate) / static_cast<float> (analyzer.getFftSize());
    const int lo = juce::jlimit (1, numBins - 1, static_cast<int> (hzLo / binHz));
    const int hi = juce::jlimit (lo, numBins - 1, static_cast<int> (hzHi / binHz));

    float level = -200.0f;
    for (int b = lo; b <= hi; ++b)
        level = juce::jmax (level, data[static_cast<size_t> (b)]);
    return level;
}

void SpectrumView::addSpectrogramColumn()
{
    const int w = spectrogram.getWidth();
    const int h = spectrogram.getHeight();

    if (verticalSpectrogram)
    {
        // Waterfall: frequency runs left to right like the spectrum above,
        // newest line at the top, history scrolling down.
        spectrogram.moveImageSection (0, 1, 0, 0, w, h - 1);
        for (int col = 0; col < w; ++col)
        {
            const float lo = static_cast<float> (col) / static_cast<float> (w);
            const float hi = static_cast<float> (col + 1) / static_cast<float> (w);
            const float hzLo = 20.0f * std::pow (1000.0f, lo);
            const float hzHi = 20.0f * std::pow (1000.0f, hi);
            const float db = levelBetween (hzLo, hzHi, false) + tiltAt (0.5f * (hzLo + hzHi));
            spectrogram.setPixelAt (col, 0, Theme::heat ((db + 90.0f) / 84.0f));
        }
        return;
    }

    spectrogram.moveImageSection (0, 0, 1, 0, w - 1, h);

    for (int row = 0; row < h; ++row)
    {
        // Row 0 is 20 kHz, the last row 20 Hz.
        const float pos = 1.0f - (static_cast<float> (row) + 0.5f) / static_cast<float> (h);
        const float hzLo = 20.0f * std::pow (1000.0f, pos - 0.5f / static_cast<float> (h));
        const float hzHi = 20.0f * std::pow (1000.0f, pos + 0.5f / static_cast<float> (h));
        const float db = levelBetween (hzLo, hzHi, false) + tiltAt (0.5f * (hzLo + hzHi));
        spectrogram.setPixelAt (w - 1, row, Theme::heat ((db + 90.0f) / 84.0f));
    }
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), tilt ? "Spectrum  4.5 dB/oct tilt" : "Spectrum",
                                  Theme::spectrumAccent).reduced (8.0f);

    auto sgArea = area.removeFromBottom (juce::jmax (60.0f, area.getHeight() * 0.30f));
    area.removeFromBottom (6.0f);

    // ---- Spectrum
    Theme::drawFace (g, area);
    auto plot = area.reduced (4.0f).withTrimmedLeft (24.0f).withTrimmedBottom (14.0f);
    constexpr float minDb = -90.0f, maxDb = 6.0f;
    auto dbToY = [&] (float db) { return plot.getBottom() - (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb) * plot.getHeight(); };
    auto hzToX = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };

    g.setFont (Theme::font (9.5f));
    for (float db = -84.0f; db <= 0.0f; db += 12.0f)
    {
        g.setColour (Theme::grid);
        g.drawHorizontalLine (juce::roundToInt (dbToY (db)), plot.getX(), plot.getRight());
        g.setColour (Theme::textSecondary);
        g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (area.getX() + 2.0f, dbToY (db) - 6.0f, 24.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = hzToX (hz);
        g.setColour (Theme::grid);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (Theme::frequencyColour (hz).withAlpha (0.9f));
        const auto label = hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
        g.drawText (label, juce::Rectangle<float> (x - 18.0f, plot.getBottom() + 2.0f, 36.0f, 12.0f), juce::Justification::centred);
    }

    juce::Path line, fill, peakLine;
    const int width = juce::roundToInt (plot.getWidth());
    for (int px = 0; px <= width; px += 2)
    {
        const float hzLo = 20.0f * std::pow (1000.0f, static_cast<float> (px) / static_cast<float> (width));
        const float hzHi = 20.0f * std::pow (1000.0f, static_cast<float> (px + 2) / static_cast<float> (width));
        const float t = tiltAt (hzLo);
        const float x = plot.getX() + static_cast<float> (px);
        const float y = dbToY (levelBetween (hzLo, hzHi, false) + t);
        const float py = dbToY (levelBetween (hzLo, hzHi, true) + t);

        if (px == 0)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, plot.getBottom());
            fill.lineTo (x, y);
            peakLine.startNewSubPath (x, py);
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
            peakLine.lineTo (x, py);
        }
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    juce::ColourGradient rainbow (Theme::frequencyColour (20.0f), plot.getX(), 0.0f,
                                  Theme::frequencyColour (20000.0f), plot.getRight(), 0.0f, false);
    for (float hz : { 60.0f, 200.0f, 600.0f, 2000.0f, 6000.0f })
        rainbow.addColour ((hzToX (hz) - plot.getX()) / plot.getWidth(), Theme::frequencyColour (hz));

    g.setGradientFill (rainbow);
    g.setOpacity (0.35f);
    g.fillPath (fill);
    g.setOpacity (1.0f);
    g.setGradientFill (rainbow);
    g.strokePath (line, juce::PathStrokeType (1.8f));
    g.setColour (Theme::textPrimary.withAlpha (0.45f));
    g.strokePath (peakLine, juce::PathStrokeType (1.0f));

    // ---- Spectrogram
    Theme::drawFace (g, sgArea);
    auto sgPlot = sgArea.reduced (4.0f).withTrimmedLeft (24.0f);
    g.drawImage (spectrogram, sgPlot, juce::RectanglePlacement::stretchToFit);
    g.setFont (Theme::font (9.0f));
    if (verticalSpectrogram)
    {
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawVerticalLine (juce::roundToInt (hzToX (hz)), sgPlot.getY(), sgPlot.getBottom());
        }
        g.setColour (Theme::textSecondary);
        g.drawText ("now", juce::Rectangle<float> (sgArea.getX() + 2.0f, sgPlot.getY(), 22.0f, 10.0f), juce::Justification::centredLeft);
        g.drawText ("time", juce::Rectangle<float> (sgArea.getX() + 2.0f, sgPlot.getBottom() - 10.0f, 22.0f, 10.0f), juce::Justification::centredLeft);
        return;
    }
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float y = sgPlot.getBottom() - sgPlot.getHeight() * std::log (hz / 20.0f) / std::log (1000.0f);
        g.setColour (Theme::frequencyColour (hz));
        g.drawText (hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz)),
                    juce::Rectangle<float> (sgArea.getX() + 2.0f, y - 5.0f, 22.0f, 10.0f), juce::Justification::centredLeft);
    }
    g.setColour (Theme::textSecondary);
    g.drawText ("time", sgPlot.reduced (4.0f, 2.0f), juce::Justification::bottomRight);
}

//==============================================================================
const juce::Colour LedPeakAverageMeter::peakColour { 0xffe6f6ff };

void LedPeakAverageMeter::setTitle (juce::String newTitle) { title = std::move (newTitle); }
void LedPeakAverageMeter::mouseDown (const juce::MouseEvent&)
{
    // Click cycles peak hold: auto 3 s -> hold until reset -> off -> auto.
    holdMode = holdMode == HoldMode::autoReset ? HoldMode::untilReset
             : holdMode == HoldMode::untilReset ? HoldMode::off : HoldMode::autoReset;
    hold = peak;
    repaint();
}

void LedPeakAverageMeter::setNumFaces (int n) { numFaces = juce::jlimit (1, 2, n); }
void LedPeakAverageMeter::setFullScale (float relDb) { fullScale = relDb; }
void LedPeakAverageMeter::setReferenceText (juce::String text) { referenceText = std::move (text); }

void LedPeakAverageMeter::setFaceLabel (int face, juce::String label)
{
    labels[static_cast<size_t> (juce::jlimit (0, 1, face))] = std::move (label);
}

void LedPeakAverageMeter::setValues (int face, float peakRel, float averageRel, bool over)
{
    const auto i = static_cast<size_t> (juce::jlimit (0, 1, face));
    peak[i] = peakRel;
    average[i] = averageRel;

    // Peak hold: off, auto (highest peak held three seconds at the 30 Hz UI
    // rate, then reset) or held until the mode is changed.
    if (holdMode == HoldMode::off || peakRel >= hold[i])
    {
        hold[i] = peakRel;
        holdFrames[i] = 90;
    }
    else if (holdMode == HoldMode::autoReset && --holdFrames[i] <= 0)
    {
        hold[i] = peakRel;
        holdFrames[i] = 90;
    }

    // The OVER lamp stays lit for a second so a single over is not missed.
    overFrames[i] = over ? 30 : juce::jmax (0, overFrames[i] - 1);
}

float LedPeakAverageMeter::arcPosition (float relDb) noexcept
{
    // Piecewise-linear scale (our design): -40..-20 squeezed into the first
    // fifth, the working range around 0 spread wide, headroom to +20.
    static constexpr std::array<std::pair<float, float>, 9> points { {
        { -40.0f, 0.00f }, { -30.0f, 0.08f }, { -20.0f, 0.18f }, { -10.0f, 0.36f }, { -5.0f, 0.49f },
        { 0.0f, 0.63f }, { 5.0f, 0.75f }, { 10.0f, 0.86f }, { 20.0f, 1.00f } } };

    if (relDb <= points.front().first)
        return 0.0f;

    for (size_t i = 1; i < points.size(); ++i)
    {
        if (relDb <= points[i].first)
        {
            const auto [x0, y0] = points[i - 1];
            const auto [x1, y1] = points[i];
            return y0 + (relDb - x0) / (x1 - x0) * (y1 - y0);
        }
    }

    return 1.0f;
}

juce::Colour LedPeakAverageMeter::zoneColour (float relDb) const noexcept
{
    if (relDb >= fullScale - 6.0f)
        return Theme::levelRed;
    if (relDb >= 0.0f)
        return Theme::levelYellow;
    return Theme::levelGreen;
}

void LedPeakAverageMeter::paint (juce::Graphics& g)
{
    auto area = Theme::drawPanel (g, getLocalBounds().toFloat(), title, Theme::arcAccent).reduced (10.0f);

    if (referenceText.isNotEmpty())
    {
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (11.0f));
        g.drawText (referenceText, area.removeFromBottom (16.0f), juce::Justification::centredRight);
        area.removeFromBottom (4.0f);
    }

    if (shape == Shape::bar)
    {
        drawBars (g, area);
        return;
    }

    constexpr float aspect = archedAspect; // width / height of one unit
    constexpr float gap = 8.0f;

    if (numFaces < 2)
    {
        drawFace (g, area, 0, true);
        return;
    }

    // Stereo: two units side by side in one rack panel with ears, as a
    // console pair is usually mounted; stacked when the slot is too tall
    // and narrow for that to stay readable.
    constexpr float earsRatio = 0.5f; // both ears together, in unit heights
    const float rackH = juce::jmin (area.getHeight(), area.getWidth() / (2.0f * aspect + earsRatio));
    const float stackH = juce::jmin ((area.getHeight() - gap) * 0.5f, area.getWidth() / aspect);

    if (rackH >= 0.6f * stackH)
    {
        auto rack = area.withSizeKeepingCentre (rackH * (2.0f * aspect + earsRatio), rackH);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff666a70), rack.getX(), rack.getY(),
                                                 juce::Colour (0xff4b4e53), rack.getX(), rack.getBottom(), false));
        g.fillRoundedRectangle (rack, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (rack.reduced (0.5f), 3.0f, 1.0f);

        // Rack ears with mounting slots and screws
        const float ear = rackH * earsRatio * 0.5f;
        for (auto x : { rack.getX() + ear * 0.5f, rack.getRight() - ear * 0.5f })
        {
            for (auto y : { rack.getY() + rackH * 0.16f, rack.getBottom() - rackH * 0.16f })
            {
                const float d = rackH * 0.075f;
                g.setColour (juce::Colour (0xff1d1e20));
                g.fillRoundedRectangle (juce::Rectangle<float> (d * 1.6f, d).withCentre ({ x, y }), d * 0.5f);
            }
            for (auto y : { rack.getY() + rackH * 0.36f, rack.getBottom() - rackH * 0.36f })
            {
                const float d = rackH * 0.05f;
                g.setColour (juce::Colour (0xff8a8d92));
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ x, y }));
                g.setColour (juce::Colour (0xff3a3c40));
                g.drawLine (x - d * 0.35f, y, x + d * 0.35f, y, 1.0f);
            }
        }

        auto units = rack.reduced (ear, 0.0f);
        auto left = units.removeFromLeft (units.getWidth() * 0.5f);
        drawFace (g, left, 0, false);
        drawFace (g, units, 1, false);
        return;
    }

    auto top = area.removeFromTop ((area.getHeight() - gap) * 0.5f);
    area.removeFromTop (gap);
    drawFace (g, top, 0, true);
    drawFace (g, area, 1, true);
}

namespace
{
    /// Draws text along a circular arc, centred on midAngle (0 = straight up,
    /// clockwise positive), letters upright relative to the arc.
    void drawTextOnArc (juce::Graphics& g, const juce::String& text, juce::Point<float> centre, float radius,
                        float midAngle, const juce::Font& font)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        const float width = glyphs.getBoundingBox (0, -1, true).getWidth();
        const float ascent = font.getAscent() * 0.35f;

        for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
        {
            const auto& glyph = glyphs.getGlyph (i);
            const float x = glyph.getLeft() + glyph.getBounds().getWidth() * 0.5f - width * 0.5f;
            const float angle = midAngle + x / radius;
            const auto p = centre + juce::Point<float> (std::sin (angle) * radius, -std::cos (angle) * radius);
            const auto t = juce::AffineTransform::translation (-(glyph.getLeft() + glyph.getBounds().getWidth() * 0.5f), ascent)
                               .rotated (angle)
                               .translated (p);
            glyph.draw (g, t);
        }
    }
}

juce::Colour LedPeakAverageMeter::archedLedColour (float relDb) const noexcept
{
    // Our zone layout for the arched monitor: green for low persistence,
    // amber for the normal persistence range, red around the reference
    // (0 +/- 1), yellow-green across the normal peak range and red again
    // in the last few dB before full scale.
    if (relDb >= fullScale - 0.5f)
        return Theme::levelRed;
    if (relDb >= juce::jmin (12.0f, fullScale - 6.0f))
        return juce::Colour (0xffff4a2a);
    if (relDb >= 2.0f)
        return juce::Colour (0xffc8e83a);
    if (relDb >= -1.0f)
        return juce::Colour (0xffff3b2a);
    if (relDb >= -10.0f)
        return juce::Colour (0xffffa526);
    return juce::Colour (0xff4fe03a);
}

void LedPeakAverageMeter::drawFace (juce::Graphics& g, juce::Rectangle<float> area, int face, bool ownPlate) const
{
    // Wide arched LED monitor in a deep recessed bezel (our design, after the
    // public look of console loudness monitors): one LED per dB from -25 to
    // +20 along a shallow arc, the average lit as a bar and the peak as a
    // single LED, a dB-re-reference scale above and a dBFS peak scale below.
    const float h = juce::jmin (area.getHeight(), area.getWidth() / archedAspect);
    if (h < 40.0f)
        return;

    auto unit = area.withSizeKeepingCentre (h * archedAspect, h);
    const float s = juce::jlimit (0.4f, 2.4f, h / 300.0f);
    const auto i = static_cast<size_t> (face);

    // --- Grey plate behind a free-standing unit (a rack pair has its own panel)
    if (ownPlate)
    {
        g.setColour (juce::Colour (0xff5d6066));
        g.fillRoundedRectangle (unit, 4.0f * s);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (unit.reduced (0.5f), 4.0f * s, 1.0f);
    }

    // --- Deep recessed bezel: black frame with angled inner walls
    auto frame = unit.reduced (h * 0.035f);
    g.setColour (juce::Colour (0xff0d0d0e));
    g.fillRoundedRectangle (frame, 6.0f * s);

    const float wall = h * 0.07f;
    auto faceRect = frame.reduced (wall + h * 0.02f);
    auto wallOuter = frame.reduced (h * 0.02f);
    auto quad = [&] (juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::Point<float> d, juce::Colour col)
    {
        juce::Path p;
        p.startNewSubPath (a); p.lineTo (b); p.lineTo (c); p.lineTo (d); p.closeSubPath();
        g.setColour (col);
        g.fillPath (p);
    };
    quad (wallOuter.getTopLeft(), wallOuter.getTopRight(), faceRect.getTopRight(), faceRect.getTopLeft(), juce::Colour (0xff050505));
    quad (wallOuter.getBottomLeft(), wallOuter.getBottomRight(), faceRect.getBottomRight(), faceRect.getBottomLeft(), juce::Colour (0xff2a2b2e));
    quad (wallOuter.getTopLeft(), wallOuter.getBottomLeft(), faceRect.getBottomLeft(), faceRect.getTopLeft(), juce::Colour (0xff141416));
    quad (wallOuter.getTopRight(), wallOuter.getBottomRight(), faceRect.getBottomRight(), faceRect.getTopRight(), juce::Colour (0xff1c1d20));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRect (wallOuter, 1.0f);

    // --- Smoked face
    g.saveState();
    g.reduceClipRegion (faceRect.toNearestInt());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2019), faceRect.getCentreX(), faceRect.getY(),
                                             juce::Colour (0xff160f0b), faceRect.getCentreX(), faceRect.getBottom(), false));
    g.fillRect (faceRect);

    const float W = faceRect.getWidth(), H = faceRect.getHeight();

    // Arc geometry: a chord across most of the face with a shallow rise.
    const float chord = W * 0.80f;
    const float sagitta = H * 0.17f;
    const float R = (chord * chord * 0.25f + sagitta * sagitta) / (2.0f * sagitta);
    const float halfAngle = std::asin (chord * 0.5f / R);
    const float ledLen = H * 0.2f;
    const float r0 = R;                              // LED inner radius
    const float topY = faceRect.getY() + H * 0.34f;  // top of the arc's inner edge
    const juce::Point<float> centre (faceRect.getCentreX(), topY + r0);

    constexpr float lo = -25.0f, hi = 20.0f;
    constexpr int numLeds = static_cast<int> (hi - lo) + 1;
    auto angleFor = [&] (float rel) { return -halfAngle + (rel - lo) / (hi - lo) * 2.0f * halfAngle; };
    auto pointAt = [&] (float angle, float r) { return centre + juce::Point<float> (std::sin (angle) * r, -std::cos (angle) * r); };

    const float pitch = 2.0f * halfAngle * R / static_cast<float> (numLeds - 1);
    const float ledW = pitch * 0.62f;

    const int avgLed = juce::roundToInt (average[i] - lo);
    const int peakLed = peak[i] > lo - 0.5f ? juce::jlimit (0, numLeds - 1, juce::roundToInt (peak[i] - lo)) : -1;
    const int holdLed = (holdMode != HoldMode::off && hold[i] > lo - 0.5f) ? juce::jlimit (0, numLeds - 1, juce::roundToInt (hold[i] - lo)) : -1;
    const bool over = overFrames[i] > 0;

    for (int k = 0; k < numLeds; ++k)
    {
        const float rel = lo + static_cast<float> (k);
        const float a = angleFor (rel);
        auto colour = archedLedColour (rel);
        if (k == numLeds - 1 && over)
            colour = Theme::levelRed;

        juce::Path led;
        led.addRoundedRectangle (-ledW * 0.5f, -(r0 + ledLen), ledW, ledLen, ledW * 0.18f);
        const auto t = juce::AffineTransform::rotation (a).translated (centre);
        led.applyTransform (t);

        const bool lit = k <= avgLed;
        const bool isPeak = k == peakLed || (k == numLeds - 1 && over);
        const bool isHold = k == holdLed;

        if (isPeak || isHold)
        {
            g.setColour (colour.withAlpha (isPeak ? 0.45f : 0.25f));
            g.strokePath (led, juce::PathStrokeType (4.0f * s));
            g.setColour (isPeak ? colour.interpolatedWith (juce::Colours::white, 0.35f) : colour);
        }
        else if (lit)
        {
            g.setColour (colour.withAlpha (0.92f));
        }
        else
        {
            g.setColour (colour.withAlpha (0.13f));
        }
        g.fillPath (led);
    }

    // --- Upper scale (dB re reference) and its rule
    const float ruleR = r0 + ledLen + H * 0.025f;
    {
        juce::Path rule;
        rule.addCentredArc (centre.x, centre.y, ruleR, ruleR, 0.0f, angleFor (lo - 0.5f), angleFor (11.5f), true);
        g.setColour (juce::Colour (0xffc8e83a).withAlpha (0.85f));
        g.strokePath (rule, juce::PathStrokeType (1.6f * s));
        juce::Path hot;
        hot.addCentredArc (centre.x, centre.y, ruleR, ruleR, 0.0f, angleFor (11.5f), angleFor (hi + 0.5f), true);
        g.setColour (juce::Colour (0xffff4a2a));
        g.strokePath (hot, juce::PathStrokeType (2.4f * s));
    }

    const auto numberFont = Theme::font (juce::jmax (6.0f, 11.0f * s), true);
    for (int v = static_cast<int> (lo); v <= static_cast<int> (hi); ++v)
    {
        if ((v > 14 && v != static_cast<int> (hi)) || (v < -10 && v % 5 != 0))
            continue;
        const float rel = static_cast<float> (v);
        const auto text = v > 0 ? "+" + juce::String (v) : juce::String (v);
        const auto col = (v >= -1 && v <= 1) || v >= 12 ? juce::Colour (0xffff6a40) : juce::Colour (0xffeee6d8);
        g.setColour (col);
        drawTextOnArc (g, v == static_cast<int> (hi) ? juce::String ("FS") : text, centre, ruleR + H * 0.055f, angleFor (rel), numberFont);
    }

    // --- Lower scale: the same LEDs read in dBFS across the peak range
    const float lowR = r0 - H * 0.055f;
    for (int v = 0; v <= static_cast<int> (hi); v += 2)
    {
        const float rel = static_cast<float> (v);
        const float dbfs = rel - fullScale;
        if (dbfs > 0.5f)
            continue;
        g.setColour (juce::Colour (0xffeee6d8).withAlpha (0.85f));
        drawTextOnArc (g, juce::String (juce::roundToInt (dbfs)), centre, lowR, angleFor (rel),
                       Theme::font (juce::jmax (6.0f, 9.5f * s), true));
    }

    // Range rules and their captions under the arc
    const float rangeR = r0 - H * 0.12f;
    auto rangeArc = [&] (float from, float to, juce::Colour col, const juce::String& caption)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, rangeR, rangeR, 0.0f, angleFor (from), angleFor (to), true);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (1.4f * s));
        g.setColour (juce::Colour (0xffff7a3a));
        drawTextOnArc (g, caption, centre, rangeR - H * 0.06f, 0.5f * (angleFor (from) + angleFor (to)),
                       Theme::font (juce::jmax (6.0f, 10.0f * s), true));
    };
    rangeArc (-20.0f, -1.0f, juce::Colour (0xffc8e83a).withAlpha (0.8f), "NORMAL PERSISTENCE RANGE");
    rangeArc (2.0f, 11.0f, juce::Colour (0xffff7a3a).withAlpha (0.8f), "NORMAL PEAK RANGE");

    // "dB" at both ends, and the "dBFS" unit for the lower scale
    g.setColour (juce::Colour (0xffeee6d8));
    g.setFont (Theme::font (juce::jmax (6.0f, 10.0f * s), true));
    g.drawText ("dB", juce::Rectangle<float> (24.0f * s, 14.0f * s).withCentre (pointAt (angleFor (lo) - 0.03f, r0 + ledLen * 0.5f)).translated (-10.0f * s, 0.0f),
                juce::Justification::centred);
    g.drawText ("dB", juce::Rectangle<float> (24.0f * s, 14.0f * s).withCentre (pointAt (angleFor (hi) + 0.03f, r0 + ledLen * 0.5f)).translated (10.0f * s, 0.0f),
                juce::Justification::centred);

    // --- PHASE lamp (stereo) and peak-hold mode, top corners
    const float lampD = juce::jmax (5.0f, 9.0f * s);
    auto lamp = juce::Rectangle<float> (lampD, lampD).withPosition (faceRect.getX() + 12.0f * s, faceRect.getY() + 10.0f * s);
    if (numFaces == 2)
    {
        g.setColour (phaseAlarm ? Theme::levelRed : juce::Colour (0xff4a1610));
        g.fillEllipse (lamp);
        g.setColour (juce::Colour (0xffc8e83a).withAlpha (0.85f));
        g.setFont (Theme::font (juce::jmax (6.0f, 10.0f * s), true));
        g.drawText ("PHASE", lamp.withWidth (70.0f * s).translated (lampD + 5.0f * s, -2.0f * s).withHeight (lampD + 4.0f * s),
                    juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (0xffeee6d8).withAlpha (0.6f));
    g.setFont (Theme::font (juce::jmax (6.0f, 9.0f * s), true));
    const juce::String holdText = holdMode == HoldMode::off ? "PEAK HOLD OFF"
                                : holdMode == HoldMode::autoReset ? "PEAK HOLD 3 s" : "PEAK HOLD";
    g.drawText (holdText, faceRect.reduced (12.0f * s, 8.0f * s).withHeight (14.0f * s), juce::Justification::centredRight);

    // --- Nameplate strip: rule, caption, readouts
    auto plate = faceRect.withTop (faceRect.getBottom() - H * 0.19f).reduced (12.0f * s, 0.0f);
    g.setColour (juce::Colour (0xffeee6d8).withAlpha (0.75f));
    g.drawLine (plate.getX(), plate.getY() + H * 0.08f, plate.getRight(), plate.getY() + H * 0.08f, 1.0f * s);

    auto title = plate.removeFromTop (H * 0.08f);
    g.setFont (Theme::font (juce::jmax (7.0f, 14.0f * s)));
    g.drawText ("Peak + Average Monitor", title.translated (0.0f, -2.0f * s), juce::Justification::centred);

    auto bottom = plate.withTrimmedTop (2.0f * s);
    g.setFont (Theme::font (juce::jmax (6.0f, 9.0f * s), true));
    g.setColour (juce::Colour (0xffeee6d8).withAlpha (0.55f));
    g.drawText ("NEW CONSTELLATION", bottom, juce::Justification::centredLeft);
    g.setColour (Theme::arcAccent);
    g.setFont (Theme::font (juce::jmax (7.0f, 12.0f * s), true));
    g.drawText (labels[i], bottom, juce::Justification::centredRight);

    auto fmt = [] (float v) { return v <= -45.0f ? juce::String ("--") : (v > 0.0f ? "+" : "") + juce::String (v, 1); };
    const bool hasCrest = peak[i] > -45.0f && average[i] > -45.0f;
    const auto readout = "AVG " + fmt (average[i]) + "    PEAK " + fmt (holdMode == HoldMode::off ? peak[i] : hold[i])
                       + "    P-A " + (hasCrest ? juce::String (juce::jmax (0.0f, peak[i] - average[i]), 1) : juce::String ("--"));
    g.setColour (juce::Colour (0xffeee6d8));
    g.setFont (Theme::mono (juce::jmax (7.0f, 12.0f * s), true));
    g.drawText (readout, bottom, juce::Justification::centred);

    // Glass sheen across the top of the window
    juce::Path sheen;
    sheen.addRectangle (faceRect.withHeight (H * 0.45f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), faceRect.getCentreX(), faceRect.getY(),
                                             juce::Colours::transparentWhite, faceRect.getCentreX(), faceRect.getY() + H * 0.45f, false));
    g.fillPath (sheen);

    g.restoreState();
}


void LedPeakAverageMeter::drawBars (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // Long LED bar graphs in one dark instrument: horizontal when the slot is
    // wide, vertical when it is tall. The calibrated scale sits between the
    // two bars (stereo) or beside the one bar (mono).
    const bool vertical = area.getHeight() > area.getWidth() * 1.1f;
    const float minSide = juce::jmin (area.getWidth(), area.getHeight());
    const float s = juce::jlimit (0.5f, 2.0f, (vertical ? area.getWidth() : area.getHeight()) / 360.0f);
    if (minSide < 40.0f)
        return;

    // --- Bezel and black face
    auto bezel = area;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), bezel.getX(), bezel.getY(),
                                             juce::Colour (0xff131417), bezel.getX(), bezel.getBottom(), false));
    g.fillRoundedRectangle (bezel, 10.0f * s);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (bezel.reduced (1.0f), 10.0f * s, 1.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bezel, 10.0f * s, 1.2f);

    auto window = bezel.reduced (juce::jmax (6.0f, 12.0f * s));
    g.setColour (Theme::arcAccent.withAlpha (0.55f));
    g.drawRoundedRectangle (window.expanded (2.5f), 6.0f * s, 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e1114), window.getCentreX(), window.getY(),
                                             juce::Colour (0xff040506), window.getCentreX(), window.getBottom(), false));
    g.fillRoundedRectangle (window, 6.0f * s);

    auto inner = window.reduced (14.0f * s);

    // --- Readouts: a column on the right (horizontal) or a strip at the bottom (vertical)
    const int n = numFaces;
    juce::Rectangle<float> readouts;
    if (vertical)
        readouts = inner.removeFromBottom (juce::jlimit (60.0f, 150.0f, inner.getHeight() * 0.2f));
    else
        readouts = inner.removeFromRight (juce::jlimit (110.0f, 280.0f, inner.getWidth() * 0.2f));
    if (vertical) inner.removeFromBottom (10.0f * s); else inner.removeFromRight (16.0f * s);

    // Leave room at both ends of the scale for the end labels.
    const float endPad = (vertical ? 16.0f : 22.0f) * s;
    if (vertical) inner.reduce (0.0f, endPad); else inner.reduce (endPad, 0.0f);

    // --- Lanes: [bar, scale, bar] for stereo, [bar, scale] for mono
    const float across = vertical ? inner.getWidth() : inner.getHeight();
    const float along = vertical ? inner.getHeight() : inner.getWidth();
    const float scaleW = juce::jlimit (34.0f, 90.0f, across * (n == 2 ? 0.26f : 0.4f));
    const float laneW = juce::jmin ((across - scaleW) / static_cast<float> (n), juce::jmax (18.0f, along * 0.11f));
    const float total = laneW * static_cast<float> (n) + scaleW;
    auto lanesArea = vertical ? inner.withSizeKeepingCentre (total, inner.getHeight())
                              : inner.withSizeKeepingCentre (inner.getWidth(), total);

    auto take = [&] (float w)
    {
        return vertical ? lanesArea.removeFromLeft (w) : lanesArea.removeFromTop (w);
    };

    std::array<juce::Rectangle<float>, 2> lanes;
    juce::Rectangle<float> scaleArea;
    lanes[0] = take (laneW);
    scaleArea = take (scaleW);
    if (n == 2)
        lanes[1] = take (laneW);

    // Position along a lane for a level: left to right, or bottom to top.
    auto posFor = [&] (float rel)
    {
        const float t = arcPosition (rel);
        return vertical ? inner.getBottom() - t * inner.getHeight() : inner.getX() + t * inner.getWidth();
    };

    auto segmentRect = [&] (const juce::Rectangle<float>& lane, float p0, float p1)
    {
        return vertical ? juce::Rectangle<float> (lane.getX(), juce::jmin (p0, p1), lane.getWidth(), std::abs (p1 - p0))
                        : juce::Rectangle<float> (juce::jmin (p0, p1), lane.getY(), std::abs (p1 - p0), lane.getHeight());
    };

    // Level at a segment's centre, for its zone colour.
    auto levelAt = [] (float t)
    {
        float lo = scaleMin, hi = scaleMax;
        for (int it = 0; it < 24; ++it)
        {
            const float mid = 0.5f * (lo + hi);
            (arcPosition (mid) < t ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    };

    const float segLen = along / static_cast<float> (numBarSegments);
    const float gapLen = juce::jmax (1.0f, segLen * 0.28f);

    for (int ch = 0; ch < n; ++ch)
    {
        const auto i = static_cast<size_t> (ch);
        const auto lane = lanes[i];

        // Recessed channel behind the LEDs
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (lane.expanded (3.0f * s), 3.0f * s);

        const float avgT = arcPosition (average[i]);
        const int peakSeg = peak[i] > scaleMin
                              ? juce::jlimit (0, numBarSegments - 1, static_cast<int> (arcPosition (peak[i]) * numBarSegments))
                              : -1;
        const int holdSeg = hold[i] > scaleMin
                              ? juce::jlimit (0, numBarSegments - 1, static_cast<int> (arcPosition (hold[i]) * numBarSegments))
                              : -1;

        for (int k = 0; k < numBarSegments; ++k)
        {
            const float t0 = static_cast<float> (k) / numBarSegments;
            const float t1 = static_cast<float> (k + 1) / numBarSegments;
            const float a = vertical ? inner.getBottom() - t0 * along : inner.getX() + t0 * along;
            const float b = vertical ? inner.getBottom() - t1 * along + gapLen : inner.getX() + t1 * along - gapLen;
            auto seg = segmentRect (lane, a, b);

            const auto colour = zoneColour (levelAt (0.5f * (t0 + t1)));
            const bool lit = t1 <= avgT + 0.5f / numBarSegments;

            if (k == peakSeg)
            {
                g.setColour (peakColour.withAlpha (0.35f));
                g.fillRect (seg.expanded (2.5f * s));
                g.setColour (peakColour);
            }
            else if (k == holdSeg)
            {
                g.setColour (peakColour.withAlpha (0.55f));
            }
            else if (lit)
            {
                g.setColour (colour.withAlpha (0.92f));
            }
            else
            {
                g.setColour (colour.withAlpha (0.09f));
            }
            g.fillRect (seg);
        }

        // Channel label at the low end of the bar
        g.setColour (Theme::arcAccent);
        g.setFont (Theme::font (juce::jmax (9.0f, 14.0f * s), true));
        const auto labelBox = vertical ? juce::Rectangle<float> (lane.getX(), inner.getBottom() + 2.0f, lane.getWidth(), endPad)
                                       : juce::Rectangle<float> (inner.getX() - endPad - 2.0f, lane.getY(), endPad, lane.getHeight());
        g.drawText (n == 2 ? labels[i] : juce::String ("M"), labelBox, juce::Justification::centred);
    }

    // --- Calibrated scale: ticks toward each bar, numbers in the middle
    g.setFont (Theme::font (juce::jmax (8.0f, 12.5f * s), true));
    for (float rel : { -40.0f, -35.0f, -30.0f, -25.0f, -20.0f, -18.0f, -16.0f, -14.0f, -12.0f, -10.0f, -8.0f, -6.0f, -5.0f,
                       -4.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f,
                       16.0f, 18.0f, 20.0f })
    {
        const float p = posFor (rel);
        static constexpr float numberedLevels[] { -40.0f, -30.0f, -20.0f, -10.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 10.0f, 14.0f, 20.0f };
        const bool numbered = std::find (std::begin (numberedLevels), std::end (numberedLevels), rel) != std::end (numberedLevels);
        const auto col = rel > 0.0f ? (rel >= fullScale - 6.0f ? Theme::levelRed : Theme::levelYellow)
                                    : Theme::textPrimary.withAlpha (rel == 0.0f ? 1.0f : 0.72f);
        g.setColour (col);
        const float tick = (numbered ? 7.0f : 4.0f) * s;
        const float thick = (rel == 0.0f ? 2.0f : 1.0f) * s;

        if (vertical)
        {
            g.drawLine (scaleArea.getX(), p, scaleArea.getX() + tick, p, thick);
            if (n == 2)
                g.drawLine (scaleArea.getRight() - tick, p, scaleArea.getRight(), p, thick);
        }
        else
        {
            g.drawLine (p, scaleArea.getY(), p, scaleArea.getY() + tick, thick);
            if (n == 2)
                g.drawLine (p, scaleArea.getBottom() - tick, p, scaleArea.getBottom(), thick);
        }

        if (numbered)
        {
            const auto text = rel > 0.0f ? "+" + juce::String (juce::roundToInt (rel)) : juce::String (juce::roundToInt (rel));
            const auto box = vertical ? juce::Rectangle<float> (scaleArea.getWidth(), 14.0f * s).withCentre ({ scaleArea.getCentreX(), p })
                                      : juce::Rectangle<float> (32.0f * s, scaleArea.getHeight()).withCentre ({ p, scaleArea.getCentreY() });
            g.drawText (text, box, juce::Justification::centred);
        }
    }

    // 0 dBFS mark across the scale
    if (fullScale <= scaleMax)
    {
        const float p = posFor (fullScale);
        g.setColour (Theme::levelRed);
        g.setFont (Theme::font (juce::jmax (7.0f, 9.0f * s), true));
        const auto box = vertical ? juce::Rectangle<float> (scaleArea.getWidth(), 12.0f * s).withCentre ({ scaleArea.getCentreX(), p })
                                  : juce::Rectangle<float> (24.0f * s, 12.0f * s).withCentre ({ p, scaleArea.getBottom() - (n == 2 ? 14.0f : 4.0f) * s });
        g.drawText ("FS", box, juce::Justification::centred);
    }

    // --- Readouts per channel, with an OVER lamp
    auto fmt = [] (float v) { return v <= scaleMin - 20.0f ? juce::String ("--") : (v > 0.0f ? "+" : "") + juce::String (v, 1); };

    for (int ch = 0; ch < n; ++ch)
    {
        const auto i = static_cast<size_t> (ch);
        juce::Rectangle<float> box;
        if (vertical)
        {
            box = readouts.withWidth (readouts.getWidth() / static_cast<float> (n)).translated (readouts.getWidth() / static_cast<float> (n) * static_cast<float> (ch), 0.0f).reduced (3.0f, 0.0f);
        }
        else
        {
            // Line each readout up with its bar.
            const auto lane = lanes[i];
            const float h = juce::jmax (lane.getHeight() + scaleW * 0.5f, 84.0f * s);
            box = juce::Rectangle<float> (readouts.getX(), lane.getCentreY() - h * 0.5f, readouts.getWidth(), h)
                      .constrainedWithin (readouts);
        }

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (box, 4.0f * s);

        auto inside = box.reduced (8.0f * s, 4.0f * s);
        auto lampRow = inside.removeFromTop (inside.getHeight() * 0.25f);
        const bool lit = overFrames[i] > 0;
        auto lamp = lampRow.removeFromRight (juce::jmin (52.0f * s, lampRow.getWidth() * 0.5f)).reduced (0.0f, 2.0f * s);
        g.setColour (lit ? Theme::levelRed : juce::Colour (0xff3a1210));
        g.fillRoundedRectangle (lamp, 3.0f * s);
        g.setColour (lit ? juce::Colours::white : juce::Colour (0xff7a3a34));
        g.setFont (Theme::font (juce::jmax (7.0f, 9.5f * s), true));
        g.drawText ("OVER", lamp, juce::Justification::centred);
        g.setColour (Theme::arcAccent);
        g.setFont (Theme::font (juce::jmax (9.0f, 14.0f * s), true));
        g.drawText (labels[i], lampRow, juce::Justification::centredLeft);

        const float rowH = inside.getHeight() / 3.0f;
        auto row = [&] (const juce::String& name, const juce::String& value, juce::Colour colour)
        {
            auto r = inside.removeFromTop (rowH);
            g.setColour (Theme::textSecondary);
            g.setFont (Theme::font (juce::jmax (7.0f, 10.0f * s), true));
            g.drawText (name, r.removeFromLeft (r.getWidth() * 0.38f), juce::Justification::centredLeft);
            g.setColour (colour);
            g.setFont (Theme::mono (juce::jmax (9.0f, juce::jmin (rowH * 0.9f, 20.0f * s)), true));
            g.drawText (value, r, juce::Justification::centredRight);
        };

        const bool hasCrest = peak[i] > scaleMin - 20.0f && average[i] > scaleMin - 20.0f;
        row ("PEAK", fmt (hold[i]), peakColour);
        row ("AVG", fmt (average[i]), zoneColour (average[i]));
        row ("P-A", hasCrest ? juce::String (juce::jmax (0.0f, peak[i] - average[i]), 1) : juce::String ("--"), Theme::arcAccent);
    }

    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.setFont (Theme::font (juce::jmax (6.5f, 8.0f * s), true));
    g.drawText ("NEW CONSTELLATION", window.reduced (8.0f * s, 4.0f * s), juce::Justification::bottomLeft);
}
