#include <JuceHeader.h>
#include "FxSelfTest.h"
#include "PresetProbe.h"
#include "MainComponent.h"
#include "Theme.h"
#include "CrashLog.h"   // last: it brings in windows.h

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
            report.replaceWithText (FxSelfTest::run());
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

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " - Audio Performance Rig");
    }

    void shutdown() override
    {
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
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (900, 560, 10000, 10000);

            // Open large, but as a normal window that can be resized or maximised.
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                centreWithSize (juce::jmin (1440, display->userArea.getWidth() * 9 / 10),
                                juce::jmin (960, display->userArea.getHeight() * 9 / 10));
            else
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
        }

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
