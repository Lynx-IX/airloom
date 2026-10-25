#include "Looper.h"

void Looper::prepare (double newSampleRate, double maxSeconds)
{
    sampleRate = newSampleRate;
    seamSamples = (int) (0.03 * sampleRate);
    take.setSize (2, (int) (sampleRate * maxSeconds) + seamSamples);
    state = (int) State::idle;
    length = loopLength = 0;
    dubTarget = nullptr;
}

bool Looper::startRecording()
{
    if (getState() == State::finished)
        return false;

    dubTarget = nullptr;
    startRequested = true;
    return true;
}

void Looper::stopRecording (juce::int64 unitSamples)
{
    stopUnit = unitSamples;
    stopRequested = true;
}

void Looper::startOverdub (std::shared_ptr<LoopClip> clip, juce::int64 anchor)
{
    dubClip = std::move (clip);
    dubAnchor = anchor;
    dubTarget = dubClip.get();
    state = (int) State::overdubbing;
}

void Looper::stopOverdub()
{
    dubTarget = nullptr;
    if (getState() == State::overdubbing)
        state = (int) State::idle;
}

bool Looper::collectTake (juce::AudioBuffer<float>& destination, juce::int64& startClock)
{
    if (getState() != State::finished)
        return false;

    const int len = loopLength;
    const int recorded = length;
    const int seam = juce::jmin (seamSamples, recorded - len, len / 3);
    destination.setSize (2, len);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = take.getReadPointer (ch);
        float* dst = destination.getWritePointer (ch);
        std::copy (src, src + len, dst);

        // Fade the start in under the sound that continued past the end (equal power).
        for (int i = 0; i < seam; ++i)
        {
            const float t = ((float) i + 0.5f) / (float) seam * juce::MathConstants<float>::halfPi;
            dst[i] = src[i] * std::sin (t) + src[len + i] * std::cos (t);
        }
    }

    startClock = takeStart;
    length = loopLength = 0;
    state = (int) State::idle;
    return true;
}

void Looper::finishAt (int newLoopLength)
{
    // Keep recording a little past the end for the seam blend.
    loopLength = newLoopLength;
    targetLength = juce::jmin (take.getNumSamples(), newLoopLength + seamSamples);
    state = (int) (length >= targetLength ? State::finished : State::finishing);
}

void Looper::process (const juce::AudioBuffer<float>& bus, int numSamples, juce::int64 clock, int latencySamples)
{
    if (startRequested.exchange (false))
    {
        stopRequested = false;
        length = loopLength = 0;
        targetLength = 0;
        takeStart = clock;
        state = (int) State::recording;
    }

    if (getState() == State::recording && stopRequested.exchange (false))
    {
        const int recorded = length;
        const auto unit = stopUnit.load();

        if (unit <= 0)
        {
            finishAt (recorded);
        }
        else
        {
            // Round to the nearest whole number of units (at least one): shorter is
            // trimmed, longer keeps recording until the unit is complete.
            const auto units = juce::jmax ((juce::int64) 1, (juce::int64) std::llround ((double) recorded / (double) unit));
            finishAt ((int) juce::jmin ((juce::int64) (take.getNumSamples() - seamSamples), units * unit));
        }
    }

    const auto current = getState();

    if (current == State::recording || current == State::finishing)
    {
        const int start = length;
        const int limit = current == State::finishing ? targetLength : take.getNumSamples() - seamSamples;
        const int n = juce::jmax (0, juce::jmin (numSamples, limit - start));

        for (int ch = 0; ch < 2; ++ch)
            take.copyFrom (ch, start, bus, ch % bus.getNumChannels(), 0, n);

        length = start + n;

        if (current == State::finishing && length >= targetLength)
            state = (int) State::finished;
        else if (current == State::recording && length >= limit)
            finishAt (limit);   // reached the maximum length
        return;
    }

    if (current == State::overdubbing)
    {
        auto* clip = dubTarget.load();
        if (clip == nullptr)
            return;

        // Write where the loop was playing when the performer heard it: the input
        // arrives 'latencySamples' after the sound left the speakers.
        const int len = clip->data.getNumSamples();
        auto pos = (clock - dubAnchor.load() - latencySamples) % len;
        if (pos < 0)
            pos += len;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < clip->data.getNumChannels(); ++ch)
                clip->data.getWritePointer (ch)[pos] += bus.getSample (ch % bus.getNumChannels(), i);

            if (++pos >= len)
                pos = 0;
        }
    }
}
