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

//==============================================================================
void LoopPlayer::prepare (double deviceSampleRate)
{
    deviceRate = deviceSampleRate;
}

void LoopPlayer::setClip (LoopClipPtr newClip)
{
    {
        const juce::SpinLock::ScopedLockType sl (clipLock);
        std::swap (clip, newClip);
    }

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

    if (resetRequested.exchange (false))
        readPos = 0.0;

    const auto& data = clip->data;
    const int length = data.getNumSamples();
    const int clipChannels = data.getNumChannels();
    const int outChannels = buffer.getNumChannels();
    const double step = clip->sampleRate / deviceRate;

    for (int i = 0; i < numSamples; ++i)
    {
        if (fade != fadeGoal)
        {
            fade += fadeStep;
            if ((fadeStep > 0.0f && fade >= fadeGoal) || (fadeStep <= 0.0f && fade <= fadeGoal))
                fade = fadeGoal;
        }

        // sin() curve: two players crossfading this way keep constant power.
        const float g = std::sin (fade * juce::MathConstants<float>::halfPi);

        const int i0 = (int) readPos;
        const int i1 = (i0 + 1 == length) ? 0 : i0 + 1;   // wrap seamlessly across the loop point
        const float frac = (float) (readPos - i0);

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const float* src = data.getReadPointer (ch % clipChannels);
            buffer.addSample (ch, startSample + i, (src[i0] + frac * (src[i1] - src[i0])) * g);
        }

        readPos += step;
        if (readPos >= length)
            readPos -= length;
    }

    fadeLevel = fade;
    return true;
}
