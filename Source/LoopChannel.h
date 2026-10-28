#pragma once

#include "Effects.h"
#include "LoopPlayer.h"
#include "PluginInsert.h"

// The audio for one row. Every channel in the row that's sounding gets its own
// voice with its own volume; loops repeat, one-shots stop by themselves.
// Voices are tagged with their channel's id ("owner"). A channel can also have
// its own effects: its voices are mixed into a separate buffer, run through
// that channel's chain, then join the row (which has effects of its own).
//
// Thread model: everything except prepare() and process() is called from the
// message thread.
class LoopChannel
{
public:
    static constexpr int numLoopVoices = 16, numShotVoices = 8, numChannelFx = 6;

    LoopChannel();

    void prepare (double sampleRate, int maxBlockSize);

    // Adds the row's output into the buffer.
    void process (juce::AudioBuffer<float>& out, int startSample, int numSamples);

    void setGain (float linearGain)   { targetGain = linearGain; }
    void setMuted (bool shouldMute)   { muted = shouldMute; }

    // Fades a loop in, phase-locked to 'anchor' on LoopPlayer::blockClock, from that
    // clock time 'startAt' (or straight away). Returns false if every voice is busy.
    bool startLoop (int owner, LoopClipPtr clip, double seconds, float gain, juce::int64 anchor, juce::int64 startAt = -1);
    void stopLoop (int owner, double seconds, juce::int64 startAt = -1);

    // Moves the owner's playing loop into phase with 'anchor' (see LoopPlayer::realign).
    void realignLoop (int owner, juce::int64 anchor);
    bool isFadingIn (int owner) const;

    // A loop pressed with Start on beat or bar waits for its moment: to come in, or to stop.
    enum class Cue { none, start, stop };
    Cue cueFor (int owner) const;

    void triggerShot (int owner, LoopClipPtr clip, float gain);

    // Changes the volume of everything the owner is playing.
    void setOwnerGain (int owner, float gain);

    // Gives a channel its own effects (or removes them when nothing is on).
    // Returns false if every per-channel effects slot in the row is taken.
    bool setOwnerFx (int owner, const FxSettings& settings);

    // Highest level the owner's voices reached since the last call (before effects and the row fader).
    float takeOwnerPeak (int owner);

    void stopAll (double seconds);

    // The row's effects, applied to everything in it before the row fader.
    FxChain& getFx()   { return fx; }

    // An effect plug-in on the whole row, after its built-in effects.
    PluginInsert& getInsert()   { return insert; }

    // Peak levels since the last call, for the meters.
    std::pair<float, float> takePeaks()  { return { peakL.exchange (0.0f), peakR.exchange (0.0f) }; }

private:
    int fxSlotFor (int owner) const;

    std::array<LoopPlayer, numLoopVoices> loops;
    std::array<LoopPlayer, numShotVoices> shots;
    // Written on the message thread, read by the audio thread to route each voice.
    std::array<std::atomic<int>, numLoopVoices> loopOwners;
    std::array<std::atomic<int>, numShotVoices> shotOwners;
    std::array<std::atomic<int>, numChannelFx> fxOwners;
    int nextShot = 0;                            // message thread only

    std::array<FxChain, numChannelFx> channelFx;
    std::array<juce::AudioBuffer<float>, numChannelFx> channelFxBuffers;   // audio thread only
    std::array<std::atomic<float>, numChannelFx> channelFxPeaks {};         // after each channel's own effects

    FxChain fx;
    PluginInsert insert;
    juce::AudioBuffer<float> scratch, sendScratch;   // audio thread only
    juce::SmoothedValue<float> gain;
    std::atomic<float> targetGain { 1.0f };
    std::atomic<bool> muted { false };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopChannel)
};
