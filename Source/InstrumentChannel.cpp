#include "InstrumentChannel.h"

InstrumentChannel::InstrumentChannel()
{
    padNotes.fill (-1);
    collector.reset (sampleRate);
}

void InstrumentChannel::prepare (double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = juce::jmax (32, newBlockSize);
    collector.reset (sampleRate);
    fx.prepare (sampleRate, blockSize);
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (muted ? 0.0f : targetGain.load());
    midi.ensureSize (4096);

    const juce::SpinLock::ScopedLockType sl (pluginLock);
    stereo.setSize (2, blockSize);

    if (plugin != nullptr)
    {
        plugin->prepareToPlay (sampleRate, blockSize);
        pluginBuffer.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), blockSize);
    }
}

void InstrumentChannel::release()
{
    const juce::SpinLock::ScopedLockType sl (pluginLock);
    if (plugin != nullptr)
        plugin->releaseResources();
}

std::unique_ptr<juce::AudioPluginInstance> InstrumentChannel::setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin)
{
    // Prepare the newcomer and its buffers before swapping, so the audio thread never waits long.
    juce::AudioBuffer<float> newPluginBuffer, newStereo (2, blockSize);

    if (newPlugin != nullptr)
    {
        newPlugin->enableAllBuses();
        newPlugin->setPlayHead (&playHead);
        newPlugin->setNonRealtime (false);
        newPlugin->prepareToPlay (sampleRate, blockSize);
        newPluginBuffer.setSize (juce::jmax (2, newPlugin->getTotalNumInputChannels(), newPlugin->getTotalNumOutputChannels()), blockSize);
    }

    {
        const juce::SpinLock::ScopedLockType sl (pluginLock);
        std::swap (plugin, newPlugin);
        std::swap (pluginBuffer, newPluginBuffer);
        std::swap (stereo, newStereo);
    }

    if (newPlugin != nullptr)
        newPlugin->releaseResources();

    return newPlugin;   // the previous plugin; the caller frees it off the audio thread
}

void InstrumentChannel::setMidiSource (const MidiSource& source)
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    midiSource = source;
}

void InstrumentChannel::setLayout (const PadLayout& newLayout)
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    layout = newLayout;
}

void InstrumentChannel::handleMidi (const juce::String& deviceName, const juce::String& deviceId, const juce::MidiMessage& m)
{
    // The MidiMix is a mixer, never a keyboard.
    if (deviceName.removeCharacters (" ").containsIgnoreCase ("MIDIMix"))
        return;

    MidiSource source;
    PadLayout pads;
    {
        const juce::SpinLock::ScopedLockType sl (configLock);
        source = midiSource;
        pads = layout;
    }

    if (! playsFrom (source, deviceName, deviceId))
        return;

    if (deviceName.containsIgnoreCase ("FIRE"))
    {
        // Only the 64 pads play notes; the Fire's buttons stay free for other jobs.
        if (! m.isNoteOnOrOff())
            return;

        const int pad = m.getNoteNumber() - firstFirePadNote;
        if (! juce::isPositiveAndBelow (pad, 64))
            return;

        juce::MidiMessage out;
        if (m.isNoteOn())
        {
            const int note = pads.noteForPad (pad);
            padNotes[(size_t) pad] = note;
            out = juce::MidiMessage::noteOn (1, note, m.getVelocity());
        }
        else
        {
            // Release the note this pad actually started, even if the layout changed meanwhile.
            const int note = padNotes[(size_t) pad] >= 0 ? padNotes[(size_t) pad] : pads.noteForPad (pad);
            padNotes[(size_t) pad] = -1;
            out = juce::MidiMessage::noteOff (1, note);
        }

        out.setTimeStamp (m.getTimeStamp());
        collector.addMessageToQueue (out);
        return;
    }

    if (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isAftertouch() || m.isChannelPressure())
        collector.addMessageToQueue (m);
}

void InstrumentChannel::process (juce::AudioBuffer<float>& out, int startSample, int numSamples, juce::AudioBuffer<float>& recordBus)
{
    const juce::SpinLock::ScopedTryLockType sl (pluginLock);

    if (! sl.isLocked() || plugin == nullptr)
    {
        // Nothing to play: drop the queued notes so they don't all fire later.
        midi.clear();
        collector.removeNextBlockOfMessages (midi, juce::jmax (1, numSamples));
        midi.clear();
        return;
    }

    gain.setTargetValue (muted ? 0.0f : targetGain.load());
    const bool record = armed, wet = recordWithFx;
    const int pluginChannels = pluginBuffer.getNumChannels();

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, blockSize);

        midi.clear();
        collector.removeNextBlockOfMessages (midi, n);

        // A view onto the preallocated buffer at this chunk's length: no allocation.
        juce::AudioBuffer<float> view (pluginBuffer.getArrayOfWritePointers(), pluginChannels, n);
        view.clear();
        plugin->processBlock (view, midi);

        stereo.copyFrom (0, 0, view, 0, 0, n);
        stereo.copyFrom (1, 0, view, plugin->getTotalNumOutputChannels() > 1 ? 1 : 0, 0, n);

        peakL = juce::jmax (peakL.load(), stereo.getMagnitude (0, 0, n));
        peakR = juce::jmax (peakR.load(), stereo.getMagnitude (1, 0, n));

        const int recordable = juce::jmax (0, juce::jmin (n, recordBus.getNumSamples() - done));
        // Recorded at the fader level, so a new loop matches what you were hearing.
        auto addToRecording = [&]
        {
            for (int ch = 0; ch < juce::jmin (2, recordBus.getNumChannels()); ++ch)
                recordBus.addFrom (ch, done, stereo, ch, 0, recordable, targetGain.load());
        };

        if (record && ! wet)
            addToRecording();

        if (fx.isActive())
            fx.process (stereo, 0, n);

        if (record && wet)
            addToRecording();

        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
                stereo.setSample (ch, i, stereo.getSample (ch, i) * g);
        }

        // To the speakers, and (after the fader) to the shared Space reverb.
        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
            out.addFrom (ch, startSample + done, stereo, ch, 0, n);
        for (int ch = 0; ch < 2; ++ch)
            SpaceReverb::addToBus (ch, done, stereo.getReadPointer (ch), n, fx.getSpaceSend());

        done += n;
    }
}
