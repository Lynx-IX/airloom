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
        Theme::onSwitched = [this] { if (mainWindow != nullptr) mainWindow->tintTitleBar(); };
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
            tintTitleBar();
        }

        // Windows draws the title bar; on Windows 11 it can take the theme's colours
        // (a shade darker than the window), and on 10 it at least goes dark.
        void tintTitleBar()
        {
            auto* hwnd = (HWND) getWindowHandle();
            if (hwnd == nullptr)
                return;

            using SetAttribute = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
            static auto* setAttribute = []() -> SetAttribute
            {
                if (auto* dwm = LoadLibraryW (L"dwmapi.dll"))
                    return (SetAttribute) GetProcAddress (dwm, "DwmSetWindowAttribute");
                return nullptr;
            }();
            if (setAttribute == nullptr)
                return;

            auto colorRef = [] (juce::Colour c) { return (DWORD) RGB (c.getRed(), c.getGreen(), c.getBlue()); };
            const auto caption = Theme::background.interpolatedWith (juce::Colours::black, 0.35f);
            const BOOL dark = TRUE;
            const DWORD captionColour = colorRef (caption), textColour = colorRef (Theme::text), borderColour = colorRef (Theme::panelEdge);

            setAttribute (hwnd, 20, &dark, sizeof (dark));                         // DWMWA_USE_IMMERSIVE_DARK_MODE
            setAttribute (hwnd, 35, &captionColour, sizeof (captionColour));       // DWMWA_CAPTION_COLOR
            setAttribute (hwnd, 36, &textColour, sizeof (textColour));             // DWMWA_TEXT_COLOR
            setAttribute (hwnd, 34, &borderColour, sizeof (borderColour));         // DWMWA_BORDER_COLOR
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
