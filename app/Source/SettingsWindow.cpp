#include "SettingsWindow.h"

#include "ui/Theme.h"

namespace
{
    struct ScaleSpec
    {
        const char* paramId;
        const char* name;
        const juce::StringArray* items;
    };

    const ScaleSpec scaleSpecs[] {
        { ParamIds::loudnessTarget, "Loudness target", &Choices::loudnessTargets },
        { ParamIds::truePeakCeiling, "True peak ceiling", &Choices::truePeakCeilings },
        { ParamIds::vuReference, "0 VU =", &Choices::vuReferences },
        { ParamIds::ppmType, "PPM type", &Choices::ppmTypes },
        { ParamIds::kScale, "K-System", &Choices::kScales },
    };

    void styleCombo (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, Theme::face);
        box.setColour (juce::ComboBox::arrowColourId, Theme::textSecondary);
        box.setColour (juce::ComboBox::outlineColourId, Theme::panelEdge);
        box.setColour (juce::ComboBox::textColourId, Theme::textPrimary);
    }

    constexpr int pad = 14;
    constexpr int leftWidth = 440;
    constexpr int rowH = 28;
    constexpr int rowGap = 6;
}

void applyMeterScales (AudioEngine& engine, const juce::ValueTree& scales)
{
    for (const auto& spec : scaleSpecs)
    {
        if (! scales.hasProperty (spec.paramId))
            continue;
        const int index = scales.getProperty (spec.paramId);
        for (int s = 0; s < Sources::count; ++s)
            if (auto* p = engine.getProcessor (s).getParameters().getParameter (spec.paramId))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
    }
}

//==============================================================================
/// One editable source: colour, name, mono / stereo, channels, remove.
class SettingsContent::LibraryRow final : public juce::Component
{
public:
    LibraryRow (const SourceDef& d, const juce::StringArray& channelNames) : def (d)
    {
        colourButton.setTooltip ("Colour for this source's trace and badge");
        colourButton.onClick = [this] { showColourMenu(); };
        colourButton.setColour (juce::TextButton::buttonColourId, def.colour);
        addAndMakeVisible (colourButton);

        name.setText (def.name, false);
        name.setTextToShowWhenEmpty ("Name, e.g. Keys 1 Nord", Theme::textSecondary);
        name.setColour (juce::TextEditor::backgroundColourId, Theme::face);
        name.setColour (juce::TextEditor::outlineColourId, Theme::panelEdge);
        name.onTextChange = [this] { def.name = name.getText().trim(); changed(); };
        addAndMakeVisible (name);

        styleCombo (type);
        type.addItem ("Stereo", 1);
        type.addItem ("Mono", 2);
        type.setSelectedId (def.stereo ? 1 : 2, juce::dontSendNotification);
        type.onChange = [this] { def.stereo = type.getSelectedId() == 1; updateEnablement(); changed(); };
        addAndMakeVisible (type);

        for (auto* box : { &left, &right })
        {
            styleCombo (*box);
            for (int ch = 0; ch < channelNames.size(); ++ch)
                box->addItem (juce::String (ch + 1) + "  " + channelNames[ch], ch + 1);
            box->setTextWhenNoChoicesAvailable ("No inputs");
            addAndMakeVisible (*box);
        }
        left.setSelectedId (def.left + 1, juce::dontSendNotification);
        right.setSelectedId (def.right + 1, juce::dontSendNotification);
        left.onChange = [this] { if (left.getSelectedId() > 0) def.left = left.getSelectedId() - 1; changed(); };
        right.onChange = [this] { if (right.getSelectedId() > 0) def.right = right.getSelectedId() - 1; changed(); };

        removeButton.setTooltip ("Remove this source from the library");
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        addAndMakeVisible (removeButton);

        updateEnablement();
    }

    SourceDef def;
    std::function<void()> onChanged, onRemove;

    void resized() override
    {
        auto r = getLocalBounds();
        colourButton.setBounds (r.removeFromLeft (rowH));
        r.removeFromLeft (8);
        removeButton.setBounds (r.removeFromRight (70));
        r.removeFromRight (8);
        right.setBounds (r.removeFromRight (120));
        r.removeFromRight (6);
        left.setBounds (r.removeFromRight (120));
        r.removeFromRight (6);
        type.setBounds (r.removeFromRight (86));
        r.removeFromRight (8);
        name.setBounds (r);
    }

private:
    void changed()
    {
        if (onChanged)
            onChanged();
    }

    void updateEnablement()
    {
        right.setEnabled (def.stereo);
        right.setAlpha (def.stereo ? 1.0f : 0.35f);
    }

    void showColourMenu()
    {
        juce::PopupMenu menu;
        for (const auto& c : Sources::palette())
        {
            juce::PopupMenu::Item item (c.name);
            item.setColour (juce::Colour (c.argb));
            item.setTicked (def.colour == juce::Colour (c.argb));
            const auto colour = juce::Colour (c.argb);
            item.setAction ([this, colour]
            {
                def.colour = colour;
                colourButton.setColour (juce::TextButton::buttonColourId, colour);
                changed();
            });
            menu.addItem (item);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (colourButton));
    }

    juce::TextButton colourButton;
    juce::TextEditor name;
    juce::ComboBox type, left, right;
    juce::TextButton removeButton { "Remove" };
};

class SettingsContent::LibraryList final : public juce::Component
{
public:
    juce::OwnedArray<LibraryRow> rows;

    void resized() override
    {
        int y = 0;
        for (auto* row : rows)
        {
            row->setBounds (0, y, getWidth(), rowH);
            y += rowH + rowGap;
        }
    }

    int idealHeight() const { return juce::jmax (rowH, rows.size() * (rowH + rowGap)); }
};

//==============================================================================
SettingsContent::SettingsContent (AudioEngine& e, AppSettings& s)
    : engine (e), settings (s)
{
    for (auto* box : { &typeBox, &deviceBox, &rateBox, &bufferBox })
    {
        styleCombo (*box);
        addAndMakeVisible (*box);
    }
    deviceBox.setTooltip ("The audio device Meter Bridge reads, for example Dante Virtual Soundcard");

    typeBox.onChange = [this]
    {
        engine.getDeviceManager().setCurrentAudioDeviceType (typeBox.getText(), true);
        refreshChannels();
    };
    deviceBox.onChange = [this]
    {
        if (deviceBox.getSelectedId() > 0)
            deviceError = engine.setInputDevice (deviceBox.getText());
        refreshChannels();
    };
    rateBox.onChange = [this]
    {
        if (rateBox.getSelectedId() > 0)
            deviceError = engine.setSampleRate ((double) rateBox.getSelectedId());
        refreshDevice();
    };
    bufferBox.onChange = [this]
    {
        if (bufferBox.getSelectedId() > 0)
            deviceError = engine.setBufferSize (bufferBox.getSelectedId());
        refreshDevice();
    };

    libraryList = std::make_unique<LibraryList>();
    libraryViewport.setViewedComponent (libraryList.get(), false);
    libraryViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (libraryViewport);

    addSourceButton.setTooltip ("Add a source to the library, for example a keyboard on two DVS channels");
    addSourceButton.onClick = [this] { addSource(); };
    addAndMakeVisible (addSourceButton);

    for (int i = 0; i < Sources::count; ++i)
    {
        auto& box = slotBoxes[(size_t) i];
        styleCombo (box);
        box.onChange = [this, i]
        {
            const int index = slotBoxes[(size_t) i].getSelectedId() - 2;   // id 1 = empty
            const auto& lib = engine.getLibrary();
            engine.setSlot (i, index >= 0 && index < (int) lib.size() ? lib[(size_t) index].id : juce::String());
        };
        addAndMakeVisible (box);
    }

    const auto saved = settings.loadScales();
    for (size_t i = 0; i < scales.size(); ++i)
    {
        auto row = std::make_unique<ScaleRow>();
        row->paramId = scaleSpecs[i].paramId;
        row->label.setText (scaleSpecs[i].name, juce::dontSendNotification);
        row->label.setColour (juce::Label::textColourId, Theme::textSecondary);
        row->label.setJustificationType (juce::Justification::centredRight);
        row->box.addItemList (*scaleSpecs[i].items, 1);
        styleCombo (row->box);

        int index = 0;
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (engine.getProcessor (0).getParameters().getParameter (row->paramId)))
            index = p->getIndex();
        row->box.setSelectedItemIndex ((int) saved.getProperty (row->paramId, index), juce::dontSendNotification);
        row->box.onChange = [this] { scaleEdited(); };

        addAndMakeVisible (row->label);
        addAndMakeVisible (row->box);
        scales[i] = std::move (row);
    }

    fullScreenOnLaunch.setToggleState (settings.getFullScreenOnLaunch(), juce::dontSendNotification);
    fullScreenOnLaunch.onClick = [this] { settings.setFullScreenOnLaunch (fullScreenOnLaunch.getToggleState()); };
    addAndMakeVisible (fullScreenOnLaunch);

    newWindowButton.setTooltip ("Open another meter window, for example for a second display");
    newWindowButton.onClick = [this] { if (onNewWindow) onNewWindow(); };
    addAndMakeVisible (newWindowButton);

    refreshChannels();
    setSize (1240, 680);
}

SettingsContent::~SettingsContent()
{
    libraryViewport.setViewedComponent (nullptr, false);
}

void SettingsContent::rebuildLibraryRows()
{
    const auto names = engine.getInputChannelNames();
    libraryList->rows.clear();

    const auto& lib = engine.getLibrary();
    for (int i = 0; i < (int) lib.size(); ++i)
    {
        auto* row = libraryList->rows.add (new LibraryRow (lib[(size_t) i], names));
        row->onChanged = [this] { libraryEdited(); };
        row->onRemove = [this, i] { juce::MessageManager::callAsync ([this, i] { removeSource (i); }); };
        libraryList->addAndMakeVisible (row);
    }

    libraryList->setSize (juce::jmax (100, libraryViewport.getMaximumVisibleWidth()), libraryList->idealHeight());
    refreshSlots();
}

void SettingsContent::refreshChannels()
{
    refreshDevice();
    rebuildLibraryRows();
}

void SettingsContent::refreshDevice()
{
    auto& dm = engine.getDeviceManager();

    typeBox.clear (juce::dontSendNotification);
    int typeId = 1;
    for (auto* type : dm.getAvailableDeviceTypes())
    {
        typeBox.addItem (type->getTypeName(), typeId);
        if (type->getTypeName() == dm.getCurrentAudioDeviceType())
            typeBox.setSelectedId (typeId, juce::dontSendNotification);
        ++typeId;
    }
    typeBox.setVisible (typeBox.getNumItems() > 1);

    deviceBox.clear (juce::dontSendNotification);
    const auto names = engine.getInputDeviceNames();
    deviceBox.addItemList (names, 1);
    deviceBox.setSelectedItemIndex (names.indexOf (engine.getCurrentInputDeviceName()), juce::dontSendNotification);
    deviceBox.setTextWhenNothingSelected ("Choose an input device");
    deviceBox.setTextWhenNoChoicesAvailable ("No input devices found");

    rateBox.clear (juce::dontSendNotification);
    bufferBox.clear (juce::dontSendNotification);
    if (auto* device = dm.getCurrentAudioDevice())
    {
        for (double rate : device->getAvailableSampleRates())
            rateBox.addItem (juce::String (rate / 1000.0, juce::roundToInt (rate) % 1000 == 0 ? 0 : 1) + " kHz", juce::roundToInt (rate));
        rateBox.setSelectedId (juce::roundToInt (device->getCurrentSampleRate()), juce::dontSendNotification);

        for (int size : device->getAvailableBufferSizes())
            bufferBox.addItem (juce::String (size) + " samples", size);
        bufferBox.setSelectedId (device->getCurrentBufferSizeSamples(), juce::dontSendNotification);

        deviceStatus = juce::String (device->getActiveInputChannels().countNumberOfSetBits()) + " input channels open at "
                       + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz";
    }
    else
    {
        deviceStatus = "No device open";
    }
    if (deviceError.isNotEmpty())
        deviceStatus = deviceError;
    repaint();
}

void SettingsContent::refreshSlots()
{
    const auto& lib = engine.getLibrary();
    for (int i = 0; i < Sources::count; ++i)
    {
        auto& box = slotBoxes[(size_t) i];
        box.clear (juce::dontSendNotification);
        box.addItem ("(empty)", 1);
        int selected = 1;
        for (int k = 0; k < (int) lib.size(); ++k)
        {
            box.addItem (lib[(size_t) k].name.isNotEmpty() ? lib[(size_t) k].name : "Source " + juce::String (k + 1), k + 2);
            if (lib[(size_t) k].id == engine.getSlotSourceId (i))
                selected = k + 2;
        }
        box.setSelectedId (selected, juce::dontSendNotification);
    }
}

void SettingsContent::libraryEdited()
{
    std::vector<SourceDef> lib;
    for (auto* row : libraryList->rows)
        lib.push_back (row->def);
    engine.setLibrary (std::move (lib));
}

void SettingsContent::addSource()
{
    auto lib = engine.getLibrary();
    int nextChannel = 0;
    for (const auto& d : lib)
        nextChannel = juce::jmax (nextChannel, d.rightChannel() + 1);

    const int numInputs = juce::jmax (2, engine.getInputChannelNames().size());
    const int left = nextChannel + 1 < numInputs ? nextChannel : 0;
    lib.push_back ({ juce::Uuid().toString(), "Source " + juce::String ((int) lib.size() + 1),
                     Sources::paletteColour ((int) lib.size()), true, left, left + 1 });
    engine.setLibrary (std::move (lib));
    rebuildLibraryRows();
    libraryViewport.setViewPosition (0, libraryList->getHeight());
}

void SettingsContent::removeSource (int index)
{
    auto lib = engine.getLibrary();
    if (index < 0 || index >= (int) lib.size())
        return;

    const auto id = lib[(size_t) index].id;
    for (int s = 0; s < Sources::count; ++s)
        if (engine.getSlotSourceId (s) == id)
            engine.setSlot (s, {});

    lib.erase (lib.begin() + index);
    engine.setLibrary (std::move (lib));
    rebuildLibraryRows();
}

void SettingsContent::scaleEdited()
{
    juce::ValueTree tree ("Scales");
    for (const auto& row : scales)
        tree.setProperty (row->paramId, row->box.getSelectedItemIndex(), nullptr);
    settings.saveScales (tree);
    applyMeterScales (engine, tree);
}

//==============================================================================
void SettingsContent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    Theme::drawPanel (g, devicePanel.toFloat(), "Audio device", Theme::spectrumAccent);
    {
        g.setFont (Theme::font (12.0f, true));
        auto label = [&g] (const juce::Component& c, const juce::String& text)
        {
            if (! c.isVisible())
                return;
            g.setColour (Theme::textSecondary);
            g.drawText (text, c.getBounds().withX (c.getX() - 130).withWidth (122), juce::Justification::centredRight);
        };
        label (typeBox, "Driver");
        label (deviceBox, "Input device");
        label (rateBox, "Sample rate");
        label (bufferBox, "Buffer size");

        g.setFont (Theme::font (11.0f));
        g.setColour (Theme::textSecondary);
        g.drawFittedText (deviceStatus + "\nPick Dante Virtual Soundcard to meter Dante channels. All sources use this one device.",
                          devicePanel.reduced (12, 10).removeFromBottom (44), juce::Justification::bottomLeft, 3);
    }
    Theme::drawPanel (g, metersPanel.toFloat(), "Meters and launch", Theme::vuAccent);
    Theme::drawPanel (g, libraryPanel.toFloat(), "Source library", Theme::loudnessAccent);
    Theme::drawPanel (g, slotsPanel.toFloat(), "In view: slots A to D", Theme::phaseAccent);

    // Library column headings
    auto heads = libraryPanel.withTrimmedTop ((int) Theme::titleHeight + 6).withHeight (16).reduced (12, 0);
    heads.removeFromLeft (rowH + 8);
    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (10.5f, true));
    heads.removeFromRight (78);
    auto rightHead = heads.removeFromRight (120);
    heads.removeFromRight (6);
    auto leftHead = heads.removeFromRight (120);
    heads.removeFromRight (6);
    auto typeHead = heads.removeFromRight (86);
    heads.removeFromRight (8);
    g.drawText ("NAME", heads, juce::Justification::centredLeft);
    g.drawText ("TYPE", typeHead, juce::Justification::centredLeft);
    g.drawText ("LEFT / MONO", leftHead, juce::Justification::centredLeft);
    g.drawText ("RIGHT", rightHead, juce::Justification::centredLeft);

    // Slot letters
    for (int i = 0; i < Sources::count; ++i)
    {
        const auto box = slotBoxes[(size_t) i].getBounds();
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (Sources::letter (i), box.withX (box.getX() - 18).withWidth (14), juce::Justification::centredRight);
    }

    g.setColour (Theme::textSecondary);
    g.setFont (Theme::font (10.5f));
    g.drawText ("A stereo source uses one slot. Its trace averages L and R power, so out-of-phase parts never cancel.",
                slotsPanel.reduced (12, 6).removeFromBottom (16), juce::Justification::centredLeft);
}

void SettingsContent::resized()
{
    auto area = getLocalBounds().reduced (pad);
    auto left = area.removeFromLeft (leftWidth);
    area.removeFromLeft (pad);

    devicePanel = left.removeFromTop (360);
    left.removeFromTop (pad);
    metersPanel = left;

    slotsPanel = area.removeFromBottom ((int) Theme::titleHeight + 74);
    area.removeFromBottom (pad);
    libraryPanel = area;

    {
        auto r = devicePanel.withTrimmedTop ((int) Theme::titleHeight + 10).reduced (12, 0);
        auto place = [&r] (juce::Component& c, int height)
        {
            auto row = r.removeFromTop (height);
            r.removeFromTop (rowGap + 18);
            c.setBounds (row.withTrimmedLeft (130));
        };
        if (typeBox.isVisible())
            place (typeBox, rowH);
        place (deviceBox, rowH + 6);
        place (rateBox, rowH);
        place (bufferBox, rowH);
    }

    auto meters = metersPanel.withTrimmedTop ((int) Theme::titleHeight + 8).reduced (12, 0);
    for (auto& row : scales)
    {
        auto r = meters.removeFromTop (rowH);
        meters.removeFromTop (rowGap);
        row->label.setBounds (r.removeFromLeft (130));
        row->box.setBounds (r.removeFromLeft (190));
    }
    meters.removeFromTop (4);
    fullScreenOnLaunch.setBounds (meters.removeFromTop (rowH));
    meters.removeFromTop (rowGap);
    newWindowButton.setBounds (meters.removeFromTop (rowH).withWidth (170));

    auto lib = libraryPanel.withTrimmedTop ((int) Theme::titleHeight + 26).reduced (12, 0).withTrimmedBottom (10);
    addSourceButton.setBounds (lib.removeFromBottom (rowH).withWidth (140));
    lib.removeFromBottom (8);
    libraryViewport.setBounds (lib);
    libraryList->setSize (libraryViewport.getMaximumVisibleWidth(), libraryList->idealHeight());

    auto slots = slotsPanel.withTrimmedTop ((int) Theme::titleHeight + 10).reduced (12, 0).removeFromTop (rowH);
    const int w = (slots.getWidth() - 3 * 12) / Sources::count;
    for (auto& box : slotBoxes)
    {
        auto r = slots.removeFromLeft (w);
        slots.removeFromLeft (12);
        box.setBounds (r.withTrimmedLeft (20));
    }
}

SettingsWindow::SettingsWindow (AudioEngine& engine, AppSettings& settings)
    : DocumentWindow ("Meter Bridge Settings", Theme::background, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    content = new SettingsContent (engine, settings);
    setContentOwned (content, true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
}
