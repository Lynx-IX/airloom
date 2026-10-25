#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "LoopChannel.h"
#include "LoopLibrary.h"
#include "MidiMap.h"

class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer,
                      private juce::ChangeListener,
                      private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    static constexpr int numLoopChannels = 4;
    enum Channel { pad1, pad2, nature, tones };

    // What a channel is playing and what is cued to replace it on GO.
    struct ChannelState
    {
        juce::File current;
        std::optional<juce::File> cued;   // an empty File cues silence
        LoopClipPtr cuedClip;             // loaded in the background once cued
        bool loading = false;
        int loadToken = 0;                // discards loads that were superseded
    };

    // Audio device
    void openAudioDevice();
    void showDeviceSettings();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Library, cueing and transport
    void showLibraryMenu();
    void chooseLibrary();
    void setLibrary (const juce::File& root, bool restoreSession);
    void rebuildKeyButtons();
    void cueKey (int index);
    void stepCuedKey (int direction);
    void cueFile (int channel, const juce::File& file);
    void clearCue (int channel);
    void clipLoaded (int channel, int token, LoopClipPtr clip, const juce::String& error);
    void showFileMenu (int channel);
    void go();
    void executeGo();
    void toggleTransport();
    void fadeOutAll (double seconds);
    void setMuted (int channel, bool muted);
    bool anyCued() const;
    bool anyLoading() const;
    void refreshUi();
    void saveSession();

    // MIDI
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m) override;
    void enableMidiInputs();
    void handleMidi (const juce::String& device, const juce::MidiMessage& m);
    void applyFaderMidi (const juce::String& target, ChannelStrip& strip, float proportion);
    void showLearnMenu (const juce::String& target, const juce::String& label);
    void updateMidiLeds (bool allOff = false);
    void saveMidiMap();

    void timerCallback() override;

    juce::ApplicationProperties settings;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (2) };

    LoopLibrary library;
    std::array<std::unique_ptr<LoopChannel>, numLoopChannels> channels;
    std::array<ChannelState, numLoopChannels> state;
    int activeKey = -1, cuedKey = -1;
    bool transportPlaying = false;
    bool goPending = false;               // GO pressed while cued loops were still loading

    std::atomic<float> masterTarget { 1.0f };
    juce::SmoothedValue<float> masterGain;

    // UI
    std::array<std::unique_ptr<ChannelStrip>, numLoopChannels> strips;
    ChannelStrip masterStrip { "Master", false };
    juce::OwnedArray<juce::TextButton> keyButtons;
    RightClickable<juce::TextButton> goButton { "GO" }, transportButton { "START" },
                                     prevKeyButton { "< PREV" }, nextKeyButton { "NEXT >" };
    juce::TextButton libraryButton { "Library" }, settingsButton { "Audio Settings" };
    juce::Slider fadeTimeSlider;
    juce::Label fadeTimeLabel { {}, "Crossfade" };
    juce::Rectangle<int> headerArea, keyDisplayArea, messageArea;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String deviceStatus, midiStatus, errorMessage;
    bool deviceOpen = false;

    // MIDI
    MidiMap midiMap;
    juce::String learnTarget, learnLabel;

    struct Takeover { bool locked = false; float last = -1.0f; };
    std::map<juce::String, Takeover> takeover;

    struct PendingMidi { juce::String device; juce::MidiMessage message; };
    std::array<PendingMidi, 512> midiQueue;
    juce::AbstractFifo midiFifo { 512 };
    juce::SpinLock midiWriteLock;         // MIDI callbacks may come from several threads

    juce::MidiDeviceListConnection midiDevicesChanged;
    std::unique_ptr<juce::MidiOutput> ledOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
