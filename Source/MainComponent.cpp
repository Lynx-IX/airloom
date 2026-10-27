#include "MainComponent.h"
#include "TempoGuess.h"
#include "Theme.h"

namespace
{
    constexpr size_t cacheBudgetBytes = (size_t) 2 * 1024 * 1024 * 1024;   // 2 GB of loaded audio
    constexpr int rowGap = 8;
    const double smoothOptions[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0 };       // seconds; 0 = off

    // Each version of a sample (smoothed, reversed) is kept in memory under its own key.
    juce::String clipKey (const juce::File& file, double smoothSeconds, bool reversed)
    {
        return file.getFullPathName() + (smoothSeconds > 0.0 ? "|smooth" + juce::String (smoothSeconds) : juce::String())
                                      + (reversed ? "|rev" : "");
    }

    double smoothingFor (const LiveSet::Slot& s)   { return s.oneShot ? 0.0 : s.smoothSeconds; }

    juce::String clipKey (const LiveSet::Slot& s)
    {
        return clipKey (s.file, smoothingFor (s), s.reverse);
    }

    bool writeWav (const LoopClip& clip, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (clip.sampleRate)
                                 .withNumChannels (clip.data.getNumChannels())
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);   // headroom: a hot take is never clipped
        auto writer = wav.createWriterFor (stream, options);
        return writer != nullptr && writer->writeFromAudioSampleBuffer (clip.data, 0, clip.data.getNumSamples());
    }

    // One-click keys for the Fire pads, matching the flutes. Pentatonic, as the flutes are tuned.
    struct KeyPreset { const char* label; int root; int scale; };
    const KeyPreset fluteKeys[] = {
        { "A min", 9, 0 },    // A minor pentatonic
        { "Bb maj", 10, 1 },  // B flat major pentatonic
        { "E maj", 4, 1 },    // E major pentatonic
    };

    juce::String formatTime (double seconds)
    {
        const int s = (int) seconds;
        return juce::String::formatted ("%d:%02d", s / 60, s % 60);
    }

    // A hosted plugin's own editor in a window of its own.
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> closed)
            : DocumentWindow (plugin.getName(), Theme::panel, DocumentWindow::closeButton), onClose (std::move (closed))
        {
            setUsingNativeTitleBar (true);

            juce::AudioProcessorEditor* editor = plugin.createEditorIfNeeded();
            if (editor == nullptr)
                editor = new juce::GenericAudioProcessorEditor (plugin);

            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            if (onClose)
                onClose();
        }

    private:
        std::function<void()> onClose;
    };

    float dbToGain (double db)
    {
        return db <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) db);
    }

    size_t clipBytes (const LoopClipPtr& clip)
    {
        return (size_t) clip->data.getNumSamples() * (size_t) clip->data.getNumChannels() * sizeof (float);
    }

    void disableKeyboardFocus (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            child->setWantsKeyboardFocus (false);
            disableKeyboardFocus (*child);
        }
    }
}

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "LiveRig";
    options.filenameSuffix = ".settings";
    options.folderName = "LiveRig";
    options.osxLibrarySubFolder = "Application Support";
    settings.setStorageParameters (options);
    auto* props = settings.getUserSettings();

    formatManager.registerBasicFormats();

    if (auto xml = props->getXmlValue ("midiMap"))
        midiMap.fromXml (*xml);

    // Groups used to take MidiMix faders and mutes by position; that's gone, as are
    // assignments for the set's controls, which now come with the set itself.
    midiMap.removeIf ([] (const juce::String& t)
    {
        return MidiMap::isSetTarget (t) || (t.startsWith ("fader.") && t != "fader.master") || t.startsWith ("mute.");
    });

    // The Fire's REC button became a default later: add it once, unless REC or that button already has a job.
    if (! props->getBoolValue ("fireRecAdded", false))
    {
        if (! midiMap.get ("rec").isValid()
            && midiMap.findTargets ("FL STUDIO FIRE", juce::MidiMessage::noteOn (1, MidiMap::fireRecNote, 1.0f)).isEmpty())
            midiMap.bind ("rec", { MidiMap::fireName, true, MidiMap::fireRecNote, 0 });
        props->setValue ("fireRecAdded", true);
        props->setValue ("midiMap", midiMap.toXml().get());
    }

    juce::Desktop::getInstance().setGlobalScaleFactor ((float) props->getDoubleValue ("uiScale", 1.0));

    for (auto& e : engines)
        e = std::make_unique<LoopChannel>();
    for (auto& e : inputEngines)
        e = std::make_unique<InputChannel>();
    for (auto& e : instrumentEngines)
        e = std::make_unique<InstrumentChannel>();

    juce::addDefaultFormatsToManager (pluginFormats);
    if (auto xml = props->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
    knownPlugins.addChangeListener (this);

    addAndMakeVisible (pool);
    pool.onFilesAdded = [this] (const juce::Array<juce::File>& files) { addToPool (files); };
    pool.onRightClick = [this] (const juce::Array<juce::File>& files) { showPoolMenu (files); };
    pool.describeUsage = [this] (const juce::File& f) { return liveSet.describeUsage (f); };
    pool.setCollapsed (props->getBoolValue ("poolCollapsed"));
    pool.onToggleCollapse = [this]
    {
        pool.setCollapsed (! pool.isCollapsed());
        settings.getUserSettings()->setValue ("poolCollapsed", pool.isCollapsed());
        resized();
    };

    addAndMakeVisible (rowsViewport);
    rowsViewport.setViewedComponent (&rowsContent, false);
    rowsViewport.setScrollBarsShown (true, false);
    rowsViewport.setScrollBarThickness (10);
    rowsContent.addAndMakeVisible (inputsView);
    inputsView.onInputStripCreated = [this] (int i, SourceStrip& s) { setupInputStrip (i, s); };
    inputsView.onInstrumentStripCreated = [this] (int i, SourceStrip& s) { setupInstrumentStrip (i, s); };
    inputsView.onLayoutChanged = [this] { layoutRows(); };
    inputsView.resizer.onColumnsChanged = [this] (int columns)
    {
        liveSet.inputsColumns = columns;
        layoutRows();
        saveSession();
    };
    inputsView.addInputButton.onClick = [this] { addInput(); };
    inputsView.addInstrumentButton.onClick = [this] { addInstrument(); };
    setupInputs();
    addAndMakeVisible (addRowButton);
    addRowButton.onClick = [this] { addRow(); };

    addAndMakeVisible (masterStrip);
    masterStrip.muteButton.setVisible (false);
    masterStrip.fader.onValueChange = [this]
    {
        liveSet.masterDb = masterStrip.fader.getValue();
        masterTarget = dbToGain (liveSet.masterDb);
        saveSession();
    };
    masterStrip.fader.onDragStart = [this] { takeover["fader.master"].locked = false; };
    masterStrip.fader.onRightClick = [this] { showLearnMenu ("fader.master", "Master fader"); };

    addAndMakeVisible (spaceButton);
    spaceButton.setWantsKeyboardFocus (false);
    spaceButton.setTooltip ("The shared reverb every channel can send to with its Space knob (in its FX)");
    spaceButton.onClick = [this] { showSpacePanel(); };
    addAndMakeVisible (masterFxButton);
    masterFxButton.setWantsKeyboardFocus (false);
    masterFxButton.setTooltip ("A plug-in effect on everything (before the master fader)");
    masterFxButton.onClick = [this] { showInsertMenu ("master", masterFxButton); };
    updateMasterButtons();

    addAndMakeVisible (stopAllButton);
    stopAllButton.onClick = [this] { stopAll (fadeTimeSlider.getValue()); };
    stopAllButton.setColour (juce::TextButton::buttonColourId, Theme::danger.darker (1.2f));
    stopAllButton.onRightClick = [this] { showLearnMenu ("stopAll", "Stop all"); };

    addAndMakeVisible (freezeButton);
    freezeButton.setWantsKeyboardFocus (false);
    freezeButton.setTooltip ("Hold what you just played as an endless pad to play over. Press again to let it go "
                             "(it fades out over the fade time). Right-click to assign a MIDI button.");
    freezeButton.onClick = [this] { toggleFreeze(); };
    freezeButton.onRightClick = [this] { showLearnMenu ("freeze", "Freeze"); };
    updateFreezeButton();

    addAndMakeVisible (recordSetButton);
    recordSetButton.setWantsKeyboardFocus (false);
    recordSetButton.setTooltip ("Record everything going to the speakers to a WAV file. Right-click for the recordings folder.");
    recordSetButton.onClick = [this] { toggleSetRecording(); };
    recordSetButton.onRightClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Show set recordings");
        m.addItem (2, "Assign MIDI button...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordSetButton), [this] (int r)
        {
            if (r == 1) { recordingsFolder().getChildFile ("Sets").createDirectory(); recordingsFolder().getChildFile ("Sets").revealToUser(); }
            if (r == 2) showLearnMenu ("recSet", "Record set");
        });
    };
    updateRecordSetButton();

    addAndMakeVisible (setButton);
    addAndMakeVisible (settingsButton);
    setButton.onClick = [this] { showSetMenu(); };
    settingsButton.onClick = [this] { showDeviceSettings(); };

    addAndMakeVisible (fadeTimeLabel);
    addAndMakeVisible (fadeTimeSlider);
    fadeTimeLabel.setColour (juce::Label::textColourId, Theme::textDim);
    fadeTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    fadeTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
    fadeTimeSlider.setRange (0.5, 30.0, 0.5);
    fadeTimeSlider.setTextValueSuffix (" s");
    fadeTimeSlider.setValue (8.0);
    fadeTimeSlider.onValueChange = [this]
    {
        liveSet.fadeSeconds = fadeTimeSlider.getValue();
        saveSession();
    };

    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmSlider);
    addAndMakeVisible (tapButton);

    addAndMakeVisible (setTimerButton);
    setTimerButton.setTooltip ("Start or pause the set timer. Right-click to reset it.");
    setTimerButton.setWantsKeyboardFocus (false);
    setTimerButton.onClick = [this] { toggleSetTimer(); };
    setTimerButton.onRightClick = [this] { showSetTimerMenu(); };
    bpmLabel.setColour (juce::Label::textColourId, Theme::textDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (30.0, 240.0, 0.01);   // fine enough to match a loop's exact tempo
    bpmSlider.textFromValueFunction = [] (double v)
    {
        // Whole numbers stay short; exact tempos from loops show their decimals.
        return std::abs (v - std::round (v)) < 0.005 ? juce::String ((int) std::round (v)) : juce::String (v, 2);
    };
    bpmSlider.setColour (juce::Slider::trackColourId, Theme::panelEdge);
    bpmSlider.setTooltip ("Drag or type the tempo. Synced delays follow it.");
    bpmSlider.onValueChange = [this] { setBpm (bpmSlider.getValue()); };
    tapButton.setTooltip ("Tap along to set the tempo");
    tapButton.onClick = [this] { tapTempo(); };
    tapButton.onRightClick = [this] { showLearnMenu ("tap", "Tap tempo"); };

    addAndMakeVisible (loopStartBox);
    loopStartBox.addItem ("Start: now", 1);
    loopStartBox.addItem ("Start: beat", 2);
    loopStartBox.addItem ("Start: bar", 3);
    loopStartBox.setWantsKeyboardFocus (false);
    loopStartBox.setTooltip ("When a loop you press comes in: straight away, or on the next beat or bar at the BPM, "
                             "in time with the loops already playing");
    loopStartBox.setSelectedId (1, juce::dontSendNotification);
    loopStartBox.onChange = [this]
    {
        liveSet.loopStart = (LiveSet::LoopStart) (loopStartBox.getSelectedId() - 1);
        saveSession();
    };

    addAndMakeVisible (syncButton);
    syncButton.setWantsKeyboardFocus (false);
    syncButton.setTooltip ("Loops drifted apart? Lines every playing loop back up with the one that's been playing longest. "
                           "Right-click to restart them all from the top together.");
    syncButton.onClick = [this] { syncLoops (false); };
    syncButton.onRightClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Restart all loops together from the top");
        m.addItem (2, "Assign MIDI button...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&syncButton), [this] (int r)
        {
            if (r == 1) syncLoops (true);
            if (r == 2) showLearnMenu ("sync", "Sync loops");
        });
    };
    addAndMakeVisible (beatIndicator);
    beatIndicator.onClick = [this] { showTimeSignatureMenu(); };

    disableKeyboardFocus (*this);
    setWantsKeyboardFocus (true);
    setSize (1440, 900);

    openAudioDevice();

    deviceManager.addMidiInputDeviceCallback ({}, this);
    midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); });
    enableMidiInputs();

    // Pick up where the last session left off, saved set or not.
    auto session = props->getXmlValue ("session");
    if (session != nullptr && session->getChildByName ("Row") != nullptr)
    {
        const juce::File setFile (props->getValue ("setFile"));
        liveSet.file = setFile.existsAsFile() ? setFile : juce::File();
        loadSet (*session, liveSet.getFolder());
    }
    else
    {
        loadSet (*LiveSet::makeDefault().toXml ({}), {});
    }

    addAndMakeVisible (perfMeter);
    perfMeter.onReset = [this] { dropoutBase = deviceManager.getXRunCount(); lastDropouts = 0; lastDropoutMs = 0; };

    addAndMakeVisible (muteButton);
    muteButton.setWantsKeyboardFocus (false);
    muteButton.setTooltip ("Silences the speakers instantly - for emergencies (feedback, a wrong sound). Everything keeps "
                           "running, so press again to come straight back. STOP ALL and Esc are the musical fades. "
                           "Right-click to assign a MIDI button.");
    muteButton.onClick = [this] { toggleMute(); };
    muteButton.onRightClick = [this] { showLearnMenu ("mute", "Mute out"); };
    updateMuteButton();

    addChildComponent (stageView);
    stageView.onScene = [this] (int i) { recallScene (i); };
    stageView.onAddScene = [this] { addScene(); };
    stageView.onMuteOut = [this] { toggleMute(); };
    stageView.onTile = [this] (int row, int slot)
    {
        if (row < (int) liveSet.rows.size() && slot < (int) liveSet.rows[(size_t) row].slots.size())
            playPressed (row, slot);
    };
    stageView.onGroupMute = [this] (int row)
    {
        if (row < (int) liveSet.rows.size())
            setRowMuted (row, ! liveSet.rows[(size_t) row].muted);
    };
    stageView.onLiveMute = [this] (bool instrument, int i)
    {
        if (instrument && i < (int) liveSet.instruments.size())
        {
            liveSet.instruments[(size_t) i].muted = ! liveSet.instruments[(size_t) i].muted;
            applyInstrument (i);
        }
        else if (! instrument && i < (int) liveSet.inputs.size())
        {
            liveSet.inputs[(size_t) i].muted = ! liveSet.inputs[(size_t) i].muted;
            applyInput (i);
        }
        saveSession();
    };

    addAndMakeVisible (stageButton);
    stageButton.setWantsKeyboardFocus (false);
    stageButton.setClickingTogglesState (false);
    stageButton.setTooltip ("Stage view: scenes, loops and mutes drawn big for performing (F11). Right-click to assign a MIDI button.");
    stageButton.onClick = [this] { setStageMode (! stageMode); };
    stageButton.onRightClick = [this] { showLearnMenu ("stage", "Stage view"); };

    addAndMakeVisible (sceneBar);
    sceneBar.onAdd = [this] { addScene(); };
    sceneBar.onRecall = [this] (int i) { recallScene (i); };
    sceneBar.onSceneMenu = [this] (int i) { showSceneMenu (i); };
    sceneBar.onBarMenu = [this] { showSceneBarMenu(); };

    addChildComponent (alertBanner);
    addChildComponent (countInOverlay);
    startedMs = juce::Time::getMillisecondCounter();
    setStageMode (settings.getUserSettings()->getBoolValue ("stageMode", false));
    if (startupWarning.isNotEmpty())
        errorMessage = startupWarning;

    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();

    // Keep the instruments' latest settings, then close their windows before the plugins go.
    captureInstrumentStates();
    saveSession();
    for (int i = 0; i < LiveSet::maxInstruments; ++i)
        closeEditor (i);
    for (auto& w : inputPluginWindows)
        w.reset();
    knownPlugins.removeChangeListener (this);

    loader.removeAllJobs (true, 10000);
    setRecorder.stop();
    updateMidiLeds (true);
    updateFireLights (true);
    updateFireButtons (true);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeChangeListener (this);
    shutdownAudio();
}

//==============================================================================
void MainComponent::openAudioDevice()
{
    // Every interface input is opened (see openAllInputChannels), so any input strip can use any of them.
    // With no saved choice this opens the Windows default device.
    std::unique_ptr<juce::XmlElement> saved (settings.getUserSettings()->getXmlValue ("audioDevice"));
    setAudioChannels (LiveSet::maxDeviceInputs, 2, saved.get());

    deviceManager.addChangeListener (this);
    changeListenerCallback (&deviceManager);
}

void MainComponent::showDeviceSettings()
{
    // Picking a device by hand ends any wait for a lost one.
    lostDeviceName.clear();
    alertBanner.setMessage ({});

    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, LiveSet::maxDeviceInputs, 0, 2,
                                                             false, false, true, false);
    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &knownPlugins)
    {
        // A scan found or removed plug-ins: remember the list for next time.
        if (auto xml = knownPlugins.createXml())
            settings.getUserSettings()->setValue ("knownPlugins", xml.get());
        return;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    lastCallbackMs = juce::Time::getMillisecondCounter();

    // The interface vanished (unplugged, powered off): JUCE falls back to the default device,
    // i.e. the laptop's speakers and mic - feedback risk. Close that and wait for the interface.
    if (lostDeviceName.isEmpty() && lastDeviceName.isNotEmpty()
        && (device == nullptr || device->getName() != lastDeviceName)
        && ! deviceAvailable (lastDeviceType, lastDeviceName))
    {
        lostDeviceName = lastDeviceName;
        if (device != nullptr)
        {
            deviceManager.closeAudioDevice();
            device = nullptr;
        }
    }

    if (device != nullptr && lostDeviceName.isEmpty())
    {
        lastDeviceName = device->getName();
        lastDeviceType = deviceManager.getCurrentAudioDeviceType();
        lastSetup = deviceManager.getAudioDeviceSetup();
    }

    deviceOpen = device != nullptr;

    if (device != nullptr)
    {
        deviceStatus = device->getName()
                     + "   " + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz"
                     + "   " + juce::String (device->getCurrentBufferSizeSamples()) + " samples"
                     + "   (" + deviceManager.getCurrentAudioDeviceType() + ")";
    }
    else
    {
        deviceStatus = lostDeviceName.isNotEmpty() ? lostDeviceName + " disconnected - waiting for it"
                                                   : "No audio device - open Audio Settings";
    }

    // While waiting for a lost interface, keep it as the saved choice for next time.
    if (lostDeviceName.isEmpty())
        if (auto xml = deviceManager.createStateXml())
            settings.getUserSettings()->setValue ("audioDevice", xml.get());

    alertBanner.setMessage (lostDeviceName.isEmpty() ? juce::String()
        : "AUDIO INTERFACE LOST: " + lostDeviceName + ".  Check its cable and power - Airloom reconnects by itself "
          "the moment it's back, and the loops carry on from where they stopped.");

    repaint();
    openAllInputChannels();
}

void MainComponent::openAllInputChannels()
{
    // Every interface input (up to 16) is opened, so an input strip can take any of them.
    // Tried once per device, in case a driver won't open them all.
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    juce::BigInteger all;
    all.setRange (0, juce::jmin (LiveSet::maxDeviceInputs, device->getInputChannelNames().size()), true);
    const auto key = device->getName() + "/" + juce::String (all.countNumberOfSetBits());
    if (device->getActiveInputChannels() == all || inputsOpenedFor == key)
        return;

    inputsOpenedFor = key;
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels = all;
    deviceManager.setAudioDeviceSetup (setup, true);
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    for (auto& e : engines)
        e->prepare (sampleRate, samplesPerBlockExpected);
    for (auto& e : inputEngines)
        e->prepare (sampleRate, samplesPerBlockExpected);
    for (auto& e : instrumentEngines)
        e->prepare (sampleRate, samplesPerBlockExpected);
    currentBlockSize = samplesPerBlockExpected;

    inputCapture.setSize (LiveSet::maxDeviceInputs, juce::jmax (4096, samplesPerBlockExpected));
    recordBus.setSize (2, juce::jmax (4096, samplesPerBlockExpected));
    looper.prepare (sampleRate);
    currentSampleRate = sampleRate;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        deviceInputs = device->getActiveInputChannels().countNumberOfSetBits();
        roundTripLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    }

    masterGain.reset (sampleRate, 0.05);
    masterGain.setCurrentAndTargetValue (masterTarget);
    muteGain.reset (sampleRate, 0.005);
    muteGain.setCurrentAndTargetValue (outputMuted ? 0.0f : 1.0f);
    outputLimiter.prepare (sampleRate);
    freezeHold.prepare (sampleRate);
    spaceReverb.prepare (sampleRate, samplesPerBlockExpected);
    fireVisualizer.prepare (sampleRate);
    masterInsert.prepare (sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    auto& buffer = *info.buffer;
    LoopPlayer::blockClock = audioClock;

    // The buffer arrives holding the interface inputs; keep them before it becomes the output.
    const int numIn = juce::jmin (deviceInputs.load(), buffer.getNumChannels(), inputCapture.getNumChannels());
    const int inputSamples = juce::jmin (info.numSamples, inputCapture.getNumSamples());
    for (int ch = 0; ch < numIn; ++ch)
        inputCapture.copyFrom (ch, 0, buffer, ch, info.startSample, inputSamples);

    info.clearActiveBufferRegion();
    recordBus.clear (0, juce::jmin (inputSamples, recordBus.getNumSamples()));

    for (int i = 0; i < juce::jmin (activeInputs.load(), LiveSet::maxInputs); ++i)
        inputEngines[(size_t) i]->process (inputCapture, numIn, buffer, info.startSample, inputSamples, recordBus);
    for (auto& inst : instrumentEngines)
        inst->process (buffer, info.startSample, info.numSamples, recordBus);

    looper.process (recordBus, juce::jmin (inputSamples, recordBus.getNumSamples()), audioClock, roundTripLatency);

    // So far the buffer holds only the live inputs and instruments: freeze listens to
    // those (not the loops) and adds its held pad.
    freezeHold.process (buffer, info.startSample, info.numSamples);

    for (auto& e : engines)
        e->process (buffer, info.startSample, info.numSamples);

    // Everything sent to Space this block comes back as one shared reverb; then the master's plug-in.
    spaceReverb.process (buffer, info.startSample, info.numSamples);
    masterInsert.process (buffer, info.startSample, info.numSamples);

    masterGain.setTargetValue (masterTarget);
    const int outChannels = juce::jmin (2, buffer.getNumChannels());

    if (masterGain.isSmoothing())
    {
        for (int i = 0; i < info.numSamples; ++i)
        {
            const float g = masterGain.getNextValue();
            for (int ch = 0; ch < outChannels; ++ch)
                buffer.getWritePointer (ch, info.startSample)[i] *= g;
        }
    }
    else
    {
        buffer.applyGain (info.startSample, info.numSamples, masterGain.getCurrentValue());
    }

    // Safety limiter: nothing reaches the speakers above -0.5 dBFS, and nothing hard-clips.
    outputLimiter.process (buffer, info.startSample, info.numSamples, outChannels);

    // MUTE OUT, after everything (the set recording hears what the room hears).
    muteGain.setTargetValue (outputMuted ? 0.0f : 1.0f);
    if (muteGain.isSmoothing())
    {
        for (int i = 0; i < info.numSamples; ++i)
        {
            const float g = muteGain.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch, info.startSample)[i] *= g;
        }
    }
    else if (muteGain.getCurrentValue() <= 0.0f)
    {
        info.clearActiveBufferRegion();
    }

    lastCallbackMs = juce::Time::getMillisecondCounter();
    setRecorder.write (buffer, info.startSample, info.numSamples);
    if (outChannels > 0)
        fireVisualizer.push (buffer.getReadPointer (0, info.startSample),
                             outChannels > 1 ? buffer.getReadPointer (1, info.startSample) : nullptr, info.numSamples);

    if (outChannels > 0) masterPeakL = juce::jmax (masterPeakL.load(), buffer.getMagnitude (0, info.startSample, info.numSamples));
    if (outChannels > 1) masterPeakR = juce::jmax (masterPeakR.load(), buffer.getMagnitude (1, info.startSample, info.numSamples));

    audioClock += info.numSamples;
}

void MainComponent::releaseResources()
{
    for (auto& e : instrumentEngines)
        e->release();
}

//==============================================================================
void MainComponent::showSetMenu()
{
    const bool saved = liveSet.file.existsAsFile();

    juce::PopupMenu m;
    m.addSectionHeader (liveSet.getName());
    m.addItem (1, "New set");
    m.addItem (2, "Open set...");
    m.addSeparator();
    m.addItem (3, "Save set", saved);
    m.addItem (4, "Save set as new folder...");
    m.addItem (9, "Export set as one file (to move to another computer)...", packProgress == nullptr);
    m.addSeparator();
    m.addItem (5, "Show set folder", saved);
    m.addSeparator();
    m.addItem (6, "Zoom in   (Ctrl +)");
    m.addItem (7, "Zoom out   (Ctrl -)");
    m.addItem (8, "Actual size   (Ctrl 0)");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setButton), [this] (int r)
    {
        const float scale = juce::Desktop::getInstance().getGlobalScaleFactor();

        if (r == 1) newSet();
        if (r == 2) openSet();
        if (r == 3) saveSet (false);
        if (r == 4) saveSet (true);
        if (r == 5) liveSet.file.revealToUser();
        if (r == 9) exportPack();
        if (r == 6) setZoom (scale + 0.1f);
        if (r == 7) setZoom (scale - 0.1f);
        if (r == 8) setZoom (1.0f);
    });
}

void MainComponent::setBpm (double bpm)
{
    liveSet.bpm = juce::jlimit (30.0, 240.0, bpm);
    FxChain::tempoBpm = liveSet.bpm;   // synced delays pick this up on their next block
    bpmSlider.setValue (liveSet.bpm, juce::dontSendNotification);
    saveSession();
}

//==============================================================================
LiveSet::Scene MainComponent::captureScene (const juce::String& name) const
{
    LiveSet::Scene sc;
    sc.name = name;
    sc.morphSeconds = fadeTimeSlider.getValue();
    for (const auto& r : liveSet.rows)
    {
        sc.groups.push_back ({ r.uid, r.faderDb, r.muted });
        for (const auto& s : r.slots)
            if (s.file != juce::File() && ! s.oneShot)
                sc.channels.push_back ({ s.uid, s.playing, s.volumeDb });
    }
    for (int i = 0; i < (int) liveSet.inputs.size(); ++i)
        sc.strips.push_back ({ LiveSet::Scene::Strip::input, i, liveSet.inputs[(size_t) i].faderDb, liveSet.inputs[(size_t) i].muted });
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
    {
        const auto& inst = liveSet.instruments[(size_t) i];
        sc.strips.push_back ({ LiveSet::Scene::Strip::instrument, i, inst.faderDb, inst.muted });
        sc.keys.push_back ({ i, inst.padRoot, inst.padScale, inst.padOctave });
    }
    return sc;
}

void MainComponent::addScene()
{
    liveSet.scenes.push_back (captureScene ("Scene " + juce::String ((int) liveSet.scenes.size() + 1)));
    activeScene = (int) liveSet.scenes.size() - 1;
    infoMessage = "Saved " + liveSet.scenes.back().name + " - right-click it to rename it, set its morph time or assign MIDI";
    updateSceneBar();
    saveSession();
    repaint();
}

void MainComponent::recallScene (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) liveSet.scenes.size()))
        return;

    // Finish any morph still going, so this one starts from where things really are.
    if (! sceneGlides.empty())
    {
        morphMs = 0.0;
        updateMorph();
    }

    // Leaving a scene keeps what you did in it (loops started or stopped, levels, mutes, keys),
    // so coming back finds it as you left it - unless that's switched off, when scenes stay as saved.
    if (liveSet.scenesKeepChanges && juce::isPositiveAndBelow (activeScene, (int) liveSet.scenes.size()) && activeScene != index)
    {
        auto& leaving = liveSet.scenes[(size_t) activeScene];
        auto kept = captureScene (leaving.name);
        kept.uid = leaving.uid;
        kept.morphSeconds = leaving.morphSeconds;
        leaving = std::move (kept);
    }

    const auto& sc = liveSet.scenes[(size_t) index];
    const double seconds = sc.morphSeconds;
    activeScene = index;
    sceneGlides.clear();

    // A strip (group, input or instrument) whose fader or mute differs glides there. A mute
    // fades out and an unmute fades in over the morph, so e.g. one instrument voice crossfades
    // into another; the mute button shows where it's going straight away.
    auto glideStrip = [&] (SceneGlide::Kind kind, const juce::String& uid, int i, double& faderDb, bool& muted,
                           double toDb, bool toMuted, juce::Button& muteButton)
    {
        if (std::abs (toDb - faderDb) <= 0.05 && toMuted == muted)
            return;
        SceneGlide g { kind, uid, i, faderDb, toDb, muted ? 0.0f : dbToGain (faderDb), toMuted ? 0.0f : dbToGain (toDb), toMuted };
        muted = toMuted;
        muteButton.setToggleState (toMuted, juce::dontSendNotification);
        sceneGlides.push_back (g);
    };

    for (int ri = 0; ri < (int) liveSet.rows.size(); ++ri)
    {
        auto& r = liveSet.rows[(size_t) ri];

        for (const auto& g : sc.groups)
            if (g.uid == r.uid && ri < rowViews.size())
                glideStrip (SceneGlide::Kind::group, r.uid, ri, r.faderDb, r.muted, g.faderDb, g.muted, rowViews[ri]->strip.muteButton);

        for (int si = 0; si < (int) r.slots.size(); ++si)
        {
            auto& s = r.slots[(size_t) si];
            for (const auto& c : sc.channels)
            {
                if (c.uid != s.uid || s.file == juce::File() || s.oneShot)
                    continue;

                const bool on = s.playing || s.loading;
                if (c.playing && ! on)
                {
                    // Starts at the scene's level, fading in over the morph (on the beat or bar if set).
                    s.volumeDb = c.volumeDb;
                    startLoop (ri, si, false, seconds);
                }
                else if (! c.playing && on)
                {
                    stopSlot (r, s, nextLoopStart(), seconds);
                }
                else if (on && std::abs (c.volumeDb - s.volumeDb) > 0.05)
                {
                    sceneGlides.push_back ({ SceneGlide::Kind::channel, s.uid, -1, s.volumeDb, c.volumeDb, 0.0f, 0.0f, false });
                }
            }
        }
    }

    for (const auto& st : sc.strips)
    {
        if (st.kind == LiveSet::Scene::Strip::input && st.index < (int) liveSet.inputs.size() && st.index < inputsView.inputStrips.size())
        {
            auto& in = liveSet.inputs[(size_t) st.index];
            glideStrip (SceneGlide::Kind::input, {}, st.index, in.faderDb, in.muted, st.faderDb, st.muted,
                        inputsView.inputStrips[st.index]->strip.muteButton);
        }
        else if (st.kind == LiveSet::Scene::Strip::instrument && st.index < (int) liveSet.instruments.size()
                 && st.index < inputsView.instrumentStrips.size())
        {
            auto& inst = liveSet.instruments[(size_t) st.index];
            glideStrip (SceneGlide::Kind::instrument, {}, st.index, inst.faderDb, inst.muted, st.faderDb, st.muted,
                        inputsView.instrumentStrips[st.index]->strip.muteButton);
        }
    }

    // Instruments change key straight away, so the next notes are in the scene's key.
    for (const auto& k : sc.keys)
    {
        if (! juce::isPositiveAndBelow (k.instrument, (int) liveSet.instruments.size()))
            continue;
        auto& inst = liveSet.instruments[(size_t) k.instrument];
        if (inst.padRoot != k.root || inst.padScale != k.scale || inst.padOctave != k.octave)
        {
            inst.padRoot = k.root;
            inst.padScale = juce::jlimit (0, PadLayout::numScales - 1, k.scale);
            inst.padOctave = k.octave;
            applyInstrument (k.instrument);
        }
    }
    updateFireLights();

    morphStartMs = juce::Time::getMillisecondCounterHiRes();
    morphMs = seconds * 1000.0;
    infoMessage = sc.name + (seconds > 0.0 ? " - morphing over " + juce::String (seconds, seconds < 10.0 ? 1 : 0) + " s" : juce::String());
    updateMorph();
    updateMidiLeds();
    repaint();
}

void MainComponent::stepScene (int delta)
{
    if (liveSet.scenes.empty())
        return;
    const int next = activeScene < 0 ? (delta > 0 ? 0 : (int) liveSet.scenes.size() - 1)
                                     : juce::jlimit (0, (int) liveSet.scenes.size() - 1, activeScene + delta);
    recallScene (next);
}

void MainComponent::updateMorph()
{
    if (sceneGlides.empty())
        return;

    // Smooth in and out, so levels don't lurch at the start or stop dead at the end.
    const double t = morphMs <= 0.0 ? 1.0 : juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - morphStartMs) / morphMs);
    const double eased = t * t * (3.0 - 2.0 * t);
    const bool done = t >= 1.0;

    for (const auto& g : sceneGlides)
    {
        const double db = g.fromDb + (g.toDb - g.fromDb) * eased;
        const float gain = g.fromGain + (g.toGain - g.fromGain) * (float) eased;

        switch (g.kind)
        {
            case SceneGlide::Kind::channel:
                for (auto& r : liveSet.rows)
                    for (auto& s : r.slots)
                        if (s.uid == g.uid)
                        {
                            s.volumeDb = db;
                            engineFor (r).setOwnerGain (s.id, dbToGain (db));
                        }
                break;

            case SceneGlide::Kind::group:
                if (g.index < (int) liveSet.rows.size() && liveSet.rows[(size_t) g.index].uid == g.uid)
                {
                    auto& r = liveSet.rows[(size_t) g.index];
                    r.faderDb = db;
                    rowViews[g.index]->strip.fader.setValue (db, juce::dontSendNotification);
                    if (done)
                    {
                        engineFor (r).setGain (dbToGain (g.toDb));
                        setRowMuted (g.index, g.endMuted);
                    }
                    else
                    {
                        engineFor (r).setMuted (false);
                        engineFor (r).setGain (gain);
                    }
                }
                break;

            case SceneGlide::Kind::input:
                if (g.index < (int) liveSet.inputs.size())
                {
                    liveSet.inputs[(size_t) g.index].faderDb = db;
                    if (done)
                        applyInput (g.index);
                    else
                    {
                        inputsView.inputStrips[g.index]->strip.fader.setValue (db, juce::dontSendNotification);
                        inputEngines[(size_t) g.index]->setMuted (false);
                        inputEngines[(size_t) g.index]->setGain (gain);
                    }
                }
                break;

            case SceneGlide::Kind::instrument:
                if (g.index < (int) liveSet.instruments.size())
                {
                    liveSet.instruments[(size_t) g.index].faderDb = db;
                    if (done)
                        applyInstrument (g.index);
                    else
                    {
                        inputsView.instrumentStrips[g.index]->strip.fader.setValue (db, juce::dontSendNotification);
                        instrumentEngines[(size_t) g.index]->setMuted (false);
                        instrumentEngines[(size_t) g.index]->setGain (gain);
                    }
                }
                break;
        }
    }

    if (done)
    {
        sceneGlides.clear();
        updateMidiLeds();
        saveSession();
    }
}

void MainComponent::setStageMode (bool on)
{
    stageMode = on;
    stageView.setVisible (on);
    if (on)
    {
        updateStageView();
        // Over the mixer, under the count-in and the alert banner.
        stageView.toFront (false);
        countInOverlay.toFront (false);
        alertBanner.toFront (false);
    }
    stageButton.setColour (juce::TextButton::buttonColourId, on ? Theme::accent.darker (0.3f) : Theme::raised);
    stageButton.setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::text);
    stageButton.setButtonText (on ? "MIXER" : "STAGE");
    settings.getUserSettings()->setValue ("stageMode", on);
    resized();
}

void MainComponent::updateStageView()
{
    if (! stageMode)
        return;

    StageView::Content c;
    for (const auto& sc : liveSet.scenes)
        c.scenes.add (sc.name);
    c.activeScene = activeScene;
    if (! sceneGlides.empty() && morphMs > 0.0)
        c.morphProgress = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - morphStartMs) / morphMs);
    c.outputMuted = outputMuted;

    const auto now = juce::Time::getMillisecondCounter();
    for (int ri = 0; ri < (int) liveSet.rows.size(); ++ri)
    {
        const auto& r = liveSet.rows[(size_t) ri];
        StageView::Group grp { r.name, Theme::rowColour (r.colour), r.muted, ri, {} };
        auto& engine = engineFor (r);
        for (int si = 0; si < (int) r.slots.size(); ++si)
        {
            const auto& s = r.slots[(size_t) si];
            if (s.file == juce::File())
                continue;

            using State = StageView::State;
            const auto cue = engine.cueFor (s.id);
            const State state = s.loading                       ? State::loading
                              : cue == LoopChannel::Cue::start   ? State::cuedStart
                              : cue == LoopChannel::Cue::stop    ? State::cuedStop
                              : s.playing                        ? (engine.isFadingIn (s.id) ? State::fadingIn : State::playing)
                              : s.flashUntil > now               ? State::playing
                                                                 : State::idle;
            grp.tiles.push_back ({ s.label.isNotEmpty() ? s.label : s.file.getFileNameWithoutExtension(), ri, si, state, s.oneShot });
        }
        if (! grp.tiles.empty())
            c.groups.push_back (std::move (grp));
    }

    for (int i = 0; i < (int) liveSet.inputs.size(); ++i)
        c.live.push_back ({ liveSet.inputs[(size_t) i].name, Theme::rowColour (4), liveSet.inputs[(size_t) i].muted, false, i });
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
        c.live.push_back ({ instrumentName (i), Theme::accent, liveSet.instruments[(size_t) i].muted, true, i });

    stageView.setContent (std::move (c));
}

void MainComponent::updateSceneBar()
{
    juce::StringArray names;
    for (const auto& sc : liveSet.scenes)
        names.add (sc.name);

    float progress = -1.0f;
    if (! sceneGlides.empty() && morphMs > 0.0)
        progress = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - morphStartMs) / morphMs);

    sceneBar.setScenes (names, activeScene, progress);
}

void MainComponent::showSceneMenu (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) liveSet.scenes.size()))
        return;
    const auto& sc = liveSet.scenes[(size_t) index];

    juce::PopupMenu m;
    m.addSectionHeader (sc.name);
    m.addItem (1, "Go to this scene");
    m.addItem (2, "Update it to the current mix");
    m.addItem (3, "Rename...");
    juce::PopupMenu morph;
    for (double s : { 0.0, 2.0, 4.0, 8.0, 15.0, 30.0, 60.0 })
        morph.addItem (100 + (int) s, s == 0.0 ? juce::String ("Instant") : juce::String ((int) s) + " s", true,
                       std::abs (sc.morphSeconds - s) < 0.01);
    m.addSubMenu ("Morph time: " + (sc.morphSeconds <= 0.0 ? juce::String ("instant") : juce::String (sc.morphSeconds, 0) + " s"), morph);
    m.addItem (4, "Assign MIDI button...");
    m.addSeparator();
    m.addItem (5, "Move left", index > 0);
    m.addItem (6, "Move right", index < (int) liveSet.scenes.size() - 1);
    m.addItem (7, "Delete");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, index] (int r)
    {
        if (! juce::isPositiveAndBelow (index, (int) liveSet.scenes.size()))
            return;
        auto& scene = liveSet.scenes[(size_t) index];

        if (r == 1) recallScene (index);
        if (r == 2)
        {
            auto updated = captureScene (scene.name);
            updated.uid = scene.uid;
            updated.morphSeconds = scene.morphSeconds;
            scene = std::move (updated);
            activeScene = index;
            infoMessage = scene.name + " updated to the current mix";
        }
        if (r == 3)
            askText ("Rename scene", scene.name, [this, index] (juce::String name)
            {
                if (juce::isPositiveAndBelow (index, (int) liveSet.scenes.size()) && name.trim().isNotEmpty())
                {
                    liveSet.scenes[(size_t) index].name = name.trim();
                    updateSceneBar();
                    saveSession();
                }
            });
        if (r >= 100 && r <= 160) scene.morphSeconds = r - 100;
        if (r == 4) showLearnMenu ("scene." + scene.uid, scene.name);
        if (r == 5 || r == 6)
        {
            const int other = index + (r == 5 ? -1 : 1);
            std::swap (liveSet.scenes[(size_t) index], liveSet.scenes[(size_t) other]);
            if (activeScene == index) activeScene = other;
            else if (activeScene == other) activeScene = index;
        }
        if (r == 7)
        {
            midiMap.clear ("scene." + scene.uid);
            liveSet.scenes.erase (liveSet.scenes.begin() + index);
            activeScene = activeScene == index ? -1 : activeScene > index ? activeScene - 1 : activeScene;
        }

        updateSceneBar();
        saveSession();
        repaint();
    });
}

void MainComponent::showSceneBarMenu()
{
    juce::PopupMenu m;
    m.addItem (3, "Scenes keep changes made in them", true, liveSet.scenesKeepChanges);
    m.addSeparator();
    m.addItem (1, "Assign MIDI button: next scene");
    m.addItem (2, "Assign MIDI button: previous scene");
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
    {
        if (r == 3)
        {
            liveSet.scenesKeepChanges = ! liveSet.scenesKeepChanges;
            infoMessage = liveSet.scenesKeepChanges ? "Scenes now keep what you change in them when you move on"
                                                    : "Scenes now stay as saved - right-click one > Update to change it";
            saveSession();
            repaint();
        }
        if (r == 1) showLearnMenu ("scene.next", "Next scene");
        if (r == 2) showLearnMenu ("scene.prev", "Previous scene");
    });
}

void MainComponent::updateCountIn()
{
    // Counts the bar's beats as they come (from wherever in the bar REC was pressed), the last
    // one in red, then shows REC for the first beat of the recording.
    const auto state = looper.getState();
    const double samplesPerBeat = 60.0 / liveSet.bpm * currentSampleRate;
    const auto now = LoopPlayer::blockClock.load();
    const int perBar = liveSet.beatsPerBar;

    if (state == Looper::State::waiting)
    {
        const double fromAnchor = (double) (now - beatAnchor) / samplesPerBeat;
        const double whole = std::floor (fromAnchor);
        const int beatInBar = ((int) whole % perBar + perBar) % perBar + 1;
        const double left = (double) (looper.getStartTime() - roundTripLatency - now) / samplesPerBeat;
        const bool last = left <= 1.0;
        countInOverlay.show (juce::String (beatInBar), last ? "play on the next 1" : "COUNT-IN  -  REC to cancel",
                             (float) (fromAnchor - whole), last);
        return;
    }

    if (state == Looper::State::recording)
    {
        const double sinceStart = (double) (now - (looper.getTakeStart() - roundTripLatency)) / samplesPerBeat;
        if (sinceStart >= 0.0 && sinceStart < 1.0)
        {
            countInOverlay.show ("REC", "recording", (float) sinceStart, true);
            return;
        }
    }

    countInOverlay.hide();
}

double MainComponent::samplesPerBar() const
{
    return liveSet.beatsPerBar * 60.0 / liveSet.bpm * currentSampleRate;
}

void MainComponent::applyTimeSignature()
{
    FxChain::beatsPerBar = liveSet.beatsPerBar;
    FxChain::beatUnit = liveSet.beatUnit;
    beatIndicator.setTimeSignature (liveSet.beatsPerBar, liveSet.beatUnit);
}

void MainComponent::setTimeSignature (int beats, int unit)
{
    liveSet.beatsPerBar = beats;
    liveSet.beatUnit = unit;
    applyTimeSignature();
    infoMessage = "Time signature " + juce::String (beats) + "/" + juce::String (unit)
                + (unit == 8 ? " - the BPM counts eighth notes" : juce::String());
    saveSession();
    repaint();
}

void MainComponent::showTimeSignatureMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Time signature");
    int id = 1;
    for (const auto& ts : LiveSet::timeSignatures)
        m.addItem (id++, juce::String (ts.beats) + "/" + juce::String (ts.unit), true,
                   ts.beats == liveSet.beatsPerBar && ts.unit == liveSet.beatUnit);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&beatIndicator), [this] (int r)
    {
        if (r >= 1 && r <= (int) std::size (LiveSet::timeSignatures))
        {
            const auto ts = LiveSet::timeSignatures[(size_t) r - 1];
            setTimeSignature (ts.beats, ts.unit);
        }
    });
}

void MainComponent::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // A long pause starts a fresh count.
    if (! tapTimes.empty() && now - tapTimes.back() > 2000.0)
        tapTimes.clear();

    tapTimes.push_back (now);
    if (tapTimes.size() > 6)
        tapTimes.erase (tapTimes.begin());

    // The latest tap is a beat 1, so the beat lights line up with the taps.
    beatAnchor = LoopPlayer::blockClock.load();

    if (tapTimes.size() >= 2)
    {
        const double averageMs = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
        setBpm (std::round (60000.0 / averageMs));   // tapping is only ever this accurate
    }
}

double MainComponent::currentBeatPosition (bool& recording) const
{
    const auto state = looper.getState();
    const bool taking = state == Looper::State::recording || state == Looper::State::finishing;
    recording = taking || state == Looper::State::waiting;   // the count-in shows red too

    // While recording, beats count from where the performer played the first note
    // (the input reaches the looper a round trip after the sound they played along to).
    const double samplesPerBeat = 60.0 / liveSet.bpm * currentSampleRate;
    const auto from = taking ? looper.getTakeStart() - roundTripLatency : beatAnchor;
    return (double) juce::jmax ((juce::int64) 0, LoopPlayer::blockClock.load() - from) / samplesPerBeat;
}

void MainComponent::setZoom (float scale)
{
    scale = juce::jlimit (0.6f, 1.6f, std::round (scale * 10.0f) / 10.0f);
    juce::Desktop::getInstance().setGlobalScaleFactor (scale);
    settings.getUserSettings()->setValue ("uiScale", (double) scale);
    infoMessage = "Zoom " + juce::String (juce::roundToInt (scale * 100.0f)) + "%";
    repaint();
}

void MainComponent::newSet()
{
    stopAll (1.0);
    liveSet.file = juce::File();
    loadSet (*LiveSet::makeDefault().toXml ({}), {});
    updateFireLights();
}

void MainComponent::openSet()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open a set (or a set packed into one file)",
                                                       liveSet.getFolder().getParentDirectory(),
                                                       juce::String ("*") + LiveSet::fileExtension + ";*" + LiveSet::packExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (! file.existsAsFile())
                                      return;

                                  if (file.hasFileExtension (LiveSet::packExtension))
                                  {
                                      openPack (file);
                                      return;
                                  }

                                  auto xml = juce::XmlDocument::parse (file);
                                  if (xml == nullptr || ! xml->hasTagName ("LiveRigSet"))
                                  {
                                      errorMessage = file.getFileName() + " is not an Airloom set";
                                      repaint();
                                      return;
                                  }

                                  stopAll (1.0);
                                  liveSet.file = file;
                                  loadSet (*xml, file.getParentDirectory());
                              });
}

void MainComponent::saveSet (bool asNewFolder)
{
    auto saveInto = [this] (const juce::File& folder)
    {
        int external = 0;
        for (auto& f : liveSet.allFiles())
            if (f.existsAsFile() && ! f.isAChildOf (folder))
                ++external;

        if (external == 0)
        {
            saveSetTo (folder, false);
            return;
        }

        askChoice ("Save set",
                   juce::String (external) + (external == 1 ? " file" : " files") + " will be gathered into\n"
                       + folder.getFullPathName() + "\n\nCopy them (the originals stay where they are), or move them?",
                   { "Copy files", "Move files", "Cancel" },
                   [this, folder] (int r)
                   {
                       if (r == 1) saveSetTo (folder, false);
                       if (r == 2) saveSetTo (folder, true);
                   });
    };

    if (! asNewFolder && liveSet.file.existsAsFile())
    {
        saveInto (liveSet.getFolder());
        return;
    }

    const auto startDir = liveSet.file.existsAsFile() ? liveSet.getFolder().getParentDirectory()
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Name the new set folder",
                                                       startDir.getChildFile (liveSet.file.existsAsFile() ? liveSet.getName() + " copy"
                                                                                                          : "My Set"));

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [saveInto] (const juce::FileChooser& fc)
                              {
                                  const auto chosen = fc.getResult();
                                  if (chosen != juce::File())
                                      saveInto (chosen.getParentDirectory().getChildFile (chosen.getFileNameWithoutExtension()));
                              });
}

void MainComponent::saveSetTo (const juce::File& folder, bool moveFiles)
{
    if (! folder.createDirectory())
    {
        errorMessage = "Could not create " + folder.getFullPathName();
        repaint();
        return;
    }

    captureInstrumentStates();

    // One subfolder per row, plus Samples for anything not on a channel.
    std::map<juce::String, juce::File> moved;
    juce::StringArray failed;

    auto place = [&] (const juce::File& f, const juce::File& destFolder)
    {
        if (! f.existsAsFile() || f.isAChildOf (folder) || moved.count (f.getFullPathName()) > 0)
            return;

        destFolder.createDirectory();
        const auto dest = destFolder.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), true);

        if (moveFiles ? f.moveFileTo (dest) : f.copyFileTo (dest))
            moved[f.getFullPathName()] = dest;
        else
            failed.add (f.getFileName());
    };

    for (auto& r : liveSet.rows)
        for (auto& s : r.slots)
            place (s.file, folder.getChildFile (juce::File::createLegalFileName (r.name)));

    for (auto& f : liveSet.pool)
        place (f, folder.getChildFile ("Samples"));

    liveSet.relocate (moved);

    // Loaded clips stay valid; just file them (and their smoothed versions) under the new paths.
    for (auto& [from, to] : moved)
    {
        std::vector<juce::String> keys;
        for (auto& [key, clip] : clipCache)
            if (key == from || key.startsWith (from + "|"))
                keys.push_back (key);

        for (auto& key : keys)
        {
            auto node = clipCache.extract (key);
            clipCache[to.getFullPathName() + key.substring (from.length())] = node.mapped();
        }
    }

    liveSet.file = folder.getChildFile (juce::File::createLegalFileName (folder.getFileName()) + LiveSet::fileExtension);

    if (! liveSet.toXml (folder)->writeTo (liveSet.file))
    {
        errorMessage = "Could not write " + liveSet.file.getFullPathName();
    }
    else
    {
        errorMessage = failed.isEmpty() ? juce::String()
                                        : "Could not " + juce::String (moveFiles ? "move" : "copy") + ": " + failed.joinIntoString (", ");
        infoMessage = "Saved " + liveSet.getName()
                    + (moved.empty() ? juce::String()
                                     : " - " + juce::String ((int) moved.size()) + (moveFiles ? " files moved" : " files copied"))
                    + " into " + folder.getFullPathName();
    }

    pool.setFiles (liveSet.pool);
    saveSession();
    repaint();
}

void MainComponent::exportPack()
{
    const auto name = liveSet.file.existsAsFile() ? liveSet.getName() : juce::String ("My Set");
    fileChooser = std::make_unique<juce::FileChooser> ("Export the set and its samples as one file",
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                           .getChildFile (name + LiveSet::packExtension),
                                                       juce::String ("*") + LiveSet::packExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (const auto chosen = fc.getResult(); chosen != juce::File())
                                      writePack (chosen.withFileExtension (LiveSet::packExtension));
                              });
}

void MainComponent::writePack (const juce::File& target)
{
    captureInstrumentStates();

    // Lay the pack out like a saved set folder: one folder per group, Samples for the
    // rest. Each sample gets a unique path inside the pack; nothing is copied yet.
    const auto setName = juce::File::createLegalFileName (target.getFileNameWithoutExtension());
    const auto staging = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("LiveRigPack-" + juce::Uuid().toDashedString());
    staging.createDirectory();   // only so paths can be written relative to it

    std::map<juce::String, juce::File> placed;          // original path -> where it sits in the pack
    std::vector<std::pair<juce::File, juce::String>> entries;
    std::set<juce::String> used;
    int missing = 0;

    auto place = [&] (const juce::File& f, const juce::String& folder)
    {
        if (f == juce::File() || placed.count (f.getFullPathName()) > 0)
            return;
        if (! f.existsAsFile())
        {
            ++missing;
            return;
        }

        auto name = f.getFileName();
        for (int n = 2; used.count ((folder + "/" + name).toLowerCase()) > 0; ++n)
            name = f.getFileNameWithoutExtension() + " (" + juce::String (n) + ")" + f.getFileExtension();

        const auto path = folder + "/" + name;
        used.insert (path.toLowerCase());
        placed[f.getFullPathName()] = staging.getChildFile (path);
        entries.push_back ({ f, setName + "/" + path });
    };

    for (auto& r : liveSet.rows)
        for (auto& s : r.slots)
            place (s.file, juce::File::createLegalFileName (r.name));
    for (auto& f : liveSet.pool)
        place (f, "Samples");

    LiveSet packed = liveSet;
    packed.relocate (placed);
    const auto xmlText = packed.toXml (staging)->toString();
    staging.deleteRecursively();

    // Plug-ins can't travel in the pack (they need installing, often with a licence), so
    // list what this set uses, to install on the other computer.
    juce::StringArray needed;
    auto note = [&needed] (const juce::String& pluginXml, const juce::String& usedFor)
    {
        juce::PluginDescription d;
        if (auto xml = juce::parseXML (pluginXml); xml != nullptr && d.loadFromXml (*xml))
            needed.addIfNotAlreadyThere (d.name + "  by " + d.manufacturerName
                                         + (d.version.isNotEmpty() ? "  (version " + d.version + ")" : juce::String())
                                         + "  -  " + usedFor);
    };
    for (auto& inst : liveSet.instruments)
        note (inst.pluginXml, "instrument");
    for (auto& in : liveSet.inputs)
        note (in.insertXml, "effect on " + in.name);
    for (auto& r : liveSet.rows)
        note (r.insert.xml, "effect on " + r.name);
    note (liveSet.masterInsert.xml, "effect on the master");

    const auto pluginNote = needed.isEmpty()
        ? juce::String ("This set doesn't use any plug-ins.\n")
        : "Install these plug-ins on the other computer (from their makers, with your licence where needed).\n"
          "Their settings are saved in the set and come back once they're installed.\n\n"
              + needed.joinIntoString ("\n") + "\n";

    packProgress = std::make_shared<std::atomic<double>> (0.0);
    infoMessage = "Exporting " + juce::String ((int) entries.size()) + " samples...";
    repaint();

    juce::Component::SafePointer<MainComponent> safe (this);
    auto progress = packProgress;

    loader.addJob ([safe, progress, target, entries, xmlText, pluginNote, setName, missing]
    {
        juce::ZipFile::Builder zip;
        for (auto& [file, path] : entries)
            zip.addFile (file, 0, path);   // audio barely compresses; storing is quick
        zip.addEntry (new juce::MemoryInputStream (xmlText.toRawUTF8(), xmlText.getNumBytesAsUTF8(), true), 6,
                      setName + "/" + setName + LiveSet::fileExtension, juce::Time::getCurrentTime());
        zip.addEntry (new juce::MemoryInputStream (pluginNote.toRawUTF8(), pluginNote.getNumBytesAsUTF8(), true), 6,
                      setName + "/Plug-ins needed.txt", juce::Time::getCurrentTime());

        // Write beside the target first, so a half-written pack never replaces a good one.
        const auto temp = target.getSiblingFile (target.getFileName() + ".part");
        bool ok = false;
        {
            juce::FileOutputStream out (temp);
            if (out.openedOk())
            {
                out.truncate();
                double fraction = 0.0;
                ok = zip.writeToStream (out, &fraction);
                progress->store (1.0);
            }
        }
        ok = ok && temp.moveFileTo (target);
        if (! ok)
            temp.deleteFile();

        const auto size = target.getSize();
        juce::MessageManager::callAsync ([safe, ok, target, missing, size, count = (int) entries.size()]
        {
            if (auto* self = safe.getComponent())
            {
                self->packProgress = nullptr;
                if (ok)
                {
                    self->infoMessage = "Exported " + juce::String (count) + " samples to " + target.getFileName()
                                      + " (" + juce::File::descriptionOfSizeInBytes (size) + ")"
                                      + (missing > 0 ? " - " + juce::String (missing) + " missing samples left out" : juce::String())
                                      + ". Plug-ins themselves must be installed on the other computer.";
                    target.revealToUser();
                }
                else
                {
                    self->errorMessage = "Couldn't write " + target.getFullPathName();
                }
                self->repaint();
            }
        });
    });
}

void MainComponent::openPack (const juce::File& pack)
{
    // Unpack into Music\Airloom Sets\<name>, never over an existing folder.
    const auto destination = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile ("Airloom Sets")
                                 .getChildFile (pack.getFileNameWithoutExtension())
                                 .getNonexistentSibling (true);
    destination.createDirectory();

    packProgress = std::make_shared<std::atomic<double>> (0.0);
    infoMessage = "Unpacking " + pack.getFileName() + "...";
    repaint();

    juce::Component::SafePointer<MainComponent> safe (this);
    loader.addJob ([safe, pack, destination]
    {
        juce::ZipFile zip (pack);
        const bool ok = zip.getNumEntries() > 0 && zip.uncompressTo (destination, true).wasOk();

        // The set file is the .liverig inside (in a folder named after the set).
        juce::File setFile;
        if (ok)
            for (auto& f : destination.findChildFiles (juce::File::findFiles, true, juce::String ("*") + LiveSet::fileExtension))
                setFile = f;

        juce::MessageManager::callAsync ([safe, ok, setFile, destination, pack]
        {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;

            self->packProgress = nullptr;
            auto xml = setFile.existsAsFile() ? juce::XmlDocument::parse (setFile) : nullptr;

            if (! ok || xml == nullptr || ! xml->hasTagName ("LiveRigSet"))
            {
                self->errorMessage = pack.getFileName() + " isn't an Airloom pack (or couldn't be unpacked)";
                self->repaint();
                return;
            }

            self->stopAll (1.0);
            self->liveSet.file = setFile;
            self->loadSet (*xml, setFile.getParentDirectory());
            self->infoMessage = "Opened " + self->liveSet.getName() + " - unpacked into " + setFile.getParentDirectory().getFullPathName();
            self->repaint();
        });
    });
}

void MainComponent::loadSet (const juce::XmlElement& xml, const juce::File& baseFolder)
{
    restoring = true;
    errorMessage.clear();
    infoMessage.clear();

    for (auto& e : engines)
        e->stopAll (0.5);

    // Unload the previous set's group and master plug-ins.
    ++insertGeneration;
    insertWindows.clear();
    masterInsert.setPlugin (nullptr);
    for (auto& e : engines)
        e->getInsert().setPlugin (nullptr);

    // Unload the previous set's instruments; any loads still in flight are ignored.
    ++instrumentGeneration;
    ++inputGeneration;
    for (int i = 0; i < LiveSet::maxInstruments; ++i)
    {
        closeEditor (i);
        instrumentEngines[(size_t) i]->takePlugin();
        instrumentLoading[(size_t) i] = false;
    }
    for (int i = 0; i < LiveSet::maxInputs; ++i)
    {
        inputPluginWindows[(size_t) i].reset();
        inputEngines[(size_t) i]->setInsert (nullptr);
    }

    const auto file = liveSet.file;
    liveSet.loadFromXml (xml, baseFolder);
    liveSet.file = file;
    applySetMidi();

    for (size_t i = 0; i < liveSet.rows.size(); ++i)
        liveSet.rows[i].engine = (int) i;

    masterStrip.fader.setValue (liveSet.masterDb);
    masterTarget = dbToGain (liveSet.masterDb);
    fadeTimeSlider.setValue (liveSet.fadeSeconds);
    loopStartBox.setSelectedId ((int) liveSet.loopStart + 1, juce::dontSendNotification);
    bpmSlider.setValue (liveSet.bpm, juce::dontSendNotification);
    looperBar.lengthBox.setSelectedId ((int) liveSet.loopLength + 1, juce::dontSendNotification);
    looper.stopOverdub();
    dubTargetId = overdubSlotId = -1;
    activeScene = -1;
    sceneGlides.clear();
    firstLoopLength = 0;
    FxChain::tempoBpm = liveSet.bpm;
    applyTimeSignature();
    pool.setFiles (liveSet.pool);

    for (int i = 0; i < (int) liveSet.inputs.size(); ++i)
    {
        applyInput (i);

        juce::PluginDescription description;
        const auto& in = liveSet.inputs[(size_t) i];
        if (auto pluginXml = juce::parseXML (in.insertXml); pluginXml != nullptr && description.loadFromXml (*pluginXml))
            loadInputPlugin (i, description, in.insertState);
    }

    updateSourceStrips();
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
    {
        applyInstrument (i);

        juce::PluginDescription description;
        if (auto pluginXml = juce::parseXML (liveSet.instruments[(size_t) i].pluginXml); pluginXml != nullptr && description.loadFromXml (*pluginXml))
            loadInstrumentPlugin (i, description, liveSet.instruments[(size_t) i].state);
    }

    spaceReverb.setSettings (liveSet.space);
    auto loadSlot = [this] (const juce::String& key, const LiveSet::PluginSlot& slot)
    {
        juce::PluginDescription description;
        if (auto pluginXml = juce::parseXML (slot.xml); pluginXml != nullptr && description.loadFromXml (*pluginXml))
            loadInsertPlugin (key, description, slot.state);
    };
    for (auto& r : liveSet.rows)
        loadSlot ("grp." + r.uid, r.insert);
    loadSlot ("master", liveSet.masterInsert);

    rebuildRows();

    int missing = 0;
    for (auto& f : liveSet.allFiles())
        if (! f.existsAsFile())
            ++missing;
    if (missing > 0)
        errorMessage = juce::String (missing) + (missing == 1 ? " file in this set was" : " files in this set were") + " not found";

    restoring = false;
    saveSession();
    preloadAll();
    repaint();
}

void MainComponent::saveSession()
{
    if (restoring)
        return;

    auto* props = settings.getUserSettings();
    props->setValue ("session", liveSet.toXml (liveSet.getFolder()).get());
    props->setValue ("setFile", liveSet.file.getFullPathName());
}

//==============================================================================
void MainComponent::rebuildRows()
{
    rowViews.clear();
    takeover.clear();   // fader positions belong to rows, which may have moved

    for (int i = 0; i < (int) liveSet.rows.size(); ++i)
    {
        auto& r = liveSet.rows[(size_t) i];
        auto* view = rowViews.add (new RowView());
        view->index = i;
        rowsContent.addAndMakeVisible (view);

        const auto faderTarget = "grp." + r.uid + ".fader";
        const auto muteTarget = "grp." + r.uid + ".mute";
        auto& strip = view->strip;
        strip.fader.setValue (r.faderDb, juce::dontSendNotification);
        engineFor (r).setGain (dbToGain (r.faderDb));
        strip.fader.onValueChange = [this, i]
        {
            auto& row = liveSet.rows[(size_t) i];
            row.faderDb = rowViews[i]->strip.fader.getValue();
            engineFor (row).setGain (dbToGain (row.faderDb));
            saveSession();
        };
        strip.fader.onDragStart = [this, faderTarget] { takeover[faderTarget].locked = false; };
        strip.fader.onRightClick = [this, faderTarget, i] { showLearnMenu (faderTarget, liveSet.rows[(size_t) i].name + " fader"); };

        strip.muteButton.onClick = [this, i] { setRowMuted (i, rowViews[i]->strip.muteButton.getToggleState()); };
        strip.muteButton.onRightClick = [this, muteTarget, i] { showLearnMenu (muteTarget, liveSet.rows[(size_t) i].name + " mute"); };
        strip.muteButton.setToggleState (r.muted, juce::dontSendNotification);
        engineFor (r).setMuted (r.muted);
        engineFor (r).getFx().setSettings (r.fx);

        for (auto& s : r.slots)
            if (s.fx.isActive())
                engineFor (r).setOwnerFx (s.id, s.fx);

        view->onChannelFx = [this, i] (int s)
        {
            auto& row = liveSet.rows[(size_t) i];
            auto& slot = row.slots[(size_t) s];
            auto* card = rowViews[i]->getCard (s);
            if (card == nullptr)
                return;

            const auto title = slot.label.isNotEmpty() ? slot.label : slot.file.getFileNameWithoutExtension();
            showFxPanel (*card, title, slot.fx, Theme::rowColour (row.colour), [this, id = slot.id] (const FxSettings& fx)
            {
                auto [rw, sl] = liveSet.findSlot (id);
                if (sl == nullptr)
                    return;

                if (! engineFor (*rw).setOwnerFx (id, fx))
                {
                    errorMessage = "Up to " + juce::String (LoopChannel::numChannelFx) + " channels per row can have their own effects - "
                                   "turn them off on another channel in " + rw->name;
                    repaint();
                    return;
                }

                sl->fx = fx;
                updateRowViews();
                saveSession();
            }, "ch." + slot.uid);
        };

        view->onFx = [this, i]
        {
            const auto& row = liveSet.rows[(size_t) i];
            showFxPanel (*rowViews[i], row.name, row.fx, Theme::rowColour (row.colour), [this, i] (const FxSettings& fx)
            {
                if (i < (int) liveSet.rows.size())
                {
                    auto& rw = liveSet.rows[(size_t) i];
                    rw.fx = fx;
                    engineFor (rw).getFx().setSettings (fx);
                    updateRowViews();
                    saveSession();
                }
            }, "grp." + row.uid);
        };

        view->onPlay = [this, i] (int s) { playPressed (i, s); };
        view->onCrossfade = [this, i] (int s) { crossfadePressed (i, s); };
        view->onMenu = [this, i] (int s) { showChannelMenu (i, s); };
        view->onHeaderMenu = [this, i] { showRowMenu (i); };
        view->onAddChannel = [this, i] { addChannel (i); };
        view->onLayoutChanged = [this] { layoutRows(); };
        view->resizer.onColumnsChanged = [this, i] (int columns)
        {
            if (i < (int) liveSet.rows.size())
            {
                liveSet.rows[(size_t) i].columns = columns;
                updateRowViews();
                layoutRows();
                saveSession();
            }
        };
        view->onChannelLearn = [this, i] (int s, const juce::String& control)
        {
            const auto& slot = liveSet.rows[(size_t) i].slots[(size_t) s];
            const auto name = slot.label.isNotEmpty() ? slot.label : slot.file.getFileNameWithoutExtension();
            const auto what = control == "play" ? (slot.oneShot ? "play" : "play / stop") : control == "xf" ? "crossfade" : "volume";
            showLearnMenu ("ch." + slot.uid + "." + control, name + " " + what);
        };

        view->onVolume = [this, i] (int s, double db)
        {
            auto& row = liveSet.rows[(size_t) i];
            auto& slot = row.slots[(size_t) s];
            slot.volumeDb = db;
            engineFor (row).setOwnerGain (slot.id, dbToGain (db));
            saveSession();
        };

        disableKeyboardFocus (*view);
    }

    addRowButton.setEnabled ((int) liveSet.rows.size() < LiveSet::maxRows);
    layoutRows();
    updateRowViews();
    updateMidiLeds();
}

void MainComponent::layoutRows()
{
    const auto bounds = arrangeGroups();
    int bottom = 0;

    for (const auto& [item, r] : bounds)
    {
        juce::Component* view = item < 0 ? static_cast<juce::Component*> (&inputsView) : rowViews[item];
        view->setBounds (r);
        bottom = juce::jmax (bottom, r.getBottom());
    }

    inputsView.setFixedColumns (liveSet.inputsColumns > 0);
    rowsContent.setSize (rowsViewport.getWidth() - rowsViewport.getScrollBarThickness() - 2, bottom + 40);
}

std::map<int, juce::Rectangle<int>> MainComponent::arrangeGroups (int moving, juce::Point<int> movingTo) const
{
    // Groups (row indices, -1 for the Inputs + Looper) sit where they were put, then
    // float up until they rest under whatever is above them, like a dashboard grid:
    // gaps beside a tall group fill in, and a group dropped onto another pushes it down.
    const int width = rowsViewport.getWidth() - rowsViewport.getScrollBarThickness() - 2;
    const int rows = juce::jmin (rowViews.size(), (int) liveSet.rows.size());

    struct Group { int item; juce::Point<int> want; juce::Point<int> size; };
    std::vector<Group> groups;

    auto sizeOf = [&] (int item)
    {
        return item < 0 ? InputsView::preferredSize (inputsView.getNumStrips(), width, liveSet.inputsColumns)
                        : RowView::preferredSize ((int) liveSet.rows[(size_t) item].slots.size(), width,
                                                  liveSet.rows[(size_t) item].columns);
    };

    groups.push_back ({ -1, liveSet.inputsPosition, sizeOf (-1) });
    for (int i = 0; i < rows; ++i)
        groups.push_back ({ i, liveSet.rows[(size_t) i].position, sizeOf (i) });

    // A set that has never been arranged: inputs first, then the rows, left to right, wrapping.
    if (std::all_of (groups.begin(), groups.end(), [] (const Group& g) { return g.want.x < 0; }))
    {
        int x = 0, y = 0, lineHeight = 0;
        for (auto& g : groups)
        {
            if (x > 0 && x + g.size.x > width)
            {
                x = 0;
                y += lineHeight + rowGap;
                lineHeight = 0;
            }
            g.want = { x, y };
            x += g.size.x + rowGap;
            lineHeight = juce::jmax (lineHeight, g.size.y);
        }
    }

    for (auto& g : groups)
    {
        if (g.item == moving)
            g.want = movingTo;
        else if (g.want.x < 0)
            g.want = { 0, 1 << 24 };   // new groups go to the bottom
    }

    // Top to bottom, then left to right; a group being dropped wins a tie.
    std::stable_sort (groups.begin(), groups.end(), [moving] (const Group& a, const Group& b)
    {
        if (a.want.y != b.want.y) return a.want.y < b.want.y;
        if ((a.item == moving) != (b.item == moving)) return a.item == moving;
        return a.want.x < b.want.x;
    });

    std::map<int, juce::Rectangle<int>> placed;
    for (const auto& g : groups)
    {
        const int x = juce::jlimit (0, juce::jmax (0, width - g.size.x), g.want.x);
        int y = 0;
        for (const auto& [other, r] : placed)
            if (x < r.getRight() + rowGap && r.getX() < x + g.size.x + rowGap)
                y = juce::jmax (y, r.getBottom() + rowGap);

        placed[g.item] = { x, y, g.size.x, g.size.y };
    }

    return placed;
}

juce::Point<int> MainComponent::droppedGroupPosition (const SourceDetails& details, int& item) const
{
    // Where the dragged group's top-left corner would go, snapped to line up with the others.
    const auto text = details.description.toString();
    juce::Point<int> grab;

    if (text == InputsView::dragId)
    {
        item = -1;
        grab = inputsView.grabOffset;
    }
    else
    {
        item = text.fromFirstOccurrenceOf (RowView::dragPrefix, false, false).getIntValue();
        if (juce::isPositiveAndBelow (item, rowViews.size()))
            grab = rowViews[item]->grabOffset;
    }

    auto pos = rowsContent.getLocalPoint (this, details.localPosition) - grab;

    int snapX = 0, best = std::abs (pos.x);
    auto consider = [&] (int candidate)
    {
        if (std::abs (pos.x - candidate) < best) { best = std::abs (pos.x - candidate); snapX = candidate; }
    };
    for (auto* c : rowsContent.getChildren())
    {
        const bool self = (item < 0 && c == &inputsView) || (item >= 0 && item < rowViews.size() && c == rowViews[item]);
        if (! self)
        {
            consider (c->getX());
            consider (c->getRight() + rowGap);
        }
    }
    if (best < 24)
        pos.x = snapX;

    return { juce::jmax (0, pos.x), juce::jmax (0, pos.y) };
}

void MainComponent::updateRowViews()
{
    const auto now = juce::Time::getMillisecondCounter();

    for (int i = 0; i < rowViews.size() && i < (int) liveSet.rows.size(); ++i)
    {
        const auto& r = liveSet.rows[(size_t) i];
        auto& engine = *engines[(size_t) r.engine];
        rowViews[i]->update (r, [&engine] (int id) { return engine.isFadingIn (id); },
                             [&engine] (int id)
                             {
                                 using State = ChannelCard::State;
                                 const auto cue = engine.cueFor (id);
                                 return cue == LoopChannel::Cue::start ? State::cuedStart
                                      : cue == LoopChannel::Cue::stop  ? State::cuedStop : State::idle;
                             }, now);

        for (int s = 0; s < (int) r.slots.size(); ++s)
            if (auto* card = rowViews[i]->getChannelCard (s))
            {
                const auto prefix = "ch." + r.slots[(size_t) s].uid + ".";
                card->setMidiAssigned (midiMap.get (prefix + "play").isValid() || midiMap.get (prefix + "xf").isValid()
                                       || midiMap.get (prefix + "vol").isValid());
            }
    }
}

void MainComponent::addRow (std::function<void (int)> then)
{
    if ((int) liveSet.rows.size() >= LiveSet::maxRows)
    {
        errorMessage = "Up to " + juce::String (LiveSet::maxRows) + " rows, one per MidiMix fader";
        repaint();
        return;
    }

    askText ("Name the new row", "Row " + juce::String ((int) liveSet.rows.size() + 1), [this, then] (juce::String name)
    {
        const int engine = freeEngine();
        if (engine < 0)
            return;

        auto r = LiveSet::makeRow (name, false);
        r.engine = engine;
        r.colour = (int) liveSet.rows.size() % Theme::numRowColours;
        liveSet.rows.push_back (std::move (r));
        rebuildRows();
        saveSession();

        if (then)
            then ((int) liveSet.rows.size() - 1);
    });
}

int MainComponent::freeEngine() const
{
    for (int e = 0; e < (int) engines.size(); ++e)
        if (std::none_of (liveSet.rows.begin(), liveSet.rows.end(), [e] (const Row& r) { return r.engine == e; }))
            return e;
    return -1;
}

void MainComponent::showRowMenu (int row)
{
    const auto& r = liveSet.rows[(size_t) row];

    juce::PopupMenu m;
    m.addSectionHeader (r.name);
    m.addItem (1, "Rename...");
    m.addItem (2, "Add a channel");
    m.addItem (3, "New channels play once", true, r.newSlotsOneShot);

    juce::PopupMenu colours;
    for (int c = 0; c < Theme::numRowColours; ++c)
        colours.addColouredItem (50 + c, Theme::rowColourNames[c], Theme::rowColours[c], true, r.colour == c);
    m.addSubMenu ("Colour", colours);

    m.addItem (7, r.insert.isEmpty() ? juce::String ("Plug-in effect...") : "Plug-in effect: " + r.insert.name + "...");
    m.addSeparator();
    m.addItem (6, "Remove row");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rowViews[row]), [this, row] (int choice)
    {
        if (row >= (int) liveSet.rows.size())
            return;

        auto& rw = liveSet.rows[(size_t) row];

        if (choice == 1)
            askText ("Rename row", rw.name, [this, row] (juce::String name)
            {
                if (row < (int) liveSet.rows.size())
                {
                    liveSet.rows[(size_t) row].name = name;
                    updateRowViews();
                    pool.refreshUsage();
                    saveSession();
                }
            });

        if (choice == 2) addChannel (row);
        if (choice == 7) showInsertMenu ("grp." + rw.uid, *rowViews[row]);

        if (choice == 3)
        {
            rw.newSlotsOneShot = ! rw.newSlotsOneShot;
            saveSession();
        }

        if (choice >= 50 && choice < 50 + Theme::numRowColours)
        {
            rw.colour = choice - 50;
            updateRowViews();
            saveSession();
        }

        if (choice == 6)
        {
            const bool hasSamples = std::any_of (rw.slots.begin(), rw.slots.end(),
                                                 [] (const Slot& s) { return s.file != juce::File(); });
            if (! hasSamples)
                removeRow (row);
            else
                askChoice ("Remove row", "Remove " + rw.name + " and its channels? The samples stay in the Samples list.",
                           { "Remove", "Cancel" }, [this, row] (int c) { if (c == 1) removeRow (row); });
        }
    });
}

void MainComponent::removeRow (int row)
{
    if (row >= (int) liveSet.rows.size())
        return;

    auto& engine = engineFor (liveSet.rows[(size_t) row]);
    engine.stopAll (0.5);
    engine.getFx().setSettings ({});
    insertWindows.erase ("grp." + liveSet.rows[(size_t) row].uid);
    engine.getInsert().setPlugin (nullptr);
    liveSet.rows.erase (liveSet.rows.begin() + row);
    rebuildRows();
    pool.refreshUsage();
    saveSession();
}

void MainComponent::addChannel (int row)
{
    auto& r = liveSet.rows[(size_t) row];
    r.slots.push_back ({});
    r.slots.back().oneShot = r.newSlotsOneShot;
    updateRowViews();
    saveSession();
}

void MainComponent::showChannelMenu (int row, int slot)
{
    const auto& r = liveSet.rows[(size_t) row];
    const auto& s = r.slots[(size_t) slot];
    const bool hasFile = s.file != juce::File();

    juce::PopupMenu m;
    m.addSectionHeader (hasFile ? (s.label.isNotEmpty() ? s.label : s.file.getFileNameWithoutExtension()) : juce::String ("Empty channel"));
    m.addItem (1, hasFile ? "Replace sample..." : "Load sample...");
    m.addItem (2, "Rename...", hasFile);
    m.addSeparator();
    m.addItem (3, "Loop", true, ! s.oneShot);
    m.addItem (4, "Play once", true, s.oneShot);

    // For samples that weren't cut as loops: blend the end into the start.
    juce::PopupMenu smooth;
    for (int i = 0; i < (int) std::size (smoothOptions); ++i)
        smooth.addItem (30 + i, smoothOptions[i] == 0.0 ? juce::String ("Off (sample is already a loop)")
                                                        : "Blend over " + juce::String (smoothOptions[i]) + " s",
                        true, std::abs (s.smoothSeconds - smoothOptions[i]) < 0.01);
    m.addSubMenu ("Smooth loop point", smooth, hasFile && ! s.oneShot);
    m.addItem (50, "Play reversed", hasFile, s.reverse);

    // Tempo: from a BPM in the file name, or from the loop's length as a whole number of bars.
    std::vector<double> tempoChoices;
    juce::PopupMenu tempo;
    if (hasFile && ! s.oneShot)
    {
        if (const double named = TempoGuess::fromName (s.file.getFileNameWithoutExtension()); named > 0.0)
        {
            tempoChoices.push_back (named);
            tempo.addItem (200, "From the name: " + juce::String (named, 2) + " BPM");
            tempo.addSeparator();
        }

        if (auto cached = clipCache.find (clipKey (s)); cached != clipCache.end())
        {
            const double seconds = cached->second->data.getNumSamples() / cached->second->sampleRate;
            for (auto& option : TempoGuess::fromLength (seconds, liveSet.beatsPerBar))
            {
                tempo.addItem (200 + (int) tempoChoices.size(),
                               juce::String (option.bars) + (option.bars == 1 ? " bar" : " bars") + " = " + juce::String (option.bpm, 2) + " BPM",
                               true, std::abs (option.bpm - liveSet.bpm) < 0.01);
                tempoChoices.push_back (option.bpm);
            }
        }
        else
        {
            tempo.addItem (-1, "Loading the loop - try again in a moment", false);
        }
    }
    m.addSubMenu ("Set BPM from this loop", tempo, ! tempoChoices.empty());
    m.addSeparator();
    m.addItem (40, "Overdub on this loop", hasFile && ! s.oneShot && s.playing);
    m.addItem (41, "Undo last overdub", s.undoClip != nullptr);
    m.addSeparator();
    m.addItem (5, "Move left", slot > 0);
    m.addItem (6, "Move right", slot < (int) r.slots.size() - 1);
    m.addItem (7, "Clear", hasFile);
    m.addItem (8, "Remove channel");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, row, slot, tempoChoices] (int choice)
    {
        if (row >= (int) liveSet.rows.size() || slot >= (int) liveSet.rows[(size_t) row].slots.size())
            return;

        auto& rw = liveSet.rows[(size_t) row];
        auto& sl = rw.slots[(size_t) slot];

        if (choice >= 200 && choice < 200 + (int) tempoChoices.size())
        {
            setBpm (tempoChoices[(size_t) (choice - 200)]);

            // If it's playing, its start is beat 1: line the beat lights up with it.
            if (sl.playing && sl.anchor >= 0)
                beatAnchor = sl.anchor;

            infoMessage = "BPM " + juce::String (liveSet.bpm, 2) + " from "
                        + (sl.label.isNotEmpty() ? sl.label : sl.file.getFileNameWithoutExtension())
                        + (sl.playing ? " - beat 1 lined up with it" : " - play it to line up beat 1");
            repaint();
            return;
        }

        if (choice == 1)
            browseForFiles ("Choose a sample", false, [this, row, slot] (juce::Array<juce::File> files)
            {
                if (row < (int) liveSet.rows.size())
                    assignToChannels (row, slot, files);
            });

        if (choice == 2)
            askText ("Rename channel", sl.label.isNotEmpty() ? sl.label : sl.file.getFileNameWithoutExtension(),
                     [this, id = sl.id] (juce::String name)
                     {
                         if (auto [r, s] = liveSet.findSlot (id); s != nullptr)
                         {
                             s->label = name;
                             updateRowViews();
                             saveSession();
                         }
                     });

        if (choice == 3 || choice == 4)
        {
            if (choice == 4)
                stopSlot (rw, sl);
            sl.oneShot = choice == 4;
            updateRowViews();
            saveSession();
        }

        if (choice >= 30 && choice < 30 + (int) std::size (smoothOptions))
        {
            sl.smoothSeconds = smoothOptions[choice - 30];
            withClip (sl, nullptr);

            // If it's playing, crossfade from the old version to the new one.
            if (sl.playing)
            {
                stopSlot (rw, sl);
                startLoop (row, slot, false);
            }

            updateRowViews();
            saveSession();
        }

        if (choice == 50)
        {
            if (sl.id == overdubSlotId)
                stopOverdubbing();

            sl.reverse = ! sl.reverse;

            // An edited loop flips in memory; anything else loads its reversed version.
            if (sl.liveClip != nullptr)
            {
                sl.liveClip = makeReversed (*sl.liveClip);
                if (sl.undoClip != nullptr)
                    sl.undoClip = makeReversed (*sl.undoClip);
                clipCache[clipKey (sl)] = sl.liveClip;
            }

            // Short crossfade to the flipped sound, keeping the loop's timing.
            withClip (sl, [this, id = sl.id] (LoopClipPtr clip)
            {
                auto [r, s] = liveSet.findSlot (id);
                if (clip != nullptr && s != nullptr && ! s->oneShot)
                    swapPlayingClip (*r, *s, clip);
            });

            infoMessage = (sl.label.isNotEmpty() ? sl.label : sl.file.getFileNameWithoutExtension())
                        + (sl.reverse ? " plays reversed" : " plays forwards");
            updateRowViews();
            saveSession();
        }

        if (choice == 40 || choice == 41)
        {
            if (looper.getState() == Looper::State::overdubbing)
                stopOverdubbing();

            dubTargetId = sl.id;
            if (choice == 40) dubPressed();
            else              undoPressed();
        }

        if (choice == 5 || choice == 6) moveChannel (row, slot, choice == 5 ? -1 : 1);

        if (choice == 7)
        {
            stopSlot (rw, sl);
            engineFor (rw).setOwnerFx (sl.id, {});
            sl = {};
            sl.oneShot = rw.newSlotsOneShot;
            updateRowViews();
            pool.refreshUsage();
            saveSession();
        }

        if (choice == 8) removeChannel (row, slot);
    });
}

void MainComponent::removeChannel (int row, int slot)
{
    auto& r = liveSet.rows[(size_t) row];
    stopSlot (r, r.slots[(size_t) slot]);
    engineFor (r).setOwnerFx (r.slots[(size_t) slot].id, {});
    r.slots.erase (r.slots.begin() + slot);
    updateRowViews();
    pool.refreshUsage();
    saveSession();
}

void MainComponent::moveChannel (int row, int slot, int delta)
{
    auto& r = liveSet.rows[(size_t) row];
    std::swap (r.slots[(size_t) slot], r.slots[(size_t) (slot + delta)]);
    updateRowViews();
    saveSession();
}

void MainComponent::assignToChannels (int row, int firstSlot, const juce::Array<juce::File>& files)
{
    auto& r = liveSet.rows[(size_t) row];

    for (int i = 0; i < files.size(); ++i)
    {
        const auto index = (size_t) (firstSlot + i);
        while (r.slots.size() <= index)
        {
            r.slots.push_back ({});
            r.slots.back().oneShot = r.newSlotsOneShot;
        }

        auto& s = r.slots[index];
        if (s.file == juce::File())
            s.oneShot = r.newSlotsOneShot;
        s.file = files[i];
        s.label = files[i].getFileNameWithoutExtension();
        withClip (s, nullptr);   // load now so the first press is instant
    }

    addToPool (files);
    updateRowViews();
    saveSession();
}

void MainComponent::appendToRow (int row, const juce::Array<juce::File>& files)
{
    // Fill empty channels first, then add new ones.
    auto& r = liveSet.rows[(size_t) row];
    size_t next = 0;

    for (auto& f : files)
    {
        while (next < r.slots.size() && r.slots[next].file != juce::File())
            ++next;

        assignToChannels (row, (int) next, { f });
        ++next;
    }
}

void MainComponent::setRowMuted (int row, bool muted)
{
    auto& r = liveSet.rows[(size_t) row];
    r.muted = muted;
    engineFor (r).setMuted (muted);
    rowViews[row]->strip.muteButton.setToggleState (muted, juce::dontSendNotification);
    updateMidiLeds();
    saveSession();
}

//==============================================================================
void MainComponent::setupInputStrip (int i, SourceStrip& s)
{
    const auto faderTarget = "inputFader." + juce::String (i);

    s.strip.fader.onValueChange = [this, i]
    {
        auto& in = liveSet.inputs[(size_t) i];
        in.faderDb = inputsView.inputStrips[i]->strip.fader.getValue();
        inputEngines[(size_t) i]->setGain (dbToGain (in.faderDb));
        saveSession();
    };
    s.strip.fader.onDragStart = [this, faderTarget] { takeover[faderTarget].locked = false; };
    s.strip.fader.onRightClick = [this, faderTarget, i] { showLearnMenu (faderTarget, liveSet.inputs[(size_t) i].name + " fader"); };

    s.strip.muteButton.onClick = [this, i]
    {
        liveSet.inputs[(size_t) i].muted = inputsView.inputStrips[i]->strip.muteButton.getToggleState();
        applyInput (i);
        saveSession();
    };
    s.strip.muteButton.onRightClick = [this, i] { showLearnMenu ("inputMute." + juce::String (i), liveSet.inputs[(size_t) i].name + " mute"); };

    s.onFirstButton = [this, i]
    {
        liveSet.inputs[(size_t) i].monitor = ! liveSet.inputs[(size_t) i].monitor;
        applyInput (i);
        saveSession();
    };

    s.onFx = [this, i]
    {
        const auto& in = liveSet.inputs[(size_t) i];
        showFxPanel (*inputsView.inputStrips[i], in.name, in.fx, InputsView::colour, [this, i] (const FxSettings& fx)
        {
            liveSet.inputs[(size_t) i].fx = fx;
            applyInput (i);
            saveSession();
        }, "in." + juce::String (i));
    };

    s.onMenu = [this, i] { showInputMenu (i); };
    s.onArm = [this, i] { setArmed (i, ! liveSet.inputs[(size_t) i].armed); };
    disableKeyboardFocus (s);
}

void MainComponent::setupInputs()
{
    looperBar.recButton.onClick = [this] { recPressed(); };
    looperBar.recButton.onRightClick = [this] { showLearnMenu ("rec", "Record"); };
    looperBar.dubButton.onClick = [this] { dubPressed(); };
    looperBar.dubButton.onRightClick = [this] { showLearnMenu ("dub", "Overdub"); };
    looperBar.undoButton.onClick = [this] { undoPressed(); };
    looperBar.undoButton.onRightClick = [this] { showLearnMenu ("undo", "Undo overdub"); };
    looperBar.lengthBox.onChange = [this]
    {
        liveSet.loopLength = (LiveSet::LoopLength) (looperBar.lengthBox.getSelectedId() - 1);
        saveSession();
    };

    disableKeyboardFocus (inputsView);
    addAndMakeVisible (looperBar);
}

void MainComponent::applyInput (int i)
{
    const auto& in = liveSet.inputs[(size_t) i];
    auto& engine = *inputEngines[(size_t) i];
    engine.setGain (dbToGain (in.faderDb));
    engine.setMuted (in.muted);
    engine.setMonitor (in.monitor);
    engine.setSource (in.channel, in.stereo);
    engine.setArmed (in.armed);
    engine.setRecordWithFx (in.recordWithFx);
    engine.getFx().setSettings (in.fx);
    activeInputs = (int) liveSet.inputs.size();

    updateSourceStrips();   // makes the strip if the input is new
    auto& strip = inputsView.inputStrips[i]->strip;
    strip.fader.setValue (in.faderDb, juce::dontSendNotification);
    strip.muteButton.setToggleState (in.muted, juce::dontSendNotification);
}

void MainComponent::addInput()
{
    if ((int) liveSet.inputs.size() >= LiveSet::maxInputs)
        return;

    // It takes the first interface input no other strip uses - or, with all of them taken
    // (a two-input interface), input 1 again: the same mic through different effects.
    int available = LiveSet::maxDeviceInputs;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        available = juce::jlimit (1, LiveSet::maxDeviceInputs, device->getInputChannelNames().size());

    juce::Array<int> used;
    for (const auto& in : liveSet.inputs)
    {
        used.add (in.channel);
        if (in.stereo)
            used.add (in.channel + 1);
    }
    int channel = 0;
    while (used.contains (channel))
        ++channel;

    LiveSet::Input in;
    in.channel = channel < available ? channel : 0;
    in.name = "Input " + juce::String ((int) liveSet.inputs.size() + 1);
    liveSet.inputs.push_back (in);

    const int i = (int) liveSet.inputs.size() - 1;
    inputPluginWindows[(size_t) i].reset();
    inputEngines[(size_t) i]->setInsert (nullptr);
    applyInput (i);
    saveSession();

    infoMessage = in.name + " added on interface input " + juce::String (in.channel + 1) + " - click its name to pick another";
    repaint();
}

void MainComponent::removeInput (int i)
{
    const int count = (int) liveSet.inputs.size();
    if (i >= count || count <= 1)
        return;

    if (liveSet.inputs[(size_t) i].armed)
        setArmed (i, false);

    ++inputGeneration;          // indices are about to shift
    activeInputs = count - 1;   // the audio thread lets go of the last engine first

    for (int j = i; j < count; ++j)
        inputPluginWindows[(size_t) j].reset();

    // Slide the later inputs down one place, plug-ins and all.
    inputEngines[(size_t) i]->setInsert (nullptr);
    for (int j = i + 1; j < count; ++j)
        inputEngines[(size_t) (j - 1)]->setInsert (inputEngines[(size_t) j]->setInsert (nullptr));

    // Their MIDI moves down with them.
    for (const juce::String prefix : { "inputFader.", "inputMute.", "arm." })
    {
        midiMap.clear (prefix + juce::String (i));
        for (int j = i + 1; j < count; ++j)
            if (const auto binding = midiMap.get (prefix + juce::String (j)); binding.isValid())
            {
                midiMap.clear (prefix + juce::String (j));
                midiMap.bind (prefix + juce::String (j - 1), binding);
            }
    }
    // Effect knobs too ("fx.in.<j>.<knob>"): note them all first, then move them.
    std::vector<std::pair<juce::String, MidiBinding>> moving;
    for (auto& t : midiMap.getTargets())
        if (t.startsWith ("fx.in."))
        {
            const int j = t.fromFirstOccurrenceOf ("fx.in.", false, false).upToFirstOccurrenceOf (".", false, false).getIntValue();
            if (j >= i)
            {
                if (j > i)
                    moving.push_back ({ "fx.in." + juce::String (j - 1) + "." + t.fromLastOccurrenceOf (".", false, false), midiMap.get (t) });
                midiMap.clear (t);
            }
        }
    for (auto& [t, binding] : moving)
        midiMap.bind (t, binding);
    saveMidiMap();

    liveSet.inputs.erase (liveSet.inputs.begin() + i);
    for (int j = i; j < (int) liveSet.inputs.size(); ++j)
        applyInput (j);
    updateSourceStrips();
    updateMidiLeds();
    saveSession();
}

void MainComponent::showInputMenu (int i)
{
    const auto& in = liveSet.inputs[(size_t) i];

    // The interface's inputs, by name where the driver gives one: singly, or in stereo pairs.
    juce::StringArray names;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        names = device->getInputChannelNames();
    names.removeRange (LiveSet::maxDeviceInputs, names.size());
    // An input the set was saved with on a bigger interface stays listed, so it can be seen and changed.
    const int available = juce::jmax (2, names.size(), in.channel + (in.stereo ? 2 : 1));

    juce::PopupMenu mono, pairs;
    for (int c = 0; c < available; ++c)
    {
        const auto driverName = c < names.size() ? names[c] : juce::String ("not on this interface");
        const auto label = "Input " + juce::String (c + 1);
        mono.addItem (100 + c, driverName.isNotEmpty() && driverName != label ? label + "  (" + driverName + ")" : label,
                      true, ! in.stereo && in.channel == c);
    }
    for (int c = 0; c + 1 < available; c += 2)
        pairs.addItem (200 + c, "Inputs " + juce::String (c + 1) + " + " + juce::String (c + 2), true, in.stereo && in.channel == c);

    juce::PopupMenu m;
    m.addSectionHeader (in.name);
    m.addItem (1, "Rename...");
    m.addSeparator();
    m.addSubMenu ("Mono input" + (in.stereo ? juce::String() : ":  " + juce::String (in.channel + 1)), mono);
    m.addSubMenu ("Stereo pair" + (in.stereo ? ":  " + juce::String (in.channel + 1) + " + " + juce::String (in.channel + 2) : juce::String()), pairs);
    m.addSeparator();

    // An effect plug-in that runs first: pitch correction, vocoder, anything.
    auto* plugin = inputEngines[(size_t) i]->getInsert();
    const auto effects = effectTypes();
    juce::PopupMenu pluginMenu;
    juce::KnownPluginList::addToMenu (pluginMenu, effects, juce::KnownPluginList::sortByManufacturer,
                                      plugin != nullptr ? plugin->getPluginDescription().createIdentifierString() : juce::String());
    if (effects.isEmpty())
        pluginMenu.addItem (-1, "No effect plug-ins found yet - scan first", false);
    m.addSubMenu (plugin != nullptr ? "Plug-in effect: " + plugin->getName() : juce::String ("Plug-in effect"), pluginMenu);
    m.addItem (30, plugin != nullptr ? "Open " + plugin->getName() : juce::String ("Open plug-in"), plugin != nullptr);
    m.addItem (31, "Remove plug-in", plugin != nullptr || in.insertXml.isNotEmpty());
    m.addItem (32, "Scan for plug-ins...");
    m.addSeparator();

    m.addItem (20, "Record loops with effects", true, in.recordWithFx);
    m.addItem (21, "Record loops dry (change effects later)", true, ! in.recordWithFx);
    m.addSeparator();
    m.addItem (40, "Remove input", liveSet.inputs.size() > 1);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (inputsView.inputStrips[i]), [this, i, effects] (int r)
    {
        if (r == 0)
            return;

        if (const int chosen = juce::KnownPluginList::getIndexChosenByMenu (effects, r); chosen >= 0)
        {
            loadInputPlugin (i, effects[chosen], {});
            return;
        }

        if (r == 30) openInputPluginEditor (i);
        if (r == 31) removeInputPlugin (i);
        if (r == 32) showPluginScanner();

        if (r == 1)
            askText ("Rename input", liveSet.inputs[(size_t) i].name, [this, i] (juce::String name)
            {
                liveSet.inputs[(size_t) i].name = name;
                applyInput (i);
                saveSession();
            });

        if (r >= 100 && r < 300)
        {
            liveSet.inputs[(size_t) i].channel = r % 100;
            liveSet.inputs[(size_t) i].stereo = r >= 200;
            applyInput (i);
            saveSession();
        }

        if (r == 40)
            removeInput (i);

        if (r == 20 || r == 21)
        {
            liveSet.inputs[(size_t) i].recordWithFx = r == 20;
            applyInput (i);
            saveSession();
        }
    });
}

//==============================================================================
void MainComponent::updateSourceStrips()
{
    inputsView.update (liveSet, [this] (int i) { return instrumentName (i); },
                                [this] (int i) { return instrumentDetail (i); });
}

juce::String MainComponent::instrumentName (int i) const
{
    // Your own name for it, else the preset loaded in it, else the plug-in's name.
    const auto& inst = liveSet.instruments[(size_t) i];
    if (auto* plugin = instrumentEngines[(size_t) i]->getPlugin())
        return inst.label.isNotEmpty() ? inst.label : presetNames[(size_t) i].isNotEmpty() ? presetNames[(size_t) i] : plugin->getName();

    if (instrumentLoading[(size_t) i])
        return "Loading " + inst.name + "...";
    return inst.name.isNotEmpty() ? "Missing: " + inst.name : juce::String ("Empty");
}

juce::String MainComponent::instrumentDetail (int i) const
{
    const auto& inst = liveSet.instruments[(size_t) i];
    PadLayout layout;
    layout.root = inst.padRoot;
    layout.scale = inst.padScale;
    layout.octave = inst.padOctave;

    // When the strip shows a preset or your name, the plug-in's name leads the line under it.
    juce::String plugin;
    if (auto* p = instrumentEngines[(size_t) i]->getPlugin(); p != nullptr && instrumentName (i) != p->getName())
        plugin = p->getName() + "  -  ";

    const auto source = midiSourceFor (i);
    const auto from = inst.midiSource.isEmpty() ? juce::String ("All keys")
                    : inst.midiSource + (inst.midiSourceId.isNotEmpty() && ! source.connected ? " (unplugged)" : "");
    return plugin + from + "  -  " + layout.describe() + (inst.recordWithFx ? juce::String() : "  -  dry");
}

juce::Array<juce::PluginDescription> MainComponent::instrumentTypes() const
{
    juce::Array<juce::PluginDescription> types;
    for (auto& t : knownPlugins.getTypes())
        if (t.isInstrument)
            types.add (t);
    return types;
}

void MainComponent::setupInstrumentStrip (int i, SourceStrip& s)
{
    const auto faderTarget = "instFader." + juce::String (i);

    s.strip.fader.onValueChange = [this, i]
    {
        auto& inst = liveSet.instruments[(size_t) i];
        inst.faderDb = inputsView.instrumentStrips[i]->strip.fader.getValue();
        instrumentEngines[(size_t) i]->setGain (dbToGain (inst.faderDb));
        saveSession();
    };
    s.strip.fader.onDragStart = [this, faderTarget] { takeover[faderTarget].locked = false; };
    s.strip.fader.onRightClick = [this, faderTarget, i] { showLearnMenu (faderTarget, instrumentName (i) + " fader"); };

    s.strip.muteButton.onClick = [this, i]
    {
        liveSet.instruments[(size_t) i].muted = inputsView.instrumentStrips[i]->strip.muteButton.getToggleState();
        applyInstrument (i);
        saveSession();
    };
    s.strip.muteButton.onRightClick = [this, i] { showLearnMenu ("instMute." + juce::String (i), instrumentName (i) + " mute"); };

    s.onFirstButton = [this, i] { openEditor (i); };
    s.onArm = [this, i]
    {
        liveSet.instruments[(size_t) i].armed = ! liveSet.instruments[(size_t) i].armed;
        applyInstrument (i);
    };
    s.onFx = [this, i]
    {
        showFxPanel (*inputsView.instrumentStrips[i], instrumentName (i), liveSet.instruments[(size_t) i].fx,
                     InputsView::instrumentColour, [this, i] (const FxSettings& fx)
                     {
                         if (i < (int) liveSet.instruments.size())
                         {
                             liveSet.instruments[(size_t) i].fx = fx;
                             applyInstrument (i);
                             saveSession();
                         }
                     }, "inst." + juce::String (i));
    };
    s.onMenu = [this, i] { showInstrumentMenu (i); };

    juce::StringArray keyLabels;
    for (auto& k : fluteKeys)
        keyLabels.add (k.label);
    s.setChoices (keyLabels);
    s.onChoice = [this, i] (int k)
    {
        auto& inst = liveSet.instruments[(size_t) i];
        inst.padRoot = fluteKeys[k].root;
        inst.padScale = fluteKeys[k].scale;
        applyInstrument (i);
        PadLayout keys;
        keys.root = inst.padRoot;
        keys.scale = inst.padScale;
        infoMessage = "Fire pads: " + keys.describe() + "  -  teal pads are " + PadLayout::noteNames[keys.root]
                    + ", amber pads are " + keys.describeRelative().upToFirstOccurrenceOf (" ", false, false)
                    + " (the relative " + keys.describeRelative().fromFirstOccurrenceOf (" ", false, false) + ")";
        saveSession();
    };

    disableKeyboardFocus (s);
}

void MainComponent::applyInstrument (int i)
{
    const auto& inst = liveSet.instruments[(size_t) i];
    auto& engine = *instrumentEngines[(size_t) i];
    engine.setGain (dbToGain (inst.faderDb));
    engine.setMuted (inst.muted);
    engine.setArmed (inst.armed);
    engine.setRecordWithFx (inst.recordWithFx);
    updateMidiSources();
    engine.getFx().setSettings (inst.fx);

    PadLayout layout;
    layout.root = inst.padRoot;
    layout.scale = inst.padScale;
    layout.octave = inst.padOctave;
    engine.setLayout (layout);

    updateSourceStrips();

    if (i < inputsView.instrumentStrips.size())
    {
        auto& s = *inputsView.instrumentStrips[i];
        s.strip.fader.setValue (inst.faderDb, juce::dontSendNotification);
        s.strip.muteButton.setToggleState (inst.muted, juce::dontSendNotification);

        int active = -1;
        for (int k = 0; k < (int) std::size (fluteKeys); ++k)
            if (fluteKeys[k].root == inst.padRoot && fluteKeys[k].scale == inst.padScale)
                active = k;
        s.setActiveChoice (active);
    }
    updateFireLights();
}

void MainComponent::addInstrument()
{
    if ((int) liveSet.instruments.size() >= LiveSet::maxInstruments)
        return;

    const auto types = instrumentTypes();
    if (types.isEmpty())
    {
        infoMessage = "No instruments found yet - scan for your VST3 plug-ins, then press + INSTRUMENT again";
        showPluginScanner();
        return;
    }

    juce::PopupMenu m;
    m.addSectionHeader ("Add an instrument");
    juce::KnownPluginList::addToMenu (m, types, juce::KnownPluginList::sortByManufacturer);
    m.addSeparator();
    m.addItem (1, "Scan for plug-ins...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputsView.addInstrumentButton), [this, types] (int r)
    {
        if (r == 1)
        {
            showPluginScanner();
            return;
        }

        const int chosen = juce::KnownPluginList::getIndexChosenByMenu (types, r);
        if (chosen < 0 || (int) liveSet.instruments.size() >= LiveSet::maxInstruments)
            return;

        liveSet.instruments.push_back ({});
        const int index = (int) liveSet.instruments.size() - 1;
        updateSourceStrips();
        applyInstrument (index);
        loadInstrumentPlugin (index, types[chosen], {});
        layoutRows();
    });
}

void MainComponent::loadInstrumentPlugin (int i, const juce::PluginDescription& description, const juce::String& state)
{
    auto& inst = liveSet.instruments[(size_t) i];
    if (auto xml = description.createXml())
        inst.pluginXml = xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    inst.name = description.name;
    instrumentLoading[(size_t) i] = true;
    updateSourceStrips();

    juce::Component::SafePointer<MainComponent> safe (this);
    const int generation = instrumentGeneration;

    pluginFormats.createPluginInstanceAsync (description, currentSampleRate, currentBlockSize,
        [safe, i, state, generation, name = description.name] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || generation != self->instrumentGeneration || i >= (int) self->liveSet.instruments.size())
                return;   // the set changed while this was loading

            self->instrumentLoading[(size_t) i] = false;

            if (instance == nullptr)
            {
                self->errorMessage = "Couldn't load " + name + (error.isNotEmpty() ? ": " + error : juce::String());
                self->updateSourceStrips();
                self->repaint();
                return;
            }

            if (state.isNotEmpty())
            {
                juce::MemoryBlock data;
                if (data.fromBase64Encoding (state))
                    instance->setStateInformation (data.getData(), (int) data.getSize());
            }

            self->closeEditor (i);
            auto previous = self->instrumentEngines[(size_t) i]->setPlugin (std::move (instance));
            previous.reset();

            self->presetNames[(size_t) i].clear();
            self->applyInstrument (i);
            self->refreshPresetName (i);
            self->infoMessage = name + " ready - play it from the Fire's pads or any keyboard";
            self->saveSession();
            self->repaint();
        });
}

void MainComponent::openEditor (int i)
{
    auto* plugin = instrumentEngines[(size_t) i]->getPlugin();
    if (plugin == nullptr)
        return;

    if (auto& window = editorWindows[(size_t) i]; window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    editorWindows[(size_t) i] = std::make_unique<PluginWindow> (*plugin, [safe, i]
    {
        // Closing from inside the window's own callback: finish the callback first.
        juce::MessageManager::callAsync ([safe, i] { if (auto* self = safe.getComponent()) self->closeEditor (i); });
    });
}

void MainComponent::closeEditor (int i)
{
    const bool wasOpen = editorWindows[(size_t) i] != nullptr;
    editorWindows[(size_t) i].reset();
    if (wasOpen)
        refreshPresetName (i);   // a new preset is usually picked in the window
}

void MainComponent::refreshPresetName (int i)
{
    if (! juce::isPositiveAndBelow (i, LiveSet::maxInstruments))
        return;

    auto* plugin = i < (int) liveSet.instruments.size() ? instrumentEngines[(size_t) i]->getPlugin() : nullptr;
    const auto name = plugin != nullptr ? PresetName::of (*plugin) : juce::String();
    if (name != presetNames[(size_t) i])
    {
        presetNames[(size_t) i] = name;
        if (i < (int) liveSet.instruments.size())
            applyInstrument (i);   // redraws its strip
    }
}

void MainComponent::removeInstrument (int i)
{
    if (i >= (int) liveSet.instruments.size())
        return;

    ++instrumentGeneration;   // indices are about to shift
    for (int j = i; j < LiveSet::maxInstruments; ++j)
        closeEditor (j);

    instrumentEngines[(size_t) i]->takePlugin();

    // Slide the later instruments down one place, plugins and all.
    for (int j = i + 1; j < (int) liveSet.instruments.size(); ++j)
    {
        instrumentEngines[(size_t) (j - 1)]->setPlugin (instrumentEngines[(size_t) j]->takePlugin());
        instrumentLoading[(size_t) (j - 1)] = instrumentLoading[(size_t) j];
    }
    instrumentLoading[(size_t) liveSet.instruments.size() - 1] = false;

    liveSet.instruments.erase (liveSet.instruments.begin() + i);
    for (int j = i; j + 1 < LiveSet::maxInstruments; ++j)
        presetNames[(size_t) j] = presetNames[(size_t) j + 1];
    presetNames.back().clear();
    updateSourceStrips();
    for (int j = 0; j < (int) liveSet.instruments.size(); ++j)
        applyInstrument (j);

    // Silence the engine that's now unused.
    instrumentEngines[liveSet.instruments.size()]->setArmed (false);
    updateFireLights();
    layoutRows();
    saveSession();
}

void MainComponent::showInstrumentMenu (int i)
{
    const auto& inst = liveSet.instruments[(size_t) i];
    auto* plugin = instrumentEngines[(size_t) i]->getPlugin();
    const auto types = instrumentTypes();

    juce::PopupMenu m;
    m.addSectionHeader (instrumentName (i));
    m.addItem (1, "Open editor", plugin != nullptr);

    juce::PopupMenu change;
    juce::KnownPluginList::addToMenu (change, types, juce::KnownPluginList::sortByManufacturer,
                                      plugin != nullptr ? plugin->getPluginDescription().createIdentifierString() : juce::String());
    m.addItem (3, "Rename...");
    if (inst.label.isNotEmpty())
        m.addItem (4, presetNames[(size_t) i].isNotEmpty() ? "Name it after its preset (" + presetNames[(size_t) i] + ")"
                                                           : "Use the plug-in's name (" + (plugin != nullptr ? plugin->getName() : inst.name) + ")");
    m.addSubMenu ("Change instrument", change, ! types.isEmpty());
    m.addItem (2, "Scan for plug-ins...");
    m.addSeparator();

    // Where its notes come from.
    juce::PopupMenu midiFrom;
    midiFrom.addItem (10, "All keyboards and the Fire", true, inst.midiSource.isEmpty());
    juce::Array<juce::MidiDeviceInfo> devices;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (! d.name.removeCharacters (" ").containsIgnoreCase ("MIDIMix"))
            devices.add (d);
    const auto source = midiSourceFor (i);
    for (int d = 0; d < devices.size(); ++d)
        midiFrom.addItem (11 + d, devices[d].name, true,
                          inst.midiSource.isNotEmpty() && InstrumentChannel::playsFrom (source, devices[d].name, devices[d].identifier));
    if (inst.midiSourceId.isNotEmpty() && ! source.connected)
        midiFrom.addItem (-2, inst.midiSource + " (not plugged in - plays the other " + InstrumentChannel::baseName (inst.midiSource) + ")",
                          false, true);
    m.addSubMenu ("Play from", midiFrom);

    // The Fire's pad keyboard.
    juce::PopupMenu root, scale, octave;
    for (int n = 0; n < 12; ++n)
        root.addItem (100 + n, PadLayout::noteNames[n], true, inst.padRoot == n);
    for (int s = 0; s < PadLayout::numScales; ++s)
        scale.addItem (120 + s, PadLayout::scaleNames[s], true, inst.padScale == s);
    for (int o = 1; o <= 6; ++o)
        octave.addItem (140 + o, "Octave " + juce::String (o) + "  (bottom-left = " + PadLayout::noteNames[inst.padRoot] + juce::String (o) + ")",
                        true, inst.padOctave == o);
    juce::PopupMenu fire;
    fire.addSubMenu ("Key", root);
    fire.addSubMenu ("Scale", scale);
    fire.addSubMenu ("Octave", octave);
    m.addSubMenu ("Fire pads: " + instrumentDetail (i).fromFirstOccurrenceOf ("-  ", false, false).upToFirstOccurrenceOf ("  -", false, false), fire);

    m.addSeparator();
    m.addItem (30, "Record loops with effects", true, inst.recordWithFx);
    m.addItem (31, "Record loops dry (change effects later)", true, ! inst.recordWithFx);
    m.addSeparator();
    m.addItem (40, "Remove instrument");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (inputsView.instrumentStrips[i]), [this, i, types, devices] (int r)
    {
        if (i >= (int) liveSet.instruments.size() || r == 0)
            return;

        auto& in = liveSet.instruments[(size_t) i];

        if (const int chosen = juce::KnownPluginList::getIndexChosenByMenu (types, r); chosen >= 0)
        {
            in.label = {};   // a name given to the old instrument doesn't fit the new one
            loadInstrumentPlugin (i, types[chosen], {});
            return;
        }

        if (r == 1) openEditor (i);
        if (r == 2) showPluginScanner();
        if (r == 3)
        {
            askText ("Rename instrument", instrumentName (i), [this, i] (juce::String name)
            {
                if (i >= (int) liveSet.instruments.size())
                    return;
                auto& inst = liveSet.instruments[(size_t) i];
                auto* plugin = instrumentEngines[(size_t) i]->getPlugin();
                name = name.trim();
                // Typing nothing, the plug-in's name or its preset's goes back to naming it automatically.
                inst.label = name.isEmpty() || (plugin != nullptr && name == plugin->getName()) || name == presetNames[(size_t) i]
                                 ? juce::String() : name;
                applyInstrument (i);
                saveSession();
            });
            return;
        }
        if (r == 4) in.label = {};
        if (r == 10) { in.midiSource = {}; in.midiSourceId = {}; }
        if (r >= 11 && r < 11 + devices.size()) { in.midiSource = devices[r - 11].name; in.midiSourceId = devices[r - 11].identifier; }
        if (r >= 100 && r < 112) in.padRoot = r - 100;
        if (r >= 120 && r < 120 + PadLayout::numScales) in.padScale = r - 120;
        if (r >= 141 && r <= 146) in.padOctave = r - 140;
        if (r == 30 || r == 31) in.recordWithFx = r == 30;

        if (r == 40)
        {
            removeInstrument (i);
            return;
        }

        applyInstrument (i);
        saveSession();
    });
}

void MainComponent::showPluginScanner()
{
    // The list remembers any plug-in that crashes while scanning and skips it next time.
    const auto crashFile = settings.getUserSettings()->getFile().getSiblingFile ("PluginScanCrashes.txt");
    auto* list = new juce::PluginListComponent (pluginFormats, knownPlugins, crashFile, settings.getUserSettings(), true);
    list->setSize (760, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Instruments - VST3 plug-ins";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();

    // First time: start scanning straight away.
    if (knownPlugins.getNumTypes() == 0)
        for (auto* format : pluginFormats.getFormats())
            if (format->canScanForPlugins())
                list->scanFor (*format);
}

void MainComponent::captureInstrumentStates()
{
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
    {
        if (auto* plugin = instrumentEngines[(size_t) i]->getPlugin())
        {
            juce::MemoryBlock data;
            plugin->getStateInformation (data);
            liveSet.instruments[(size_t) i].state = data.toBase64Encoding();
            if (const auto preset = PresetName::fromState (data); preset.isNotEmpty() && preset != presetNames[(size_t) i])
            {
                presetNames[(size_t) i] = preset;
                applyInstrument (i);
            }
        }
    }

    for (int i = 0; i < (int) liveSet.inputs.size(); ++i)
    {
        if (auto* plugin = inputEngines[(size_t) i]->getInsert())
        {
            juce::MemoryBlock data;
            plugin->getStateInformation (data);
            liveSet.inputs[(size_t) i].insertState = data.toBase64Encoding();
        }
    }

    auto capture = [] (PluginInsert& insert, LiveSet::PluginSlot& slot)
    {
        if (auto* plugin = insert.getPlugin())
        {
            juce::MemoryBlock data;
            plugin->getStateInformation (data);
            slot.state = data.toBase64Encoding();
        }
    };
    for (auto& r : liveSet.rows)
        if (r.engine >= 0)
            capture (engineFor (r).getInsert(), r.insert);
    capture (masterInsert, liveSet.masterInsert);

    lastStateCapture = juce::Time::getMillisecondCounter();
}

void MainComponent::updateFireLights (bool allOff)
{
    // Each Fire plays the first instrument listening to it; light that one's scale on its pads.
    for (auto& fire : fireOutputs)
    {
        if (! allOff && lightShowOf (fire).on)
            continue;   // showing the light show instead

        PadLayout chosen;
        const PadLayout* layout = ! allOff && currentKey (chosen, fire.name, fire.inputId) ? &chosen : nullptr;

        // Akai Fire pad colours: F0 47 7F 43 65 <length> then (pad, red, green, blue) per pad, 0-127 each.
        juce::MemoryBlock data;
        const juce::uint8 header[] = { 0x47, 0x7f, 0x43, 0x65, (juce::uint8) ((64 * 4) >> 7), (juce::uint8) ((64 * 4) & 0x7f) };
        data.append (header, sizeof (header));

        for (int pad = 0; pad < 64; ++pad)
        {
            juce::uint8 rgb[3] = { 0, 0, 0 };
            if (layout != nullptr)
            {
                if (layout->isRootPad (pad))              { rgb[0] = 20;  rgb[1] = 110; rgb[2] = 100; }   // teal: the key's root
                else if (layout->isRelativeRootPad (pad)) { rgb[0] = 120; rgb[1] = 55;  rgb[2] = 0;   }   // amber: relative major/minor root
                else                              { rgb[0] = 18;  rgb[1] = 18;  rgb[2] = 22;  }   // dim: other scale notes
            }

            const juce::uint8 entry[] = { (juce::uint8) pad, rgb[0], rgb[1], rgb[2] };
            data.append (entry, sizeof (entry));
        }

        fire.output->sendMessageNow (juce::MidiMessage::createSysExMessage (data.getData(), (int) data.getSize()));
    }
}

MainComponent::LightShow& MainComponent::lightShowFor (const FireOutput& fire)
{
    return fireLightShows[fire.inputId.isNotEmpty() ? fire.inputId : fire.name];
}

MainComponent::FireOutput* MainComponent::partnerOf (const FireOutput& fire)
{
    // The other Fire, sitting opposite this one (left/right, top/bottom).
    const int position = lightShowOf (fire).position;
    if (position == separate)
        return nullptr;
    const int opposite = position == left ? right : position == right ? left : position == top ? bottom : top;
    for (auto& other : fireOutputs)
        if (&other != &fire && lightShowOf (other).position == opposite)
            return &other;
    return nullptr;
}

MainComponent::LightShow MainComponent::lightShowOf (const FireOutput& fire) const
{
    const auto it = fireLightShows.find (fire.inputId.isNotEmpty() ? fire.inputId : fire.name);
    return it != fireLightShows.end() ? it->second : LightShow();
}

void MainComponent::sendFirePads (FireOutput& fire, const std::array<juce::Colour, 64>& pads)
{
    // Akai Fire pad colours: F0 47 7F 43 65 <length> then (pad, red, green, blue) per pad, 0-127 each.
    // Curved (power 1.8), since the pads are much brighter at low values than a screen.
    juce::MemoryBlock data;
    const juce::uint8 header[] = { 0x47, 0x7f, 0x43, 0x65, (juce::uint8) ((64 * 4) >> 7), (juce::uint8) ((64 * 4) & 0x7f) };
    data.append (header, sizeof (header));
    auto level = [] (float v) { return (juce::uint8) juce::jlimit (0, 127, juce::roundToInt (std::pow (v, 1.8f) * 127.0f)); };
    for (int pad = 0; pad < 64; ++pad)
    {
        const auto& c = pads[(size_t) pad];
        const juce::uint8 entry[] = { (juce::uint8) pad, level (c.getFloatRed()), level (c.getFloatGreen()), level (c.getFloatBlue()) };
        data.append (entry, sizeof (entry));
    }
    fire.output->sendMessageNow (juce::MidiMessage::createSysExMessage (data.getData(), (int) data.getSize()));
}

void MainComponent::updateFireLightShow()
{
    if (std::none_of (fireOutputs.begin(), fireOutputs.end(), [this] (const FireOutput& f) { return lightShowOf (f).on; }))
        return;

    bool counting = false;
    const double beats = currentBeatPosition (counting);
    fireVisualizer.update (beats, anyLoopPlaying() || looper.getState() != Looper::State::idle);

    // One picture per pattern, palette and shape in use, shared by every Fire showing it. Two
    // joined Fires get one picture across both (32 x 4 side by side, 16 x 8 stacked), each its part.
    std::map<std::tuple<int, int, int>, std::vector<juce::Colour>> frames;
    for (auto& fire : fireOutputs)
    {
        const auto show = lightShowOf (fire);
        if (! show.on)
            continue;

        const int shape = partnerOf (fire) != nullptr ? (show.position == left || show.position == right ? 1 : 2) : 0;
        const int width = shape == 1 ? 32 : 16, height = shape == 2 ? 8 : 4;
        const auto key = std::make_tuple (show.pattern, show.palette, shape);
        if (frames.find (key) == frames.end())
            frames[key] = fireVisualizer.render (show.pattern, show.palette, width, height);
        const auto& frame = frames[key];

        const int offsetX = show.position == right && shape == 1 ? 16 : 0;
        const int offsetY = show.position == bottom && shape == 2 ? 4 : 0;
        std::array<juce::Colour, 64> pads;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 16; ++c)
                pads[(size_t) (r * 16 + c)] = frame[(size_t) ((r + offsetY) * width + c + offsetX)];
        sendFirePads (fire, pads);
    }
}

//==============================================================================
juce::File MainComponent::recordingsFolder()
{
    return settings.getUserSettings()->getFile().getParentDirectory().getChildFile ("Recordings");
}

bool MainComponent::anyArmed() const
{
    return std::any_of (liveSet.inputs.begin(), liveSet.inputs.end(), [] (const LiveSet::Input& in) { return in.armed; })
        || std::any_of (liveSet.instruments.begin(), liveSet.instruments.end(), [] (const LiveSet::Instrument& in) { return in.armed; });
}

void MainComponent::setArmed (int input, bool armed)
{
    liveSet.inputs[(size_t) input].armed = armed;
    applyInput (input);
    updateMidiLeds();
}

void MainComponent::recPressed()
{
    const auto state = looper.getState();
    errorMessage.clear();

    if (state == Looper::State::recording)
    {
        // Work out what length to round to, if any.
        juce::int64 unit = 0;
        if (liveSet.loopLength == LiveSet::LoopLength::snapToBars)
            unit = (juce::int64) std::llround (samplesPerBar());
        else if (liveSet.loopLength == LiveSet::LoopLength::matchFirstLoop)
            unit = firstLoopLength;

        looper.stopRecording (unit);
        return;
    }

    if (state == Looper::State::finishing)
        return;   // already rounding off the last bar

    if (state == Looper::State::waiting)
    {
        looper.cancelStart();
        infoMessage = "Count-in called off";
        repaint();
        return;
    }

    if (state == Looper::State::overdubbing)
        stopOverdubbing();

    if (! anyArmed())
    {
        errorMessage = "Arm a source first: press ARM on an input or instrument";
        repaint();
        return;
    }

    // On the bar: the take starts a round trip after the bar line, when the note the
    // performer plays on "1" (in time with what they hear) reaches the looper.
    const auto barLine = nextRecordStart();
    if (! looper.startRecording (barLine < 0 ? -1 : barLine + roundTripLatency))
        errorMessage = "Still saving the last loop - press REC again";

    repaint();
}

bool MainComponent::currentKey (PadLayout& key, const juce::String& fireName, const juce::String& fireId) const
{
    // The key of the first instrument the Fire plays: the one being performed in.
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
    {
        const auto& inst = liveSet.instruments[(size_t) i];
        const bool fromThisFire = fireName.isEmpty() ? inst.midiSource.isEmpty() || inst.midiSource.containsIgnoreCase ("FIRE")
                                                     : InstrumentChannel::playsFrom (midiSourceFor (i), fireName, fireId);
        if (instrumentEngines[(size_t) i]->getPlugin() != nullptr && fromThisFire)
        {
            key.root = inst.padRoot;
            key.scale = inst.padScale;
            key.octave = inst.padOctave;
            return true;
        }
    }
    return false;
}

juce::String MainComponent::nextLoopLabel (const Row& row) const
{
    // Named after the key it was played in and numbered within that key, with "Loop"
    // first so it can't be mistaken for a sample in that key:
    // "Loop A minor 1", "Loop A minor 2", "Loop Bb major 1". "Loop 3" when there's no key.
    PadLayout key;
    const auto keyName = currentKey (key) ? key.keyName() : juce::String();
    const auto name = keyName.isEmpty() ? juce::String ("Loop") : "Loop " + keyName;

    int highest = 0;
    for (const auto& s : row.slots)
        if (s.file != juce::File() && s.label.startsWith (name + " ")
            && s.label.substring (name.length() + 1).containsOnly ("0123456789"))
            highest = juce::jmax (highest, s.label.substring (name.length() + 1).getIntValue());

    return name + " " + juce::String (highest + 1);
}

void MainComponent::collectFinishedTake()
{
    juce::AudioBuffer<float> audio;
    juce::int64 takeStart = 0;
    if (! looper.collectTake (audio, takeStart))
        return;

    const double sr = currentSampleRate;
    if (audio.getNumSamples() < (int) (sr * 0.25))
    {
        infoMessage = "That loop was too short to keep";
        return;
    }

    const int rowIndex = findOrCreateLiveLoopsRow();
    if (rowIndex < 0)
    {
        errorMessage = "No room for a loops group (8 groups max, one per MidiMix fader) - remove one to record";
        return;
    }

    auto clip = std::make_shared<LoopClip>();
    clip->sampleRate = sr;
    clip->data = std::move (audio);

    auto& row = liveSet.rows[(size_t) rowIndex];

    // The first empty channel in the row, or a new one.
    auto it = std::find_if (row.slots.begin(), row.slots.end(), [] (const Slot& s) { return s.file == juce::File(); });
    if (it == row.slots.end())
    {
        row.slots.push_back ({});
        it = row.slots.end() - 1;
    }

    auto& slot = *it;
    slot = {};
    slot.label = nextLoopLabel (row);
    slot.file = recordingsFolder().getNonexistentChildFile (slot.label + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".wav");
    clip->file = slot.file;
    slot.liveClip = clip;

    clipCache[clipKey (slot)] = clip;
    cacheBytes += clipBytes (clip);
    clip->file = slot.file;

    if (firstLoopLength == 0)
        firstLoopLength = clip->data.getNumSamples();

    // The first loop of free length, with nothing else playing, sets the tempo: the BPM
    // becomes the one (of those a whole number of bars fits) nearest the current one.
    juce::String tempoNote;
    const bool firstLoop = ! anyLoopPlaying();
    if (firstLoop && liveSet.loopLength == LiveSet::LoopLength::free)
    {
        const auto fit = TempoGuess::nearest (clip->data.getNumSamples() / sr, liveSet.bpm, liveSet.beatsPerBar);
        if (fit.bars > 0 && std::abs (fit.bpm - liveSet.bpm) >= 0.01)
        {
            setBpm (std::round (fit.bpm * 100.0) / 100.0);
            tempoNote = " - BPM " + juce::String (liveSet.bpm, 2) + " to match its " + juce::String (fit.bars)
                      + (fit.bars == 1 ? " bar" : " bars");
        }
    }

    // Carry straight on: phase-lock playback to when the performer actually played
    // the first sample (the input arrived a round trip later).
    slot.anchor = takeStart - roundTripLatency;
    if (firstLoop)
        beatAnchor = slot.anchor;   // the beat grid starts with the first loop
    if (engineFor (row).startLoop (slot.id, clip, 0.01, dbToGain (slot.volumeDb), slot.anchor))
        slot.playing = true;

    dubTargetId = slot.id;
    persistLiveClip (slot);
    addToPool ({ slot.file });
    infoMessage = slot.label + " recorded (" + formatTime (clip->data.getNumSamples() / sr) + ")" + tempoNote + " - DUB to layer on top";
    updateRowViews();
}

int MainComponent::findOrCreateLiveLoopsRow()
{
    // Each key gets its own loops group ("Loops Bb major"); "Live Loops" when there's no key.
    PadLayout key;
    const auto keyName = currentKey (key) ? key.keyName() : juce::String();
    const auto name = keyName.isEmpty() ? juce::String ("Live Loops") : "Loops " + keyName;

    for (int r = 0; r < (int) liveSet.rows.size(); ++r)
        if (liveSet.rows[(size_t) r].name == name)
            return r;

    if ((int) liveSet.rows.size() >= LiveSet::maxRows || freeEngine() < 0)
    {
        // No room for another group: use any existing loops group instead.
        for (int r = 0; r < (int) liveSet.rows.size(); ++r)
            if (liveSet.rows[(size_t) r].name == "Live Loops" || liveSet.rows[(size_t) r].name.startsWith ("Loops "))
                return r;
        return -1;
    }

    auto r = LiveSet::makeRow (name, false, 0);
    r.engine = freeEngine();

    // A colour no other group is using, starting from rose (like the inputs).
    r.colour = 4;
    for (int c = 0; c < Theme::numRowColours; ++c)
    {
        const int candidate = (4 + c) % Theme::numRowColours;
        if (std::none_of (liveSet.rows.begin(), liveSet.rows.end(), [candidate] (const Row& other) { return other.colour == candidate; }))
        {
            r.colour = candidate;
            break;
        }
    }

    liveSet.rows.push_back (std::move (r));
    rebuildRows();
    infoMessage = "New group for loops in " + (keyName.isEmpty() ? juce::String ("no key") : keyName);
    return (int) liveSet.rows.size() - 1;
}

void MainComponent::swapPlayingClip (Row& row, Slot& slot, LoopClipPtr clip)
{
    // Same phase, very short crossfade: the change is inaudible apart from the new material.
    if (slot.playing)
    {
        engineFor (row).stopLoop (slot.id, 0.02);
        engineFor (row).startLoop (slot.id, std::move (clip), 0.02, dbToGain (slot.volumeDb), slot.anchor);
    }
}

void MainComponent::persistLiveClip (Slot& slot)
{
    if (slot.liveClip == nullptr)
        return;

    // Edited loops are saved as new recordings, never over a sample from elsewhere.
    if (! slot.file.isAChildOf (recordingsFolder()))
    {
        clipCache.erase (clipKey (slot));
        slot.file = recordingsFolder().getNonexistentChildFile (slot.label + " overdub", ".wav");
        slot.smoothSeconds = 0.0;
        clipCache[clipKey (slot)] = slot.liveClip;
        addToPool ({ slot.file });
    }

    // Write a snapshot in the background so playing and overdubbing carry on.
    // Files are always written forwards; a reversed channel stays reversed only in playback.
    std::shared_ptr<const LoopClip> snapshot = slot.reverse ? makeReversed (*slot.liveClip)
                                                            : std::make_shared<const LoopClip> (*slot.liveClip);
    const auto file = slot.file;
    loader.addJob ([snapshot, file] { writeWav (*snapshot, file); });
    saveSession();
}

void MainComponent::dubPressed()
{
    errorMessage.clear();
    const auto state = looper.getState();

    if (state == Looper::State::overdubbing)
    {
        stopOverdubbing();
        return;
    }

    if (state != Looper::State::idle)
    {
        errorMessage = "Finish recording first";
        return;
    }

    if (! anyArmed())
    {
        errorMessage = "Arm an input first: press ARM on Mic or Instrument";
        return;
    }

    auto [row, slot] = liveSet.findSlot (dubTargetId);
    if (slot == nullptr || ! slot->playing || slot->oneShot)
    {
        errorMessage = "Play the loop you want to overdub (or choose \"Overdub on this loop\" from its menu)";
        return;
    }

    if (slot->liveClip == nullptr)
    {
        // First overdub on this loop: make an editable copy and play that instead.
        auto cached = clipCache.find (clipKey (*slot));
        if (cached == clipCache.end())
        {
            errorMessage = "That loop is still loading - try again";
            return;
        }

        if (std::abs (cached->second->sampleRate - currentSampleRate) > 1.0)
        {
            errorMessage = "Overdub needs a loop at the interface's sample rate ("
                         + juce::String (currentSampleRate / 1000.0, 1) + " kHz); this one is "
                         + juce::String (cached->second->sampleRate / 1000.0, 1) + " kHz";
            return;
        }

        slot->liveClip = std::make_shared<LoopClip> (*cached->second);
        cached->second = slot->liveClip;
        swapPlayingClip (*row, *slot, slot->liveClip);
    }

    slot->undoClip = std::make_shared<LoopClip> (*slot->liveClip);
    looper.startOverdub (slot->liveClip, slot->anchor);
    overdubSlotId = slot->id;
}

void MainComponent::stopOverdubbing()
{
    looper.stopOverdub();

    if (auto [row, slot] = liveSet.findSlot (overdubSlotId); slot != nullptr)
    {
        persistLiveClip (*slot);
        infoMessage = "Overdub added to " + slot->label + " - UNDO to remove it";
    }

    overdubSlotId = -1;
}

void MainComponent::undoPressed()
{
    if (looper.getState() == Looper::State::overdubbing)
        stopOverdubbing();

    auto [row, slot] = liveSet.findSlot (dubTargetId);
    if (slot == nullptr || slot->undoClip == nullptr)
    {
        infoMessage = "Nothing to undo";
        return;
    }

    slot->liveClip = slot->undoClip;
    slot->undoClip = nullptr;
    clipCache[clipKey (*slot)] = slot->liveClip;
    swapPlayingClip (*row, *slot, slot->liveClip);
    persistLiveClip (*slot);
    infoMessage = "Last overdub removed from " + slot->label;
}

void MainComponent::updateLooperStatus()
{
    const auto state = looper.getState();
    juce::String status;

    juce::StringArray armed;
    for (auto& in : liveSet.inputs)
        if (in.armed)
            armed.add (in.name);
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
        if (liveSet.instruments[(size_t) i].armed)
            armed.add (instrumentName (i));

    // Bar and beat since REC, so the end of a bar is easy to see.
    bool recording = false;
    const double beats = currentBeatPosition (recording);
    const int perBar = liveSet.beatsPerBar;
    const auto barBeat = "bar " + juce::String ((int) beats / perBar + 1) + "  -  beat " + juce::String ((int) beats % perBar + 1);

    if (state == Looper::State::waiting)
    {
        const double samplesPerBeat = 60.0 / liveSet.bpm * currentSampleRate;
        const auto barLine = looper.getStartTime() - roundTripLatency;
        const int beatsLeft = (int) std::ceil ((double) (barLine - LoopPlayer::blockClock.load()) / samplesPerBeat);
        status = "COUNT-IN  " + juce::String (juce::jmax (1, beatsLeft)) + "   - play on the next 1\nREC again to call it off";
    }
    else if (state == Looper::State::recording)
        status = "RECORDING  " + formatTime (looper.getRecordedSeconds()) + "   " + barBeat + "\nfrom " + armed.joinIntoString (", ");
    else if (state == Looper::State::finishing)
        status = "Finishing the bar...   " + barBeat;
    else if (state == Looper::State::overdubbing)
    {
        auto [row, slot] = liveSet.findSlot (overdubSlotId);
        status = "OVERDUBBING  " + (slot != nullptr ? slot->label : juce::String());
    }
    else
    {
        status = armed.isEmpty() ? "Arm an input, then press REC" : "Armed: " + armed.joinIntoString (", ");
        if (auto [row, slot] = liveSet.findSlot (dubTargetId); slot != nullptr)
            status << "\nDUB target: " << slot->label;
    }

    looperBar.setLooperStatus (status, state == Looper::State::recording || state == Looper::State::finishing
                                           || state == Looper::State::waiting,
                                state == Looper::State::overdubbing);
}

juce::Array<juce::PluginDescription> MainComponent::effectTypes() const
{
    juce::Array<juce::PluginDescription> types;
    for (auto& t : knownPlugins.getTypes())
        if (! t.isInstrument)
            types.add (t);
    return types;
}

void MainComponent::loadInputPlugin (int i, const juce::PluginDescription& description, const juce::String& state)
{
    auto& in = liveSet.inputs[(size_t) i];
    if (auto xml = description.createXml())
        in.insertXml = xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    in.insertName = description.name;
    updateSourceStrips();

    juce::Component::SafePointer<MainComponent> safe (this);
    const int generation = inputGeneration;

    pluginFormats.createPluginInstanceAsync (description, currentSampleRate, currentBlockSize,
        [safe, i, state, generation, name = description.name] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || generation != self->inputGeneration || i >= (int) self->liveSet.inputs.size())
                return;   // the set or its inputs changed while this was loading

            if (instance == nullptr)
            {
                self->errorMessage = "Couldn't load " + name + (error.isNotEmpty() ? ": " + error : juce::String());
                self->repaint();
                return;
            }

            if (state.isNotEmpty())
            {
                juce::MemoryBlock data;
                if (data.fromBase64Encoding (state))
                    instance->setStateInformation (data.getData(), (int) data.getSize());
            }

            self->inputPluginWindows[(size_t) i].reset();
            auto previous = self->inputEngines[(size_t) i]->setInsert (std::move (instance));
            previous.reset();

            self->infoMessage = name + " is on " + self->liveSet.inputs[(size_t) i].name + " - open it from the input's menu to set it up";
            self->updateSourceStrips();
            self->saveSession();
            self->repaint();
        });
}

void MainComponent::removeInputPlugin (int i)
{
    inputPluginWindows[(size_t) i].reset();
    inputEngines[(size_t) i]->setInsert (nullptr);

    auto& in = liveSet.inputs[(size_t) i];
    in.insertXml.clear();
    in.insertState.clear();
    in.insertName.clear();
    updateSourceStrips();
    saveSession();
}

void MainComponent::openInputPluginEditor (int i)
{
    auto* plugin = inputEngines[(size_t) i]->getInsert();
    if (plugin == nullptr)
        return;

    if (auto& window = inputPluginWindows[(size_t) i]; window != nullptr)
    {
        window->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    inputPluginWindows[(size_t) i] = std::make_unique<PluginWindow> (*plugin, [safe, i]
    {
        juce::MessageManager::callAsync ([safe, i] { if (auto* self = safe.getComponent()) self->inputPluginWindows[(size_t) i].reset(); });
    });
}

void MainComponent::showFxPanel (juce::Component& anchor, const juce::String& title, const FxSettings& fx,
                                 juce::Colour colour, std::function<void (const FxSettings&)> onChange,
                                 const juce::String& owner, FxPanel::Kind kind)
{
    auto panel = std::make_unique<FxPanel> (title, fx, colour, std::move (onChange), kind);
    panel->onKnobRightClick = [this, owner, title] (const juce::String& parameter, const juce::String& label)
    {
        showLearnMenu ("fx." + owner + "." + parameter, title + " " + label);
    };
    openFxPanel = panel.get();
    openFxOwner = owner;
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

bool MainComponent::changeFx (const juce::String& owner, const std::function<void (FxSettings&)>& change)
{
    const FxSettings* changed = nullptr;

    if (owner == "space")
    {
        change (liveSet.space);
        spaceReverb.setSettings (liveSet.space);
        changed = &liveSet.space;
    }
    else if (owner.startsWith ("in."))
    {
        const int i = owner.getTrailingIntValue();
        if (i >= (int) liveSet.inputs.size())
            return false;
        change (liveSet.inputs[(size_t) i].fx);
        applyInput (i);
        changed = &liveSet.inputs[(size_t) i].fx;
    }
    else if (owner.startsWith ("inst."))
    {
        const int i = owner.getTrailingIntValue();
        if (i >= (int) liveSet.instruments.size())
            return false;
        change (liveSet.instruments[(size_t) i].fx);
        applyInstrument (i);
        changed = &liveSet.instruments[(size_t) i].fx;
    }
    else if (owner.startsWith ("grp."))
    {
        const auto uid = owner.fromFirstOccurrenceOf ("grp.", false, false);
        const auto it = std::find_if (liveSet.rows.begin(), liveSet.rows.end(), [&uid] (const Row& r) { return r.uid == uid; });
        if (it == liveSet.rows.end())
            return false;
        change (it->fx);
        engineFor (*it).getFx().setSettings (it->fx);
        changed = &it->fx;
    }
    else if (owner.startsWith ("ch."))
    {
        int r = -1, s = -1;
        if (! findSlotByUid (owner.fromFirstOccurrenceOf ("ch.", false, false), r, s))
            return false;
        auto& row = liveSet.rows[(size_t) r];
        auto& slot = row.slots[(size_t) s];
        auto fx = slot.fx;
        change (fx);
        if (! engineFor (row).setOwnerFx (slot.id, fx))
            return false;   // every per-channel effects slot in the row is taken
        slot.fx = fx;
        changed = &slot.fx;
    }

    if (changed == nullptr)
        return false;

    if (openFxPanel != nullptr && openFxOwner == owner)
        openFxPanel->setSettings (*changed);
    updateRowViews();
    saveSession();
    return true;
}

void MainComponent::showSpacePanel()
{
    showFxPanel (spaceButton, "Space", liveSet.space, Theme::accent, [this] (const FxSettings& fx)
    {
        liveSet.space = fx;
        spaceReverb.setSettings (fx);
        saveSession();
    }, "space", FxPanel::Kind::space);
}

PluginInsert* MainComponent::insertFor (const juce::String& key)
{
    if (key == "master")
        return &masterInsert;

    const auto uid = key.fromFirstOccurrenceOf ("grp.", false, false);
    for (auto& r : liveSet.rows)
        if (r.uid == uid && r.engine >= 0)
            return &engineFor (r).getInsert();
    return nullptr;
}

LiveSet::PluginSlot* MainComponent::insertSlotFor (const juce::String& key)
{
    if (key == "master")
        return &liveSet.masterInsert;

    const auto uid = key.fromFirstOccurrenceOf ("grp.", false, false);
    for (auto& r : liveSet.rows)
        if (r.uid == uid)
            return &r.insert;
    return nullptr;
}

juce::String MainComponent::insertTitle (const juce::String& key)
{
    if (key == "master")
        return "the master";

    const auto uid = key.fromFirstOccurrenceOf ("grp.", false, false);
    for (auto& r : liveSet.rows)
        if (r.uid == uid)
            return r.name;
    return {};
}

void MainComponent::showInsertMenu (const juce::String& key, juce::Component& target)
{
    auto* insert = insertFor (key);
    auto* slot = insertSlotFor (key);
    if (insert == nullptr || slot == nullptr)
        return;

    auto* plugin = insert->getPlugin();
    const auto effects = effectTypes();

    juce::PopupMenu m;
    m.addSectionHeader ("Plug-in effect on " + insertTitle (key) + ":  "
                        + (plugin != nullptr ? plugin->getName() : slot->isEmpty() ? juce::String ("none") : slot->name + " (not loaded)"));

    juce::PopupMenu choose;
    juce::KnownPluginList::addToMenu (choose, effects, juce::KnownPluginList::sortByManufacturer,
                                      plugin != nullptr ? plugin->getPluginDescription().createIdentifierString() : juce::String());
    if (effects.isEmpty())
        choose.addItem (-1, "No effect plug-ins found yet - scan first", false);
    m.addSubMenu (plugin != nullptr ? "Change plug-in" : "Choose a plug-in", choose);
    m.addItem (1, plugin != nullptr ? "Open " + plugin->getName() : juce::String ("Open plug-in"), plugin != nullptr);
    m.addItem (2, "Remove plug-in", plugin != nullptr || ! slot->isEmpty());
    m.addItem (3, "Scan for plug-ins...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, key, effects] (int r)
    {
        if (r == 0)
            return;

        if (const int chosen = juce::KnownPluginList::getIndexChosenByMenu (effects, r); chosen >= 0)
            loadInsertPlugin (key, effects[chosen], {});
        else if (r == 1) openInsertEditor (key);
        else if (r == 2) removeInsertPlugin (key);
        else if (r == 3) showPluginScanner();
    });
}

void MainComponent::loadInsertPlugin (const juce::String& key, const juce::PluginDescription& description, const juce::String& state)
{
    auto* slot = insertSlotFor (key);
    if (slot == nullptr)
        return;

    if (auto xml = description.createXml())
        slot->xml = xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    slot->name = description.name;
    slot->state = state;
    saveSession();

    juce::Component::SafePointer<MainComponent> safe (this);
    const int generation = insertGeneration;

    pluginFormats.createPluginInstanceAsync (description, currentSampleRate, currentBlockSize,
        [safe, key, state, generation, name = description.name] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || generation != self->insertGeneration)
                return;   // the set changed while this was loading

            auto* insert = self->insertFor (key);
            if (insert == nullptr)
                return;   // the group was removed meanwhile

            if (instance == nullptr)
            {
                self->errorMessage = "Couldn't load " + name + (error.isNotEmpty() ? ": " + error : juce::String());
                self->repaint();
                return;
            }

            if (state.isNotEmpty())
            {
                juce::MemoryBlock data;
                if (data.fromBase64Encoding (state))
                    instance->setStateInformation (data.getData(), (int) data.getSize());
            }

            self->insertWindows.erase (key);
            auto previous = insert->setPlugin (std::move (instance));
            previous.reset();

            self->infoMessage = name + " is on " + self->insertTitle (key) + " - open it from the same menu to set it up";
            self->updateMasterButtons();
            self->saveSession();
            self->repaint();
        });
}

void MainComponent::removeInsertPlugin (const juce::String& key)
{
    insertWindows.erase (key);
    if (auto* insert = insertFor (key))
        insert->setPlugin (nullptr);
    if (auto* slot = insertSlotFor (key))
        *slot = {};
    updateMasterButtons();
    saveSession();
}

void MainComponent::openInsertEditor (const juce::String& key)
{
    auto* insert = insertFor (key);
    auto* plugin = insert != nullptr ? insert->getPlugin() : nullptr;
    if (plugin == nullptr)
        return;

    if (auto it = insertWindows.find (key); it != insertWindows.end() && it->second != nullptr)
    {
        it->second->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    insertWindows[key] = std::make_unique<PluginWindow> (*plugin, [safe, key]
    {
        juce::MessageManager::callAsync ([safe, key] { if (auto* self = safe.getComponent()) self->insertWindows.erase (key); });
    });
}

void MainComponent::updateMasterButtons()
{
    // FX lights up while a plug-in is on the master.
    const bool on = masterInsert.isLoaded();
    masterFxButton.setColour (juce::TextButton::buttonColourId, on ? Theme::accent : Theme::raised);
    masterFxButton.setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::text);
}

//==============================================================================
void MainComponent::playPressed (int row, int slot)
{
    auto& r = liveSet.rows[(size_t) row];
    auto& s = r.slots[(size_t) slot];

    if (s.file == juce::File())
    {
        browseForFiles ("Choose a sample", true, [this, row, slot] (juce::Array<juce::File> files)
        {
            if (row < (int) liveSet.rows.size())
                assignToChannels (row, slot, files);
        });
        return;
    }

    infoMessage.clear();
    errorMessage.clear();

    if (s.oneShot)
    {
        // Callbacks find the channel again by id, in case channels were moved meanwhile.
        withClip (s, [this, id = s.id] (LoopClipPtr clip)
        {
            auto [rw, sl] = liveSet.findSlot (id);
            if (clip == nullptr || sl == nullptr)
                return;

            engineFor (*rw).triggerShot (id, clip, dbToGain (sl->volumeDb));
            sl->flashUntil = juce::Time::getMillisecondCounter()
                           + (juce::uint32) (1000.0 * clip->data.getNumSamples() / clip->sampleRate);
        });
        return;
    }

    if (s.playing || s.loading)
        stopSlot (r, s);
    else
        startLoop (row, slot, false);
}

void MainComponent::crossfadePressed (int row, int slot)
{
    auto& r = liveSet.rows[(size_t) row];
    auto& s = r.slots[(size_t) slot];

    if (s.file == juce::File() || s.oneShot)
        return;

    infoMessage.clear();
    errorMessage.clear();

    if (s.playing)
    {
        // Already on: just fade the rest of the row out.
        for (auto& other : r.slots)
            if (other.id != s.id)
                stopSlot (r, other);
        return;
    }

    startLoop (row, slot, true);
}

void MainComponent::startLoop (int row, int slot, bool fadeOutOthers, double seconds)
{
    if (seconds < 0.0)
        seconds = fadeTimeSlider.getValue();
    auto& s = liveSet.rows[(size_t) row].slots[(size_t) slot];
    const int id = s.id;
    const int request = ++s.request;
    s.loading = true;
    updateRowViews();

    withClip (s, [this, id, request, fadeOutOthers, seconds] (LoopClipPtr clip)
    {
        auto [r, sl] = liveSet.findSlot (id);
        if (sl == nullptr || sl->request != request)
            return;   // removed, or pressed again since

        sl->loading = false;

        if (clip != nullptr)
        {
            const auto startAt = nextLoopStart();

            // The other loops fade out over the same time this one fades in.
            if (fadeOutOthers)
                for (auto& other : r->slots)
                    if (other.id != id)
                        stopSlot (*r, other, startAt, seconds);

            // Anchor to the start, so an overdub later lines up with this playback.
            sl->anchor = startAt;
            if (engineFor (*r).startLoop (id, clip, seconds, dbToGain (sl->volumeDb), sl->anchor, startAt))
                sl->playing = true;
            else
                errorMessage = "Too many loops at once in " + r->name;
        }

        updateRowViews();
        repaint();
    });
}

juce::int64 MainComponent::nextLoopStart()
{
    const auto now = LoopPlayer::blockClock.load();
    if (liveSet.loopStart == LiveSet::LoopStart::now)
        return now;

    // Nothing to keep time with: start now, and the grid follows this loop.
    if (! anyLoopPlaying())
    {
        beatAnchor = now;
        return now;
    }

    const double beat = 60.0 / liveSet.bpm * currentSampleRate;
    const double unit = liveSet.loopStart == LiveSet::LoopStart::bar ? samplesPerBar() : beat;
    return nextGridPoint (now, beatAnchor, unit, (juce::int64) (0.05 * currentSampleRate));
}

juce::int64 MainComponent::nextRecordStart()
{
    if (liveSet.loopStart == LiveSet::LoopStart::now)
        return -1;

    const auto now = LoopPlayer::blockClock.load();
    const double beat = 60.0 / liveSet.bpm * currentSampleRate;

    // Nothing playing: a bar's count-in from the press, and the grid starts with it.
    if (! anyLoopPlaying())
    {
        beatAnchor = now;
        return now + (juce::int64) std::llround (samplesPerBar());
    }

    return nextGridPoint (now + (juce::int64) beat, beatAnchor, samplesPerBar(), 0);
}

bool MainComponent::anyLoopPlaying() const
{
    for (auto& r : liveSet.rows)
        for (auto& s : r.slots)
            if (s.playing)
                return true;
    return false;
}

void MainComponent::stopSlot (Row& row, Slot& slot, juce::int64 startAt, double seconds)
{
    if (seconds < 0.0)
        seconds = fadeTimeSlider.getValue();
    if (slot.id == overdubSlotId)
        stopOverdubbing();

    if (slot.playing)
        engineFor (row).stopLoop (slot.id, seconds, startAt);

    slot.playing = false;
    slot.loading = false;
    ++slot.request;
    updateRowViews();
}

void MainComponent::stopAll (double seconds)
{
    if (looper.getState() == Looper::State::overdubbing)
        stopOverdubbing();

    for (auto& r : liveSet.rows)
    {
        if (r.engine >= 0)
            engineFor (r).stopAll (seconds);

        for (auto& s : r.slots)
        {
            s.playing = s.loading = false;
            ++s.request;
            s.flashUntil = 0;
        }
    }

    updateRowViews();
}

//==============================================================================
void MainComponent::addToPool (const juce::Array<juce::File>& files)
{
    const int added = liveSet.addToPool (files);
    pool.setFiles (liveSet.pool);

    if (added > 1)
        infoMessage = "Added " + juce::String (added) + " samples";

    saveSession();
    repaint();
}

void MainComponent::showPoolMenu (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    juce::PopupMenu m;
    m.addSectionHeader (files.size() == 1 ? files[0].getFileNameWithoutExtension()
                                          : juce::String (files.size()) + " samples");

    for (int r = 0; r < (int) liveSet.rows.size(); ++r)
        m.addItem (100 + r, "Add to " + liveSet.rows[(size_t) r].name);

    m.addSeparator();
    m.addItem (1, "Remove from samples");
    m.addItem (2, "Show in Explorer", files.size() == 1);

    m.showMenuAsync (juce::PopupMenu::Options(), [this, files] (int choice)
    {
        if (choice >= 100 && choice < 100 + (int) liveSet.rows.size())
            appendToRow (choice - 100, files);

        if (choice == 1)
        {
            juce::StringArray inUse;
            for (auto& f : files)
            {
                if (liveSet.describeUsage (f).isNotEmpty())
                    inUse.add (f.getFileNameWithoutExtension());
                else
                    liveSet.pool.removeAllInstancesOf (f);
            }

            pool.setFiles (liveSet.pool);
            errorMessage = inUse.isEmpty() ? juce::String()
                                           : "Still on a channel, clear it there first: " + inUse.joinIntoString (", ");
            saveSession();
            repaint();
        }

        if (choice == 2) files[0].revealToUser();
    });
}

void MainComponent::dropFilesAt (const juce::Array<juce::File>& files, juce::Point<int> pos, bool fromOutside)
{
    if (files.isEmpty())
        return;

    if (auto* hit = getComponentAt (pos))
    {
        auto* card = dynamic_cast<ChannelCard*> (hit);
        if (card == nullptr)
            card = hit->findParentComponentOfClass<ChannelCard>();

        auto* rowView = dynamic_cast<RowView*> (hit);
        if (rowView == nullptr)
            rowView = hit->findParentComponentOfClass<RowView>();

        if (rowView != nullptr && rowView->index < (int) liveSet.rows.size())
        {
            if (card != nullptr)
                assignToChannels (rowView->index, card->index, files);
            else
                appendToRow (rowView->index, files);
            return;
        }

        if (hit == &addRowButton)
        {
            addRow ([this, files] (int r) { appendToRow (r, files); });
            return;
        }
    }

    // Dropped anywhere else: files from outside just go into the samples list.
    if (fromOutside)
        addToPool (files);
}

void MainComponent::browseForFiles (const juce::String& title, bool multiple,
                                    std::function<void (juce::Array<juce::File>)> onChosen)
{
    const juce::File startDir (settings.getUserSettings()->getValue (
        "lastFolder", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));

    fileChooser = std::make_unique<juce::FileChooser> (title, startDir, formatManager.getWildcardForAllFormats());

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    if (multiple)
        chooserFlags |= juce::FileBrowserComponent::canSelectMultipleItems;

    fileChooser->launchAsync (chooserFlags, [this, onChosen] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty())
            return;

        settings.getUserSettings()->setValue ("lastFolder", files[0].getParentDirectory().getFullPathName());
        onChosen (files);
    });
}

//==============================================================================
void MainComponent::withClip (const Slot& slot, std::function<void (LoopClipPtr)> then)
{
    const auto file = slot.file;
    const auto smoothSeconds = smoothingFor (slot);
    const bool reversed = slot.reverse;
    const auto key = clipKey (slot);

    if (auto it = clipCache.find (key); it != clipCache.end())
    {
        if (then)
            then (it->second);
        return;
    }

    auto [pending, isNew] = pendingLoads.try_emplace (key);
    if (then)
        pending->second.push_back (std::move (then));

    if (! isNew)
        return;   // already loading

    juce::Component::SafePointer<MainComponent> safe (this);
    auto* formats = &formatManager;

    loader.addJob ([safe, formats, file, key, smoothSeconds, reversed]
    {
        juce::String error;
        auto clip = loadLoopClip (file, *formats, error);

        if (clip != nullptr && smoothSeconds > 0.0)
            clip = makeSeamlessLoop (clip, smoothSeconds);

        if (clip != nullptr && reversed)
            clip = makeReversed (*clip);

        juce::MessageManager::callAsync ([safe, key, clip, error]
        {
            if (auto* self = safe.getComponent())
                self->clipLoaded (key, clip, error);
        });
    });
}

void MainComponent::clipLoaded (const juce::String& key, LoopClipPtr clip, const juce::String& error)
{
    std::vector<std::function<void (LoopClipPtr)>> callbacks;
    if (auto node = pendingLoads.extract (key); ! node.empty())
        callbacks = std::move (node.mapped());

    if (clip == nullptr)
    {
        errorMessage = error;
    }
    else if (! callbacks.empty() || cacheBytes + clipBytes (clip) <= cacheBudgetBytes)
    {
        // Background preloads stop at the budget; anything actually pressed is always kept.
        clipCache[key] = clip;
        cacheBytes += clipBytes (clip);
        trimCache (key);
    }

    for (auto& cb : callbacks)
        cb (clip);

    repaint();
}

void MainComponent::preloadAll()
{
    // One-shots first: they're short and need to respond instantly.
    for (int pass = 0; pass < 2; ++pass)
        for (auto& r : liveSet.rows)
            for (auto& s : r.slots)
                if (s.file.existsAsFile() && s.oneShot == (pass == 0))
                    withClip (s, nullptr);
}

void MainComponent::trimCache (const juce::String& keep)
{
    // Drop clips nothing is playing until back under budget.
    for (auto it = clipCache.begin(); it != clipCache.end() && cacheBytes > cacheBudgetBytes;)
    {
        if (it->first != keep && it->second.use_count() == 1)
        {
            cacheBytes -= clipBytes (it->second);
            it = clipCache.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        const float scale = juce::Desktop::getInstance().getGlobalScaleFactor();
        const auto c = key.getTextCharacter();
        const int code = key.getKeyCode();

        if (c == '=' || c == '+' || code == '=' || code == '+')  { setZoom (scale + 0.1f); return true; }
        if (c == '-' || code == '-')                             { setZoom (scale - 0.1f); return true; }
        if (c == '0' || code == '0')                             { setZoom (1.0f); return true; }
    }

    if (key == juce::KeyPress::F11Key)
    {
        setStageMode (! stageMode);
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (learnTarget.isNotEmpty())
            learnTarget.clear();
        else
            stopAll (1.5);   // panic: quick fade, not a hard cut

        repaint();
        return true;
    }

    return false;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray&)
{
    return true;   // folders too; non-audio files are filtered out on drop
}

void MainComponent::filesDropped (const juce::StringArray& paths, int x, int y)
{
    dropFilesAt (pool.collectAudio (paths), { x, y }, true);
}

bool MainComponent::isInterestedInDragSource (const SourceDetails& details)
{
    const auto text = details.description.toString();
    return text.startsWith (SamplePool::dragPrefix) || text.startsWith (ChannelCard::dragPrefix)
        || text.startsWith (RowView::dragPrefix);
}

juce::Component* MainComponent::groupViewAt (juce::Point<int> pos)
{
    if (auto* rv = rowViewAt (pos))
        return rv;

    if (auto* hit = getComponentAt (pos))
        if (hit == &inputsView || hit->findParentComponentOfClass<InputsView>() != nullptr)
            return &inputsView;

    return nullptr;
}

RowView* MainComponent::rowViewAt (juce::Point<int> pos)
{
    if (auto* hit = getComponentAt (pos))
    {
        if (auto* rv = dynamic_cast<RowView*> (hit))
            return rv;
        return hit->findParentComponentOfClass<RowView>();
    }
    return nullptr;
}

void MainComponent::clearDropMarkers()
{
    for (auto* rv : rowViews)
    {
        rv->setDropMarker (-1);
        rv->setGroupDropSide (0);
    }
    inputsView.setGroupDropSide (0);
    rowsContent.showGhost ({}, {});
}

void MainComponent::itemDragMove (const SourceDetails& details)
{
    // A group being moved: show which side of the group underneath it would land.
    if (details.description.toString().startsWith (RowView::dragPrefix))
    {
        int item = 0;
        const auto pos = droppedGroupPosition (details, item);
        const auto colour = item < 0 ? InputsView::colour
                                     : juce::isPositiveAndBelow (item, (int) liveSet.rows.size())
                                         ? Theme::rowColour (liveSet.rows[(size_t) item].colour) : Theme::accent;
        rowsContent.showGhost (arrangeGroups (item, pos)[item], colour);
        return;
    }

    if (! details.description.toString().startsWith (ChannelCard::dragPrefix))
        return;

    // Show where a dragged channel would land.
    auto* target = rowViewAt (details.localPosition);
    for (auto* rv : rowViews)
        rv->setDropMarker (rv == target ? rv->dropIndexAt (rv->getLocalPoint (this, details.localPosition)) : -1);
}

void MainComponent::itemDragExit (const SourceDetails&)
{
    clearDropMarkers();
}

void MainComponent::itemDropped (const SourceDetails& details)
{
    clearDropMarkers();
    const auto text = details.description.toString();

    if (text.startsWith (RowView::dragPrefix))
    {
        int item = 0;
        const auto pos = droppedGroupPosition (details, item);
        if (item >= (int) liveSet.rows.size())
            return;

        // Keep every group where it now lands, so the arrangement stays as it is.
        for (const auto& [i, r] : arrangeGroups (item, pos))
        {
            if (i < 0) liveSet.inputsPosition = r.getPosition();
            else       liveSet.rows[(size_t) i].position = r.getPosition();
        }

        layoutRows();
        saveSession();
        return;
    }

    if (text.startsWith (ChannelCard::dragPrefix))
    {
        if (auto* rv = rowViewAt (details.localPosition))
            moveChannelTo (text.fromFirstOccurrenceOf (ChannelCard::dragPrefix, false, false).getIntValue(),
                           rv->index, rv->dropIndexAt (rv->getLocalPoint (this, details.localPosition)));
        return;
    }

    dropFilesAt (SamplePool::filesFromDragDescription (details.description), details.localPosition, false);
}

void MainComponent::moveChannelTo (int slotId, int targetRow, int targetIndex)
{
    if (targetRow >= (int) liveSet.rows.size())
        return;

    // Find where the channel is now.
    int sourceRow = -1, sourceIndex = -1;
    for (int r = 0; r < (int) liveSet.rows.size() && sourceRow < 0; ++r)
        for (int s = 0; s < (int) liveSet.rows[(size_t) r].slots.size(); ++s)
            if (liveSet.rows[(size_t) r].slots[(size_t) s].id == slotId)
            {
                sourceRow = r;
                sourceIndex = s;
                break;
            }

    if (sourceRow < 0)
        return;

    auto& from = liveSet.rows[(size_t) sourceRow].slots;
    const bool changingRow = sourceRow != targetRow;
    const bool wasPlaying = from[(size_t) sourceIndex].playing;

    // A playing loop lives in its row's audio engine, so moving rows means restarting it there.
    if (changingRow)
        stopSlot (liveSet.rows[(size_t) sourceRow], from[(size_t) sourceIndex]);

    // Its own effects live in the row's engine too, so they move with it.
    if (changingRow)
    {
        const auto& moving = from[(size_t) sourceIndex];
        engineFor (liveSet.rows[(size_t) sourceRow]).setOwnerFx (moving.id, {});
        if (! engineFor (liveSet.rows[(size_t) targetRow]).setOwnerFx (moving.id, moving.fx))
            errorMessage = "No room for this channel's own effects in " + liveSet.rows[(size_t) targetRow].name
                         + " (" + juce::String (LoopChannel::numChannelFx) + " per row)";
    }

    auto slot = std::move (from[(size_t) sourceIndex]);
    from.erase (from.begin() + sourceIndex);

    if (! changingRow && targetIndex > sourceIndex)
        --targetIndex;

    auto& to = liveSet.rows[(size_t) targetRow].slots;
    targetIndex = juce::jlimit (0, (int) to.size(), targetIndex);
    to.insert (to.begin() + targetIndex, std::move (slot));

    if (changingRow && wasPlaying)
        startLoop (targetRow, targetIndex, false);

    updateRowViews();
    pool.refreshUsage();
    saveSession();
}

//==============================================================================
void MainComponent::askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Modal callbacks run before the window is deleted, so reading it here is safe.
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int r)
    {
        const auto text = w->getTextEditorContents ("text").trim();
        if (r == 1 && text.isNotEmpty())
            onOk (text);
    }), true);
}

void MainComponent::askChoice (const juce::String& title, const juce::String& message,
                               const juce::StringArray& buttons, std::function<void (int)> onChosen)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, this);

    // Buttons return 1, 2, ...; the last one is Cancel and returns 0.
    for (int i = 0; i < buttons.size(); ++i)
    {
        const bool isCancel = i == buttons.size() - 1;
        w->addButton (buttons[i], isCancel ? 0 : i + 1,
                      isCancel ? juce::KeyPress (juce::KeyPress::escapeKey) : juce::KeyPress());
    }

    w->enterModalState (true, juce::ModalCallbackFunction::create ([onChosen] (int r) { onChosen (r); }), true);
}

//==============================================================================
void MainComponent::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    const auto device = source != nullptr ? source->getName() : juce::String();
    const auto deviceId = source != nullptr ? source->getIdentifier() : juce::String();

    // Notes go straight to the instruments from this MIDI thread, for the lowest latency.
    for (auto& inst : instrumentEngines)
        inst->handleMidi (device, deviceId, m);

    if (! (m.isController() || m.isNoteOnOrOff()))
        return;

    // Controls are handled on the message thread: queue them.
    const juce::SpinLock::ScopedLockType sl (midiWriteLock);
    midiFifo.write (1).forEach ([&] (int index) { midiQueue[(size_t) index] = { device, m }; });
}

void MainComponent::enableMidiInputs()
{
    juce::StringArray names;

    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (! deviceManager.isMidiInputDeviceEnabled (d.identifier))
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);

        if (deviceManager.isMidiInputDeviceEnabled (d.identifier))
            names.add (d.name);
    }

    midiStatus = names.isEmpty() ? "MIDI: no controllers connected" : "MIDI: " + names.joinIntoString (", ");

    // The MidiMix lights its mute buttons when it receives the matching notes back;
    // the Fire's pads light to show the instrument's scale.
    ledOutput.reset();
    fireOutputs.clear();
    fireButtonState.clear();
    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        if (ledOutput == nullptr && d.name.removeCharacters (" ").containsIgnoreCase (juce::String (MidiMap::midiMixName).removeCharacters (" ")))
            ledOutput = juce::MidiOutput::openDevice (d.identifier);
        else if (d.name.containsIgnoreCase ("FIRE"))
            if (auto out = juce::MidiOutput::openDevice (d.identifier))
            {
                // Its input is the one on the same USB port (the path up to the interface GUID), else the same name.
                juce::String inputId;
                const auto port = d.identifier.toLowerCase().upToLastOccurrenceOf ("#{", false, false);
                for (const auto& in : juce::MidiInput::getAvailableDevices())
                    if ((port.isNotEmpty() && in.identifier.toLowerCase().upToLastOccurrenceOf ("#{", false, false) == port)
                        || (inputId.isEmpty() && in.name == d.name))
                        inputId = in.identifier;
                fireOutputs.push_back ({ d.name, inputId, std::move (out) });
            }
    }

    updateMidiSources();
    updateMidiLeds();
    updateFireLights();
    repaint();
}

InstrumentChannel::MidiSource MainComponent::midiSourceFor (int i) const
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    auto isConnected = [&devices] (const juce::String& id)
    {
        return id.isNotEmpty() && std::any_of (devices.begin(), devices.end(), [&id] (const juce::MidiDeviceInfo& d) { return d.identifier == id; });
    };

    const auto& inst = liveSet.instruments[(size_t) i];
    InstrumentChannel::MidiSource s { inst.midiSource, inst.midiSourceId, isConnected (inst.midiSourceId), {} };
    for (int j = 0; j < (int) liveSet.instruments.size(); ++j)
        if (j != i && isConnected (liveSet.instruments[(size_t) j].midiSourceId))
            s.takenIds.addIfNotAlreadyThere (liveSet.instruments[(size_t) j].midiSourceId);
    return s;
}

void MainComponent::updateMidiSources()
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
    {
        auto& inst = liveSet.instruments[(size_t) i];
        for (const auto& d : devices)
        {
            // A set saved before devices were known by USB path: adopt the device with that exact name.
            if (inst.midiSourceId.isEmpty() && inst.midiSource.isNotEmpty() && d.name == inst.midiSource)
                inst.midiSourceId = d.identifier;
            // Windows renamed it ("FL STUDIO FIRE" -> "3 - FL STUDIO FIRE"): show its name now.
            if (inst.midiSourceId.isNotEmpty() && d.identifier == inst.midiSourceId)
                inst.midiSource = d.name;
        }
    }

    for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
        instrumentEngines[(size_t) i]->setMidiSource (midiSourceFor (i));
}

void MainComponent::handleMidi (const juce::String& device, const juce::MidiMessage& m)
{
    const bool pressed = (m.isNoteOn() && m.getVelocity() > 0) || (m.isController() && m.getControllerValue() >= 64);

    // On a Fire, SHIFT + ALT steps its pads through the light show: spectrum, glow, back to the key.
    if (device.containsIgnoreCase (MidiMap::fireName) && m.isNoteOnOrOff())
    {
        if (m.getNoteNumber() == MidiMap::fireShiftNote)
            fireShiftHeld[device] = pressed;

        const int note = m.getNoteNumber();
        const bool lightShowButton = note == MidiMap::fireAltNote || note == MidiMap::fireGridLeftNote || note == MidiMap::fireGridRightNote
                                  || note == MidiMap::firePatternUpNote || note == MidiMap::firePatternDownNote
                                  || note == MidiMap::fireBrowserNote;
        if (lightShowButton && fireShiftHeld[device])
        {
            if (! pressed)
                return;

            for (auto& fire : fireOutputs)
            {
                if (fire.name != device)
                    continue;
                auto& show = lightShowFor (fire);
                const auto wrap = [] (int v, int n) { return (v % n + n) % n; };

                // Where this Fire sits: separate, left, right, top, bottom. The other Fire takes the
                // opposite place, and the two then show one picture across both.
                if (note == MidiMap::fireBrowserNote)
                {
                    show.position = wrap (show.position + 1, numFirePositions);
                    show.on = true;
                    for (auto& other : fireOutputs)
                    {
                        if (&other == &fire)
                            continue;
                        auto& theirs = lightShowFor (other);
                        theirs.position = show.position == separate ? (int) separate
                                        : show.position == left ? (int) right : show.position == right ? (int) left
                                        : show.position == top ? (int) bottom : (int) top;
                        if (show.position != separate)
                            theirs = { true, show.pattern, show.palette, theirs.position };
                        break;
                    }
                    static const char* places[] = { "on its own", "on the LEFT", "on the RIGHT", "on TOP", "BELOW" };
                    infoMessage = device + " light show " + places[show.position]
                                + (show.position == separate ? juce::String() : " - joined with the other Fire")
                                + "   (SHIFT + BROWSER to move it)";
                    continue;
                }

                if (note == MidiMap::fireAltNote)                show.on = ! show.on;
                else if (note == MidiMap::fireGridRightNote)     { show.pattern = wrap (show.pattern + 1, FireVisualizer::numPatterns); show.on = true; }
                else if (note == MidiMap::fireGridLeftNote)      { show.pattern = wrap (show.pattern - 1, FireVisualizer::numPatterns); show.on = true; }
                else if (note == MidiMap::firePatternUpNote)     { show.palette = wrap (show.palette + 1, FireVisualizer::numPalettes); show.on = true; }
                else if (note == MidiMap::firePatternDownNote)   { show.palette = wrap (show.palette - 1, FireVisualizer::numPalettes); show.on = true; }

                // Joined Fires change together.
                if (auto* partner = partnerOf (fire))
                {
                    auto& theirs = lightShowFor (*partner);
                    theirs.on = show.on;
                    theirs.pattern = show.pattern;
                    theirs.palette = show.palette;
                }

                infoMessage = show.on ? device + " light show: " + FireVisualizer::patternName (show.pattern) + " in "
                                            + FireVisualizer::paletteName (show.palette)
                                            + "   (SHIFT + GRID < > pattern, SHIFT + PATTERN up/down colours, SHIFT + ALT off)"
                                      : device + " light show off - showing the key again";
            }
            updateFireLights();
            repaint();
            return;
        }
    }

    if (learnTarget.isNotEmpty())
    {
        const bool isFader = learnTarget.containsIgnoreCase ("fader") || learnTarget.endsWith (".vol") || learnTarget.startsWith ("fx.");

        if ((isFader && m.isController()) || (! isFader && pressed))
        {
            midiMap.bind (learnTarget, { device, m.isNoteOn(),
                                         m.isNoteOn() ? m.getNoteNumber() : m.getControllerNumber(),
                                         m.getChannel() });
            learnTarget.clear();
            saveMidiMap();
            updateMidiLeds();
            repaint();
        }
        return;
    }

    for (auto& target : midiMap.findTargets (device, m))
    {
        const float proportion = (float) m.getControllerValue() / 127.0f;

        // An effect knob: "fx.<owner>.<knob>". Moving it switches its section on, as on screen.
        if (target.startsWith ("fx."))
        {
            if (m.isController())
            {
                const auto owner = target.fromFirstOccurrenceOf ("fx.", false, false).upToLastOccurrenceOf (".", false, false);
                if (const auto* param = findFxParameter (target.fromLastOccurrenceOf (".", false, false)))
                    changeFx (owner, [param, proportion] (FxSettings& fx)
                    {
                        fx.*(param->member) = rangeOf (*param).convertFrom0to1 ((double) proportion);
                        if (param->section != nullptr)
                            fx.*(param->section) = true;
                    });
            }
            continue;
        }

        // A channel: "ch.<uid>.play", ".xf" or ".vol".
        if (target.startsWith ("ch."))
        {
            const auto uid = target.fromFirstOccurrenceOf ("ch.", false, false).upToLastOccurrenceOf (".", false, false);
            const auto control = target.fromLastOccurrenceOf (".", false, false);
            int r = -1, s = -1;
            if (! findSlotByUid (uid, r, s))
                continue;

            if (control == "vol")
            {
                if (m.isController() && r < rowViews.size())
                    if (auto* card = rowViews[r]->getChannelCard (s))
                        applyFaderMidi (target, card->getVolumeSlider(), proportion);
            }
            else if (pressed && control == "play") playPressed (r, s);
            else if (pressed && control == "xf")   crossfadePressed (r, s);
            continue;
        }

        // A group: "grp.<uid>.fader" or ".mute".
        if (target.startsWith ("grp."))
        {
            const auto uid = target.fromFirstOccurrenceOf ("grp.", false, false).upToLastOccurrenceOf (".", false, false);
            const auto control = target.fromLastOccurrenceOf (".", false, false);
            const auto it = std::find_if (liveSet.rows.begin(), liveSet.rows.end(), [&uid] (const Row& row) { return row.uid == uid; });
            const int r = (int) std::distance (liveSet.rows.begin(), it);
            if (it == liveSet.rows.end() || r >= rowViews.size())
                continue;

            if (control == "fader" && m.isController())
                applyFaderMidi (target, rowViews[r]->strip, proportion);
            else if (control == "mute" && pressed)
                setRowMuted (r, ! it->muted);
            continue;
        }

        if (target.startsWith ("inputFader."))
        {
            if (const int i = target.getTrailingIntValue(); m.isController() && i < inputsView.inputStrips.size())
                applyFaderMidi (target, inputsView.inputStrips[i]->strip, (float) m.getControllerValue() / 127.0f);
            continue;
        }

        if (target.startsWith ("instFader."))
        {
            if (const int i = target.getTrailingIntValue(); m.isController() && i < inputsView.instrumentStrips.size())
                applyFaderMidi (target, inputsView.instrumentStrips[i]->strip, (float) m.getControllerValue() / 127.0f);
            continue;
        }

        if (target.startsWith ("instMute."))
        {
            if (const int i = target.getTrailingIntValue(); pressed && i < (int) liveSet.instruments.size())
            {
                liveSet.instruments[(size_t) i].muted = ! liveSet.instruments[(size_t) i].muted;
                applyInstrument (i);
                saveSession();
            }
            continue;
        }

        if (target.startsWith ("inputMute."))
        {
            if (const int i = target.getTrailingIntValue(); pressed && i < (int) liveSet.inputs.size())
            {
                liveSet.inputs[(size_t) i].muted = ! liveSet.inputs[(size_t) i].muted;
                applyInput (i);
                saveSession();
            }
            continue;
        }

        if (target.startsWith ("fader."))
        {
            if (! m.isController())
                continue;

            if (target == "fader.master")
                applyFaderMidi (target, masterStrip, proportion);
            continue;
        }

        if (! pressed)
            continue;

        if (target == "stopAll")
        {
            stopAll (fadeTimeSlider.getValue());
        }
        else if (target == "tap")   tapTempo();
        else if (target == "rec")   recPressed();
        else if (target == "freeze") toggleFreeze();
        else if (target == "mute")  toggleMute();
        else if (target == "stage") setStageMode (! stageMode);
        else if (target == "sync")  syncLoops (false);
        else if (target == "recSet") toggleSetRecording();
        else if (target == "dub")   dubPressed();
        else if (target == "undo")  undoPressed();
        else if (target == "scene.next") stepScene (1);
        else if (target == "scene.prev") stepScene (-1);
        else if (target.startsWith ("scene."))
        {
            for (int i = 0; i < (int) liveSet.scenes.size(); ++i)
                if (target == "scene." + liveSet.scenes[(size_t) i].uid)
                    recallScene (i);
        }
        else if (target.startsWith ("arm."))
        {
            if (const int i = target.getTrailingIntValue(); i < (int) liveSet.inputs.size())
                setArmed (i, ! liveSet.inputs[(size_t) i].armed);
        }
    }
}

void MainComponent::applyFaderMidi (const juce::String& target, ChannelStrip& strip, float proportion)
{
    applyFaderMidi (target, strip.fader, proportion, [&strip] (float p) { strip.setHardwarePosition (p); });
}

void MainComponent::applyFaderMidi (const juce::String& target, juce::Slider& fader, float proportion,
                                    const std::function<void (float)>& showHardwarePosition)
{
    // Soft takeover: the hardware control only takes over once it reaches the
    // on-screen position, so a knob left somewhere else can't cause a jump.
    auto& t = takeover[target];
    const float current = (float) fader.valueToProportionOfLength (fader.getValue());

    // Moved on screen since the hardware last set it: the hardware has to catch up again.
    if (t.locked && std::abs (current - t.last) > 0.02f)
        t.locked = false;

    if (! t.locked)
    {
        const bool crossed = t.last >= 0.0f && (t.last - current) * (proportion - current) <= 0.0f;
        t.locked = std::abs (proportion - current) < 0.03f || crossed;
    }
    t.last = proportion;

    if (t.locked)
    {
        fader.setValue (fader.proportionOfLengthToValue (proportion));
        if (showHardwarePosition) showHardwarePosition (-1.0f);
    }
    else if (showHardwarePosition)
    {
        showHardwarePosition (proportion);
    }
}

void MainComponent::showLearnMenu (const juce::String& target, const juce::String& label)
{
    const auto binding = midiMap.get (target);

    juce::PopupMenu m;
    m.addSectionHeader (label + ": " + binding.describe());
    m.addItem (1, "Learn MIDI control");
    m.addItem (2, "Clear MIDI", binding.isValid());
    m.addSeparator();
    m.addItem (3, "Clear all MIDI assignments");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, target, label] (int r)
    {
        if (r == 1) { learnTarget = target; learnLabel = label; }
        if (r == 2) { midiMap.clear (target); saveMidiMap(); }
        if (r == 3)
        {
            askChoice ("Clear all MIDI", "Remove every MIDI assignment? The MidiMix master fader and first two REC ARM buttons go back to their defaults.",
                       { "Clear", "Cancel" }, [this] (int c)
                       {
                           if (c != 1) return;
                           midiMap.setDefaults();
                           saveMidiMap();
                           updateRowViews();
                       });
        }
        updateMidiLeds();
        repaint();
    });
}

void MainComponent::updateMidiLeds (bool allOff)
{
    if (ledOutput == nullptr)
        return;

    // Lights only make sense for buttons on the MidiMix itself.
    std::map<int, bool> wanted;
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    auto want = [&] (const juce::String& target, bool lit)
    {
        const auto b = midiMap.get (target);
        if (b.isValid() && b.isNote
            && (b.device.isEmpty() || b.device.removeCharacters (" ").containsIgnoreCase ("MIDIMix")))
            wanted[b.number] = wanted[b.number] || lit;
    };

    for (const auto& row : liveSet.rows)
    {
        want ("grp." + row.uid + ".mute", ! row.muted);   // lit = group is on, like most DAWs
        for (const auto& slot : row.slots)
        {
            // A loop waiting for its beat (to start or to stop) blinks.
            const auto cue = row.engine >= 0 ? engineFor (row).cueFor (slot.id) : LoopChannel::Cue::none;
            want ("ch." + slot.uid + ".play", cue != LoopChannel::Cue::none ? blink : slot.playing);
        }
    }
    for (int i = 0; i < (int) liveSet.inputs.size(); ++i)
        want ("arm." + juce::String (i), liveSet.inputs[(size_t) i].armed);
    want ("mute", outputMuted);

    if (allOff)
        for (auto& [note, lit] : wanted)
            lit = false;

    // Turn off lights for buttons that no longer have a job, then send what changed.
    for (auto& [note, lit] : ledState)
        if (lit && wanted.find (note) == wanted.end())
            wanted[note] = false;

    for (auto& [note, lit] : wanted)
    {
        auto it = ledState.find (note);
        if (it == ledState.end() || it->second != lit)
            ledOutput->sendMessageNow (juce::MidiMessage::noteOn (1, note, (juce::uint8) (lit ? 127 : 0)));
        ledState[note] = lit;
    }
}

void MainComponent::applySetMidi()
{
    // The set brings its own channel and group assignments.
    midiMap.removeIf ([] (const juce::String& t) { return MidiMap::isSetTarget (t); });
    if (liveSet.midi != nullptr)
        midiMap.addFromXml (*liveSet.midi);
    takeover.clear();
    updateMidiLeds();
}

bool MainComponent::findSlotByUid (const juce::String& uid, int& row, int& slot) const
{
    for (int r = 0; r < (int) liveSet.rows.size(); ++r)
        for (int i = 0; i < (int) liveSet.rows[(size_t) r].slots.size(); ++i)
            if (liveSet.rows[(size_t) r].slots[(size_t) i].uid == uid)
            {
                row = r;
                slot = i;
                return true;
            }
    return false;
}

void MainComponent::saveMidiMap()
{
    // The computer keeps its own controls; the set keeps its channels' and groups'.
    settings.getUserSettings()->setValue ("midiMap", midiMap.toXml ([] (const juce::String& t) { return ! MidiMap::isSetTarget (t); }).get());
    liveSet.midi = std::shared_ptr<juce::XmlElement> (midiMap.toXml ([] (const juce::String& t) { return MidiMap::isSetTarget (t); }).release());
    saveSession();
}

void MainComponent::syncLoops (bool restart)
{
    struct Playing { LiveSet::Row* row; LiveSet::Slot* slot; };
    std::vector<Playing> playing;
    LiveSet::Slot* reference = nullptr;

    for (auto& r : liveSet.rows)
        for (auto& s : r.slots)
            if (s.playing && ! s.loading && s.anchor >= 0)
            {
                playing.push_back ({ &r, &s });
                if (reference == nullptr || s.anchor < reference->anchor)
                    reference = &s;
            }

    if (playing.empty())
    {
        infoMessage = "No loops playing to sync";
        repaint();
        return;
    }

    // An overdub records against its loop's timing, so that loop stays put.
    if (auto [row, dubbed] = liveSet.findSlot (overdubSlotId); dubbed != nullptr && dubbed->playing)
        reference = dubbed;

    const bool keepReference = ! restart || overdubSlotId >= 0;
    const auto anchor = keepReference ? reference->anchor : LoopPlayer::blockClock.load();
    int moved = 0;

    for (auto& p : playing)
    {
        if (p.slot->anchor == anchor)
            continue;

        p.slot->anchor = anchor;
        engineFor (*p.row).realignLoop (p.slot->id, anchor);
        ++moved;
    }

    beatAnchor = anchor;   // the beat lights count from the loops' shared start

    if (keepReference)
        infoMessage = moved == 0 ? "Loops already in sync"
                                 : "Synced " + juce::String (moved) + (moved == 1 ? " loop" : " loops") + " to " + (reference->label.isNotEmpty() ? reference->label : reference->file.getFileNameWithoutExtension());
    else
        infoMessage = "Restarted " + juce::String (moved) + (moved == 1 ? " loop" : " loops") + " together";

    repaint();
}

void MainComponent::toggleFreeze()
{
    freezeHold.held = ! freezeHold.held.load();
    updateFreezeButton();
}

void MainComponent::updateFreezeButton()
{
    // Ice blue while held; a dim outline while the pad fades away.
    const bool held = freezeHold.held.load();
    const auto ice = juce::Colour (0xff7cc8ff);
    freezeButton.setColour (juce::TextButton::buttonColourId, held ? ice : freezeHold.isSounding() ? ice.withAlpha (0.25f) : Theme::raised);
    freezeButton.setColour (juce::TextButton::textColourOffId, held ? Theme::background : Theme::text);
    freezeButton.setButtonText (held ? "FROZEN" : "FREEZE");
}

void MainComponent::toggleSetRecording()
{
    if (setRecorder.isRecording())
    {
        const auto file = setRecorder.getFile();
        const bool dropped = setRecorder.hasDroppedAudio();
        const double seconds = setRecorder.getSeconds();
        setRecorder.stop();
        infoMessage = "Set saved: " + file.getFileName() + " (" + formatTime (seconds) + ", "
                    + juce::File::descriptionOfSizeInBytes (file.getSize()) + ") - right-click REC SET to find it";
        if (dropped)
            errorMessage = "The disk couldn't keep up, so the set recording has gaps";
    }
    else
    {
        const auto file = recordingsFolder().getChildFile ("Sets")
                              .getNonexistentChildFile ("Set " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), ".wav");
        if (currentSampleRate <= 0.0 || ! setRecorder.start (file, currentSampleRate))
            errorMessage = "Couldn't start recording the set to " + file.getFullPathName();
        else
            infoMessage = "Recording the set to " + file.getFileName();
    }

    updateRecordSetButton();
    repaint();
}

void MainComponent::updateRecordSetButton()
{
    const bool on = setRecorder.isRecording();
    const auto text = on ? "REC  " + formatTime (setRecorder.getSeconds()) : juce::String ("REC SET");
    if (recordSetButton.getButtonText() != text)
        recordSetButton.setButtonText (text);
    recordSetButton.setColour (juce::TextButton::buttonColourId, on ? Theme::danger : Theme::danger.darker (0.9f));
}

void MainComponent::updateFireButtons (bool allOff)
{
    if (fireOutputs.empty())
        return;

    // The Fire lights its buttons from a CC with the button's number. For REC:
    // 0 off, 1 dim red, 3 bright red, 4 bright amber. For PLAY: 0 off, 3 bright yellow, 4 bright green.
    bool counting = false;
    const double beats = currentBeatPosition (counting);
    const bool onBeat = beats - std::floor (beats) < 0.25;   // the first quarter of each beat
    const auto state = looper.getState();

    std::map<int, int> wanted;
    const auto rec = midiMap.get ("rec");
    if (rec.isValid() && rec.isNote && rec.device.containsIgnoreCase (MidiMap::fireName))
    {
        if (state == Looper::State::waiting)
            wanted[rec.number] = onBeat ? 3 : 1;   // blinks through the count-in
        else
            wanted[rec.number] = state == Looper::State::overdubbing ? 4 : state != Looper::State::idle ? 3 : 1;
    }

    // PLAY is a beat light: green on 1, yellow on the other beats, while anything's in time.
    if (anyLoopPlaying() || state != Looper::State::idle)
        wanted[MidiMap::firePlayNote] = onBeat ? ((int) std::floor (beats) % liveSet.beatsPerBar == 0 ? 4 : 3) : 0;

    // Buttons that no longer have a job go dark.
    for (auto& [number, value] : fireButtonState)
        if (wanted.find (number) == wanted.end())
            wanted[number] = 0;

    for (auto& [number, value] : wanted)
    {
        if (allOff)
            value = 0;
        auto it = fireButtonState.find (number);
        if (it == fireButtonState.end() || it->second != value)
            for (auto& fire : fireOutputs)
                fire.output->sendMessageNow (juce::MidiMessage::controllerEvent (1, number, value));
        fireButtonState[number] = value;
    }
}

double MainComponent::setTimerSeconds() const
{
    const double ms = setTimerRunning ? juce::Time::getMillisecondCounterHiRes() - setTimerStartMs : setTimerElapsedMs;
    return ms * 0.001;
}

void MainComponent::toggleSetTimer()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (setTimerRunning)
        setTimerElapsedMs = now - setTimerStartMs;   // pause, keeping the time so far
    else
        setTimerStartMs = now - setTimerElapsedMs;   // start, or carry on from a pause

    setTimerRunning = ! setTimerRunning;
    setTimerButton.setButtonText (setTimerRunning ? "STOP" : "START");
    setTimerButton.setColour (juce::TextButton::buttonColourId, setTimerRunning ? Theme::accent.darker (0.2f) : Theme::raised);
    setTimerButton.setColour (juce::TextButton::textColourOffId, setTimerRunning ? Theme::background : Theme::text);
    repaint (setTimerArea);
}

void MainComponent::showSetTimerMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Reset timer");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setTimerButton), [this] (int r)
    {
        if (r != 1)
            return;
        setTimerElapsedMs = 0.0;
        setTimerStartMs = juce::Time::getMillisecondCounterHiRes();
        setTimerText = "0:00";
        repaint (setTimerArea);
    });
}

void MainComponent::timerCallback()
{
    updateMidiLeds();   // sends only lights that changed (channels starting, groups muting)
    updateFireButtons();
    freezeHold.releaseSeconds = (float) fadeTimeSlider.getValue();
    updateFreezeButton();
    updateRecordSetButton();
    updateMuteButton();
    updatePerfMeter();
    updateFireLightShow();

    if (juce::Time::getMillisecondCounter() - lastPresetCheck > 2000)
    {
        lastPresetCheck = juce::Time::getMillisecondCounter();
        for (int i = 0; i < (int) liveSet.instruments.size(); ++i)
            if (editorWindows[(size_t) i] != nullptr)
                refreshPresetName (i);
    }
    watchAudioDevice();

    {
        // The set timer reads h:mm:ss once it passes an hour.
        const int total = (int) setTimerSeconds();
        const auto text = total >= 3600 ? juce::String::formatted ("%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60)
                                        : juce::String::formatted ("%d:%02d", total / 60, total % 60);
        if (text != setTimerText)
        {
            setTimerText = text;
            repaint (setTimerArea);
        }
    }

    std::vector<PendingMidi> incoming;
    midiFifo.read (midiFifo.getNumReady()).forEach ([&] (int index) { incoming.push_back (midiQueue[(size_t) index]); });

    for (auto& p : incoming)
        handleMidi (p.device, p.message);

    for (int i = 0; i < rowViews.size() && i < (int) liveSet.rows.size(); ++i)
    {
        auto& row = liveSet.rows[(size_t) i];
        auto& engine = engineFor (row);

        const auto [l, r] = engine.takePeaks();
        rowViews[i]->strip.meter.pushPeak (0, l);
        rowViews[i]->strip.meter.pushPeak (1, r);

        for (int s = 0; s < (int) row.slots.size(); ++s)
            rowViews[i]->pushChannelPeak (s, engine.takeOwnerPeak (row.slots[(size_t) s].id));
    }

    collectFinishedTake();
    updateLooperStatus();

    {
        bool recording = false;
        const double beats = currentBeatPosition (recording);
        beatIndicator.setPosition (beats, recording);
    }
    updateCountIn();
    updateMorph();
    updateSceneBar();
    updateStageView();

    for (size_t i = 0; i < liveSet.instruments.size() && (int) i < inputsView.instrumentStrips.size(); ++i)
    {
        const auto [l, r] = instrumentEngines[i]->takePeaks();
        inputsView.instrumentStrips[(int) i]->strip.meter.pushPeak (0, l);
        inputsView.instrumentStrips[(int) i]->strip.meter.pushPeak (1, r);
    }

    // Keep the instruments' own settings in the autosave (they can change inside their windows).
    const bool anyInputPlugin = std::any_of (liveSet.inputs.begin(), liveSet.inputs.end(),
                                             [] (const LiveSet::Input& in) { return in.insertXml.isNotEmpty(); })
                             || std::any_of (liveSet.rows.begin(), liveSet.rows.end(), [] (const Row& r) { return ! r.insert.isEmpty(); })
                             || ! liveSet.masterInsert.isEmpty();
    if ((! liveSet.instruments.empty() || anyInputPlugin) && juce::Time::getMillisecondCounter() - lastStateCapture > 30000)
    {
        captureInstrumentStates();
        saveSession();
    }

    for (int i = 0; i < inputsView.inputStrips.size(); ++i)
    {
        const auto [l, r] = inputEngines[(size_t) i]->takePeaks();
        inputsView.inputStrips[i]->strip.meter.pushPeak (0, l);
        inputsView.inputStrips[i]->strip.meter.pushPeak (1, r);
    }

    masterStrip.meter.pushPeak (0, masterPeakL.exchange (0.0f));
    masterStrip.meter.pushPeak (1, masterPeakR.exchange (0.0f));

    updateRowViews();

    // Take keyboard focus back after dialogs and menus so Esc always works.
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! hasKeyboardFocus (true)
        && juce::Component::getCurrentlyModalComponent() == nullptr)
        grabKeyboardFocus();
}

void MainComponent::toggleMute()
{
    outputMuted = ! outputMuted;
    updateMuteButton();
}

void MainComponent::updateMuteButton()
{
    // Muted: blinks red, so nobody wonders why there's no sound.
    const bool muted = outputMuted;
    const bool on = muted && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    muteButton.setButtonText (muted ? "MUTED" : "MUTE OUT");
    muteButton.setColour (juce::TextButton::buttonColourId, on ? Theme::danger : muted ? Theme::danger.darker (1.0f) : Theme::raised);
    muteButton.setColour (juce::TextButton::textColourOffId, muted ? juce::Colours::white : Theme::text);
}

void MainComponent::updatePerfMeter()
{
    const auto now = juce::Time::getMillisecondCounter();
    const int xruns = deviceManager.getXRunCount();

    // Loading the set and plug-ins at startup can stutter once; that isn't a performance problem.
    if (now - startedMs < 8000 || xruns < dropoutBase)
        dropoutBase = xruns;

    const int dropouts = xruns - dropoutBase;
    if (dropouts > lastDropouts)
        lastDropoutMs = now;
    lastDropouts = dropouts;

    perfMeter.setValues (deviceOpen ? deviceManager.getCpuUsage() : 0.0, dropouts,
                         lastDropoutMs != 0 && now - lastDropoutMs < 5000);
}

bool MainComponent::deviceAvailable (const juce::String& type, const juce::String& name)
{
    for (auto* t : deviceManager.getAvailableDeviceTypes())
        if (t->getTypeName() == type)
            return t->getDeviceNames (false).contains (name) || t->getDeviceNames (true).contains (name);
    return false;
}

void MainComponent::watchAudioDevice()
{
    const auto now = juce::Time::getMillisecondCounter();

    if (lostDeviceName.isEmpty())
    {
        // Open but silent for 2 s: the driver has stopped (e.g. the interface hung). Close it and
        // reopen it below, the same way as after an unplug.
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device != nullptr && device->isPlaying() && now - lastCallbackMs.load() > 2000)
        {
            lostDeviceName = device->getName();
            deviceManager.closeAudioDevice();
            changeListenerCallback (&deviceManager);
        }
        return;
    }

    if (now - lastRecoveryTry < 1500)
        return;
    lastRecoveryTry = now;

    for (auto* t : deviceManager.getAvailableDeviceTypes())
        if (t->getTypeName() == lastDeviceType)
            t->scanForDevices();

    if (! deviceAvailable (lastDeviceType, lostDeviceName))
        return;

    // It's back: reopen it exactly as it was.
    const auto name = lostDeviceName;
    lostDeviceName.clear();
    if (deviceManager.getCurrentAudioDeviceType() != lastDeviceType)
        deviceManager.setCurrentAudioDeviceType (lastDeviceType, true);
    const auto error = deviceManager.setAudioDeviceSetup (lastSetup, true);

    if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        lostDeviceName = name;   // not ready yet; try again shortly
        deviceManager.closeAudioDevice();
    }
    else
    {
        infoMessage = name + " is back - audio reconnected.";
    }

    changeListenerCallback (&deviceManager);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header and footer bands, a step lighter than the workspace, with a hairline edge.
    const int headerBottom = headerArea.getBottom() + 8, footerTop = messageArea.getY() - 6;
    g.setGradientFill (juce::ColourGradient (Theme::panel.brighter (0.04f), 0.0f, 0.0f,
                                             Theme::panel, 0.0f, (float) headerBottom, false));
    g.fillRect (0, 0, getWidth(), headerBottom);
    g.setColour (Theme::panel);
    g.fillRect (0, footerTop, getWidth(), getHeight() - footerTop);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine (headerBottom, 0.0f, (float) getWidth());
    g.drawHorizontalLine (footerTop, 0.0f, (float) getWidth());

    // Header: app name, set name and audio device status.
    auto header = headerArea;
    static const auto logoImage = juce::ImageCache::getFromMemory (BinaryData::AppIcon_png, BinaryData::AppIcon_pngSize);
    g.drawImage (logoImage, header.removeFromLeft (32).withSizeKeepingCentre (32, 32).toFloat(), juce::RectanglePlacement::centred);

    header.removeFromLeft (10);
    g.setColour (Theme::text);
    g.setFont (Theme::heading (19.0f, 0.22f));
    g.drawText ("AIRLOOM", header.removeFromLeft (124), juce::Justification::centredLeft);

    g.setColour (Theme::panelEdge);
    g.fillRect (header.removeFromLeft (1).reduced (0, 6));
    header.removeFromLeft (16);

    g.setColour (liveSet.file.existsAsFile() ? Theme::text : Theme::textDim);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawFittedText (liveSet.getName(), header.removeFromLeft (220), juce::Justification::centredLeft, 1);

    // The set timer: big enough to read at a glance from the stage.
    {
        const bool started = setTimerRunning || setTimerElapsedMs > 0.0;
        auto area = setTimerArea.toFloat();
        g.setColour (juce::Colour (0xff08090b));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (setTimerRunning ? Theme::accent.withAlpha (0.5f) : Theme::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

        g.setColour (Theme::textDim);
        g.setFont (Theme::heading (9.5f, 0.12f));
        g.drawText ("SET", area.removeFromLeft (34.0f), juce::Justification::centred);

        g.setColour (setTimerRunning ? Theme::accent : started ? Theme::warn : Theme::textDim);
        g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        g.drawText (setTimerText, area.withTrimmedRight (10.0f), juce::Justification::centredRight);
    }

    // Device status as a pill.
    const auto statusColour = deviceOpen ? Theme::accent : Theme::danger;
    g.setFont (juce::FontOptions (13.0f));
    const int pillWidth = juce::jmin (header.getWidth(),
                                      juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), deviceStatus) + 40);
    auto pill = header.removeFromLeft (pillWidth).withSizeKeepingCentre (pillWidth, 26).toFloat();
    g.setColour (statusColour.withAlpha (0.08f));
    g.fillRoundedRectangle (pill, 13.0f);
    g.setColour (statusColour.withAlpha (0.3f));
    g.drawRoundedRectangle (pill.reduced (0.5f), 13.0f, 1.0f);
    auto dot = pill.removeFromLeft (26.0f).withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (statusColour);
    g.fillEllipse (dot);
    g.setColour (deviceOpen ? Theme::text.withAlpha (0.8f) : Theme::danger);
    g.drawText (deviceStatus, pill.withTrimmedRight (12.0f), juce::Justification::centredLeft, true);

    // One status line: MIDI learn prompt, then errors, then confirmations, then a hint or MIDI devices.
    g.setFont (juce::FontOptions (15.0f));
    juce::String message = midiStatus;
    auto colour = Theme::textDim;

    if (learnTarget.isNotEmpty())
    {
        message = "MIDI LEARN: move or press a control for " + learnLabel + "   (Esc to cancel)";
        colour = Theme::warn;
    }
    else if (errorMessage.isNotEmpty())
    {
        message = errorMessage;
        colour = Theme::danger;
    }
    else if (infoMessage.isNotEmpty())
    {
        message = infoMessage;
        colour = Theme::accent;
    }
    else if (liveSet.pool.isEmpty())
    {
        message = "Load your samples on the left, then drag them onto the channels. Click a name for options.";
    }

    g.setColour (colour);
    g.drawText (message, messageArea, juce::Justification::centredLeft, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (36);
    settingsButton.setBounds (headerArea.removeFromRight (140).reduced (0, 2));
    headerArea.removeFromRight (8);
    setButton.setBounds (headerArea.removeFromRight (90).reduced (0, 2));
    headerArea.removeFromRight (8);
    addRowButton.setBounds (headerArea.removeFromRight (90).reduced (0, 2));
    headerArea.removeFromRight (8);
    stageButton.setBounds (headerArea.removeFromRight (90).reduced (0, 2));
    headerArea.removeFromRight (20);
    setTimerButton.setBounds (headerArea.removeFromRight (72).reduced (0, 2));
    headerArea.removeFromRight (8);
    setTimerArea = headerArea.removeFromRight (120);
    headerArea.removeFromRight (8);
    recordSetButton.setBounds (headerArea.removeFromRight (96).reduced (0, 2));
    headerArea.removeFromRight (12);
    perfMeter.setBounds (headerArea.removeFromRight (176));
    headerArea.removeFromRight (12);
    area.removeFromTop (12);

    // Bottom bar: stop all, the looper, fade time, then tempo on the right.
    auto bottom = area.removeFromBottom (60);

    // In a narrow window (a laptop, or not maximised) everything tightens up and the
    // "BPM" label goes, so the looper keeps room for its length box.
    const bool narrow = bottom.getWidth() < 1720;
    const int gap = narrow ? 12 : 20;

    stopAllButton.setBounds (bottom.removeFromLeft (narrow ? 110 : 150));
    bottom.removeFromLeft (10);
    freezeButton.setBounds (bottom.removeFromLeft (narrow ? 90 : 110).withSizeKeepingCentre (narrow ? 90 : 110, 40));
    bottom.removeFromLeft (gap);
    auto controls = bottom.withSizeKeepingCentre (bottom.getWidth(), 40);

    auto tempo = controls.removeFromRight (narrow ? 442 : 562);
    const int tempoGap = narrow ? 6 : 8;
    tapButton.setBounds (tempo.removeFromRight (narrow ? 56 : 64));
    tempo.removeFromRight (tempoGap);
    syncButton.setBounds (tempo.removeFromRight (narrow ? 58 : 64));
    tempo.removeFromRight (tempoGap);
    loopStartBox.setBounds (tempo.removeFromRight (narrow ? 104 : 112).reduced (0, 4));
    tempo.removeFromRight (tempoGap);
    bpmSlider.setBounds (tempo.removeFromRight (narrow ? 88 : 110));
    tempo.removeFromRight (narrow ? 8 : 12);
    beatIndicator.setBounds (tempo.removeFromRight (narrow ? 112 : 136).reduced (0, 8));
    bpmLabel.setVisible (! narrow);
    bpmLabel.setBounds (tempo);
    controls.removeFromRight (narrow ? 12 : 24);

    auto fade = controls.removeFromRight (narrow ? 200 : 280);
    fadeTimeLabel.setBounds (fade.removeFromLeft (narrow ? 64 : 76));
    fadeTimeSlider.setBounds (fade);
    controls.removeFromRight (gap);

    // The looper's buttons sit centred between FREEZE and the fade time, with its
    // status text trailing to the right (moved left if that text would be squeezed).
    const int centredX = (freezeButton.getRight() + controls.getRight()) / 2 - LooperBar::buttonsWidth / 2;
    const int looperX = juce::jlimit (controls.getX(), juce::jmax (controls.getX(), controls.getRight() - LooperBar::buttonsWidth - 160), centredX);
    const int looperWidth = juce::jmin (640, controls.getRight() - looperX);
    looperBar.setBounds (controls.withX (looperX).withWidth (looperWidth));

    messageArea = area.removeFromBottom (30);
    area.removeFromBottom (6);
    stageArea = area;
    stageView.setBounds (stageArea);

    pool.setBounds (area.removeFromLeft (pool.isCollapsed() ? SamplePool::collapsedWidth : 260));
    area.removeFromLeft (16);

    {
        // MUTE OUT sits under the master fader.
        auto masterColumn = area.removeFromRight (110);
        muteButton.setBounds (masterColumn.removeFromBottom (44));
        masterColumn.removeFromBottom (8);
        masterStrip.setBounds (masterColumn);
    }
    {
        // SPACE and FX sit where the master's (hidden) mute button would be.
        auto buttons = masterStrip.muteButton.getBounds() + masterStrip.getPosition();
        spaceButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
        masterFxButton.setBounds (buttons.withTrimmedLeft (4));
    }
    area.removeFromRight (16);

    sceneBar.setBounds (area.removeFromTop (36));
    area.removeFromTop (10);
    rowsViewport.setBounds (area);
    countInOverlay.setBounds (stageMode ? stageArea : area);
    alertBanner.setBounds (area.removeFromTop (60).reduced (40, 4));
    layoutRows();
}
