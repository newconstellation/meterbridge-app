#include "AppLayouts.h"
#include "AppSettings.h"
#include "AudioEngine.h"
#include "DisplayWindow.h"
#include "MeterHub.h"
#include "SettingsWindow.h"
#include "ui/Theme.h"

#include <juce_gui_extra/juce_gui_extra.h>

/// Dark look for the stock JUCE controls, in Prism's colours.
class MeterBridgeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MeterBridgeLookAndFeel()
        : LookAndFeel_V4 ({ Theme::background, Theme::panel, Theme::face, Theme::panelEdge, Theme::textPrimary,
                            Theme::panel, Theme::textPrimary, Theme::spectrumAccent.darker (0.4f), Theme::textPrimary })
    {
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::spectrumAccent.withAlpha (0.35f));
        setColour (juce::TooltipWindow::backgroundColourId, Theme::panel);
    }
};

class MeterBridgeApplication final : public juce::JUCEApplication,
                                     private juce::MenuBarModel
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        settings = std::make_unique<AppSettings>();
        engine = std::make_unique<AudioEngine> (*settings);
        applyMeterScales (*engine, settings->loadScales());
        hub = std::make_unique<MeterHub> (*engine);

        engine->onSourcesChanged = [this] { refreshSourceNames(); };

        engine->onDeviceChanged = [this]
        {
            if (settingsWindow != nullptr)
                settingsWindow->getContent().refreshChannels();
            refreshSourceNames();
        };

        restoreWindows();

#if JUCE_MAC
        juce::PopupMenu appleExtras;
        appleExtras.addItem (juce::String::fromUTF8 ("Settings\xe2\x80\xa6"), [this] { showSettings(); });
        juce::MenuBarModel::setMacMainMenu (this, &appleExtras);
#endif

        if (settings->getFullScreenOnLaunch())
            juce::Timer::callAfterDelay (600, [this]
            {
                for (auto* w : windows)
                    if (! w->isFullScreen())
                        w->setFullScreen (true);
            });
    }

    void shutdown() override
    {
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
        saveWindows();
        settingsWindow.reset();
        windows.clear();
        hub.reset();
        engine.reset();
        settings->flush();
        settings.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    //==========================================================================
    void restoreWindows()
    {
        const auto saved = settings->loadWindows();
        for (int i = 0; i < saved.getNumChildren(); ++i)
            addWindow (saved.getChild (i));

        if (! windows.isEmpty())
            return;

        // First launch: one window per display, each with a different view.
        const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
        const juce::String views[] { AppViews::quad, AppViews::panel, AppViews::overlay };
        for (int d = 0; d < juce::jmin (3, displays.size()); ++d)
        {
            juce::ValueTree state ("Window");
            state.setProperty ("view", views[d], nullptr);
            auto* w = addWindow (state);
            const auto area = displays.getReference (d).userArea;
            const auto preset = AppViews::grid (views[d]);
            w->setBounds (area.withSizeKeepingCentre (juce::jmin (preset.width, area.getWidth()),
                                                      juce::jmin (preset.height, area.getHeight())));
        }
        saveWindows();
    }

    DisplayWindow* addWindow (const juce::ValueTree& state)
    {
        auto* w = windows.add (new DisplayWindow (*hub, state));
        w->getContent().onStateChanged = [this] { saveWindows(); };
        w->getContent().onOpenSettings = [this] { showSettings(); };
        w->onCloseRequested = [this] (DisplayWindow* closing) { closeWindow (closing); };
        w->setVisible (true);
        return w;
    }

    void closeWindow (DisplayWindow* w)
    {
        if (windows.size() <= 1)
        {
            systemRequestedQuit();
            return;
        }
        windows.removeObject (w);
        saveWindows();
    }

    void saveWindows()
    {
        if (settings == nullptr)
            return;
        juce::ValueTree tree ("Windows");
        for (auto* w : windows)
            tree.appendChild (w->getState(), nullptr);
        settings->saveWindows (tree);
    }

    void refreshSourceNames()
    {
        for (auto* w : windows)
            w->getContent().refreshSourceNames();
    }

    void showSettings()
    {
        if (settingsWindow == nullptr)
        {
            settingsWindow = std::make_unique<SettingsWindow> (*engine, *settings);
            settingsWindow->getContent().onNewWindow = [this]
            {
                juce::ValueTree state ("Window");
                state.setProperty ("view", AppViews::quad, nullptr);
                addWindow (state);
                saveWindows();
            };
        }
        settingsWindow->setVisible (true);
        settingsWindow->toFront (true);
    }

    DisplayWindow* activeWindow()
    {
        for (auto* w : windows)
            if (w->isActiveWindow())
                return w;
        return windows.getFirst();
    }

    //==========================================================================
    juce::StringArray getMenuBarNames() override { return { "View", "Window" }; }

    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu menu;
        if (index == 0)
        {
            juce::PopupMenu::Item item (juce::String::fromUTF8 ("Toggle Full Screen    \xe2\x8c\x83\xe2\x8c\x98" "F"));
            item.setAction ([this] { if (auto* w = activeWindow()) w->toggleFullScreen(); });
            menu.addItem (item);
        }
        else
        {
            menu.addItem ("New Display Window", [this]
            {
                juce::ValueTree state ("Window");
                state.setProperty ("view", AppViews::quad, nullptr);
                addWindow (state);
                saveWindows();
            });
            menu.addItem (juce::String::fromUTF8 ("Settings\xe2\x80\xa6"), [this] { showSettings(); });
        }
        return menu;
    }

    void menuItemSelected (int, int) override {}

    MeterBridgeLookAndFeel lookAndFeel;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MeterHub> hub;
    juce::OwnedArray<DisplayWindow> windows;
    std::unique_ptr<SettingsWindow> settingsWindow;
};

START_JUCE_APPLICATION (MeterBridgeApplication)
