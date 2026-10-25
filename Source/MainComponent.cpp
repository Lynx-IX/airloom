#include "MainComponent.h"
#include "Theme.h"

namespace
{
    const char* const channelNames[] = { "Pad 1", "Pad 2", "Nature", "Tones" };

    float dbToGain (double db)
    {
        return db <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) db);
    }

    void disableKeyboardFocus (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            child->setWantsKeyboardFocus (false);
            disableKeyboardFocus (*child);
        }
    }

    void colourButton (juce::TextButton& b, juce::Colour fill, juce::Colour text)
    {
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, text);
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

    for (int i = 0; i < numLoopChannels; ++i)
    {
        const juce::String name (channelNames[i]);
        const auto faderTarget = "fader." + juce::String (i);

        channels[(size_t) i] = std::make_unique<LoopChannel> (name);
        strips[(size_t) i] = std::make_unique<ChannelStrip> (name);
        auto& strip = *strips[(size_t) i];
        addAndMakeVisible (strip);

        strip.fader.onValueChange = [this, i, faderTarget]
        {
            const auto db = strips[(size_t) i]->fader.getValue();
            channels[(size_t) i]->setGain (dbToGain (db));
            settings.getUserSettings()->setValue (faderTarget, db);
        };
        strip.fader.onDragStart = [this, faderTarget] { takeover[faderTarget].locked = false; };
        strip.fader.onRightClick = [this, faderTarget, name] { showLearnMenu (faderTarget, name + " fader"); };
        strip.fader.setValue (props->getDoubleValue (faderTarget, 0.0));

        strip.muteButton.onClick = [this, i] { setMuted (i, strips[(size_t) i]->muteButton.getToggleState()); };
        strip.muteButton.onRightClick = [this, i, name] { showLearnMenu ("mute." + juce::String (i), name + " mute"); };
        setMuted (i, props->getBoolValue ("mute." + juce::String (i)));

        strip.fileButton.onClick = [this, i] { showFileMenu (i); };
    }

    addAndMakeVisible (masterStrip);
    masterStrip.muteButton.setVisible (false);
    masterStrip.fader.onValueChange = [this]
    {
        masterTarget = dbToGain (masterStrip.fader.getValue());
        settings.getUserSettings()->setValue ("fader.master", masterStrip.fader.getValue());
    };
    masterStrip.fader.onDragStart = [this] { takeover["fader.master"].locked = false; };
    masterStrip.fader.onRightClick = [this] { showLearnMenu ("fader.master", "Master fader"); };
    masterStrip.fader.setValue (props->getDoubleValue ("fader.master", 0.0));

    for (auto* b : { &goButton, &transportButton, &prevKeyButton, &nextKeyButton })
        addAndMakeVisible (b);

    goButton.onClick = [this] { go(); };
    goButton.onRightClick = [this] { showLearnMenu ("go", "GO"); };
    transportButton.onClick = [this] { toggleTransport(); };
    transportButton.onRightClick = [this] { showLearnMenu ("transport", "Start/Stop"); };
    prevKeyButton.onClick = [this] { stepCuedKey (-1); };
    prevKeyButton.onRightClick = [this] { showLearnMenu ("keyPrev", "Previous key"); };
    nextKeyButton.onClick = [this] { stepCuedKey (1); };
    nextKeyButton.onRightClick = [this] { showLearnMenu ("keyNext", "Next key"); };

    addAndMakeVisible (libraryButton);
    addAndMakeVisible (settingsButton);
    libraryButton.onClick = [this] { showLibraryMenu(); };
    settingsButton.onClick = [this] { showDeviceSettings(); };

    addAndMakeVisible (fadeTimeLabel);
    addAndMakeVisible (fadeTimeSlider);
    fadeTimeLabel.setColour (juce::Label::textColourId, Theme::textDim);
    fadeTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    fadeTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
    fadeTimeSlider.setRange (0.5, 30.0, 0.5);
    fadeTimeSlider.setTextValueSuffix (" s");
    fadeTimeSlider.setValue (props->getDoubleValue ("fadeTime", 8.0));
    fadeTimeSlider.onValueChange = [this] { settings.getUserSettings()->setValue ("fadeTime", fadeTimeSlider.getValue()); };

    disableKeyboardFocus (*this);   // keep Space / arrows for GO and key selection
    setWantsKeyboardFocus (true);
    setSize (1200, 760);

    openAudioDevice();

    deviceManager.addMidiInputDeviceCallback ({}, this);
    midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); });
    enableMidiInputs();

    const juce::File lib (props->getValue ("library"));
    if (lib.isDirectory())
        setLibrary (lib, true);
    else
        errorMessage = "Choose your loop library folder with the Library button";

    refreshUi();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    loader.removeAllJobs (true, 10000);
    updateMidiLeds (true);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeChangeListener (this);
    shutdownAudio();
}

//==============================================================================
void MainComponent::openAudioDevice()
{
    // Open two inputs now so later stages (live mic / instrument) can use them.
    // With no saved choice this opens the Windows default device.
    std::unique_ptr<juce::XmlElement> saved (settings.getUserSettings()->getXmlValue ("audioDevice"));
    setAudioChannels (2, 2, saved.get());

    deviceManager.addChangeListener (this);
    changeListenerCallback (&deviceManager);
}

void MainComponent::showDeviceSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 2, 0, 2,
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

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto* device = deviceManager.getCurrentAudioDevice();
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
        deviceStatus = "No audio device - open Audio Settings";
    }

    if (auto xml = deviceManager.createStateXml())
        settings.getUserSettings()->setValue ("audioDevice", xml.get());

    repaint();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    for (auto& c : channels)
        c->prepare (sampleRate, samplesPerBlockExpected);

    masterGain.reset (sampleRate, 0.05);
    masterGain.setCurrentAndTargetValue (masterTarget);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();   // live inputs aren't used until Stage 3
    auto& buffer = *info.buffer;

    for (size_t i = 0; i < channels.size(); ++i)
    {
        float peakL, peakR;
        channels[i]->process (buffer, info.startSample, info.numSamples, peakL, peakR);
        strips[i]->meter.pushPeak (0, peakL);
        strips[i]->meter.pushPeak (1, peakR);
    }

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

    for (int ch = 0; ch < outChannels; ++ch)
        masterStrip.meter.pushPeak (ch, buffer.getMagnitude (ch, info.startSample, info.numSamples));
}

void MainComponent::releaseResources() {}

//==============================================================================
void MainComponent::showLibraryMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Choose library folder...");
    m.addItem (2, "Rescan library", library.root.isDirectory());
    m.addItem (3, "Open library folder", library.root.isDirectory());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&libraryButton), [this] (int r)
    {
        if (r == 1) chooseLibrary();
        if (r == 2) setLibrary (library.root, false);
        if (r == 3) library.root.startAsProcess();
    });
}

void MainComponent::chooseLibrary()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose the loop library folder",
        library.root.isDirectory() ? library.root : juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().isDirectory())
                                      setLibrary (fc.getResult(), false);
                              });
}

void MainComponent::setLibrary (const juce::File& root, bool restoreSession)
{
    const auto activeName = activeKey >= 0 ? library.keys[(size_t) activeKey].name : juce::String();
    const auto cuedName = cuedKey >= 0 ? library.keys[(size_t) cuedKey].name : juce::String();

    LoopLibrary::createFolders (root);
    library.scan (root, formatManager.getWildcardForAllFormats());
    settings.getUserSettings()->setValue ("library", root.getFullPathName());

    activeKey = library.indexOfKey (activeName);
    cuedKey = library.indexOfKey (cuedName);
    rebuildKeyButtons();

    errorMessage = library.keys.empty()
                       ? "No keys yet: add one folder per key inside " + root.getChildFile ("Keys").getFullPathName()
                       : juce::String();

    if (restoreSession)
    {
        auto* props = settings.getUserSettings();

        if (const int k = library.indexOfKey (props->getValue ("activeKey")); k >= 0)
            cueKey (k);

        for (int ch : { (int) nature, (int) tones })
            if (const juce::File f (props->getValue ("file." + juce::String (ch))); f.existsAsFile())
                cueFile (ch, f);

        go();   // the transport is stopped, so this just loads everything silently
    }

    refreshUi();
}

void MainComponent::rebuildKeyButtons()
{
    keyButtons.clear();

    for (size_t i = 0; i < library.keys.size(); ++i)
    {
        auto* b = keyButtons.add (new juce::TextButton (library.keys[i].name));
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, i] { cueKey ((int) i); };
        addAndMakeVisible (b);
    }

    resized();
}

void MainComponent::cueKey (int index)
{
    if (index < 0 || index >= (int) library.keys.size())
        return;

    if (index == activeKey)
    {
        // Selecting the key that's already playing cancels the cue.
        cuedKey = -1;
        clearCue (pad1);
        clearCue (pad2);
        refreshUi();
        return;
    }

    cuedKey = index;
    const auto& files = library.keys[(size_t) index].files;
    cueFile (pad1, files.size() > 0 ? files[0] : juce::File());
    cueFile (pad2, files.size() > 1 ? files[1] : juce::File());
}

void MainComponent::stepCuedKey (int direction)
{
    const int n = (int) library.keys.size();
    if (n == 0)
        return;

    int base = cuedKey >= 0 ? cuedKey : activeKey;
    if (base < 0)
        base = direction > 0 ? -1 : 0;

    cueKey ((base + direction + n) % n);
}

void MainComponent::cueFile (int channel, const juce::File& file)
{
    auto& s = state[(size_t) channel];

    if (file == s.current && channels[(size_t) channel]->getClip() != nullptr)
    {
        clearCue (channel);   // already playing this one
        refreshUi();
        return;
    }

    s.cued = file;
    s.cuedClip = nullptr;
    const int token = ++s.loadToken;
    s.loading = file != juce::File();

    if (s.loading)
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        auto* formats = &formatManager;

        loader.addJob ([safe, formats, channel, file, token]
        {
            juce::String error;
            auto clip = loadLoopClip (file, *formats, error);

            juce::MessageManager::callAsync ([safe, channel, token, clip, error]
            {
                if (auto* self = safe.getComponent())
                    self->clipLoaded (channel, token, clip, error);
            });
        });
    }

    refreshUi();
}

void MainComponent::clearCue (int channel)
{
    auto& s = state[(size_t) channel];
    s.cued.reset();
    s.cuedClip = nullptr;
    s.loading = false;
    ++s.loadToken;
}

void MainComponent::clipLoaded (int channel, int token, LoopClipPtr clip, const juce::String& error)
{
    auto& s = state[(size_t) channel];
    if (token != s.loadToken)
        return;   // superseded by a newer cue

    s.loading = false;

    if (clip == nullptr)
    {
        errorMessage = error;
        s.cued.reset();
    }
    else
    {
        s.cuedClip = std::move (clip);
    }

    if (goPending && ! anyLoading())
    {
        goPending = false;
        executeGo();
    }

    refreshUi();
}

void MainComponent::showFileMenu (int channel)
{
    juce::Array<juce::File> files;

    if (channel == pad1 || channel == pad2)
    {
        if (const int k = cuedKey >= 0 ? cuedKey : activeKey; k >= 0)
            files = library.keys[(size_t) k].files;
    }
    else
    {
        files = channel == nature ? library.nature : library.tones;
    }

    const auto& s = state[(size_t) channel];
    juce::PopupMenu m;
    m.addSectionHeader ("Cue for " + juce::String (channelNames[channel]));
    m.addItem (1, "Silence", true, s.current == juce::File());

    for (int i = 0; i < files.size(); ++i)
        m.addItem (i + 2, files[i].getFileNameWithoutExtension(), true, files[i] == s.current);

    if (files.isEmpty())
        m.addItem (-1, "No loops found in the library folder", false);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&strips[(size_t) channel]->fileButton),
                     [this, channel, files] (int r)
                     {
                         if (r == 1)      cueFile (channel, juce::File());
                         else if (r >= 2) cueFile (channel, files[r - 2]);
                     });
}

void MainComponent::go()
{
    if (! anyCued())
        return;

    if (anyLoading())
    {
        goPending = true;   // fires as soon as the last cued loop has loaded
        refreshUi();
        return;
    }

    executeGo();
}

void MainComponent::executeGo()
{
    const double seconds = fadeTimeSlider.getValue();
    bool allChanged = true;

    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto& s = state[i];
        if (! s.cued.has_value())
            continue;

        if (channels[i]->changeClip (s.cuedClip, seconds, transportPlaying))
        {
            s.current = *s.cued;
            clearCue ((int) i);
        }
        else
        {
            allChanged = false;
        }
    }

    if (cuedKey >= 0 && ! state[pad1].cued && ! state[pad2].cued)
    {
        activeKey = cuedKey;
        cuedKey = -1;
    }

    errorMessage = allChanged ? juce::String() : "Still crossfading - press GO again when it finishes";
    saveSession();
    refreshUi();
}

void MainComponent::toggleTransport()
{
    const double seconds = fadeTimeSlider.getValue();

    if (transportPlaying)
    {
        fadeOutAll (seconds);
        return;
    }

    for (auto& c : channels)
        c->start (seconds);

    transportPlaying = true;
    refreshUi();
}

void MainComponent::fadeOutAll (double seconds)
{
    for (auto& c : channels)
        c->stop (seconds);

    transportPlaying = false;
    refreshUi();
}

void MainComponent::setMuted (int channel, bool muted)
{
    channels[(size_t) channel]->setMuted (muted);
    strips[(size_t) channel]->muteButton.setToggleState (muted, juce::dontSendNotification);
    settings.getUserSettings()->setValue ("mute." + juce::String (channel), muted);
    updateMidiLeds();
}

bool MainComponent::anyCued() const
{
    return std::any_of (state.begin(), state.end(), [] (const ChannelState& s) { return s.cued.has_value(); });
}

bool MainComponent::anyLoading() const
{
    return std::any_of (state.begin(), state.end(), [] (const ChannelState& s) { return s.loading; });
}

void MainComponent::saveSession()
{
    auto* props = settings.getUserSettings();
    props->setValue ("activeKey", activeKey >= 0 ? library.keys[(size_t) activeKey].name : juce::String());

    for (int ch : { (int) nature, (int) tones })
        props->setValue ("file." + juce::String (ch), state[(size_t) ch].current.getFullPathName());
}

void MainComponent::refreshUi()
{
    for (size_t i = 0; i < strips.size(); ++i)
    {
        const auto& s = state[i];
        juce::String cued;

        if (s.cued.has_value())
            cued = *s.cued == juce::File() ? "silence"
                                           : s.cued->getFileNameWithoutExtension() + (s.loading ? " ..." : "");

        strips[i]->setFileText (s.current.getFileNameWithoutExtension(), cued);
    }

    for (int i = 0; i < keyButtons.size(); ++i)
    {
        auto& b = *keyButtons[i];
        if (i == activeKey)     colourButton (b, Theme::accent, Theme::background);
        else if (i == cuedKey)  colourButton (b, Theme::warn, Theme::background);
        else                    colourButton (b, Theme::panel, Theme::text);
    }

    const bool cued = anyCued();
    goButton.setButtonText (goPending ? "LOADING..." : "GO");
    colourButton (goButton, cued ? Theme::warn : Theme::panel, cued ? Theme::background : Theme::textDim);

    transportButton.setButtonText (transportPlaying ? "STOP" : "START");
    colourButton (transportButton, transportPlaying ? Theme::accent : Theme::panel,
                  transportPlaying ? Theme::background : Theme::text);

    repaint();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)       { go(); return true; }
    if (key == juce::KeyPress::rightKey)       { stepCuedKey (1); return true; }
    if (key == juce::KeyPress::leftKey)        { stepCuedKey (-1); return true; }

    if (key == juce::KeyPress::escapeKey)
    {
        if (learnTarget.isNotEmpty())
            learnTarget.clear();
        else
            fadeOutAll (1.5);   // panic: quick fade, not a hard cut

        repaint();
        return true;
    }

    return false;
}

//==============================================================================
void MainComponent::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (! (m.isController() || m.isNoteOnOrOff()))
        return;

    // Called on a MIDI thread: queue it for the message thread.
    const juce::SpinLock::ScopedLockType sl (midiWriteLock);
    const auto device = source != nullptr ? source->getName() : juce::String();
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

    // The MidiMix lights its mute buttons when it receives the matching notes back.
    ledOutput.reset();
    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        if (d.name.removeCharacters (" ").containsIgnoreCase (juce::String (MidiMap::midiMixName).removeCharacters (" ")))
        {
            ledOutput = juce::MidiOutput::openDevice (d.identifier);
            break;
        }
    }

    updateMidiLeds();
    repaint();
}

void MainComponent::handleMidi (const juce::String& device, const juce::MidiMessage& m)
{
    const bool pressed = (m.isNoteOn() && m.getVelocity() > 0) || (m.isController() && m.getControllerValue() >= 64);

    if (learnTarget.isNotEmpty())
    {
        const bool isFader = learnTarget.startsWith ("fader.");

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
        if (target.startsWith ("fader."))
        {
            if (! m.isController())
                continue;

            const float v = (float) m.getControllerValue() / 127.0f;
            if (target == "fader.master")
                applyFaderMidi (target, masterStrip, v);
            else if (const int i = target.getTrailingIntValue(); i < numLoopChannels)
                applyFaderMidi (target, *strips[(size_t) i], v);

            continue;
        }

        if (! pressed)
            continue;

        if (target.startsWith ("mute."))
        {
            if (const int i = target.getTrailingIntValue(); i < numLoopChannels)
                setMuted (i, ! channels[(size_t) i]->isMuted());
        }
        else if (target == "go")        go();
        else if (target == "keyNext")   stepCuedKey (1);
        else if (target == "keyPrev")   stepCuedKey (-1);
        else if (target == "transport") toggleTransport();
    }
}

void MainComponent::applyFaderMidi (const juce::String& target, ChannelStrip& strip, float proportion)
{
    // Soft takeover: the hardware fader only takes control once it reaches the
    // on-screen position, so a fader left somewhere else can't cause a jump.
    auto& t = takeover[target];
    const float current = (float) strip.fader.valueToProportionOfLength (strip.fader.getValue());

    if (! t.locked)
    {
        const bool crossed = t.last >= 0.0f && (t.last - current) * (proportion - current) <= 0.0f;
        t.locked = std::abs (proportion - current) < 0.03f || crossed;
        t.last = proportion;
    }

    if (t.locked)
    {
        strip.fader.setValue (strip.fader.proportionOfLengthToValue (proportion));
        strip.setHardwarePosition (-1.0f);
    }
    else
    {
        strip.setHardwarePosition (proportion);
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
    m.addItem (3, "Reset all MIDI to MidiMix defaults");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, target, label] (int r)
    {
        if (r == 1) { learnTarget = target; learnLabel = label; }
        if (r == 2) { midiMap.clear (target); saveMidiMap(); }
        if (r == 3) { midiMap.setDefaults(); saveMidiMap(); }
        updateMidiLeds();
        repaint();
    });
}

void MainComponent::updateMidiLeds (bool allOff)
{
    if (ledOutput == nullptr)
        return;

    for (int i = 0; i < 8; ++i)
    {
        const auto b = midiMap.get ("mute." + juce::String (i));
        if (! b.isValid() || ! b.isNote)
            continue;

        // Lit = layer is on, like the MidiMix in most DAWs.
        const bool lit = ! allOff && i < numLoopChannels && ! channels[(size_t) i]->isMuted();
        ledOutput->sendMessageNow (juce::MidiMessage::noteOn (1, b.number, (juce::uint8) (lit ? 127 : 0)));
    }
}

void MainComponent::saveMidiMap()
{
    settings.getUserSettings()->setValue ("midiMap", midiMap.toXml().get());
}

void MainComponent::timerCallback()
{
    std::vector<PendingMidi> incoming;
    midiFifo.read (midiFifo.getNumReady()).forEach ([&] (int index) { incoming.push_back (midiQueue[(size_t) index]); });

    for (auto& p : incoming)
        handleMidi (p.device, p.message);

    // Take keyboard focus back after dialogs and menus so Space always means GO.
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! hasKeyboardFocus (true))
        grabKeyboardFocus();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header: app name and audio device status.
    auto header = headerArea;
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("LIVE RIG", header.removeFromLeft (130), juce::Justification::centredLeft);

    auto dot = header.removeFromLeft (14).withSizeKeepingCentre (10, 10).toFloat();
    g.setColour (deviceOpen ? Theme::accent : Theme::danger);
    g.fillEllipse (dot);
    header.removeFromLeft (8);
    g.setColour (deviceOpen ? Theme::textDim : Theme::danger);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (deviceStatus, header, juce::Justification::centredLeft, true);

    // Current key, as large as space allows, with the cued key underneath.
    auto keyArea = keyDisplayArea;
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("KEY", keyArea.removeFromTop (20), juce::Justification::centredLeft);

    g.setColour (activeKey < 0 ? Theme::textDim : transportPlaying ? Theme::accent : Theme::text);
    g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
    g.drawFittedText (activeKey >= 0 ? library.keys[(size_t) activeKey].name : juce::String ("-"),
                      keyArea.removeFromTop (80), juce::Justification::centredLeft, 1);

    if (cuedKey >= 0)
    {
        g.setColour (Theme::warn);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawFittedText ("NEXT  " + library.keys[(size_t) cuedKey].name, keyArea.removeFromTop (36),
                          juce::Justification::centredLeft, 1);
    }

    // One status line: MIDI learn prompt, then errors, then MIDI devices.
    g.setFont (juce::FontOptions (15.0f));
    if (learnTarget.isNotEmpty())
    {
        g.setColour (Theme::warn);
        g.drawText ("MIDI LEARN: move or press a control for " + learnLabel + "   (Esc to cancel)",
                    messageArea, juce::Justification::centredLeft, true);
    }
    else if (errorMessage.isNotEmpty())
    {
        g.setColour (Theme::danger);
        g.drawText (errorMessage, messageArea, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (Theme::textDim);
        g.drawText (midiStatus, messageArea, juce::Justification::centredLeft, true);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (36);
    settingsButton.setBounds (headerArea.removeFromRight (140).reduced (0, 2));
    headerArea.removeFromRight (8);
    libraryButton.setBounds (headerArea.removeFromRight (110).reduced (0, 2));
    headerArea.removeFromRight (12);
    area.removeFromTop (12);

    // Bottom bar: transport, GO, crossfade time.
    auto bottom = area.removeFromBottom (76);
    transportButton.setBounds (bottom.removeFromLeft (180));
    bottom.removeFromLeft (12);
    goButton.setBounds (bottom.removeFromLeft (280));
    bottom.removeFromLeft (24);
    auto fade = bottom.withSizeKeepingCentre (bottom.getWidth(), 36);
    fadeTimeLabel.setBounds (fade.removeFromLeft (90));
    fadeTimeSlider.setBounds (fade.removeFromLeft (juce::jmin (fade.getWidth(), 360)));

    messageArea = area.removeFromBottom (30);
    area.removeFromBottom (6);

    // Left column: key display and key grid.
    auto left = area.removeFromLeft (300);
    area.removeFromLeft (16);
    keyDisplayArea = left.removeFromTop (140);

    auto nav = left.removeFromTop (40);
    prevKeyButton.setBounds (nav.removeFromLeft (nav.getWidth() / 2).withTrimmedRight (4));
    nextKeyButton.setBounds (nav.withTrimmedLeft (4));
    left.removeFromTop (10);

    constexpr int columns = 3, rowHeight = 46, gap = 6;
    const int cellWidth = (left.getWidth() - (columns - 1) * gap) / columns;
    for (int i = 0; i < keyButtons.size(); ++i)
        keyButtons[i]->setBounds (left.getX() + (i % columns) * (cellWidth + gap),
                                  left.getY() + (i / columns) * (rowHeight + gap),
                                  cellWidth, rowHeight);

    // Mixer: loop channels on the left, master on the right.
    constexpr int stripGap = 10;
    const int stripWidth = juce::jmin (170, (area.getWidth() - numLoopChannels * stripGap) / (numLoopChannels + 1));
    masterStrip.setBounds (area.removeFromRight (stripWidth));

    for (auto& strip : strips)
    {
        strip->setBounds (area.removeFromLeft (stripWidth));
        area.removeFromLeft (stripGap);
    }
}
