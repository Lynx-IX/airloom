#pragma once

#include "LoopPlayer.h"

// A mixer channel holding one loop layer. It owns two players so a new clip
// can crossfade in while the old one fades out.
//
// Thread model: everything except prepare() and process() is called from the
// message thread.
class LoopChannel
{
public:
    explicit LoopChannel (juce::String channelName) : name (std::move (channelName)) {}

    const juce::String& getName() const   { return name; }

    void prepare (double sampleRate, int maxBlockSize);

    // Adds the channel's output into the buffer and reports its peak levels.
    void process (juce::AudioBuffer<float>& out, int startSample, int numSamples, float& peakL, float& peakR);

    void setGain (float linearGain)   { targetGain = linearGain; }
    void setMuted (bool shouldMute)   { muted = shouldMute; }
    bool isMuted() const              { return muted; }

    // Swaps in a new clip (nullptr = silence). If audible, crossfades over the
    // given time; otherwise swaps silently. Returns false if the channel is
    // still busy finishing a previous crossfade.
    bool changeClip (LoopClipPtr clip, double seconds, bool audible);

    void start (double fadeSeconds);
    void stop (double fadeSeconds);

    bool isCrossfading() const        { return players[0].isAudible() && players[1].isAudible(); }
    LoopClipPtr getClip() const       { return players[(size_t) active].getClip(); }

private:
    juce::String name;
    std::array<LoopPlayer, 2> players;
    int active = 0;                       // message thread only

    juce::AudioBuffer<float> scratch;     // audio thread only
    juce::SmoothedValue<float> gain;
    std::atomic<float> targetGain { 1.0f };
    std::atomic<bool> muted { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopChannel)
};
