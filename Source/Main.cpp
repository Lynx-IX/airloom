#include <JuceHeader.h>
#include "FxSelfTest.h"
#include "PresetProbe.h"
#include "MainComponent.h"
#include "Theme.h"
#include "CrashLog.h"   // last: it brings in windows.h
#include "TitleBar.h"

class LiveRigApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // One copy at a time, except the self-test, which can run beside it.
    bool moreThanOneInstanceAllowed() override          { return getCommandLineParameters().contains ("--fx-selftest")
                                                                      || getCommandLineParameters().contains ("--preset-probe")
                                                                      || getCommandLineParameters().contains ("--crash-test"); }

    void initialise (const juce::String& commandLine) override
    {
        // Offline effects checks for development; writes a report and quits.
        if (commandLine.contains ("--fx-selftest"))
        {
            const juce::File report (commandLine.fromFirstOccurrenceOf ("--fx-selftest", false, false).trim().unquoted());
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // so its pictures look like the app
            report.replaceWithText (FxSelfTest::run());
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            quit();
            return;
        }

        // What installed instruments report about their presets; writes a report and quits.
        if (commandLine.contains ("--preset-probe"))
        {
            const juce::File report (commandLine.fromFirstOccurrenceOf ("--preset-probe", false, false).trim().unquoted());
            report.replaceWithText (PresetProbe::run());
            quit();
            return;
        }

        // Development: checks the crash log by crashing (writes a dump and a log line, no running marker).
        if (commandLine.contains ("--crash-test"))
        {
            juce::SystemStats::setApplicationCrashHandler (CrashLog::onCrash);
            CrashLog::writeLine ("crash test starting");
            volatile int* nowhere = nullptr;
            *nowhere = 1;
        }

        CrashLog::start();
        MainComponent::startupWarning = CrashLog::previousRunProblem();
        running = true;

        // The theme is picked before any window is built, so everything starts in it.
        {
            juce::PropertiesFile props (MainComponent::settingsOptions());
            Theme::applyPalette (Theme::paletteIndex (props.getValue ("theme")));
            lookAndFeel.refreshColours();
        }
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " - Audio Performance Rig");
        Theme::onSwitched = [] { TitleBar::tintAll(); };
        openFile (commandLine);
    }

    // A set double-clicked while Airloom is already open arrives here (one copy at a time).
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            return;
        mainWindow->setMinimised (false);
        mainWindow->toFront (true);
        openFile (commandLine);
    }

    // Once the last session has come back, so the set opened replaces it.
    void openFile (const juce::String& commandLine)
    {
        if (commandLine.trim().isEmpty())
            return;
        juce::MessageManager::callAsync ([this, commandLine]
        {
            if (mainWindow != nullptr)
                if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    main->openFromCommandLine (commandLine);
        });
    }

    void shutdown() override
    {
        Theme::onSwitched = nullptr;
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        if (running)
            CrashLog::stop();   // a proper quit: no "closed unexpectedly" next time
    }

    bool running = false;   // the app proper (not a self-test) is running

    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, Theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* main = new MainComponent();
            setContentOwned (main, true);
            setResizable (true, true);
            setResizeLimits (900, 560, 10000, 10000);

            // Where it was last time (size and place; maximised once it's showing - maximising a window
            // that isn't shown yet can leave it minimised); the first time, large in the middle.
            const auto state = main->savedWindowState().trim();
            const bool maximised = state.startsWithIgnoreCase ("fs");
            if (! restoreWindowStateFromString (maximised ? state.substring (2).trim() : state))
            {
                if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                    centreWithSize (juce::jmin (1440, display->userArea.getWidth() * 9 / 10),
                                    juce::jmin (960, display->userArea.getHeight() * 9 / 10));
                else
                    centreWithSize (getWidth(), getHeight());
            }

            setVisible (true);
            tintTitleBar();
            if (maximised)
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainWindow> (this)]
                {
                    if (auto* w = safe.getComponent())
                    {
                        w->setMinimised (false);
                        w->setFullScreen (true);
                    }
                });
        }

        ~MainWindow() override
        {
            if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
                main->rememberWindowState (getWindowStateAsString());
        }

        void tintTitleBar()   { TitleBar::tint (*this); }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    Theme::LookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LiveRigApplication)
