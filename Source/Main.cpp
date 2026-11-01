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

            // Where it was last time (size, place, maximised); the first time, large in the middle.
            if (! restoreWindowStateFromString (main->savedWindowState()))
            {
                if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                    centreWithSize (juce::jmin (1440, display->userArea.getWidth() * 9 / 10),
                                    juce::jmin (960, display->userArea.getHeight() * 9 / 10));
                else
                    centreWithSize (getWidth(), getHeight());
            }

            setVisible (true);
            if (main->savedWindowState().startsWith ("fs"))
                setFullScreen (true);   // maximised again, now it's on screen
            tintTitleBar();
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
