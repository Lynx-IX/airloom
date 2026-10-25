#include "LoopChannel.h"

void LoopChannel::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& p : players)
        p.prepare (sampleRate);

    scratch.setSize (2, juce::jmax (512, maxBlockSize));
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (muted ? 0.0f : targetGain.load());
}

void LoopChannel::process (juce::AudioBuffer<float>& out, int startSample, int numSamples, float& peakL, float& peakR)
{
    peakL = peakR = 0.0f;
    gain.setTargetValue (muted ? 0.0f : targetGain.load());

    const int outChannels = juce::jmin (out.getNumChannels(), scratch.getNumChannels());

    // The device may occasionally deliver a block larger than announced.
    for (int done = 0; done < numSamples;)
    {
        const int num = juce::jmin (numSamples - done, scratch.getNumSamples());
        scratch.clear (0, num);

        bool sounding = false;
        for (auto& p : players)
            sounding |= p.process (scratch, 0, num);

        if (! sounding)
        {
            gain.skip (num);
            done += num;
            continue;
        }

        for (int i = 0; i < num; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
                scratch.setSample (ch, i, scratch.getSample (ch, i) * g);
        }

        for (int ch = 0; ch < outChannels; ++ch)
            out.addFrom (ch, startSample + done, scratch, ch, 0, num);

        peakL = juce::jmax (peakL, scratch.getMagnitude (0, 0, num));
        peakR = juce::jmax (peakR, scratch.getMagnitude (1, 0, num));
        done += num;
    }
}

bool LoopChannel::changeClip (LoopClipPtr clip, double seconds, bool audible)
{
    const int incoming = 1 - active;

    // Replacing a clip that can still be heard would click.
    if (players[(size_t) incoming].isAudible())
        return false;

    players[(size_t) incoming].setClip (std::move (clip));

    if (audible)
    {
        players[(size_t) incoming].fadeTo (1.0f, seconds);
        players[(size_t) active].fadeTo (0.0f, seconds);
    }

    active = incoming;
    return true;
}

void LoopChannel::start (double fadeSeconds)
{
    players[(size_t) active].fadeTo (1.0f, fadeSeconds);
}

void LoopChannel::stop (double fadeSeconds)
{
    for (auto& p : players)
        if (p.isAudible())
            p.fadeTo (0.0f, fadeSeconds);
}
