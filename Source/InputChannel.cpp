#include "InputChannel.h"

void InputChannel::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    fx.prepare (sampleRate, maxBlockSize);
    scratch.setSize (2, juce::jmax (4096, maxBlockSize));
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (muted ? 0.0f : targetGain.load());
    insert.prepare (sampleRate, maxBlockSize);
}

void InputChannel::process (const juce::AudioBuffer<float>& in, int numInputs,
                            juce::AudioBuffer<float>& out, int startSample, int numSamples,
                            juce::AudioBuffer<float>& recordBus, juce::AudioBuffer<float>* sendBus)
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

    gain.setTargetValue (muted ? 0.0f : targetGain.load());

    // The meter shows what you hear: after the effects, fader and mute. With MONITOR off
    // nothing is heard, so it shows the input itself instead (for setting the interface gain).
    if (! monitor)
    {
        peakL = juce::jmax (peakL.load(), scratch.getMagnitude (0, 0, numSamples));
        peakR = juce::jmax (peakR.load(), scratch.getMagnitude (1, 0, numSamples));
    }

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
        insert.process (scratch, 0, numSamples);

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
        for (int ch = 0; ch < 2; ++ch)
            scratch.setSample (ch, i, scratch.getSample (ch, i) * g);
    }

    peakL = juce::jmax (peakL.load(), scratch.getMagnitude (0, 0, numSamples));
    peakR = juce::jmax (peakR.load(), scratch.getMagnitude (1, 0, numSamples));

    // To the speakers, and (after the fader) to the shared Space reverb.
    for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
        out.addFrom (ch, startSample, scratch, ch, 0, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        if (sendBus != nullptr)
            sendBus->addFrom (ch, 0, scratch, ch, 0, juce::jmin (numSamples, sendBus->getNumSamples()), fx.getSpaceSend());
        else
            SpaceReverb::addToBus (ch, 0, scratch.getReadPointer (ch), numSamples, fx.getSpaceSend());
}
