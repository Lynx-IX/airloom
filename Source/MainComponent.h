#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "FxPanel.h"
#include "InputChannel.h"
#include "InputsView.h"
#include "InstrumentChannel.h"
#include "LiveSet.h"
#include "LoopChannel.h"
#include "Looper.h"
#include "MidiMap.h"
#include "RowView.h"
#include "SamplePool.h"

class MainComponent : public juce::AudioAppComponent,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropContainer,
                      public juce::DragAndDropTarget,
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

    // Samples dragged from the pool, and channels dragged to a new place.
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

private:
    using Row = LiveSet::Row;
    using Slot = LiveSet::Slot;

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
    void exportPack();
    void writePack (const juce::File& target);
    void openPack (const juce::File& pack);
    void loadSet (const juce::XmlElement& xml, const juce::File& baseFolder);
    void saveSession();

    // Rows and channels
    void rebuildRows();
    void layoutRows();
    void updateRowViews();
    void addRow (std::function<void (int)> then = {});
    void showRowMenu (int row);
    void removeRow (int row);
    void addChannel (int row);
    void showChannelMenu (int row, int slot);
    void removeChannel (int row, int slot);
    void moveChannel (int row, int slot, int delta);
    void moveChannelTo (int slotId, int targetRow, int targetIndex);
    RowView* rowViewAt (juce::Point<int> pos);
    void clearDropMarkers();
    void assignToChannels (int row, int firstSlot, const juce::Array<juce::File>& files);
    void appendToRow (int row, const juce::Array<juce::File>& files);
    void setRowMuted (int row, bool muted);
    int freeEngine() const;
    LoopChannel& engineFor (const Row& r)    { return *engines[(size_t) r.engine]; }

    // Live inputs and effects
    void setupInputs();
    void applyInput (int input);
    void showInputMenu (int input);
    void loadInputPlugin (int input, const juce::PluginDescription& description, const juce::String& state);
    void removeInputPlugin (int input);
    void openInputPluginEditor (int input);
    juce::Array<juce::PluginDescription> effectTypes() const;
    void showFxPanel (juce::Component& anchor, const juce::String& title, const FxSettings& fx, juce::Colour colour,
                      std::function<void (const FxSettings&)> onChange);

    // Hosted instruments
    void updateSourceStrips();
    void setupInstrumentStrip (int index, SourceStrip& strip);
    void addInstrument();
    void removeInstrument (int index);
    void showInstrumentMenu (int index);
    void loadInstrumentPlugin (int index, const juce::PluginDescription& description, const juce::String& state);
    void openEditor (int index);
    void closeEditor (int index);
    void showPluginScanner();
    void applyInstrument (int index);
    void captureInstrumentStates();
    void updateFireLights (bool allOff = false);
    juce::Array<juce::PluginDescription> instrumentTypes() const;
    juce::String instrumentName (int index) const;
    juce::String instrumentDetail (int index) const;

    // Loop recording
    void recPressed();
    void dubPressed();
    void undoPressed();
    void stopOverdubbing();
    void setArmed (int input, bool armed);
    bool anyArmed() const;
    void collectFinishedTake();
    int findOrCreateLiveLoopsRow();
    void swapPlayingClip (Row& row, Slot& slot, LoopClipPtr clip);
    void persistLiveClip (Slot& slot);
    void updateLooperStatus();
    juce::File recordingsFolder();

    // Playing
    void playPressed (int row, int slot);
    void crossfadePressed (int row, int slot);
    void startLoop (int row, int slot, bool fadeOutOthers);
    void stopSlot (Row& row, Slot& slot);
    void stopAll (double seconds);

    // Samples
    void addToPool (const juce::Array<juce::File>& files);
    void showPoolMenu (const juce::Array<juce::File>& files);
    void dropFilesAt (const juce::Array<juce::File>& files, juce::Point<int> pos, bool fromOutside);
    void browseForFiles (const juce::String& title, bool multiple, std::function<void (juce::Array<juce::File>)> onChosen);

    // Clip loading: clips are read in the background and kept in memory. A
    // smoothed (seamless-loop) version is cached separately from the plain one.
    void withClip (const Slot& slot, std::function<void (LoopClipPtr)> then);
    void clipLoaded (const juce::String& key, LoopClipPtr clip, const juce::String& error);
    void preloadAll();
    void trimCache (const juce::String& keep);

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
    void setZoom (float scale);
    void setBpm (double bpm);
    void tapTempo();

    juce::ApplicationProperties settings;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (2) };

    LiveSet liveSet;
    std::array<std::unique_ptr<LoopChannel>, LiveSet::maxRows> engines;
    std::array<std::unique_ptr<InputChannel>, LiveSet::numInputs> inputEngines;

    juce::AudioPluginFormatManager pluginFormats;   // declared before the instruments, so it outlives them
    juce::KnownPluginList knownPlugins;
    std::array<std::unique_ptr<InstrumentChannel>, LiveSet::maxInstruments> instrumentEngines;
    std::array<std::unique_ptr<juce::DocumentWindow>, LiveSet::maxInstruments> editorWindows;
    std::array<std::unique_ptr<juce::DocumentWindow>, LiveSet::numInputs> inputPluginWindows;
    std::array<bool, LiveSet::maxInstruments> instrumentLoading {};
    int instrumentGeneration = 0;              // discards plugin loads meant for an earlier set
    std::atomic<int> currentBlockSize { 512 };
    juce::uint32 lastStateCapture = 0;
    std::unique_ptr<juce::MidiOutput> fireOutput;
    juce::AudioBuffer<float> inputCapture;   // audio thread: the interface inputs for this block
    juce::AudioBuffer<float> recordBus;      // audio thread: armed inputs for the looper
    std::atomic<int> deviceInputs { 0 };
    std::atomic<int> roundTripLatency { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };
    juce::int64 audioClock = 0;              // audio thread: samples processed since start

    Looper looper;
    int dubTargetId = -1;                    // the loop DUB records onto
    int overdubSlotId = -1;                  // the loop being overdubbed right now
    juce::int64 firstLoopLength = 0;         // for "Match first loop"
    bool restoring = false;              // suppresses autosave while a set is being loaded

    std::shared_ptr<std::atomic<double>> packProgress;   // set while a pack is being written or opened

    std::map<juce::String, LoopClipPtr> clipCache;
    std::map<juce::String, std::vector<std::function<void (LoopClipPtr)>>> pendingLoads;
    size_t cacheBytes = 0;

    std::atomic<float> masterTarget { 1.0f };
    juce::SmoothedValue<float> masterGain;
    std::atomic<float> masterPeakL { 0.0f }, masterPeakR { 0.0f };

    // UI
    SamplePool pool { formatManager };
    juce::Viewport rowsViewport;
    juce::Component rowsContent;
    InputsView inputsView;
    juce::OwnedArray<RowView> rowViews;
    juce::TextButton addRowButton { "+ ROW" };
    ChannelStrip masterStrip { "Master", false };
    RightClickable<juce::TextButton> stopAllButton { "STOP ALL" };
    juce::TextButton setButton { "Set" }, settingsButton { "Audio Settings" };
    juce::Slider fadeTimeSlider;
    juce::Label fadeTimeLabel { {}, "Fade time" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel { {}, "BPM" };
    RightClickable<juce::TextButton> tapButton { "TAP" };
    std::vector<double> tapTimes;   // recent TAP presses, in ms
    BeatIndicator beatIndicator;
    juce::int64 beatAnchor = 0;     // clock time of a beat 1 (set by TAP)

    // Beats since the reference point: REC while recording, otherwise the last TAP.
    double currentBeatPosition (bool& recording) const;
    juce::Rectangle<int> headerArea, messageArea;
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
