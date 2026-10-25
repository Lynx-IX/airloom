#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "LiveSet.h"
#include "LoopChannel.h"
#include "MidiMap.h"

class MainComponent : public juce::AudioAppComponent,
                      public juce::FileDragAndDropTarget,
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

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

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

    // Sets
    void showSetMenu();
    void newSet();
    void openSet();
    void saveSet (bool asNewFolder);
    void saveSetTo (const juce::File& folder, bool moveFiles);
    std::unique_ptr<juce::XmlElement> createSetXml (const juce::File& baseFolder) const;
    void loadSetXml (const juce::XmlElement& xml, const juce::File& baseFolder);
    void saveSession();

    // Keys
    void addKey (std::function<void (int)> then = {});
    void showKeyMenu (int index);
    void deleteKey (int index);
    void browseForPad (int keyIndex, int pad);
    void assignPad (int keyIndex, int pad, const juce::File& file);
    void rebuildKeyButtons();

    // Loading and cueing
    void browseForFiles (const juce::String& title, bool multiple, std::function<void (juce::Array<juce::File>)> onChosen);
    void loadIntoChannel (int channel, const juce::Array<juce::File>& files);
    void addToBin (int channel, const juce::Array<juce::File>& files);
    void showFileMenu (int channel);
    void cueKey (int index);
    void stepCuedKey (int direction);
    void cueFile (int channel, const juce::File& file);
    void clearCue (int channel);
    void clipLoaded (int channel, int token, LoopClipPtr clip, const juce::String& error);
    int editingKey() const;

    // Transport
    void go();
    void executeGo();
    void toggleTransport();
    void fadeOutAll (double seconds);
    void setMuted (int channel, bool muted);
    bool anyCued() const;
    bool anyLoading() const;
    void refreshUi();

    // Dialogs
    void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);
    void askChoice (const juce::String& title, const juce::String& message, const juce::StringArray& buttons,
                    std::function<void (int)> onChosen);

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

    LiveSet liveSet;
    std::array<std::unique_ptr<LoopChannel>, numLoopChannels> channels;
    std::array<ChannelState, numLoopChannels> state;
    int activeKey = -1, cuedKey = -1;
    bool transportPlaying = false;
    bool goPending = false;               // GO pressed while cued loops were still loading
    bool restoring = false;               // suppresses autosave while a set is being loaded

    std::atomic<float> masterTarget { 1.0f };
    juce::SmoothedValue<float> masterGain;

    // UI
    std::array<std::unique_ptr<ChannelStrip>, numLoopChannels> strips;
    ChannelStrip masterStrip { "Master", false };
    juce::OwnedArray<RightClickable<juce::TextButton>> keyButtons;
    juce::TextButton addKeyButton { "+ KEY" };
    RightClickable<juce::TextButton> goButton { "GO" }, transportButton { "START" },
                                     prevKeyButton { "< PREV" }, nextKeyButton { "NEXT >" };
    juce::TextButton setButton { "Set" }, settingsButton { "Audio Settings" };
    juce::Slider fadeTimeSlider;
    juce::Label fadeTimeLabel { {}, "Crossfade" };
    juce::Rectangle<int> headerArea, keyDisplayArea, messageArea;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String deviceStatus, midiStatus, errorMessage, infoMessage;
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
