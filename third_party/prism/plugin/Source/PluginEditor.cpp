#include "PluginEditor.h"

namespace
{
    const juce::Identifier layoutTag { "Layout" };
    const juce::Identifier presetProp { "preset" };
    const juce::Identifier slotsProp { "slots" };

    constexpr int headerHeight = 44;
    constexpr int gap = 10;
}

PrismMeterEditor::PrismMeterEditor (PrismMeterProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // Display preset menu
    styleCombo (displayBox);
    {
        int id = 1;
        for (const auto& preset : DisplayPresets::all())
            displayBox.addItem (preset.name, id++);
    }
    displayBox.setTooltip ("Display preset: a layout sized for a particular screen");
    displayBox.onChange = [this]
    {
        const int index = displayBox.getSelectedItemIndex();
        if (index >= 0 && DisplayPresets::all()[static_cast<size_t> (index)].id != currentPreset->id)
            applyPreset (DisplayPresets::all()[static_cast<size_t> (index)].id);
    };
    addAndMakeVisible (displayBox);

    setUpCombo (targetCombo, "Target", Choices::loudnessTargets, ParamIds::loudnessTarget);
    setUpCombo (ceilingCombo, "Ceiling", Choices::truePeakCeilings, ParamIds::truePeakCeiling);
    setUpCombo (vuCombo, "0 VU =", Choices::vuReferences, ParamIds::vuReference);
    setUpCombo (ppmCombo, "PPM", Choices::ppmTypes, ParamIds::ppmType);
    setUpCombo (kCombo, "K", Choices::kScales, ParamIds::kScale);

    resetButton.setTooltip ("Reset integrated loudness, LRA, peak holds and histories");
    resetButton.setColour (juce::TextButton::buttonColourId, Theme::face);
    resetButton.setColour (juce::TextButton::textColourOffId, Theme::loudnessAccent);
    resetButton.onClick = [this]
    {
        processor.requestReset();
        for (auto& slot : slots)
            if (auto* m = slot->getModule())
                m->resetDisplay();
    };
    addAndMakeVisible (resetButton);

    tiltButton.setToggleState (true, juce::dontSendNotification);
    tiltButton.setTooltip ("Tilt the spectrum 4.5 dB/oct so a balanced mix reads flat");
    tiltButton.setColour (juce::ToggleButton::tickColourId, Theme::spectrumAccent);
    tiltButton.setColour (juce::ToggleButton::textColourId, Theme::textSecondary);
    addAndMakeVisible (tiltButton);

    setResizable (true, true);
    setResizeLimits (640, 400, 3840, 2160);

    loadLayoutFromState();
    startTimerHz (30);
}

PrismMeterEditor::~PrismMeterEditor()
{
    stopTimer();
}

void PrismMeterEditor::styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, Theme::face);
    box.setColour (juce::ComboBox::arrowColourId, Theme::textSecondary);
    box.setColour (juce::ComboBox::outlineColourId, Theme::panelEdge);
    box.setColour (juce::ComboBox::textColourId, Theme::textPrimary);
}

void PrismMeterEditor::setUpCombo (LabelledCombo& c, const juce::String& name, const juce::StringArray& items, const char* paramId)
{
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredRight);
    c.label.setColour (juce::Label::textColourId, Theme::textSecondary);
    c.box.addItemList (items, 1);
    c.box.setTooltip (name);
    styleCombo (c.box);
    addAndMakeVisible (c.label);
    addAndMakeVisible (c.box);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(), paramId, c.box);
}

//==============================================================================
void PrismMeterEditor::applyPreset (const juce::String& presetId)
{
    currentPreset = &DisplayPresets::byId (presetId);
    displayBox.setSelectedItemIndex (DisplayPresets::indexOf (currentPreset->id), juce::dontSendNotification);

    std::vector<MeterKind> kinds;
    for (const auto& s : currentPreset->slots)
        kinds.push_back (s.kind);

    buildSlots (kinds);
    saveLayoutToState();

    if (getWidth() == currentPreset->width && getHeight() == currentPreset->height)
        resized();
    else
        setSize (currentPreset->width, currentPreset->height);
}

void PrismMeterEditor::buildSlots (const std::vector<MeterKind>& kinds)
{
    slots.clear();

    for (size_t i = 0; i < currentPreset->slots.size(); ++i)
    {
        auto slot = std::make_unique<MeterSlot>();
        slot->setKind (i < kinds.size() ? kinds[i] : currentPreset->slots[i].kind);
        slot->onKindChosen = [this] (MeterKind) { saveLayoutToState(); };
        addAndMakeVisible (*slot);
        slots.push_back (std::move (slot));
    }
}

void PrismMeterEditor::saveLayoutToState()
{
    juce::StringArray ids;
    for (const auto& slot : slots)
        ids.add (MeterKinds::toId (slot->getKind()));

    auto layout = processor.getParameters().state.getOrCreateChildWithName (layoutTag, nullptr);
    layout.setProperty (presetProp, currentPreset->id, nullptr);
    layout.setProperty (slotsProp, ids.joinIntoString (","), nullptr);
}

void PrismMeterEditor::loadLayoutFromState()
{
    const auto layout = processor.getParameters().state.getChildWithName (layoutTag);

    if (! layout.isValid())
    {
        applyPreset (DisplayPresets::all().front().id);
        return;
    }

    currentPreset = &DisplayPresets::byId (layout.getProperty (presetProp).toString());
    displayBox.setSelectedItemIndex (DisplayPresets::indexOf (currentPreset->id), juce::dontSendNotification);

    // Saved slot choices override the preset defaults, slot by slot.
    const auto ids = juce::StringArray::fromTokens (layout.getProperty (slotsProp).toString(), ",", {});
    std::vector<MeterKind> kinds;
    for (size_t i = 0; i < currentPreset->slots.size(); ++i)
        kinds.push_back (static_cast<int> (i) < ids.size()
                             ? MeterKinds::fromId (ids[static_cast<int> (i)], currentPreset->slots[i].kind)
                             : currentPreset->slots[i].kind);

    buildSlots (kinds);
    setSize (currentPreset->width, currentPreset->height);
}

//==============================================================================
void PrismMeterEditor::timerCallback()
{
    // Drain the analyser FIFOs once per tick and share the samples with every slot.
    spectrumCollected.clear();
    for (;;)
    {
        const int n = processor.readSpectrumSamples (spectrumScratch.data(), static_cast<int> (spectrumScratch.size()));
        if (n <= 0)
            break;
        spectrumCollected.insert (spectrumCollected.end(), spectrumScratch.begin(), spectrumScratch.begin() + n);
    }

    int numScope = 0;
    for (;;)
    {
        const int got = processor.readScopeSamples (scopeLeft.data(), scopeRight.data(), static_cast<int> (scopeLeft.size()));
        if (got <= 0)
            break;
        numScope = got; // keep only the newest chunk
    }

    ModuleContext context { processor.getMeterState(), processor.getParameters() };
    context.spectrumSamples = spectrumCollected.data();
    context.numSpectrumSamples = static_cast<int> (spectrumCollected.size());
    context.scopeLeft = scopeLeft.data();
    context.scopeRight = scopeRight.data();
    context.numScopeSamples = numScope;
    context.sampleRate = processor.getCurrentSampleRate();
    context.historyTick = (++tickCount % 3) == 0;
    context.spectrumTilt = tiltButton.getToggleState();
    context.shortTermHistory = &processor.getShortTermHistory();

    for (auto& slot : slots)
        if (auto* m = slot->getModule())
            m->update (context);
}

//==============================================================================
void PrismMeterEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (14, 0);

    // Brand line above the product wordmark.
    auto brand = header.removeFromTop (15).withTrimmedTop (4);
    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (9.0f, true).withExtraKerningFactor (0.18f));
    g.drawText ("NEW CONSTELLATION", brand, juce::Justification::centredLeft);
    header = header.withTrimmedBottom (3);

    // Wordmark: each letter of PRISM in one of the tool colours.
    const juce::Colour letters[] = { Theme::loudnessAccent, Theme::spectrumAccent, Theme::phaseAccent, Theme::vuAccent, Theme::ppmAccent };
    const juce::String word ("PRISM");
    g.setFont (Theme::font (19.0f, true));
    for (int i = 0; i < word.length(); ++i)
    {
        g.setColour (letters[i]);
        const auto letter = word.substring (i, i + 1);
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), letter)) + 1;
        g.drawText (letter, header.removeFromLeft (w), juce::Justification::centredLeft);
    }
    header.removeFromLeft (6);
    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (17.0f));
    g.drawText ("meter", header.removeFromLeft (70), juce::Justification::centredLeft);

    g.setColour (Theme::panelEdge);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, static_cast<float> (getWidth()));
}

void PrismMeterEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (14, 8);
    header.removeFromLeft (150);

    // Controls shrink on narrow displays: labels go first, then the less used menus.
    const int width = header.getWidth();
    const bool showLabels = width >= 1150;
    const bool showScaleMenus = width >= 760;

    resetButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (6);
    tiltButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (6);

    displayBox.setBounds (header.removeFromLeft (juce::jlimit (180, 300, width / 5)));
    header.removeFromLeft (10);

    auto placeCombo = [&] (LabelledCombo& c, int labelWidth, int boxWidth, bool visible)
    {
        c.label.setVisible (visible && showLabels);
        c.box.setVisible (visible);
        if (! visible)
            return;
        if (showLabels)
            c.label.setBounds (header.removeFromLeft (labelWidth));
        c.box.setBounds (header.removeFromLeft (boxWidth));
        header.removeFromLeft (6);
    };

    placeCombo (targetCombo, 50, showLabels ? 170 : 140, true);
    placeCombo (ceilingCombo, 56, 96, true);
    placeCombo (vuCombo, 50, 94, showScaleMenus);
    placeCombo (ppmCombo, 38, 80, showScaleMenus);
    placeCombo (kCombo, 22, 70, showScaleMenus);

    layoutSlots (area.reduced (gap));
}

void PrismMeterEditor::layoutSlots (juce::Rectangle<int> area)
{
    const auto& preset = *currentPreset;

    auto edges = [] (const std::vector<float>& weights, int start, int length)
    {
        float total = 0.0f;
        for (float w : weights)
            total += w;

        const int usable = length - gap * (static_cast<int> (weights.size()) - 1);
        std::vector<int> positions { start };
        float running = 0.0f;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            running += weights[i];
            positions.push_back (start + juce::roundToInt (running / total * static_cast<float> (usable))
                                 + gap * static_cast<int> (i + 1));
        }
        return positions; // cell i spans [positions[i], positions[i + 1] - gap]
    };

    const auto xs = edges (preset.columnWeights, area.getX(), area.getWidth());
    const auto ys = edges (preset.rowWeights, area.getY(), area.getHeight());

    for (size_t i = 0; i < slots.size() && i < preset.slots.size(); ++i)
    {
        const auto& spec = preset.slots[i];
        const auto c0 = static_cast<size_t> (spec.col);
        const auto r0 = static_cast<size_t> (spec.row);
        const auto c1 = static_cast<size_t> (spec.col + spec.colSpan);
        const auto r1 = static_cast<size_t> (spec.row + spec.rowSpan);

        slots[i]->setBounds (juce::Rectangle<int>::leftTopRightBottom (xs[c0], ys[r0], xs[c1] - gap, ys[r1] - gap));
    }
}
