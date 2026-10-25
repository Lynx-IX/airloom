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

        strip.fader.onValueChange = [this, i]
        {
            channels[(size_t) i]->setGain (dbToGain (strips[(size_t) i]->fader.getValue()));
            saveSession();
        };
        strip.fader.onDragStart = [this, faderTarget] { takeover[faderTarget].locked = false; };
        strip.fader.onRightClick = [this, faderTarget, name] { showLearnMenu (faderTarget, name + " fader"); };

        strip.muteButton.onClick = [this, i] { setMuted (i, strips[(size_t) i]->muteButton.getToggleState()); };
        strip.muteButton.onRightClick = [this, i, name] { showLearnMenu ("mute." + juce::String (i), name + " mute"); };

        strip.fileButton.onClick = [this, i] { showFileMenu (i); };
    }

    addAndMakeVisible (masterStrip);
    masterStrip.muteButton.setVisible (false);
    masterStrip.fader.onValueChange = [this]
    {
        masterTarget = dbToGain (masterStrip.fader.getValue());
        saveSession();
    };
    masterStrip.fader.onDragStart = [this] { takeover["fader.master"].locked = false; };
    masterStrip.fader.onRightClick = [this] { showLearnMenu ("fader.master", "Master fader"); };

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

    addAndMakeVisible (addKeyButton);
    addKeyButton.onClick = [this] { addKey ([this] (int k) { cueKey (k); }); };

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
    fadeTimeSlider.onValueChange = [this] { saveSession(); };

    disableKeyboardFocus (*this);   // keep Space / arrows for GO and key selection
    setWantsKeyboardFocus (true);
    setSize (1200, 760);

    openAudioDevice();

    deviceManager.addMidiInputDeviceCallback ({}, this);
    midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); });
    enableMidiInputs();

    // Pick up where the last session left off, saved set or not.
    if (auto xml = props->getXmlValue ("session"))
    {
        const juce::File setFile (props->getValue ("setFile"));
        liveSet.file = setFile.existsAsFile() ? setFile : juce::File();
        loadSetXml (*xml, liveSet.getFolder());
    }

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
    m.addSeparator();
    m.addItem (5, "Show set folder", saved);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setButton), [this] (int r)
    {
        if (r == 1) newSet();
        if (r == 2) openSet();
        if (r == 3) saveSet (false);
        if (r == 4) saveSet (true);
        if (r == 5) liveSet.file.revealToUser();
    });
}

void MainComponent::newSet()
{
    if (transportPlaying)
    {
        errorMessage = "Stop playback before starting a new set";
        repaint();
        return;
    }

    liveSet.file = juce::File();
    loadSetXml (juce::XmlElement ("LiveRigSet"), {});
}

void MainComponent::openSet()
{
    if (transportPlaying)
    {
        errorMessage = "Stop playback before opening another set";
        repaint();
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> ("Open a set",
                                                       liveSet.getFolder().getParentDirectory(),
                                                       juce::String ("*") + LiveSet::fileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (! file.existsAsFile())
                                      return;

                                  auto xml = juce::XmlDocument::parse (file);
                                  if (xml == nullptr || ! xml->hasTagName ("LiveRigSet"))
                                  {
                                      errorMessage = file.getFileName() + " is not a Live Rig set";
                                      repaint();
                                      return;
                                  }

                                  liveSet.file = file;
                                  loadSetXml (*xml, file.getParentDirectory());
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

    // Lay the folder out as a library: Keys/<key>/, Nature/, Tones/.
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

    for (auto& k : liveSet.keys)
        for (auto& f : k.pads)
            place (f, folder.getChildFile ("Keys").getChildFile (juce::File::createLegalFileName (k.name)));

    for (auto& f : liveSet.bins[0]) place (f, folder.getChildFile ("Nature"));
    for (auto& f : liveSet.bins[1]) place (f, folder.getChildFile ("Tones"));

    liveSet.relocate (moved);

    for (auto& s : state)
    {
        if (auto it = moved.find (s.current.getFullPathName()); it != moved.end())
            s.current = it->second;
        if (s.cued.has_value())
            if (auto it = moved.find (s.cued->getFullPathName()); it != moved.end())
                s.cued = it->second;
    }

    liveSet.file = folder.getChildFile (juce::File::createLegalFileName (folder.getFileName()) + LiveSet::fileExtension);

    if (! createSetXml (folder)->writeTo (liveSet.file))
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

    saveSession();
    refreshUi();
}

std::unique_ptr<juce::XmlElement> MainComponent::createSetXml (const juce::File& baseFolder) const
{
    auto xml = std::make_unique<juce::XmlElement> ("LiveRigSet");
    xml->setAttribute ("version", 1);
    liveSet.addToXml (*xml, baseFolder);

    auto* mixer = xml->createNewChildElement ("Mixer");
    for (size_t i = 0; i < strips.size(); ++i)
    {
        mixer->setAttribute ("fader" + juce::String ((int) i), strips[i]->fader.getValue());
        mixer->setAttribute ("mute" + juce::String ((int) i), channels[i]->isMuted());
    }
    mixer->setAttribute ("master", masterStrip.fader.getValue());
    mixer->setAttribute ("crossfade", fadeTimeSlider.getValue());

    auto* playing = xml->createNewChildElement ("Playing");
    playing->setAttribute ("key", activeKey >= 0 ? liveSet.keys[(size_t) activeKey].name : juce::String());
    playing->setAttribute ("nature", LiveSet::encode (state[nature].current, baseFolder));
    playing->setAttribute ("tones", LiveSet::encode (state[tones].current, baseFolder));
    return xml;
}

void MainComponent::loadSetXml (const juce::XmlElement& xml, const juce::File& baseFolder)
{
    restoring = true;

    for (size_t i = 0; i < channels.size(); ++i)
    {
        clearCue ((int) i);
        channels[i]->changeClip (nullptr, 0.0, false);
        state[i].current = juce::File();
    }

    activeKey = cuedKey = -1;
    goPending = false;
    errorMessage.clear();
    infoMessage.clear();

    liveSet.loadFromXml (xml, baseFolder);

    const auto* mixer = xml.getChildByName ("Mixer");
    for (int i = 0; i < numLoopChannels; ++i)
    {
        strips[(size_t) i]->fader.setValue (mixer != nullptr ? mixer->getDoubleAttribute ("fader" + juce::String (i)) : 0.0);
        setMuted (i, mixer != nullptr && mixer->getBoolAttribute ("mute" + juce::String (i)));
    }
    masterStrip.fader.setValue (mixer != nullptr ? mixer->getDoubleAttribute ("master") : 0.0);
    fadeTimeSlider.setValue (mixer != nullptr ? mixer->getDoubleAttribute ("crossfade", 8.0) : 8.0);

    rebuildKeyButtons();

    int missing = 0;
    for (auto& f : liveSet.allFiles())
        if (! f.existsAsFile())
            ++missing;
    if (missing > 0)
        errorMessage = juce::String (missing) + (missing == 1 ? " file in this set was" : " files in this set were") + " not found";

    // Cue what was playing; with the transport stopped it loads silently.
    if (const auto* playing = xml.getChildByName ("Playing"))
    {
        const auto keyName = playing->getStringAttribute ("key");
        for (size_t k = 0; k < liveSet.keys.size(); ++k)
            if (liveSet.keys[k].name == keyName)
                cueKey ((int) k);

        for (int ch : { (int) nature, (int) tones })
            if (const auto f = LiveSet::decode (playing->getStringAttribute (ch == nature ? "nature" : "tones"), baseFolder);
                f.existsAsFile())
                cueFile (ch, f);
    }

    restoring = false;
    saveSession();
    refreshUi();
}

void MainComponent::saveSession()
{
    if (restoring)
        return;

    auto* props = settings.getUserSettings();
    props->setValue ("session", createSetXml (liveSet.getFolder()).get());
    props->setValue ("setFile", liveSet.file.getFullPathName());
}

//==============================================================================
void MainComponent::addKey (std::function<void (int)> then)
{
    askText ("Name the new key", "Key " + juce::String ((int) liveSet.keys.size() + 1), [this, then] (juce::String name)
    {
        liveSet.keys.push_back ({ name, {} });
        rebuildKeyButtons();
        saveSession();

        if (then)
            then ((int) liveSet.keys.size() - 1);
    });
}

void MainComponent::showKeyMenu (int index)
{
    const auto& key = liveSet.keys[(size_t) index];
    auto padLabel = [&] (int p)
    {
        return key.pads[(size_t) p] == juce::File() ? juce::String ("(empty)") : key.pads[(size_t) p].getFileNameWithoutExtension();
    };

    juce::PopupMenu m;
    m.addSectionHeader (key.name);
    m.addItem (1, "Load Pad 1...   " + padLabel (0));
    m.addItem (2, "Load Pad 2...   " + padLabel (1));
    m.addItem (3, "Clear Pad 1", key.pads[0] != juce::File());
    m.addItem (4, "Clear Pad 2", key.pads[1] != juce::File());
    m.addSeparator();
    m.addItem (5, "Rename...");
    m.addItem (6, "Move left", index > 0);
    m.addItem (7, "Move right", index < (int) liveSet.keys.size() - 1);
    m.addSeparator();
    m.addItem (8, "Delete key");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (keyButtons[index]), [this, index] (int r)
    {
        if (index >= (int) liveSet.keys.size())
            return;

        if (r == 1 || r == 2) browseForPad (index, r - 1);
        if (r == 3 || r == 4) assignPad (index, r - 3, juce::File());

        if (r == 5)
            askText ("Rename key", liveSet.keys[(size_t) index].name, [this, index] (juce::String name)
            {
                if (index < (int) liveSet.keys.size())
                {
                    liveSet.keys[(size_t) index].name = name;
                    rebuildKeyButtons();
                    saveSession();
                    refreshUi();
                }
            });

        if (r == 6 || r == 7)
        {
            const int other = index + (r == 6 ? -1 : 1);
            std::swap (liveSet.keys[(size_t) index], liveSet.keys[(size_t) other]);
            auto fix = [&] (int& k) { if (k == index) k = other; else if (k == other) k = index; };
            fix (activeKey);
            fix (cuedKey);
            rebuildKeyButtons();
            saveSession();
            refreshUi();
        }

        if (r == 8) deleteKey (index);
    });
}

void MainComponent::deleteKey (int index)
{
    if (index == cuedKey)
    {
        cuedKey = -1;
        clearCue (pad1);
        clearCue (pad2);
    }
    else if (index < cuedKey)
    {
        --cuedKey;
    }

    if (index == activeKey)     activeKey = -1;   // its pads keep playing until the next key
    else if (index < activeKey) --activeKey;

    liveSet.keys.erase (liveSet.keys.begin() + index);
    rebuildKeyButtons();
    saveSession();
    refreshUi();
}

void MainComponent::browseForPad (int keyIndex, int pad)
{
    browseForFiles ("Choose Pad " + juce::String (pad + 1) + " for " + liveSet.keys[(size_t) keyIndex].name, false,
                    [this, keyIndex, pad] (juce::Array<juce::File> files)
                    {
                        if (keyIndex < (int) liveSet.keys.size())
                            assignPad (keyIndex, pad, files[0]);
                    });
}

void MainComponent::assignPad (int keyIndex, int pad, const juce::File& file)
{
    liveSet.keys[(size_t) keyIndex].pads[(size_t) pad] = file;

    // Keep what's cued in step with the key being edited.
    if (keyIndex == cuedKey || (keyIndex == activeKey && cuedKey < 0))
        cueFile (pad, file);
    else if (activeKey < 0 && cuedKey < 0)
        cueKey (keyIndex);

    saveSession();
    refreshUi();
}

void MainComponent::rebuildKeyButtons()
{
    keyButtons.clear();

    for (size_t i = 0; i < liveSet.keys.size(); ++i)
    {
        auto* b = keyButtons.add (new RightClickable<juce::TextButton> (liveSet.keys[i].name));
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, i] { cueKey ((int) i); };
        b->onRightClick = [this, i] { showKeyMenu ((int) i); };
        addAndMakeVisible (b);
    }

    resized();
}

//==============================================================================
void MainComponent::browseForFiles (const juce::String& title, bool multiple,
                                    std::function<void (juce::Array<juce::File>)> onChosen)
{
    const juce::File startDir (settings.getUserSettings()->getValue (
        "lastFolder", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));

    fileChooser = std::make_unique<juce::FileChooser> (title, startDir, formatManager.getWildcardForAllFormats());

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    if (multiple)
        chooserFlags |= juce::FileBrowserComponent::canSelectMultipleItems;

    fileChooser->launchAsync (chooserFlags,[this, onChosen] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty())
            return;

        settings.getUserSettings()->setValue ("lastFolder", files[0].getParentDirectory().getFullPathName());
        onChosen (files);
    });
}

void MainComponent::loadIntoChannel (int channel, const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    if (channel == nature || channel == tones)
    {
        addToBin (channel, files);
        cueFile (channel, files[0]);
        saveSession();
        return;
    }

    // Pads belong to a key. Two files at once fill both pads.
    auto assign = [this, channel, files] (int k)
    {
        if (files.size() >= 2)
        {
            assignPad (k, 0, files[0]);
            assignPad (k, 1, files[1]);
        }
        else
        {
            assignPad (k, channel, files[0]);
        }
    };

    if (const int k = editingKey(); k >= 0)
        assign (k);
    else
        addKey (assign);
}

void MainComponent::addToBin (int channel, const juce::Array<juce::File>& files)
{
    auto& bin = liveSet.bins[(size_t) (channel - nature)];
    for (auto& f : files)
        bin.addIfNotAlreadyThere (f);
}

void MainComponent::showFileMenu (int channel)
{
    const auto& s = state[(size_t) channel];
    juce::PopupMenu m;

    if (channel == pad1 || channel == pad2)
    {
        const int k = editingKey();
        m.addSectionHeader (k >= 0 ? liveSet.keys[(size_t) k].name + " - " + channelNames[channel] : juce::String (channelNames[channel]));

        if (k >= 0 && liveSet.keys[(size_t) k].pads[(size_t) channel] != juce::File())
            m.addItem (-1, liveSet.keys[(size_t) k].pads[(size_t) channel].getFileNameWithoutExtension(), false, true);

        m.addItem (1, k >= 0 ? "Load file..." : "Load file (creates a key)...");
        m.addItem (2, "Clear", k >= 0 && liveSet.keys[(size_t) k].pads[(size_t) channel] != juce::File());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&strips[(size_t) channel]->fileButton),
                         [this, channel] (int r)
                         {
                             if (r == 1)
                                 browseForFiles ("Choose a loop for " + juce::String (channelNames[channel]), false,
                                                 [this, channel] (juce::Array<juce::File> files) { loadIntoChannel (channel, files); });

                             if (const int k = editingKey(); r == 2 && k >= 0)
                                 assignPad (k, channel, juce::File());
                         });
        return;
    }

    const auto files = liveSet.bins[(size_t) (channel - nature)];
    m.addSectionHeader (channelNames[channel]);

    for (int i = 0; i < files.size(); ++i)
        m.addItem (100 + i, files[i].getFileNameWithoutExtension(), true, files[i] == s.current);

    if (! files.isEmpty())
        m.addSeparator();

    m.addItem (1, "Add files...");
    m.addItem (2, "Silence", s.current != juce::File());

    if (! files.isEmpty())
    {
        juce::PopupMenu remove;
        for (int i = 0; i < files.size(); ++i)
            remove.addItem (1000 + i, files[i].getFileNameWithoutExtension());
        m.addSubMenu ("Remove from list", remove);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&strips[(size_t) channel]->fileButton),
                     [this, channel, files] (int r)
                     {
                         if (r == 1)
                             browseForFiles ("Add loops to " + juce::String (channelNames[channel]), true,
                                             [this, channel] (juce::Array<juce::File> chosen) { loadIntoChannel (channel, chosen); });

                         if (r == 2)
                             cueFile (channel, juce::File());

                         if (r >= 100 && r < 100 + files.size())
                             cueFile (channel, files[r - 100]);

                         if (r >= 1000 && r < 1000 + files.size())
                         {
                             const auto f = files[r - 1000];
                             liveSet.bins[(size_t) (channel - nature)].removeAllInstancesOf (f);
                             if (state[(size_t) channel].cued == f)
                                 clearCue (channel);
                             saveSession();
                             refreshUi();
                         }
                     });
}

int MainComponent::editingKey() const
{
    return cuedKey >= 0 ? cuedKey : activeKey;
}

void MainComponent::cueKey (int index)
{
    if (index < 0 || index >= (int) liveSet.keys.size())
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
    const auto& pads = liveSet.keys[(size_t) index].pads;
    cueFile (pad1, pads[0]);
    cueFile (pad2, pads[1]);
}

void MainComponent::stepCuedKey (int direction)
{
    const int n = (int) liveSet.keys.size();
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
    infoMessage.clear();

    if (file == s.current && (file == juce::File() || channels[(size_t) channel]->getClip() != nullptr))
    {
        clearCue (channel);   // already playing this one
    }
    else
    {
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
    }

    // While stopped there's nothing to protect, so changes apply as soon as they're loaded.
    if (! transportPlaying && anyCued() && ! anyLoading())
        executeGo();

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

    if ((goPending || ! transportPlaying) && anyCued() && ! anyLoading())
    {
        goPending = false;
        executeGo();
    }

    refreshUi();
}

//==============================================================================
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

    if (! allChanged)
        errorMessage = "Still crossfading - press GO again when it finishes";

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
    updateMidiLeds();
    saveSession();
}

bool MainComponent::anyCued() const
{
    return std::any_of (state.begin(), state.end(), [] (const ChannelState& s) { return s.cued.has_value(); });
}

bool MainComponent::anyLoading() const
{
    return std::any_of (state.begin(), state.end(), [] (const ChannelState& s) { return s.loading; });
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

    const bool cued = transportPlaying && anyCued();
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

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& paths, int x, int y)
{
    juce::Array<juce::File> files;
    for (auto& p : paths)
        if (formatManager.findFormatForFileExtension (juce::File (p).getFileExtension()) != nullptr)
            files.add (juce::File (p));

    const juce::Point<int> pos (x, y);

    for (int i = 0; i < numLoopChannels; ++i)
        if (strips[(size_t) i]->getBounds().contains (pos))
            return loadIntoChannel (i, files);

    for (int k = 0; k < keyButtons.size(); ++k)
    {
        if (keyButtons[k]->getBounds().contains (pos))
        {
            const auto& pads = liveSet.keys[(size_t) k].pads;
            const int firstPad = (files.size() == 1 && pads[0] != juce::File() && pads[1] == juce::File()) ? 1 : 0;

            for (int i = 0; i < juce::jmin (files.size(), 2 - firstPad); ++i)
                assignPad (k, firstPad + i, files[i]);
            return;
        }
    }

    if (addKeyButton.getBounds().contains (pos))
    {
        addKey ([this, files] (int k)
        {
            for (int i = 0; i < juce::jmin (files.size(), 2); ++i)
                assignPad (k, i, files[i]);
        });
        return;
    }

    errorMessage = "Drop audio files onto a channel or a key";
    repaint();
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
    if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && ! hasKeyboardFocus (true)
        && juce::Component::getCurrentlyModalComponent() == nullptr)
        grabKeyboardFocus();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header: app name, set name and audio device status.
    auto header = headerArea;
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("LIVE RIG", header.removeFromLeft (120), juce::Justification::centredLeft);

    g.setColour (liveSet.file.existsAsFile() ? Theme::text : Theme::textDim);
    g.setFont (juce::FontOptions (17.0f));
    g.drawFittedText (liveSet.getName(), header.removeFromLeft (240), juce::Justification::centredLeft, 1);

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

    if (liveSet.keys.empty())
    {
        g.setFont (juce::FontOptions (18.0f));
        g.drawFittedText ("Drag audio files onto a channel to start, or add a key with + KEY.",
                          keyArea.removeFromTop (80), juce::Justification::centredLeft, 3);
    }
    else
    {
        g.setColour (activeKey < 0 ? Theme::textDim : transportPlaying ? Theme::accent : Theme::text);
        g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
        g.drawFittedText (activeKey >= 0 ? liveSet.keys[(size_t) activeKey].name : juce::String ("-"),
                          keyArea.removeFromTop (80), juce::Justification::centredLeft, 1);
    }

    if (cuedKey >= 0)
    {
        g.setColour (Theme::warn);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawFittedText ("NEXT  " + liveSet.keys[(size_t) cuedKey].name, keyArea.removeFromTop (36),
                          juce::Justification::centredLeft, 1);
    }

    // One status line: MIDI learn prompt, then errors, then confirmations, then MIDI devices.
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
    else if (infoMessage.isNotEmpty())
    {
        g.setColour (Theme::accent);
        g.drawText (infoMessage, messageArea, juce::Justification::centredLeft, true);
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
    setButton.setBounds (headerArea.removeFromRight (90).reduced (0, 2));
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
    auto cell = [&] (int i)
    {
        return juce::Rectangle<int> (left.getX() + (i % columns) * (cellWidth + gap),
                                     left.getY() + (i / columns) * (rowHeight + gap),
                                     cellWidth, rowHeight);
    };

    for (int i = 0; i < keyButtons.size(); ++i)
        keyButtons[i]->setBounds (cell (i));
    addKeyButton.setBounds (cell (keyButtons.size()));

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
