#include <JuceHeader.h>
#include "FxSelfTest.h"
#include "MainComponent.h"
#include "Theme.h"

class LiveRigApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // One copy at a time, except the self-test, which can run beside it.
    bool moreThanOneInstanceAllowed() override          { return getCommandLineParameters().contains ("--fx-selftest"); }

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

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

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
