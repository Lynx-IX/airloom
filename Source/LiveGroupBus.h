#pragma once

#include <JuceHeader.h>
#include "Effects.h"

// A live group's mix: its inputs and instruments add into 'main' (and their Space sends
// into 'send'), then the group's effects, fader and mute go over both, like a loop group.
class LiveGroupBus
{
public:
    static constexpr int capacity = 8192;   // samples a block may hold

    juce::AudioBuffer<float> main { 2, capacity }, send { 2, capacity };   // audio thread only

    void prepare (double sampleRate, int maxBlockSize)
    {
        main.setSize (2, juce::jmax (capacity, maxBlockSize));
        send.setSize (2, juce::jmax (capacity, maxBlockSize));
        fx.prepare (sampleRate, juce::jmax (capacity, maxBlockSize));
        gain.reset (sampleRate, 0.05);
        gain.setCurrentAndTargetValue (muted ? 0.0f : targetGain.load());
    }

    void setGain (float linear)       { targetGain = linear; }
    void setMuted (bool shouldMute)   { muted = shouldMute; }
    FxChain& getFx()                  { return fx; }

    // Start of a block: empty, ready for the members to add into.
    void begin (int numSamples)
    {
        const int n = juce::jmin (numSamples, main.getNumSamples());
        main.clear (0, n);
        send.clear (0, n);
    }

    // End of a block: the group's effects, fader and mute, then out to the master and Space.
    void finish (juce::AudioBuffer<float>& out, int startSample, int numSamples)
    {
        const int n = juce::jmin (numSamples, main.getNumSamples());

        if (fx.isActive() || fx.needsTail())
            fx.process (main, 0, n);
        for (int ch = 0; ch < 2; ++ch)
            send.addFrom (ch, 0, main, ch, 0, n, fx.getSpaceSend());

        gain.setTargetValue (muted ? 0.0f : targetGain.load());
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                main.setSample (ch, i, main.getSample (ch, i) * g);
                send.setSample (ch, i, send.getSample (ch, i) * g);
            }
        }

        peakL = juce::jmax (peakL.load(), main.getMagnitude (0, 0, n));
        peakR = juce::jmax (peakR.load(), main.getMagnitude (1, 0, n));

        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
            out.addFrom (ch, startSample, main, ch, 0, n);
        for (int ch = 0; ch < 2; ++ch)
            SpaceReverb::addToBus (ch, 0, send.getReadPointer (ch), n, 1.0f);
    }

    std::pair<float, float> takePeaks()   { return { peakL.exchange (0.0f), peakR.exchange (0.0f) }; }

    // Muted and faded right out: nothing in the group can be heard (audio thread).
    bool isSilenced() const   { return muted.load() && ! gain.isSmoothing() && gain.getCurrentValue() <= 0.0f; }

private:
    FxChain fx;
    juce::SmoothedValue<float> gain;
    std::atomic<float> targetGain { 1.0f }, peakL { 0.0f }, peakR { 0.0f };
    std::atomic<bool> muted { false };
};
