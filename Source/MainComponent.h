#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "FxPanel.h"
#include "InputChannel.h"
#include "BeatIndicator.h"
#include "InputsView.h"
#include "LiveGroupBus.h"
#include "LiveGroupView.h"
#include "LiveDock.h"
#include "SampleEditor.h"
#include "Help.h"
#include "SettingsPanel.h"
#include "LooperBar.h"
#include "InstrumentChannel.h"
#include "LiveSet.h"
#include "LoopChannel.h"
#include "Looper.h"
#include "MidiMap.h"
#include "RowView.h"
#include "SceneBar.h"
#include "StageView.h"
#include "PresetName.h"
#include "FireVisualizer.h"
#include "SamplePool.h"
#include "SetRecorder.h"
#include "StageStatus.h"

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

    // A set or pack double-clicked in Explorer (or passed on the command line): opens it, asking
    // about unsaved changes first. Anything else is ignored.
    void openFromCommandLine (const juce::String& commandLine);

    // Shown when the window opens: e.g. that the last run ended unexpectedly (see CrashLog).
    static inline juce::String startupWarning;

    // Where this computer's settings are kept (also read at startup for the theme).
    static juce::PropertiesFile::Options settingsOptions();

    // The main window's size, place and maximised state, kept between runs.
    juce::String savedWindowState()                       { return settings.getUserSettings()->getValue ("windowState"); }
    void rememberWindowState (const juce::String& state)  { settings.getUserSettings()->setValue ("windowState", state); }

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

    // The Settings window (Audio, Look, Layout); tab 0 is Audio.
    void showSettings (int tab = 0);
    void styleDialog (juce::AlertWindow& w);
    void showHelp();
    void showWelcome();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Watching the interface: if it's unplugged (or stops answering) Airloom closes the audio
    // rather than falling back to the computer's own speakers and mic, shows a red banner, and
    // reopens it with the same settings as soon as it's back.
    void watchAudioDevice();
    bool deviceAvailable (const juce::String& type, const juce::String& name);
    juce::String lastDeviceName, lastDeviceType;   // the interface that was last working
    juce::AudioDeviceManager::AudioDeviceSetup lastSetup;
    juce::String lostDeviceName;                   // set while waiting for it to come back
    juce::uint32 lastRecoveryTry = 0;
    std::atomic<juce::uint32> lastCallbackMs { 0 };   // when the audio thread last ran

    // CPU and dropouts in the header.
    PerformanceMeter perfMeter;
    int dropoutBase = 0, lastDropouts = 0;
    juce::uint32 lastDropoutMs = 0, startedMs = 0;
    void updatePerfMeter();
    AlertBanner alertBanner;
    CountInOverlay countInOverlay;
    void updateCountIn();

    // MUTE OUT: an instant, hard silence on the outputs (not a fade); everything keeps running.
    RightClickable<juce::TextButton> muteButton { "MUTE OUT" };
    std::atomic<bool> outputMuted { false };
    juce::SmoothedValue<float> muteGain;   // audio thread: 5 ms, just enough not to click
    void toggleMute();
    void updateMuteButton();

    // Stage view: the set drawn big for performing (STAGE in the header, F11, or a MIDI button).
    StageView stageView;
    RightClickable<juce::TextButton> stageButton { "STAGE" };
    bool stageMode = false;
    juce::Rectangle<int> stageArea;
    void setStageMode (bool on);
    void updateStageView();

    // Scenes: saved mixes, recalled with a morph (see LiveSet::Scene).
    SceneBar sceneBar;
    int activeScene = -1;                  // the one last recalled (index), or -1
    // A level moving to the scene's: a channel's volume, or a group's, input's or instrument's
    // fader and mute (what's heard glides from fromGain to toGain; 0 = muted).
    struct SceneGlide
    {
        enum class Kind { channel, group, liveGroup, input, instrument } kind;
        juce::String uid;        // channel or group
        int index;               // group row, input or instrument
        double fromDb, toDb;     // the fader
        float fromGain, toGain;
        bool endMuted;
    };
    std::vector<SceneGlide> sceneGlides;

    // A scene's effects, morphing from what's on now to what the scene keeps.
    struct SceneFxGlide
    {
        enum class Kind { channel, group, liveGroup, input, instrument } kind;
        juce::String uid;   // channel, group or live group
        int index;          // input or instrument
        FxSettings from, to;
    };
    std::vector<SceneFxGlide> sceneFxGlides;
    void applyFxGlide (const SceneFxGlide& g, const FxSettings& fx, bool done);
    double morphStartMs = 0.0, morphMs = 0.0;
    LiveSet::Scene captureScene (const juce::String& name) const;
    void addScene();
    void recallScene (int index);
    void stepScene (int delta);
    void updateMorph();
    void showSceneMenu (int index);
    void showSceneBarMenu();
    void updateSceneBar();

    // Sets
    void showSetMenu();
    void newSet();
    void openSet();
    void openSetFile (const juce::File& file);

    // Keeping work safe: a saved set's file is rewritten a moment after any change (with dated
    // backups beside it); leaving an untitled set that has something in it asks first.
    // Undo: each edit (not performance moves) keeps the set as it was before; undo and redo put
    // it back softly - what's playing keeps playing, faders and mutes stay, plug-ins stay loaded.
    struct UndoState { juce::String xml, edit; };
    std::vector<UndoState> undoStack, redoStack;
    UndoState committed;                 // the set as of the last save
    juce::uint32 lastUndoPushMs = 0;
    int quietSaves = 0;                  // > 0: saves that are performance (scene recalls), never undo steps
    bool applyingUndo = false;
    bool undoCheckPending = false, undoCheckQuiet = false;   // a save since the last check (quiet: performance only)
    juce::uint32 lastUndoCheckMs = 0;
    void recordUndoPoint();   // compares the set with the last check; an edit keeps the state before it
    static constexpr int undoLimit = 60;
    static constexpr juce::uint32 undoMergeMs = 700;   // changes this close together are one step (a knob being dragged)
    void undo();
    void redo();
    void restoreEdit (const juce::String& xmlText);
    void writeSetFile();                                  // now, if there are unsaved changes
    void backUpSetFile();
    bool setHasContent() const;
    void confirmLeavingSet (std::function<void()> then);  // runs 'then' once it's safe to replace the set
    void renameSet();
    void addRecentSet (const juce::File& file);
    juce::StringArray recentSets();
    bool setFileDirty = false;
    juce::uint32 lastChangeMs = 0, lastBackupMs = 0;
    static constexpr juce::uint32 autosaveDelayMs = 1500, backupEveryMs = 10 * 60 * 1000;
    static constexpr int backupsKept = 20;
    void saveSet (bool asNewFolder);
    void saveSetTo (const juce::File& folder, bool moveFiles);
    void exportPack();
    void writePack (const juce::File& target);
    void openPack (const juce::File& pack);
    void loadSet (const juce::XmlElement& xml, const juce::File& baseFolder);
    void saveSession();

    // Rows and channels
    void rebuildRows();

    // Live groups: inputs and instruments in groups with their own fader, mute and effects.
    void rebuildLiveGroups();
    void updateLiveGroupViews();
    void applyLiveRouting();
    int liveGroupIndex (const juce::String& uid) const;
    int liveGroupSize (int g) const;
    void showLiveGroupMenu (int g);
    void showAddGroupMenu();
    void addLiveGroup (bool instruments);
    void removeLiveGroup (int g);
    void setLiveGroupMuted (int g, bool muted);
    void moveStripToGroup (bool instrument, int i, const juce::String& uid);
    void addMoveToGroupMenu (juce::PopupMenu& m, const juce::String& currentUid);
    juce::Colour liveGroupColour (const juce::String& uid) const;
    std::vector<std::pair<bool, int>> liveMembers (int g) const;   // (is an instrument, index), in strip order
    void toggleLiveCollapsed (int g);
    void moveLiveStrip (bool instrument, int i, int toGroup, int insertAt);
    LiveGroupView* liveGroupViewAt (juce::Point<int> pos);
    int freshColour() const;   // the group colour used least so far

    // Keys: an instrument in Any key plays every note on the Fire; the set's key buttons pick one.
    PadLayout layoutFor (int instrument) const;
    juce::StringArray keyButtonLabels() const;
    int activeKeyButton (int instrument) const;
    void refreshKeyButtons();   // the set's key buttons changed: every strip shows the new row
    static constexpr int maxKeyButtons = 5;
    void layoutRows();
    void updateRowViews();
    void addRow (std::function<void (int)> then = {});
    void showRowMenu (int row);
    void setTheme (int index);
    void shiftFireOctave (const juce::String& fireName, int direction);
    void nudgeFireVolume (const juce::String& fireName, int steps);
    struct KnobLevel { double tracked = 0.0, shown = 1000.0; };   // tracked can go below off (-60)
    std::map<juce::String, KnobLevel> knobLevels;
    int runawaysSeen = 0;   // FxChain::runaways already reported                 // the Fire VOLUME knob's, per instrument
    static constexpr int minPadOctave = 1, maxPadOctave = 6;   // the bottom-left pad's octave
    void toggleCollapsed (int row);
    void setAllCollapsed (bool collapse);
    void removeRow (int row);
    void addChannel (int row);
    void showChannelMenu (int row, int slot);
    void showSampleEditor (int row, int slot);
    void applySampleShape (int slotId, const SampleShape& shape);
    void removeChannel (int row, int slot);
    void moveChannel (int row, int slot, int delta);
    void moveChannelTo (int slotId, int targetRow, int targetIndex);
    RowView* rowViewAt (juce::Point<int> pos);
    juce::Component* groupViewAt (juce::Point<int> pos);
    static constexpr int noGroup = std::numeric_limits<int>::min();   // not a row (>= 0) or live group (< 0)
    // The width the groups are laid out in: the window's, or the set's fixed one (then the page scrolls sideways).
    int groupPageWidth() const;
    void setRearrangeToFit (bool rearrange);
    std::map<int, juce::Rectangle<int>> arrangeGroups (int moving = noGroup, juce::Point<int> movingTo = {},
                                                       std::map<int, juce::Point<int>>* wants = nullptr) const;
    juce::Point<int> droppedGroupPosition (const SourceDetails& details, int& item) const;
    void clearDropMarkers();
    void assignToChannels (int row, int firstSlot, const juce::Array<juce::File>& files);
    void appendToRow (int row, const juce::Array<juce::File>& files);
    void setRowMuted (int row, bool muted);
    int freeEngine() const;
    LoopChannel& engineFor (const Row& r)    { return *engines[(size_t) r.engine]; }

    // Live inputs and effects
    void setupInputs();
    void setupInputStrip (int input, SourceStrip& strip);
    void addInput (const juce::String& groupUid = {});
    void removeInput (int input);
    void openAllInputChannels();
    juce::String inputsOpenedFor;   // the device openAllInputChannels() last tried
    void applyInput (int input);
    void showInputMenu (int input);
    void loadInputPlugin (int input, const juce::PluginDescription& description, const juce::String& state);
    void removeInputPlugin (int input);
    void openInputPluginEditor (int input);
    juce::Array<juce::PluginDescription> effectTypes() const;
    // 'owner' says whose effects these are, for MIDI on the knobs: "in.<i>", "inst.<i>",
    // "grp.<uid>", "ch.<uid>" or "space".
    void showFxPanel (juce::Component& anchor, const juce::String& title, const FxSettings& fx, juce::Colour colour,
                      std::function<void (const FxSettings&)> onChange, const juce::String& owner,
                      FxPanel::Kind kind = FxPanel::Kind::channel);
    juce::Component::SafePointer<FxPanel> openFxPanel;   // the effects panel showing, if any
    juce::String openFxOwner;

    // Changes an owner's effects (from a MIDI knob) and puts the change into effect.
    // False if that owner doesn't exist any more.
    bool changeFx (const juce::String& owner, const std::function<void (FxSettings&)>& change);

    // The shared Space reverb, and effect plug-ins on groups ("grp.<uid>") and the master ("master").
    void showSpacePanel();
    PluginInsert* insertFor (const juce::String& key);
    LiveSet::PluginSlot* insertSlotFor (const juce::String& key);
    juce::String insertTitle (const juce::String& key);
    void showInsertMenu (const juce::String& key, juce::Component& target);
    void loadInsertPlugin (const juce::String& key, const juce::PluginDescription& description, const juce::String& state);
    void removeInsertPlugin (const juce::String& key);
    void openInsertEditor (const juce::String& key);
    void updateMasterButtons();

    // Hosted instruments
    void updateSourceStrips();
    void setupInstrumentStrip (int index, SourceStrip& strip);
    void addInstrument (const juce::String& groupUid = {}, juce::Component* menuTarget = nullptr);
    void removeInstrument (int index);
    void showInstrumentMenu (int index);
    void loadInstrumentPlugin (int index, const juce::PluginDescription& description, const juce::String& state);
    void openEditor (int index);
    void closeEditor (int index);
    void showPluginScanner();
    void applyInstrument (int index);
    void captureInstrumentStates();
    void updateFireLights (bool allOff = false);
    void applySetMidi();
    bool findSlotByUid (const juce::String& uid, int& row, int& slot) const;
    void applyFaderMidi (const juce::String& target, juce::Slider& fader, float proportion,
                         const std::function<void (float)>& showHardwarePosition = {});
    // The key of the first instrument a Fire plays ('fireName' = that Fire; empty = any Fire).
    bool currentKey (PadLayout& key, const juce::String& fireName = {}, const juce::String& fireId = {}) const;

    // Each instrument's MIDI device, matched by USB path (see InstrumentChannel::MidiSource);
    // also picks up a device's new name after Windows renames it.
    InstrumentChannel::MidiSource midiSourceFor (int instrument) const;
    void updateMidiSources();
    juce::String nextLoopLabel (const Row& row) const;
    juce::Array<juce::PluginDescription> instrumentTypes() const;
    juce::String instrumentName (int index) const;

    // The preset loaded in each instrument, read from its saved settings (see PresetName):
    // checked when it loads, when its window closes, and every 2 s while the window is open.
    std::array<juce::String, LiveSet::maxInstruments> presetNames;
    juce::uint32 lastPresetCheck = 0;
    void refreshPresetName (int index);
    juce::String instrumentDetail (int index) const;

    // Loop recording
    void recPressed();
    void dubPressed();
    void channelDubPressed (int row, int slot);
    void startPendingDub();
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
    // 'seconds' is the fade; -1 = the fade time.
    void startLoop (int row, int slot, bool fadeOutOthers, double seconds = -1.0);
    void stopSlot (Row& row, Slot& slot, juce::int64 startAt = -1, double seconds = -1.0);

    // When a loop pressed now comes in: straight away, or on the next beat or bar of the
    // tempo grid (counted from the last TAP, SYNC or the first loop to start).
    juce::int64 nextLoopStart();

    // When REC starts recording: now, or (with Start on beat or bar) on the next bar line at
    // least a beat away, so the beat lights and the Fire count you in. -1 = now.
    juce::int64 nextRecordStart();
    bool anyLoopPlaying() const;
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
    void setTimeSignature (int beats, int unit);   // and save
    void applyTimeSignature();                     // puts the set's into effect
    void showTimeSignatureMenu();
    double samplesPerBar() const;
    void tapTempo();

    juce::ApplicationProperties settings;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (2) };

    LiveSet liveSet;
    std::array<std::unique_ptr<LoopChannel>, LiveSet::maxRows> engines;
    // Live groups' mixes, and which one each input and instrument plays into (read by the audio thread).
    std::array<std::unique_ptr<LiveGroupBus>, LiveSet::maxLiveGroups> liveBuses;
    std::array<std::atomic<int>, LiveSet::maxInputs> inputBus {};
    std::array<std::atomic<int>, LiveSet::maxInstruments> instrumentBus {};
    std::array<std::unique_ptr<InputChannel>, LiveSet::maxInputs> inputEngines;
    std::atomic<int> activeInputs { 0 };   // how many of them the audio thread runs
    int inputGeneration = 0;               // discards input plug-in loads meant for an earlier set or layout

    juce::AudioPluginFormatManager pluginFormats;   // declared before the instruments, so it outlives them
    juce::KnownPluginList knownPlugins;
    std::array<std::unique_ptr<InstrumentChannel>, LiveSet::maxInstruments> instrumentEngines;
    SpaceReverb spaceReverb;
    PluginInsert masterInsert;
    std::map<juce::String, std::unique_ptr<juce::DocumentWindow>> insertWindows;
    int insertGeneration = 0;                  // discards group and master plug-in loads meant for an earlier set
    juce::TextButton spaceButton { "SPACE" }, masterFxButton { "FX" };
    std::array<std::unique_ptr<juce::DocumentWindow>, LiveSet::maxInstruments> editorWindows;
    std::array<std::unique_ptr<juce::DocumentWindow>, LiveSet::maxInputs> inputPluginWindows;
    std::array<bool, LiveSet::maxInstruments> instrumentLoading {};
    int instrumentGeneration = 0;              // discards plugin loads meant for an earlier set
    std::atomic<int> currentBlockSize { 512 };
    juce::uint32 lastStateCapture = 0;
    // Every connected Fire, by name: each lights its pads in the key of the instrument it plays.
    struct FireOutput { juce::String name, inputId; std::unique_ptr<juce::MidiOutput> output; };

    // Light show: a Fire's pads as a visualizer of the output. On the Fire, with SHIFT held:
    // ALT turns it on and off, GRID left/right picks the pattern, PATTERN up/down the palette.
    // (Without SHIFT, GRID left/right move the Fire's instruments down/up an octave.)
    // Kept per Fire, by its USB path.
    // 'position' joins two Fires into one display: where this one sits beside the other.
    enum FirePosition { separate, left, right, top, bottom, numFirePositions };
    struct LightShow { bool on = false; int pattern = 0, palette = 0, position = separate; };
    LightShow& lightShowFor (const FireOutput& fire);
    FireOutput* partnerOf (const FireOutput& fire);
    FireVisualizer fireVisualizer;
    std::map<juce::String, LightShow> fireLightShows;
    std::map<juce::String, bool> fireShiftHeld;    // by device name
    LightShow lightShowOf (const FireOutput& fire) const;
    void sendFirePads (FireOutput& fire, const std::array<juce::Colour, 64>& pads);
    void updateFireLightShow();
    std::vector<FireOutput> fireOutputs;
    juce::AudioBuffer<float> inputCapture;   // audio thread: the interface inputs for this block
    juce::AudioBuffer<float> recordBus;      // audio thread: armed sources for the looper, two channels per track
    juce::AudioBuffer<float> recordDiscard;  // audio thread: where a source armed mid-take goes (not recorded)

    // Recording several armed sources at once: each on its own track (and channel). The track each
    // input and instrument records into (-1: none); all 0 outside a recording, so overdubs hear them all.
    static constexpr int maxRecordTracks = LiveSet::maxInputs + LiveSet::maxInstruments;
    std::array<std::atomic<int>, LiveSet::maxInputs> inputTrack {};
    std::array<std::atomic<int>, LiveSet::maxInstruments> instrumentTrack {};
    struct RecordSource { bool instrument; int index; juce::String name, key; };
    std::vector<RecordSource> recordSources;   // message thread: who's on which track of the take
    void routeAllArmedToTrackZero();
    juce::String nextLoopLabelFor (const Row& row, const juce::String& base) const;
    std::atomic<int> deviceInputs { 0 };
    std::atomic<int> roundTripLatency { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };
    juce::int64 audioClock = 0;              // audio thread: samples processed since start

    Looper looper;
    int dubTargetId = -1;                    // the loop DUB records onto
    int overdubSlotId = -1;                  // the loop being overdubbed right now
    int pendingDubId = -1;                   // a channel DUB waiting for its loop to come in
    juce::uint32 pendingDubSince = 0;
    int pendingDubOldTarget = -1;            // where the main DUB pointed before, for a cancel
    void cancelPendingDub();
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
    // The area the groups sit on; while a group is dragged it shows where it would land.
    struct GroupCanvas : public juce::Component
    {
        juce::Rectangle<int> ghost;
        juce::Colour ghostColour;

        void showGhost (juce::Rectangle<int> r, juce::Colour c)
        {
            if (r != ghost) { repaint (ghost.expanded (4)); ghost = r; ghostColour = c; repaint (ghost.expanded (4)); }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (ghost.isEmpty())
                return;
            g.setColour (ghostColour.withAlpha (0.12f));
            g.fillRoundedRectangle (ghost.toFloat(), 8.0f);
            g.setColour (ghostColour);
            g.drawRoundedRectangle (ghost.toFloat().reduced (1.0f), 8.0f, 2.0f);
        }
    };
    GroupCanvas rowsContent;
    InputsView inputsView;
    LooperBar looperBar;
    juce::OwnedArray<RowView> rowViews;
    juce::OwnedArray<LiveGroupView> liveGroupViews;
    LiveDock liveDock;   // the inputs' and instruments' groups, along the bottom
    void layoutLiveDock();
    void setLiveDockFolded (bool fold);
    juce::TextButton addRowButton { "GROUP" };
    ChannelStrip masterStrip { "Master", false };
    RightClickable<juce::TextButton> stopAllButton { "STOP ALL" };
    RightClickable<juce::TextButton> freezeButton { "FREEZE" };
    FreezeHold freezeHold;
    void toggleFreeze();
    void updateFreezeButton();

    // Recording the whole set to a file.
    RightClickable<juce::TextButton> recordSetButton { "REC SET" };
    SetRecorder setRecorder;
    void toggleSetRecording();
    void updateRecordSetButton();
    juce::TextButton setButton { "Set" }, settingsButton { "Settings" };
    juce::Slider fadeTimeSlider;
    juce::Label fadeTimeLabel { {}, "Fade time" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel { {}, "BPM" };
    RightClickable<juce::TextButton> tapButton { "TAP" };
    RightClickable<juce::TextButton> syncButton { "SYNC" };
    juce::ComboBox loopStartBox;

    // Puts every playing loop back in phase: with the one that's been playing longest
    // (the one being overdubbed, if any), or with 'restart' all from the top together now.
    void syncLoops (bool restart);

    // Set timer: how long the performance has been going.
    RightClickable<juce::TextButton> setTimerButton { "START" };
    double setTimerStartMs = 0.0, setTimerElapsedMs = 0.0;
    bool setTimerRunning = false;
    juce::Rectangle<int> setTimerArea;
    juce::String setTimerText { "0:00" };
    double setTimerSeconds() const;
    void toggleSetTimer();
    void showSetTimerMenu();
    std::vector<double> tapTimes;   // recent TAP presses, in ms
    BeatIndicator beatIndicator;
    juce::int64 beatAnchor = 0;     // clock time of a beat 1 (set by TAP)

    // Beats since the reference point: REC while recording, otherwise the last TAP.
    double currentBeatPosition (bool& recording) const;
    juce::Rectangle<int> headerArea, messageArea;
    std::vector<int> transportDividers;   // x of the thin lines between the bottom bar's sections
    ToastStack toasts;                    // confirmations and errors, top right
    juce::Rectangle<int> busyArea;        // the end of the status line: what's loading or saving
    juce::String busyText;
    void updateNotifications();
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String deviceStatus, midiStatus, errorMessage, infoMessage;
    bool deviceOpen = false;

    // MIDI
    MidiMap midiMap;
    juce::String learnTarget, learnLabel;

    struct Takeover { bool locked = false; float last = -1.0f; };
    std::map<juce::String, Takeover> takeover;
    OutputLimiter outputLimiter;
    std::map<int, bool> ledState;   // MidiMix button lights as last sent
    std::map<int, int> fireButtonState;   // Fire button lights as last sent
    void updateFireButtons (bool allOff = false);

    struct PendingMidi { juce::String device; juce::MidiMessage message; };
    std::array<PendingMidi, 512> midiQueue;
    juce::AbstractFifo midiFifo { 512 };
    juce::SpinLock midiWriteLock;         // MIDI callbacks may come from several threads

    juce::MidiDeviceListConnection midiDevicesChanged;
    std::unique_ptr<juce::MidiOutput> ledOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
