#include "InputChannel.h"

void InputChannel::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = juce::jmax (32, maxBlockSize);

    fx.prepare (sampleRate, maxBlockSize);
    scratch.setSize (2, juce::jmax (4096, maxBlockSize));
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (muted ? 0.0f : targetGain.load());

    const juce::SpinLock::ScopedLockType sl (insertLock);
    if (insert != nullptr)
    {
        insert->prepareToPlay (sampleRate, blockSize);
        insertBuffer.setSize (juce::jmax (2, insert->getTotalNumInputChannels(), insert->getTotalNumOutputChannels()), blockSize);
    }
}

std::unique_ptr<juce::AudioPluginInstance> InputChannel::setInsert (std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    // Prepare the newcomer and its buffer first, so the audio thread never waits long.
    juce::AudioBuffer<float> newBuffer;
    if (plugin != nullptr)
    {
        plugin->enableAllBuses();
        plugin->setNonRealtime (false);
        plugin->prepareToPlay (sampleRate, blockSize);
        newBuffer.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), blockSize);
    }

    {
        const juce::SpinLock::ScopedLockType sl (insertLock);
        std::swap (insert, plugin);
        std::swap (insertBuffer, newBuffer);
    }

    if (plugin != nullptr)
        plugin->releaseResources();
    return plugin;   // the previous one, freed by the caller off the audio thread
}

void InputChannel::processInsert (int numSamples)
{
    const juce::SpinLock::ScopedTryLockType sl (insertLock);
    if (! sl.isLocked() || insert == nullptr)
        return;

    const int channels = insertBuffer.getNumChannels();
    const bool stereoOut = insert->getTotalNumOutputChannels() > 1;

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, blockSize);

        // A view onto the preallocated buffer at this chunk's length: no allocation.
        juce::AudioBuffer<float> view (insertBuffer.getArrayOfWritePointers(), channels, n);
        view.clear();
        view.copyFrom (0, 0, scratch, 0, done, n);
        view.copyFrom (1, 0, scratch, 1, done, n);

        noMidi.clear();
        insert->processBlock (view, noMidi);

        scratch.copyFrom (0, done, view, 0, 0, n);
        scratch.copyFrom (1, done, view, stereoOut ? 1 : 0, 0, n);
        done += n;
    }
}

void InputChannel::process (const juce::AudioBuffer<float>& in, int numInputs,
                            juce::AudioBuffer<float>& out, int startSample, int numSamples,
                            juce::AudioBuffer<float>& recordBus)
{
    numSamples = juce::jmin (numSamples, scratch.getNumSamples(), in.getNumSamples());

    // A mono source feeds both sides; a stereo pair is its input and the next.
    const int leftIn = firstChannel;
    const int rightIn = stereoPair ? leftIn + 1 : leftIn;

    for (int ch = 0; ch < 2; ++ch)
    {
        const int inputIndex = ch == 0 ? leftIn : rightIn;
        if (inputIndex < numInputs)
            scratch.copyFrom (ch, 0, in, inputIndex, 0, numSamples);
        else
            scratch.clear (ch, 0, numSamples);
    }

    peakL = juce::jmax (peakL.load(), scratch.getMagnitude (0, 0, numSamples));
    peakR = juce::jmax (peakR.load(), scratch.getMagnitude (1, 0, numSamples));

    gain.setTargetValue (muted ? 0.0f : targetGain.load());

    const bool hear = monitor, record = armed, wet = recordWithFx;
    const int recordSamples = juce::jmin (numSamples, recordBus.getNumSamples());

    // Loops are recorded at the channel's fader level, so a new loop comes in at the
    // level you were hearing (mute aside: a muted input still records).
    const float recordGain = targetGain.load();
    auto addToRecording = [&]
    {
        for (int ch = 0; ch < juce::jmin (2, recordBus.getNumChannels()); ++ch)
            recordBus.addFrom (ch, 0, scratch, ch, 0, recordSamples, recordGain);
    };

    if (record && ! wet)
        addToRecording();

    // Effects run when they're heard or recorded, so a wet recording works with monitoring off.
    // The plug-in (e.g. pitch correction) comes first, then the built-in chain.
    if (hear || (record && wet))
    {
        processInsert (numSamples);

        if (fx.isActive())
            fx.process (scratch, 0, numSamples);
    }

    if (record && wet)
        addToRecording();

    if (! hear)
    {
        gain.skip (numSamples);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();
        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
            out.addSample (ch, startSample + i, scratch.getSample (ch, i) * g);
    }
}
