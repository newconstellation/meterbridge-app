#include "DisplayWindow.h"

#include "AppLayouts.h"
#include "Brand.h"
#include "ui/Theme.h"

namespace
{
    constexpr int toolbarHeight = 44;
    constexpr int margin = 8;
    constexpr juce::uint32 hideAfterMs = 3000;
}

DisplayContent::DisplayContent (MeterHub& h, const juce::ValueTree& state)
    : hub (h)
{
    setWantsKeyboardFocus (true);

    // Restore
    source = juce::jlimit (0, Sources::count - 1, (int) state.getProperty ("source", 0));
    overlayStyle = OverlayView::styleFromId (state.getProperty ("overlayStyle", "grey3d").toString());
    overlayMask = state.getProperty ("overlayShown", 0xf);
    overlayAveraging = state.getProperty ("overlayAveraging", 2);
    for (const auto& id : AppViews::ids())
        if (state.hasProperty ("slots_" + id))
            kindsByView[id] = juce::StringArray::fromTokens (state.getProperty ("slots_" + id).toString(), ",", {});

    styleCombo (viewBox);
    for (int i = 0; i < AppViews::ids().size(); ++i)
        viewBox.addItem (AppViews::name (AppViews::ids()[i]), i + 1);
    viewBox.onChange = [this] { setView (AppViews::ids()[viewBox.getSelectedItemIndex()]); };
    addAndMakeVisible (viewBox);

    styleCombo (sourceBox);
    refreshSourceNames();
    sourceBox.onChange = [this]
    {
        source = juce::jmax (0, sourceBox.getSelectedItemIndex());
        if (grid != nullptr)
            grid->resetDisplay();
        stateChanged();
    };
    addAndMakeVisible (sourceBox);

    styleCombo (deviceBox);
    deviceBox.setTooltip ("Input device. Every source reads from this one device.");
    deviceBox.onChange = [this]
    {
        if (deviceBox.getSelectedId() > 0)
            hub.getEngine().setInputDevice (deviceBox.getText());
    };
    addAndMakeVisible (deviceBox);

    resetButton.setTooltip ("Reset integrated loudness, peak holds and histories on every source");
    resetButton.onClick = [this]
    {
        hub.getEngine().resetAll();
        if (grid != nullptr) grid->resetDisplay();
        if (overlay != nullptr) overlay->resetDisplay();
    };
    fullScreenButton.setTooltip (juce::String::fromUTF8 ("Full screen on this display (\xe2\x8c\x83\xe2\x8c\x98" "F, Esc to leave)"));
    fullScreenButton.onClick = [this] { toggleWindowFullScreen(); };
    settingsButton.setTooltip ("Audio device, input routing, meter scales and launch options");
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };

    for (auto* b : { &resetButton, &fullScreenButton, &settingsButton })
    {
        b->setColour (juce::TextButton::buttonColourId, Theme::face);
        b->setColour (juce::TextButton::textColourOffId, Theme::textPrimary);
        addAndMakeVisible (*b);
    }
    settingsButton.setColour (juce::TextButton::textColourOffId, Theme::spectrumAccent);

    setView (state.getProperty ("view", AppViews::quad).toString());

    // Catch mouse movement over every child, for the idle auto-hide.
    addMouseListener (this, true);
    lastActivity = juce::Time::getMillisecondCounter();
    hub.addListener (this);
    startTimerHz (5);
}

DisplayContent::~DisplayContent()
{
    stopTimer();
    hub.removeListener (this);
    removeMouseListener (this);
    if (chromeHidden)
        juce::Desktop::getInstance().getMainMouseSource().revealCursor();
}

void DisplayContent::styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, Theme::face);
    box.setColour (juce::ComboBox::arrowColourId, Theme::textSecondary);
    box.setColour (juce::ComboBox::outlineColourId, Theme::panelEdge);
    box.setColour (juce::ComboBox::textColourId, Theme::textPrimary);
}

void DisplayContent::refreshSourceNames()
{
    sourceBox.clear (juce::dontSendNotification);
    for (int i = 0; i < Sources::count; ++i)
    {
        const auto config = hub.getEngine().getSource (i);
        sourceBox.addItem (Sources::letter (i) + juce::String::fromUTF8 (" \xc2\xb7 ")
                               + (config.enabled ? config.name : juce::String ("empty slot")),
                           i + 1);
    }
    sourceBox.setSelectedItemIndex (source, juce::dontSendNotification);

    auto& engine = hub.getEngine();
    const auto devices = engine.getInputDeviceNames();
    deviceBox.clear (juce::dontSendNotification);
    deviceBox.addItemList (devices, 1);
    deviceBox.setSelectedItemIndex (devices.indexOf (engine.getCurrentInputDeviceName()), juce::dontSendNotification);
    deviceBox.setTextWhenNothingSelected ("No input device");
    deviceBox.setTextWhenNoChoicesAvailable ("No input devices");
}

juce::ValueTree DisplayContent::getState() const
{
    juce::ValueTree state ("Window");
    state.setProperty ("view", viewId, nullptr);
    state.setProperty ("source", source, nullptr);
    state.setProperty ("overlayStyle", OverlayView::styleId (overlay != nullptr ? overlay->getStyle() : overlayStyle), nullptr);
    state.setProperty ("overlayShown", overlay != nullptr ? overlay->getShownMask() : overlayMask, nullptr);
    state.setProperty ("overlayAveraging", overlay != nullptr ? overlay->getAveragingIndex() : overlayAveraging, nullptr);

    auto kinds = kindsByView;
    if (grid != nullptr)
        kinds[viewId] = grid->getKindIds();
    for (const auto& [id, list] : kinds)
        state.setProperty ("slots_" + id, list.joinIntoString (","), nullptr);
    return state;
}

void DisplayContent::setView (const juce::String& newView)
{
    if (! AppViews::ids().contains (newView) || newView == viewId)
    {
        viewBox.setSelectedItemIndex (AppViews::ids().indexOf (viewId), juce::dontSendNotification);
        return;
    }

    if (grid != nullptr)
        kindsByView[viewId] = grid->getKindIds();
    if (overlay != nullptr)
    {
        overlayStyle = overlay->getStyle();
        overlayMask = overlay->getShownMask();
        overlayAveraging = overlay->getAveragingIndex();
    }
    grid.reset();
    overlay.reset();

    viewId = newView;
    viewBox.setSelectedItemIndex (AppViews::ids().indexOf (viewId), juce::dontSendNotification);

    if (viewId == AppViews::overlay)
    {
        overlay = std::make_unique<OverlayView>();
        overlay->setStyle (overlayStyle);
        overlay->setShownMask (overlayMask);
        overlay->setAveragingIndex (overlayAveraging);
        overlay->onSettingsChanged = [this] { stateChanged(); };
        addAndMakeVisible (*overlay);
    }
    else
    {
        grid = std::make_unique<GridView> (viewId, kindsByView[viewId]);
        grid->onLayoutChanged = [this] { stateChanged(); };
        addAndMakeVisible (*grid);
    }

    sourceBox.setVisible (overlay == nullptr && ! chromeHidden);
    resized();
    stateChanged();
}

void DisplayContent::stateChanged()
{
    if (onStateChanged)
        onStateChanged();
}

void DisplayContent::meterTick (MeterHub& h)
{
    if (grid != nullptr)
        grid->update (h, source);
    if (overlay != nullptr)
        overlay->update (h);
}

//==============================================================================
void DisplayContent::noteActivity()
{
    lastActivity = juce::Time::getMillisecondCounter();
    if (chromeHidden)
        setChromeHidden (false);
}

void DisplayContent::timerCallback()
{
    if (isWindowFullScreen() && ! chromeHidden
        && juce::Time::getMillisecondCounter() - lastActivity > hideAfterMs)
        setChromeHidden (true);
}

void DisplayContent::setChromeHidden (bool hidden)
{
    chromeHidden = hidden;
    for (auto* c : std::initializer_list<juce::Component*> { &viewBox, &deviceBox, &resetButton, &fullScreenButton, &settingsButton })
        c->setVisible (! hidden);
    sourceBox.setVisible (! hidden && overlay == nullptr);

    auto mouse = juce::Desktop::getInstance().getMainMouseSource();
    if (hidden) mouse.hideCursor();
    else        mouse.revealCursor();

    resized();
    repaint();
}

void DisplayContent::fullScreenChanged()
{
    fullScreenButton.setButtonText (isWindowFullScreen() ? "Exit full screen" : "Full screen");
    if (! isWindowFullScreen() && chromeHidden)
        setChromeHidden (false);
    lastActivity = juce::Time::getMillisecondCounter();
    grabKeyboardFocus();
    stateChanged();
}

bool DisplayContent::keyPressed (const juce::KeyPress& key)
{
    noteActivity();
    const auto mods = key.getModifiers();

    // Ctrl-Cmd-F, the standard Mac full-screen shortcut.
    if (mods.isCommandDown() && mods.isCtrlDown() && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) == 'F')
    {
        toggleWindowFullScreen();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && isWindowFullScreen())
    {
        toggleWindowFullScreen();
        return true;
    }

    return false;
}

//==============================================================================
void DisplayContent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    if (chromeHidden)
        return;

    auto header = getLocalBounds().removeFromTop (toolbarHeight).reduced (margin + 4, 0);

    Brand::drawLogo (g, header.removeFromLeft (230).toFloat().reduced (0.0f, 5.0f));

    if (deviceBox.isVisible())
    {
        g.setColour (Theme::textSecondary);
        g.setFont (Theme::font (10.5f, true));
        g.drawText ("INPUT", deviceBox.getBounds().withX (deviceBox.getX() - 44).withWidth (40), juce::Justification::centredRight);
    }

    g.setColour (Theme::panelEdge);
    g.drawHorizontalLine (toolbarHeight - 1, 0.0f, (float) getWidth());
}

void DisplayContent::resized()
{
    auto area = getLocalBounds();

    if (! chromeHidden)
    {
        auto bar = area.removeFromTop (toolbarHeight).reduced (margin + 4, 8);
        bar.removeFromLeft (250);

        settingsButton.setBounds (bar.removeFromRight (84));
        bar.removeFromRight (6);
        fullScreenButton.setBounds (bar.removeFromRight (124));
        bar.removeFromRight (6);
        resetButton.setBounds (bar.removeFromRight (64));
        bar.removeFromRight (12);
        deviceBox.setVisible (bar.getWidth() > 700);
        if (deviceBox.isVisible())
            deviceBox.setBounds (bar.removeFromRight (juce::jmin (300, bar.getWidth() - 440)).withTrimmedLeft (44));

        viewBox.setBounds (bar.removeFromLeft (190));
        bar.removeFromLeft (8);
        sourceBox.setBounds (bar.removeFromLeft (230));
    }

    area.reduce (margin, margin);
    if (grid != nullptr)
        grid->setBounds (area);
    if (overlay != nullptr)
        overlay->setBounds (area);
}

//==============================================================================
DisplayWindow::DisplayWindow (MeterHub& hub, const juce::ValueTree& state)
    : DocumentWindow ("Meter Bridge", Theme::background, DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    content = new DisplayContent (hub, state);
    content->isWindowFullScreen = [this] { return isFullScreen(); };
    content->toggleWindowFullScreen = [this] { toggleFullScreen(); };
    setContentOwned (content, false);
    setResizable (true, false);
    setResizeLimits (480, 240, 8192, 4320);

    const auto bounds = state.getProperty ("bounds").toString();
    if (bounds.isEmpty() || ! restoreWindowStateFromString (bounds))
        centreWithSize (1280, 480);
}

juce::ValueTree DisplayWindow::getState() const
{
    auto state = content->getState();
    state.setProperty ("bounds", const_cast<DisplayWindow*> (this)->getWindowStateAsString(), nullptr);
    state.setProperty ("fullScreen", const_cast<DisplayWindow*> (this)->isFullScreen(), nullptr);
    return state;
}

void DisplayWindow::toggleFullScreen()
{
    setFullScreen (! isFullScreen());
}

void DisplayWindow::closeButtonPressed()
{
    if (onCloseRequested)
        onCloseRequested (this);
}

void DisplayWindow::resized()
{
    DocumentWindow::resized();
    if (content != nullptr && isFullScreen() != lastFullScreen)
    {
        lastFullScreen = isFullScreen();
        content->fullScreenChanged();
    }
}
