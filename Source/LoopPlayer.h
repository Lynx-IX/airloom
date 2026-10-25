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

    void prepare (double deviceSampleRate);

    // Swaps in a new clip (or nullptr) and restarts from the top.
    // The previous clip is released on the calling thread.
    void setClip (LoopClipPtr newClip);
    LoopClipPtr getClip() const;

    // Ramps the fade level linearly to target (0..1) over the given time.
    void fadeTo (float target, double seconds);

    float getFadeLevel() const  { return fadeLevel; }
    bool isAudible() const      { return fadeTarget > 0.0f || fadeLevel > 0.0f; }

    // Adds this loop's output into the buffer. Returns false if it was silent.
    bool process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

private:
    LoopClipPtr clip;                // swapped under clipLock
    juce::SpinLock clipLock;

    double deviceRate = 44100.0;
    double readPos = 0.0;            // audio thread only
    std::atomic<bool> resetRequested { false };

    // Fade commands from the message thread, picked up by the audio thread.
    std::atomic<float> fadeTarget { 0.0f };
    std::atomic<double> fadeSeconds { 0.0 };
    std::atomic<int> fadeCommand { 0 };

    int fadeCommandSeen = 0;         // audio thread only
    float fade = 0.0f, fadeGoal = 0.0f, fadeStep = 0.0f;
    std::atomic<float> fadeLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopPlayer)
};
