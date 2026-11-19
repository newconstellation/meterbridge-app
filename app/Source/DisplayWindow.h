#pragma once

#include "GridView.h"
#include "OverlayView.h"

#include <map>
#include <memory>


/// What a display window shows: a slim toolbar (view, source, reset, full
/// screen, settings) above a grid of Prism meters or the spectrum overlay.
/// In full screen the toolbar and the cursor hide after a few idle seconds
/// and come back when the mouse moves.
class DisplayContent final : public juce::Component,
                             private MeterHub::Listener,
                             private juce::Timer
{
public:
    DisplayContent (MeterHub&, const juce::ValueTree& state);

    /// Set by the hosting window.
    std::function<bool()> isWindowFullScreen = [] { return false; };
    std::function<void()> toggleWindowFullScreen = [] {};
    ~DisplayContent() override;

    juce::ValueTree getState() const;
    void refreshSourceNames();

    std::function<void()> onStateChanged;
    std::function<void()> onOpenSettings;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override { noteActivity(); }
    void mouseDrag (const juce::MouseEvent&) override { noteActivity(); }
    void mouseDown (const juce::MouseEvent&) override { noteActivity(); }

    void fullScreenChanged();

private:
    void meterTick (MeterHub&) override;
    void timerCallback() override;

    void setView (const juce::String& viewId);
    void noteActivity();
    void setChromeHidden (bool hidden);
    void stateChanged();
    void styleCombo (juce::ComboBox&);

    MeterHub& hub;

    juce::String viewId;
    int source = 0;
    std::map<juce::String, juce::StringArray> kindsByView;
    OverlayView::Style overlayStyle = OverlayView::Style::grey3d;
    int overlayMask = 0xf;

    juce::ComboBox viewBox, sourceBox;
    juce::TextButton resetButton { "Reset" }, fullScreenButton { "Full screen" }, settingsButton { "Settings" };

    std::unique_ptr<GridView> grid;
    std::unique_ptr<OverlayView> overlay;

    juce::uint32 lastActivity = 0;
    bool chromeHidden = false;
};

/// One window per display. Uses the native title bar so the green button and
/// macOS full screen behave as usual; each window goes full screen on its own
/// display.
class DisplayWindow final : public juce::DocumentWindow
{
public:
    DisplayWindow (MeterHub&, const juce::ValueTree& state);

    juce::ValueTree getState() const;
    DisplayContent& getContent() noexcept { return *content; }

    void toggleFullScreen();

    std::function<void (DisplayWindow*)> onCloseRequested;

    void closeButtonPressed() override;
    void resized() override;

private:
    DisplayContent* content = nullptr;
    bool lastFullScreen = false;
};
