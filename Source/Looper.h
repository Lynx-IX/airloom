#pragma once

#include "LoopPlayer.h"

// Records new loops from the armed inputs and overdubs onto playing ones.
//
// Recording: startRecording() begins capturing at the next block, or at a given
// clock time (waiting until then - the count-in);
// stopRecording() ends it, optionally rounding the length to a unit (a bar,
// or the first loop's length) - if that means going longer, it keeps
// recording until the unit is complete. The finished take is then collected
// on the message thread with collectTake().
//
// Overdubbing: adds the input into a loop's own audio at the position that's
// playing, compensating for the interface's round-trip latency so the new
// layer lines up with what the performer heard.
//
// Thread model: the start/stop/collect calls are made from the message
// thread, process() from the audio thread.
class Looper
{
public:
    enum class State { idle, waiting, recording, finishing, finished, overdubbing };

    void prepare (double sampleRate, double maxSeconds = 300.0);

    // Starts at clock time 'at' (to the sample), or with -1 at the next block.
    // False if the last take hasn't been collected yet.
    bool startRecording (juce::int64 at = -1);
    void cancelStart();                          // calls off a recording that's still waiting
    juce::int64 getStartTime() const    { return startAt; }
    void stopRecording (juce::int64 unitSamples);
    void startOverdub (std::shared_ptr<LoopClip> clip, juce::int64 anchor);
    void stopOverdub();

    State getState() const              { return (State) state.load(); }
    double getRecordedSeconds() const   { return length.load() / sampleRate; }
    juce::int64 getTakeStart() const    { return takeStart; }

    // Copies out a finished take, ready to loop: exactly 'loopLength' samples, with
    // the audio recorded just past the end blended into the start so the seam is
    // inaudible. Also returns when the take started on LoopPlayer::blockClock.
    bool collectTake (juce::AudioBuffer<float>& destination, juce::int64& startClock);

    // 'bus' holds this block's armed-input signal (2 channels) from sample 0.
    void process (const juce::AudioBuffer<float>& bus, int numSamples, juce::int64 clock, int latencySamples);

private:
    void finishAt (int loopLength);

    double sampleRate = 48000.0;
    int seamSamples = 1440;                        // recorded past the end, blended into the start
    juce::AudioBuffer<float> take;                 // preallocated, written by the audio thread
    std::atomic<int> state { (int) State::idle };
    std::atomic<int> length { 0 }, loopLength { 0 };
    std::atomic<juce::int64> takeStart { 0 }, stopUnit { 0 };
    std::atomic<bool> startRequested { false }, stopRequested { false }, cancelRequested { false };
    std::atomic<juce::int64> startAt { -1 };
    int targetLength = 0;                          // audio thread only

    std::shared_ptr<LoopClip> dubClip;             // keeps the overdub target alive (message thread)
    std::atomic<LoopClip*> dubTarget { nullptr };
    std::atomic<juce::int64> dubAnchor { 0 };
};
