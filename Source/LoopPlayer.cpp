#include "LoopPlayer.h"

LoopClipPtr loadLoopClip (const juce::File& file, juce::AudioFormatManager& formats, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName();
        return {};
    }

    if (reader->lengthInSamples < 2)
    {
        error = file.getFileName() + " is empty";
        return {};
    }

    // 10 minutes of stereo 48k float is ~230 MB; anything longer is probably a mistake.
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60.0 * 10.0))
    {
        error = file.getFileName() + " is longer than 10 minutes";
        return {};
    }

    auto clip = std::make_shared<LoopClip>();
    clip->sampleRate = reader->sampleRate;
    clip->file = file;
    clip->data.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), (int) reader->lengthInSamples);
    reader->read (&clip->data, 0, (int) reader->lengthInSamples, 0, true, true);
    return clip;
}

LoopClipPtr makeSeamlessLoop (const LoopClipPtr& source, double crossfadeSeconds)
{
    const int length = source->data.getNumSamples();
    const int fadeLength = juce::jlimit (0, length / 3, (int) (crossfadeSeconds * source->sampleRate));

    if (fadeLength < 2)
        return source;

    // The tail (from newLength to the end) is mixed into the head. Playing the
    // result in a loop, its last sample runs straight into the start of that
    // tail, and the blend then hands over to the head: no jump anywhere.
    const int newLength = length - fadeLength;
    auto clip = std::make_shared<LoopClip>();
    clip->sampleRate = source->sampleRate;
    clip->file = source->file;
    clip->data.setSize (source->data.getNumChannels(), newLength);

    for (int ch = 0; ch < source->data.getNumChannels(); ++ch)
    {
        const float* src = source->data.getReadPointer (ch);
        float* dst = clip->data.getWritePointer (ch);

        for (int i = 0; i < fadeLength; ++i)
        {
            const float t = ((float) i + 0.5f) / (float) fadeLength * juce::MathConstants<float>::halfPi;
            dst[i] = src[i] * std::sin (t) + src[newLength + i] * std::cos (t);   // equal power
        }

        std::copy (src + fadeLength, src + newLength, dst + fadeLength);
    }

    return clip;
}

std::shared_ptr<LoopClip> makeReversed (const LoopClip& source)
{
    auto clip = std::make_shared<LoopClip> (source);
    clip->data.reverse (0, clip->data.getNumSamples());
    return clip;
}

//==============================================================================
void LoopPlayer::prepare (double deviceSampleRate)
{
    deviceRate = deviceSampleRate;
    voiceGainRamp.reset (deviceRate, 0.05);
    voiceGainRamp.setCurrentAndTargetValue (voiceGain);
}

void LoopPlayer::setClip (LoopClipPtr newClip, juce::int64 anchor)
{
    {
        const juce::SpinLock::ScopedLockType sl (clipLock);
        std::swap (clip, newClip);
    }

    startAnchor = anchor;
    resetRequested = true;
}   // the old clip is freed here, off the audio thread

LoopClipPtr LoopPlayer::getClip() const
{
    const juce::SpinLock::ScopedLockType sl (clipLock);
    return clip;
}

void LoopPlayer::fadeTo (float target, double seconds)
{
    fadeTarget = target;
    fadeSeconds = seconds;
    ++fadeCommand;
}

bool LoopPlayer::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (const int command = fadeCommand; command != fadeCommandSeen)
    {
        fadeCommandSeen = command;
        fadeGoal = fadeTarget;
        fadeStep = (float) ((fadeGoal - fade) / juce::jmax (1.0, fadeSeconds * deviceRate));
    }

    if (fade <= 0.0f && fadeGoal <= 0.0f)
        return false;

    const juce::SpinLock::ScopedTryLockType sl (clipLock);

    if (! sl.isLocked())
        return false;   // a new clip is being swapped in; skip this block

    if (clip == nullptr)
    {
        fade = fadeGoal;   // nothing to fade, so jump straight to the target
        fadeLevel = fade;
        return false;
    }

    const auto& data = clip->data;
    const int length = data.getNumSamples();
    const int clipChannels = data.getNumChannels();
    const int outChannels = buffer.getNumChannels();
    const double step = clip->sampleRate / deviceRate;

    if (resetRequested.exchange (false))
    {
        readPos = 0.0;

        // Anchored: pick up where the loop would be had it started at the anchor.
        if (const auto anchor = startAnchor.load(); anchor >= 0)
            readPos = std::fmod ((double) juce::jmax ((juce::int64) 0, blockClock.load() - anchor) * step, (double) length);
    }
    const bool loop = looping;
    float blockPeak = 0.0f;

    if (snapGain.exchange (false))
        voiceGainRamp.setCurrentAndTargetValue (voiceGain);
    else
        voiceGainRamp.setTargetValue (voiceGain);

    for (int i = 0; i < numSamples; ++i)
    {
        if (! loop && readPos >= length - 1)
        {
            // A one-shot reached its end: go silent until triggered again.
            fade = fadeGoal = 0.0f;
            fadeTarget = 0.0f;
            break;
        }

        if (fade != fadeGoal)
        {
            fade += fadeStep;
            if ((fadeStep > 0.0f && fade >= fadeGoal) || (fadeStep <= 0.0f && fade <= fadeGoal))
                fade = fadeGoal;
        }

        // sin() curve: two players crossfading this way keep constant power.
        const float g = std::sin (fade * juce::MathConstants<float>::halfPi) * voiceGainRamp.getNextValue();

        const int i0 = (int) readPos;
        const int i1 = (i0 + 1 == length) ? 0 : i0 + 1;   // wrap seamlessly across the loop point
        const float frac = (float) (readPos - i0);

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const float* src = data.getReadPointer (ch % clipChannels);
            const float s = (src[i0] + frac * (src[i1] - src[i0])) * g;
            buffer.addSample (ch, startSample + i, s);
            blockPeak = juce::jmax (blockPeak, std::abs (s));
        }

        readPos += step;
        if (readPos >= length)
            readPos -= length;
    }

    fadeLevel = fade;
    peak = juce::jmax (peak.load(), blockPeak);
    return true;
}
