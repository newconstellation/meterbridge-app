#include "OverlayView.h"

#include "ui/Theme.h"

#include <cmath>

namespace
{
    constexpr float minHz = 20.0f, maxHz = 20000.0f;
    constexpr float sidebarGap = 8.0f;

    float dbToPower (float db) noexcept { return std::pow (10.0f, db * 0.1f); }
}

float OverlayView::freqAt (float norm) noexcept { return minHz * std::pow (maxHz / minHz, norm); }
float OverlayView::normAt (float hz) noexcept { return std::log (hz / minHz) / std::log (maxHz / minHz); }

juce::String OverlayView::styleId (Style s)
{
    switch (s)
    {
        case Style::split:     return "split";
        case Style::quietBlue: return "quietBlue";
        case Style::grey3d:    return "grey3d";
    }
    return "split";
}

OverlayView::Style OverlayView::styleFromId (const juce::String& id)
{
    if (id == "quietBlue") return Style::quietBlue;
    if (id == "split")     return Style::split;
    return Style::grey3d;
}

OverlayView::OverlayView()
{
    prepare (48000.0);

    styleButton.setTooltip ("Background: Split, Quiet blue or 3D");
    styleButton.setColour (juce::TextButton::buttonColourId, Theme::face);
    styleButton.setColour (juce::TextButton::textColourOffId, Theme::spectrumAccent);
    styleButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem ("Split: traces above, sum waterfall below", true, style == Style::split, [this] { setStyle (Style::split); });
        menu.addItem ("Quiet blue: sum waterfall behind the traces", true, style == Style::quietBlue, [this] { setStyle (Style::quietBlue); });
        menu.addItem ("3D: sum as a grey 3D surface, heat colour on loud ridges", true, style == Style::grey3d, [this] { setStyle (Style::grey3d); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (styleButton));
    };
    addAndMakeVisible (styleButton);

    averagingButton.setTooltip ("Spectrum averaging: how quickly the traces and background follow the music");
    averagingButton.setColour (juce::TextButton::buttonColourId, Theme::face);
    averagingButton.setColour (juce::TextButton::textColourOffId, Theme::spectrumAccent);
    averagingButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Averaging (response time)");
        const char* labels[] { "Off (instant)", "Fast  0.1 s", "Medium  0.3 s", "Slow  1 s", "Very slow  3 s" };
        for (int i = 0; i < (int) averagingChoices.size(); ++i)
            menu.addItem (labels[i], true, i == averagingIndex, [this, i] { setAveragingIndex (i); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (averagingButton));
    };
    addAndMakeVisible (averagingButton);

    setStyle (Style::grey3d);
    setAveragingIndex (2);
}

void OverlayView::setAveragingIndex (int index)
{
    averagingIndex = juce::jlimit (0, (int) averagingChoices.size() - 1, index);
    const float seconds = averagingChoices[(size_t) averagingIndex];
    averagingButton.setButtonText ((seconds <= 0.0f ? juce::String ("Avg: off") : "Avg: " + juce::String (seconds, seconds < 1.0f ? 1 : 0) + " s")
                                   + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    applyAveraging();
    if (onSettingsChanged)
        onSettingsChanged();
}

/// Exponential averaging per analysis frame, set from the response time.
void OverlayView::applyAveraging()
{
    const float seconds = averagingChoices[(size_t) averagingIndex];
    const float frameSeconds = (float) hopSize / (float) sampleRate;
    const float amount = seconds <= 0.0f ? 0.0f : std::exp (-frameSeconds / seconds);
    for (auto& c : channels)
        for (auto& side : c.sides)
            side.analyzer.setAveraging (amount);
}

void OverlayView::setStyle (Style s)
{
    style = s;
    const char* labels[] { "Split", "Quiet blue", "3D" };
    styleButton.setButtonText (juce::String ("Background: ") + labels[(int) s] + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    rebuildWaterfallImage();
    repaint();
    if (onSettingsChanged)
        onSettingsChanged();
}

int OverlayView::getShownMask() const noexcept
{
    int mask = 0;
    for (int i = 0; i < Sources::count; ++i)
        if (shown[(size_t) i])
            mask |= 1 << i;
    return mask;
}

void OverlayView::setShownMask (int mask)
{
    for (int i = 0; i < Sources::count; ++i)
        shown[(size_t) i] = (mask >> i) & 1;
    repaint();
}

void OverlayView::prepare (double rate)
{
    sampleRate = rate;
    for (auto& c : channels)
    {
        for (auto& side : c.sides)
        {
            side.analyzer.prepare (fftOrder, sampleRate);
            side.ring.assign ((size_t) side.analyzer.getFftSize(), 0.0f);
            side.frame.assign (side.ring.size(), 0.0f);
            side.ringWrite = side.samplesSinceFrame = 0;
        }
        c.trace.fill (-150.0f);
        c.peak.fill (-150.0f);
    }
    applyAveraging();
}

void OverlayView::resetDisplay()
{
    for (auto& c : channels)
        for (auto& side : c.sides)
            side.analyzer.reset();
    std::fill (history.begin(), history.end(), -150.0f);
    std::fill (slices.begin(), slices.end(), -150.0f);
    pendingRows = 0;
    rebuildWaterfallImage();
    repaint();
}

bool OverlayView::pushSamples (Side& c, const float* samples, int n)
{
    bool fresh = false;
    const int size = (int) c.ring.size();
    for (int i = 0; i < n; ++i)
    {
        c.ring[(size_t) c.ringWrite] = samples[i];
        c.ringWrite = (c.ringWrite + 1) % size;

        if (++c.samplesSinceFrame >= hopSize)
        {
            c.samplesSinceFrame = 0;
            for (int k = 0; k < size; ++k)
                c.frame[(size_t) k] = c.ring[(size_t) ((c.ringWrite + k) % size)];
            c.analyzer.processFrame (c.frame.data());
            fresh = true;
        }
    }
    return fresh;
}

float OverlayView::sideLevel (const Side& c, float hzLo, float hzHi, bool peak) const noexcept
{
    const auto& data = peak ? c.analyzer.getPeakHoldDb() : c.analyzer.getMagnitudesDb();
    const int numBins = c.analyzer.getNumBins();
    const float binHz = (float) sampleRate / (float) c.analyzer.getFftSize();
    const float loBin = hzLo / binHz, hiBin = hzHi / binHz;

    // Narrower than a bin (the low end): interpolate between bins for a smooth line.
    if (hiBin - loBin < 1.0f)
    {
        const float pos = juce::jlimit (1.0f, (float) (numBins - 2), 0.5f * (loBin + hiBin));
        const int b = (int) pos;
        const float frac = pos - (float) b;
        return data[(size_t) b] + (data[(size_t) b + 1] - data[(size_t) b]) * frac;
    }

    const int lo = juce::jlimit (1, numBins - 1, (int) loBin);
    const int hi = juce::jlimit (lo, numBins - 1, (int) hiBin);

    float level = -150.0f;
    for (int b = lo; b <= hi; ++b)
        level = juce::jmax (level, data[(size_t) b]);
    return level;
}

float OverlayView::bandLevel (const Channel& c, float hzLo, float hzHi, bool peak) const noexcept
{
    const float left = sideLevel (c.sides[0], hzLo, hzHi, peak);
    if (! c.stereo)
        return left;
    const float right = sideLevel (c.sides[1], hzLo, hzHi, peak);
    const float power = 0.5f * (dbToPower (left) + dbToPower (right));
    return power > 0.0f ? 10.0f * std::log10 (power) : -150.0f;
}

bool OverlayView::isVisible (int source) const noexcept
{
    if (! enabled[(size_t) source])
        return false;
    if (solo >= 0)
        return solo == source;
    return shown[(size_t) source];
}

void OverlayView::update (MeterHub& hub)
{
    engine = &hub.getEngine();
    if (std::abs (engine->getSampleRate() - sampleRate) > 1.0)
        prepare (engine->getSampleRate());

    bool anyFresh = false;
    for (int s = 0; s < Sources::count; ++s)
    {
        const auto config = engine->getSource (s);
        enabled[(size_t) s] = config.enabled;
        names[(size_t) s] = config.displayName (s);
        colours[(size_t) s] = config.colour;

        const auto& state = hub.getState (s);
        integrated[(size_t) s] = state.integrated.load();
        momentary[(size_t) s] = state.momentary.load();

        auto& c = channels[(size_t) s];
        if (! config.enabled)
            continue;

        c.stereo = config.stereo;
        const auto& left = hub.getLeftSamples (s);
        const auto& right = hub.getRightSamples (s);
        bool fresh = pushSamples (c.sides[0], left.data(), (int) left.size());
        if (c.stereo)
            fresh = pushSamples (c.sides[1], right.data(), (int) right.size()) || fresh;
        if (! fresh)
            continue;

        anyFresh = true;

        // Trace: loudest bin in each log band, lightly smoothed so lines stay readable.
        std::array<float, tracePoints> raw {}, rawPeak {};
        for (int i = 0; i < tracePoints; ++i)
        {
            const float lo = freqAt (((float) i - 0.5f) / (float) (tracePoints - 1));
            const float hi = freqAt (((float) i + 0.5f) / (float) (tracePoints - 1));
            raw[(size_t) i] = bandLevel (c, lo, hi, false);
            rawPeak[(size_t) i] = bandLevel (c, lo, hi, true);
        }
        for (int i = 0; i < tracePoints; ++i)
        {
            float sum = 0.0f, sumPeak = 0.0f;
            int count = 0;
            for (int k = juce::jmax (0, i - 2); k <= juce::jmin (tracePoints - 1, i + 2); ++k, ++count)
            {
                sum += raw[(size_t) k];
                sumPeak += rawPeak[(size_t) k];
            }
            c.trace[(size_t) i] = sum / (float) count;
            c.peak[(size_t) i] = sumPeak / (float) count;
        }
    }

    if (anyFresh)
        addHistoryRow();

    repaint();
}

/// One new line of the sum of the shown sources (power sum per frequency).
void OverlayView::addHistoryRow()
{
    historyNewest = (historyNewest + historyRows - 1) % historyRows;
    float* row = history.data() + (size_t) historyNewest * historyCols;

    for (int x = 0; x < historyCols; ++x)
    {
        const float lo = freqAt (((float) x - 0.5f) / (float) (historyCols - 1));
        const float hi = freqAt (((float) x + 0.5f) / (float) (historyCols - 1));

        float power = 0.0f;
        for (int s = 0; s < Sources::count; ++s)
            if (isVisible (s))
                power += dbToPower (bandLevel (channels[(size_t) s], lo, hi, false));

        row[x] = power > 0.0f ? 10.0f * std::log10 (power) : -150.0f;
    }

    commitRowTo3d (row);
    rebuildWaterfallImage();
}

/// Gathers rows into the pending 3D slice (loudest per column). Every few rows
/// the slice is committed: it never changes again, it only moves back.
void OverlayView::commitRowTo3d (const float* row)
{
    if (pendingRows == 0)
        pendingSlice.fill (-150.0f);

    for (int x = 0; x < sliceCols; ++x)
        pendingSlice[(size_t) x] = juce::jmax (pendingSlice[(size_t) x], row[x * 2], row[x * 2 + 1]);

    if (++pendingRows < rowsPerSlice)
        return;

    pendingRows = 0;
    sliceNewest = (sliceNewest + slices3d - 1) % slices3d;
    std::copy (pendingSlice.begin(), pendingSlice.end(), slices.begin() + (std::ptrdiff_t) sliceNewest * sliceCols);
}

void OverlayView::rebuildWaterfallImage()
{
    if (style == Style::grey3d)
        return;

    const juce::Image::BitmapData pixels (waterfall, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < historyRows; ++y)
    {
        const float* row = history.data() + (size_t) ((historyNewest + y) % historyRows) * historyCols;
        for (int x = 0; x < historyCols; ++x)
        {
            const float db = row[x];
            juce::Colour colour;
            if (style == Style::split)
            {
                const float v = std::pow (juce::jlimit (0.0f, 1.0f, (db - (heatTopDb - heatRangeDb)) / heatRangeDb), 1.4f);
                colour = Theme::heat (0.92f * v);
            }
            else
            {
                const float v = std::pow (juce::jlimit (0.0f, 1.0f, (db - (heatTopDb - heatRangeDb)) / heatRangeDb), 1.6f);
                const auto blue = juce::Colour::fromFloatRGBA ((20.0f + 60.0f * v) / 255.0f, (60.0f + 120.0f * v) / 255.0f,
                                                               (90.0f + 150.0f * v) / 255.0f, 1.0f);
                colour = Theme::face.interpolatedWith (blue, 0.55f * v);
            }
            pixels.setPixelColour (x, y, colour);
        }
    }
}

//==============================================================================
void OverlayView::resized()
{
    auto bounds = getLocalBounds().toFloat();
    const float sideWidth = juce::jlimit (220.0f, 360.0f, bounds.getWidth() * 0.18f);
    sidePanel = bounds.removeFromRight (sideWidth);
    bounds.removeFromRight (sidebarGap);
    mainPanel = bounds;

    styleButton.setBounds (juce::Rectangle<float> (mainPanel.getRight() - 186.0f, mainPanel.getY() + 2.0f, 180.0f,
                                                   Theme::titleHeight - 4.0f).toNearestInt());
    averagingButton.setBounds (styleButton.getBounds().translated (-124, 0).withWidth (118));
}

juce::Rectangle<float> OverlayView::plotArea (juce::Rectangle<float> face, bool withHzLabels) const
{
    return face.withTrimmedLeft (34.0f).withTrimmedRight (10.0f).withTrimmedTop (8.0f)
               .withTrimmedBottom (withHzLabels ? 18.0f : 6.0f);
}

void OverlayView::drawAxes (juce::Graphics& g, juce::Rectangle<float> face, juce::Rectangle<float> plot, bool hzLabels) const
{
    g.setFont (Theme::font (9.5f));

    const float step = plot.getHeight() < 140.0f ? 24.0f : 12.0f;
    for (float db = topDb; db >= bottomDb; db -= step)
    {
        const float y = juce::jmap (db, topDb, bottomDb, plot.getY(), plot.getBottom());
        g.setColour (Theme::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (Theme::textSecondary);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (face.getX() + 2.0f, y - 7.0f, 28.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    const std::pair<float, const char*> marks[] { { 30.0f, "30" }, { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" },
                                                  { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" },
                                                  { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" } };
    for (const auto& [hz, label] : marks)
    {
        const float x = plot.getX() + normAt (hz) * plot.getWidth();
        g.setColour (Theme::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        if (hzLabels)
        {
            g.setColour (Theme::textSecondary);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 13.0f),
                        juce::Justification::centred);
        }
    }
}

void OverlayView::drawTraces (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    // A drawn last, so it sits on top.
    for (int s = Sources::count - 1; s >= 0; --s)
    {
        if (! isVisible (s))
            continue;

        const auto& c = channels[(size_t) s];
        juce::Path line, peaks;
        for (int i = 0; i < tracePoints; ++i)
        {
            const float x = plot.getX() + (float) i / (float) (tracePoints - 1) * plot.getWidth();
            const float y = juce::jmap (juce::jlimit (bottomDb, topDb, c.trace[(size_t) i]), topDb, bottomDb, plot.getY(), plot.getBottom());
            const float py = juce::jmap (juce::jlimit (bottomDb, topDb, c.peak[(size_t) i]), topDb, bottomDb, plot.getY(), plot.getBottom());
            if (i == 0) { line.startNewSubPath (x, y); peaks.startNewSubPath (x, py); }
            else        { line.lineTo (x, y); peaks.lineTo (x, py); }
        }

        const auto colour = colours[(size_t) s];

        // Peak hold: thin dashed line in the source colour.
        juce::Path dashed;
        const float dashes[] { 6.0f, 6.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, peaks, dashes, 2);
        g.setColour (colour.withAlpha (0.55f));
        g.fillPath (dashed);

        // Black outline keeps each trace readable where lines cross.
        g.setColour (juce::Colours::black);
        g.strokePath (line, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

/// Colour for a 3D ridge: grey when quiet, warming through the heat colours
/// as it gets louder, and fading with depth (t = 0 now, 1 oldest).
static juce::Colour ridgeColour (float level, float t) noexcept
{
    const float g = (30.0f + 190.0f * level) / 255.0f;
    const auto grey = juce::Colour::fromFloatRGBA (g, g, juce::jmin (1.0f, g + 0.025f), 1.0f);
    const float heat = 0.9f * std::pow (juce::jmax (0.0f, (level - 0.22f) / 0.78f), 0.8f);
    return grey.interpolatedWith (Theme::heat (0.42f + 0.56f * level), heat).withMultipliedBrightness (1.0f - 0.7f * t);
}

void OverlayView::drawGrey3d (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const float depthHeight = plot.getHeight() * 0.62f;
    const float amplitude = plot.getHeight() * 0.36f;

    // Back (oldest) to front (newest), so nearer ridges hide what is behind them.
    // Slices stack straight up with no perspective, so each frequency stays on
    // its own Hz mark at every depth.
    for (int k = slices3d - 1; k >= 0; --k)
    {
        const float t = (float) k / (float) (slices3d - 1);
        const float base = plot.getBottom() - 2.0f - t * depthHeight;
        const float height = amplitude * (1.0f - 0.15f * t);
        const float* row = this->slices.data() + (size_t) ((sliceNewest + k) % slices3d) * sliceCols;

        juce::Path ridge;
        juce::ColourGradient lit (juce::Colours::black, plot.getX(), base, juce::Colours::black, plot.getRight(), base, false);
        juce::ColourGradient face = lit;
        lit.clearColours();
        face.clearColours();

        for (int x = 0; x < sliceCols; ++x)
        {
            const float u = (float) x / (float) (sliceCols - 1);
            const float px = plot.getX() + u * plot.getWidth();
            const float level = std::pow (juce::jlimit (0.0f, 1.0f, (row[x] - (heatTopDb - 48.0f)) / 48.0f), 1.8f);
            const float py = base - level * height;
            if (x == 0) ridge.startNewSubPath (px, py);
            else        ridge.lineTo (px, py);

            if (x % 2 == 0)
            {
                const auto c = ridgeColour (level, t);
                // Quiet floor lines stay faint so the ridges carry the picture.
                lit.addColour (u, c.brighter (0.25f).withMultipliedAlpha (0.12f + 0.88f * juce::jmin (1.0f, level * 4.0f)));
                face.addColour (u, c.withMultipliedBrightness (0.45f));
            }
        }

        auto fill = ridge;
        fill.lineTo (plot.getRight(), base);
        fill.lineTo (plot.getX(), base);
        fill.closeSubPath();

        // Face: the ridge colour near the top, falling to black at the base.
        g.setGradientFill (face);
        g.fillPath (fill);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, base - height,
                                                 juce::Colour (0xff050607).withAlpha (0.92f), 0.0f, base, false));
        g.fillPath (fill);

        g.setGradientFill (lit);
        g.strokePath (ridge, juce::PathStrokeType (1.2f));
    }
}

void OverlayView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    int numShown = 0;
    for (int s = 0; s < Sources::count; ++s)
        numShown += isVisible (s) ? 1 : 0;

    const auto title = "Spectrum overlay  " + juce::String (numShown) + (numShown == 1 ? " input" : " inputs");
    auto content = Theme::drawPanel (g, mainPanel, title, Theme::spectrumAccent).reduced (6.0f);

    if (style == Style::split)
    {
        auto top = content.removeFromTop (content.getHeight() * 0.58f);
        content.removeFromTop (6.0f);

        Theme::drawFace (g, top);
        const auto plot = plotArea (top, true);
        drawAxes (g, top, plot, true);
        drawTraces (g, plot);

        Theme::drawFace (g, content);
        const auto wf = content.withTrimmedLeft (34.0f).withTrimmedRight (10.0f).reduced (0.0f, 4.0f);
        g.drawImage (waterfall, wf, juce::RectanglePlacement::stretchToFit);
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (9.5f));
        g.drawText ("now", juce::Rectangle<float> (content.getX() + 4.0f, wf.getY(), 28.0f, 12.0f), juce::Justification::left);
        g.drawText ("time", juce::Rectangle<float> (content.getX() + 4.0f, wf.getBottom() - 12.0f, 28.0f, 12.0f), juce::Justification::left);
        g.setColour (Theme::textPrimary.withAlpha (0.8f));
        g.drawText ("sum of shown inputs", wf.withHeight (14.0f).withTrimmedRight (8.0f), juce::Justification::right);
    }
    else
    {
        Theme::drawFace (g, content);
        const auto plot = plotArea (content, true);

        if (style == Style::grey3d)
        {
            g.saveState();
            g.reduceClipRegion (plot.toNearestInt());
            drawGrey3d (g, plot);
            g.restoreState();
        }
        else
        {
            g.drawImage (waterfall, plot, juce::RectanglePlacement::stretchToFit);
        }

        drawAxes (g, content, plot, true);
        drawTraces (g, plot);
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (9.5f));
        g.drawText ("background: sum of shown inputs over time", plot.withHeight (14.0f).withTrimmedRight (8.0f),
                    juce::Justification::right);
    }

    drawSidebar (g, sidePanel);
}

void OverlayView::drawSidebar (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto content = Theme::drawPanel (g, area, "Inputs", Theme::textSecondary).reduced (6.0f);

    for (auto& r : eyeRects) r = {};
    for (auto& r : soloRects) r = {};
    for (auto& r : nameRects) r = {};

    // All four slots are always listed; click a name to swap in another source.
    const float rowHeight = juce::jmin (88.0f, (content.getHeight() - 6.0f * (float) (Sources::count - 1)) / (float) Sources::count);

    for (int s = 0; s < Sources::count; ++s)
    {
        if (! enabled[(size_t) s])
        {
            auto row = content.removeFromTop (rowHeight);
            content.removeFromTop (6.0f);
            nameRects[(size_t) s] = row;
            g.setColour (Theme::face.withAlpha (0.6f));
            g.fillRoundedRectangle (row, 3.0f);
            g.setColour (Theme::panelEdge);
            g.drawRoundedRectangle (row.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (Theme::textSecondary);
            g.setFont (Theme::font (12.0f));
            g.drawText (Sources::letter (s) + juce::String::fromUTF8 ("   + Pick a source \xe2\x96\xbe"), row.reduced (12.0f, 0.0f),
                        juce::Justification::centredLeft);
            continue;
        }

        auto row = content.removeFromTop (rowHeight);
        content.removeFromTop (6.0f);

        const auto colour = colours[(size_t) s];
        const bool visible = isVisible (s);

        Theme::drawFace (g, row);
        g.setColour (visible ? colour : colour.withAlpha (0.3f));
        g.fillRect (row.withWidth (4.0f));

        auto inner = row.withTrimmedLeft (10.0f).reduced (4.0f, 6.0f);
        auto head = inner.removeFromTop (18.0f);

        auto badge = head.removeFromLeft (18.0f);
        g.setColour (visible ? colour : colour.withAlpha (0.35f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (juce::Colours::black);
        g.setFont (Theme::font (11.0f, true));
        g.drawText (Sources::letter (s), badge, juce::Justification::centred);

        auto soloBox = head.removeFromRight (18.0f);
        head.removeFromRight (6.0f);
        auto eyeBox = head.removeFromRight (18.0f);
        soloRects[(size_t) s] = soloBox;
        eyeRects[(size_t) s] = eyeBox;

        g.setColour (solo == s ? Theme::levelYellow : juce::Colour (0xff3a3f47));
        g.fillRoundedRectangle (soloBox, 3.0f);
        g.setColour (solo == s ? juce::Colours::black : Theme::textPrimary);
        g.drawText ("S", soloBox, juce::Justification::centred);

        g.setColour (shown[(size_t) s] ? Theme::textPrimary : Theme::textSecondary.withAlpha (0.5f));
        g.drawRoundedRectangle (eyeBox.reduced (0.5f), 3.0f, 1.0f);
        g.drawEllipse (eyeBox.withSizeKeepingCentre (12.0f, 7.0f), 1.2f);
        g.fillEllipse (eyeBox.withSizeKeepingCentre (4.0f, 4.0f));
        if (! shown[(size_t) s])
            g.drawLine (eyeBox.getX() + 3.0f, eyeBox.getBottom() - 3.0f, eyeBox.getRight() - 3.0f, eyeBox.getY() + 3.0f, 1.2f);

        nameRects[(size_t) s] = head;
        g.setColour (Theme::textPrimary);
        g.setFont (Theme::font (12.5f, true));
        g.drawText (names[(size_t) s] + juce::String::fromUTF8 (" \xe2\x96\xbe"), head.withTrimmedLeft (8.0f).withTrimmedRight (4.0f),
                    juce::Justification::centredLeft, true);

        if (inner.getHeight() < 30.0f)
            continue;

        auto readings = inner.removeFromBottom (30.0f);
        auto intArea = readings.removeFromLeft (juce::jmin (130.0f, readings.getWidth() * 0.45f));
        const float integ = integrated[(size_t) s];
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (9.5f, true));
        g.drawText ("INT", intArea.removeFromLeft (26.0f), juce::Justification::centredLeft);
        g.setColour (std::isfinite (integ) && integ > -18.0f ? Theme::levelRed : Theme::levelGreen);
        g.setFont (Theme::mono (19.0f, true));
        g.drawText (Theme::formatDb (integ), intArea, juce::Justification::centredLeft);

        const float mom = momentary[(size_t) s];
        auto bar = readings.reduced (4.0f, 0.0f).withTrimmedTop (16.0f).withHeight (8.0f);
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (9.0f));
        g.drawText ("M " + Theme::formatDb (mom), bar.translated (0.0f, -15.0f).withHeight (13.0f), juce::Justification::left);
        g.setColour (juce::Colour (0xff101418));
        g.fillRect (bar);
        const float frac = std::isfinite (mom) ? juce::jlimit (0.0f, 1.0f, (mom + 42.0f) / 42.0f) : 0.0f;
        g.setColour (colour);
        g.fillRect (bar.withWidth (bar.getWidth() * frac));
    }
}

void OverlayView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    for (int s = 0; s < Sources::count; ++s)
    {
        if (eyeRects[(size_t) s].contains (p))
        {
            shown[(size_t) s] = ! shown[(size_t) s];
            if (onSettingsChanged)
                onSettingsChanged();
            repaint();
            return;
        }
        if (soloRects[(size_t) s].contains (p))
        {
            solo = (solo == s) ? -1 : s;
            repaint();
            return;
        }
        if (nameRects[(size_t) s].contains (p))
        {
            showSlotMenu (s);
            return;
        }
    }
}

void OverlayView::showSlotMenu (int slot)
{
    if (engine == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Slot " + Sources::letter (slot));
    const auto current = engine->getSlotSourceId (slot);
    for (const auto& def : engine->getLibrary())
    {
        juce::PopupMenu::Item item (def.name + "   (" + (def.stereo ? "stereo " : "mono ") + def.channelText() + ")");
        item.setColour (def.colour);
        item.setTicked (def.id == current);
        const auto id = def.id;
        item.setAction ([this, slot, id] { engine->setSlot (slot, id); });
        menu.addItem (item);
    }
    menu.addSeparator();
    menu.addItem ("Empty this slot", current.isNotEmpty(), false, [this, slot] { engine->setSlot (slot, {}); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (nameRects[(size_t) slot].toNearestInt())));
}
