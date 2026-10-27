#pragma once

#include <JuceHeader.h>
#include "Effects.h"
#include "PluginInsert.h"

// A live input from the audio interface (mic or instrument): picks the input
// channel(s), runs the effects, and adds the result to the output when
// monitoring is on.
//
// Thread model: the setters are called from the message thread, prepare() and
// process() from the audio thread.
class InputChannel
{
public:
    InputChannel() = default;

    void prepare (double sampleRate, int maxBlockSize);

    // 'in' holds the interface's inputs (numInputs of them) starting at sample 0.
    // When armed, the input is also added to 'recordBus' (from sample 0), with or
    // without its effects, before the fader.
    void process (const juce::AudioBuffer<float>& in, int numInputs,
                  juce::AudioBuffer<float>& out, int startSample, int numSamples,
                  juce::AudioBuffer<float>& recordBus);

    void setGain (float linearGain)   { targetGain = linearGain; }
    void setMuted (bool shouldMute)   { muted = shouldMute; }
    void setMonitor (bool shouldHear) { monitor = shouldHear; }
    void setSource (int channel, bool stereo)   { firstChannel = channel; stereoPair = stereo; }
    void setArmed (bool shouldArm)    { armed = shouldArm; }
    void setRecordWithFx (bool wet)   { recordWithFx = wet; }

    FxChain& getFx()                  { return fx; }

    // A hosted effect plug-in (e.g. pitch correction) that runs first, before the built-in
    // effects. Installs it (prepared here for the current device) and returns the old one.
    std::unique_ptr<juce::AudioPluginInstance> setInsert (std::unique_ptr<juce::AudioPluginInstance> plugin)
    {
        return insert.setPlugin (std::move (plugin));
    }
    juce::AudioPluginInstance* getInsert() const   { return insert.getPlugin(); }

    // Input level (before effects and fader) since the last call, for the meter.
    std::pair<float, float> takePeaks()  { return { peakL.exchange (0.0f), peakR.exchange (0.0f) }; }

private:
    PluginInsert insert;
    double sampleRate = 48000.0;

    FxChain fx;
    juce::AudioBuffer<float> scratch;     // audio thread only
    juce::SmoothedValue<float> gain;
    std::atomic<float> targetGain { 1.0f };
    std::atomic<bool> muted { false }, monitor { false }, armed { false }, recordWithFx { true };
    std::atomic<int> firstChannel { 0 };
    std::atomic<bool> stereoPair { false };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputChannel)
};
