#pragma once

#include <JuceHeader.h>

// One audio file held entirely in memory.
struct LoopClip
{
    juce::AudioBuffer<float> data;
    double sampleRate = 44100.0;
    juce::File file;
};

using LoopClipPtr = std::shared_ptr<const LoopClip>;

// Reads a whole audio file into memory. Safe to call from a background thread.
LoopClipPtr loadLoopClip (const juce::File& file, juce::AudioFormatManager& formats, juce::String& error);

// Makes a sample that wasn't cut as a loop repeat seamlessly: the last
// 'crossfadeSeconds' are blended into the start, and the result is that much
// shorter. The crossfade is capped at a third of the sample.
LoopClipPtr makeSeamlessLoop (const LoopClipPtr& source, double crossfadeSeconds);

// A copy of the clip playing backwards.
std::shared_ptr<LoopClip> makeReversed (const LoopClip& source);

// The first point on a grid (every 'unit' samples from 'origin') at or after 'now'. A grid
// point passed less than 'grace' samples ago counts, so a slightly late press isn't pushed
// back a whole beat.
inline juce::int64 nextGridPoint (juce::int64 now, juce::int64 origin, double unit, juce::int64 grace)
{
    const double steps = std::ceil ((double) (now - grace - origin) / unit);
    return origin + (juce::int64) std::llround (steps * unit);
}

// Plays one clip as a gapless loop with an equal-power fade in/out.
// The wrap from the last sample back to the first is interpolated, so a
// properly cut loop plays with no click or gap. If the clip's sample rate
// differs from the device's, it is resampled on the fly.
//
// Thread model: setClip() and fadeTo() are called from the message thread,
// process() from the audio thread.
class LoopPlayer
{
public:
    LoopPlayer() = default;

    // Sample count at the start of the current audio block, shared by every player.
    // Loops started against an anchor on this clock stay in phase with it.
    static inline std::atomic<juce::int64> blockClock { 0 };

    void prepare (double deviceSampleRate);

    // Swaps in a new clip (or nullptr). With an anchor, playback is positioned as
    // if the loop had started at that clock time; otherwise it starts from the top.
    // The previous clip is released on the calling thread.
    void setClip (LoopClipPtr newClip, juce::int64 anchor = -1);
    LoopClipPtr getClip() const;

    // Moves a playing loop to where it would be had it started at 'anchor', with a
    // quick crossfade from the old position so the jump doesn't click.
    void realign (juce::int64 anchor);
    static constexpr double realignSeconds = 0.03;

    // Ramps the fade level linearly to target (0..1) over the given time, starting
    // straight away or, with 'at', once blockClock reaches that time.
    void fadeTo (float target, double seconds, juce::int64 at = -1);

    // When false the clip plays once and then goes silent.
    void setLooping (bool shouldLoop)   { looping = shouldLoop; }

    // This voice's own volume. Changes are smoothed unless 'immediately' (for a fresh start).
    void setVoiceGain (float g, bool immediately = false)
    {
        voiceGain = g;
        if (immediately)
            snapGain = true;
    }

    float getFadeLevel() const  { return fadeLevel; }

    // Highest output level since the last call, for a channel meter.
    float takePeak()            { return peak.exchange (0.0f); }
    float getFadeTarget() const { return fadeTarget; }

    // True while the last fade is held back for a later beat (fadeTo with 'at').
    bool isWaiting() const      { return fadeStartAt.load() > blockClock.load(); }
    bool isAudible() const      { return fadeTarget > 0.0f || fadeLevel > 0.0f; }

    // Adds this loop's output into the buffer. Returns false if it was silent.
    bool process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

private:
    LoopClipPtr clip;                // swapped under clipLock
    juce::SpinLock clipLock;

    double deviceRate = 44100.0;
    double readPos = 0.0;            // audio thread only
    std::atomic<bool> resetRequested { false }, realignRequested { false };
    double jumpFromPos = 0.0;        // audio thread only: the old position while realigning
    int jumpLeft = 0, jumpLength = 1;
    std::atomic<juce::int64> startAnchor { -1 };
    std::atomic<bool> looping { true };
    std::atomic<float> voiceGain { 1.0f };
    std::atomic<bool> snapGain { true };
    juce::SmoothedValue<float> voiceGainRamp;   // audio thread only
    std::atomic<float> peak { 0.0f };

    // Fade commands from the message thread, picked up by the audio thread.
    std::atomic<float> fadeTarget { 0.0f };
    std::atomic<double> fadeSeconds { 0.0 };
    std::atomic<juce::int64> fadeStartAt { -1 };
    std::atomic<int> fadeCommand { 0 };

    int fadeCommandSeen = 0;         // audio thread only
    float fade = 0.0f, fadeGoal = 0.0f, fadeStep = 0.0f;
    std::atomic<float> fadeLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopPlayer)
};
