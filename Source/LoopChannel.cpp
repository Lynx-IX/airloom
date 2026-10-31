#include "LoopChannel.h"

LoopChannel::LoopChannel()
{
    for (auto& o : loopOwners) o = -1;
    for (auto& o : shotOwners) o = -1;
    for (auto& o : fxOwners)   o = -1;
    for (auto& v : channelFxVolume) v = 1.0f;
}

void LoopChannel::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& p : loops) p.prepare (sampleRate);
    for (auto& p : shots) p.prepare (sampleRate);

    const int size = juce::jmax (512, maxBlockSize);
    fx.prepare (sampleRate, size);
    insert.prepare (sampleRate, size);
    scratch.setSize (2, size);
    sendScratch.setSize (2, size);

    for (size_t k = 0; k < channelFx.size(); ++k)
    {
        channelFx[k].prepare (sampleRate, size);
        channelFxBuffers[k].setSize (2, size);
        channelFxVolumeRamp[k].reset (sampleRate, 0.05);
        channelFxVolumeRamp[k].setCurrentAndTargetValue (channelFxVolume[k].load());
    }

    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (muted ? 0.0f : targetGain.load());
}

float LoopChannel::voiceGainFor (int owner) const
{
    if (fxSlotFor (owner) >= 0)
        return 1.0f;
    const auto it = ownerVolume.find (owner);
    return it != ownerVolume.end() ? it->second : 1.0f;
}

int LoopChannel::fxSlotFor (int owner) const
{
    if (owner < 0)
        return -1;

    for (int k = 0; k < (int) fxOwners.size(); ++k)
        if (fxOwners[(size_t) k] == owner)
            return k;
    return -1;
}

void LoopChannel::process (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    gain.setTargetValue (muted ? 0.0f : targetGain.load());

    const int outChannels = juce::jmin (out.getNumChannels(), scratch.getNumChannels());
    float pl = 0.0f, pr = 0.0f;

    // The device may occasionally deliver a block larger than announced.
    for (int done = 0; done < numSamples;)
    {
        const int num = juce::jmin (numSamples - done, scratch.getNumSamples());
        scratch.clear (0, num);
        sendScratch.clear (0, num);
        for (auto& b : channelFxBuffers)
            b.clear (0, num);

        // Each voice goes to its channel's effects buffer if it has one, else straight to the row.
        bool sounding = false;
        std::array<bool, numChannelFx> fxSounding {};

        auto route = [&] (LoopPlayer& p, int owner)
        {
            const int k = fxSlotFor (owner);
            const bool s = p.process (k >= 0 ? channelFxBuffers[(size_t) k] : scratch, 0, num);
            sounding |= s;
            if (k >= 0) fxSounding[(size_t) k] |= s;
        };

        for (size_t i = 0; i < loops.size(); ++i) route (loops[i], loopOwners[i]);
        for (size_t i = 0; i < shots.size(); ++i) route (shots[i], shotOwners[i]);

        for (size_t k = 0; k < channelFx.size(); ++k)
        {
            if (fxOwners[k] < 0 || ! (fxSounding[k] || channelFx[k].needsTail()))
                continue;

            channelFx[k].process (channelFxBuffers[k], 0, num);

            // The channel's volume, after its effects: a fader turns the tails down too.
            if (channelFxVolumeSnap[k].exchange (false))
                channelFxVolumeRamp[k].setCurrentAndTargetValue (channelFxVolume[k].load());
            else
                channelFxVolumeRamp[k].setTargetValue (channelFxVolume[k].load());
            channelFxVolumeRamp[k].applyGain (channelFxBuffers[k], num);

            const float fxPeak = juce::jmax (channelFxBuffers[k].getMagnitude (0, 0, num), channelFxBuffers[k].getMagnitude (1, 0, num));
            channelFxPeaks[k] = juce::jmax (channelFxPeaks[k].load(), fxPeak);
            for (int ch = 0; ch < 2; ++ch)
            {
                scratch.addFrom (ch, 0, channelFxBuffers[k], ch, 0, num);
                sendScratch.addFrom (ch, 0, channelFxBuffers[k], ch, 0, num, channelFx[k].getSpaceSend());
            }
            sounding = true;
        }

        // With effects on, keep going through silence so reverb and delay tails ring out
        // (a plug-in may have a tail of its own).
        if (! sounding && ! fx.needsTail() && ! insert.isLoaded())
        {
            gain.skip (num);
            done += num;
            continue;
        }

        if (fx.isActive())
            fx.process (scratch, 0, num);
        insert.process (scratch, 0, num);

        // The row's own send joins its channels'; the row fader and mute apply to both.
        for (int ch = 0; ch < 2; ++ch)
            sendScratch.addFrom (ch, 0, scratch, ch, 0, num, fx.getSpaceSend());

        for (int i = 0; i < num; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
            {
                scratch.setSample (ch, i, scratch.getSample (ch, i) * g);
                sendScratch.setSample (ch, i, sendScratch.getSample (ch, i) * g);
            }
        }

        for (int ch = 0; ch < outChannels; ++ch)
            out.addFrom (ch, startSample + done, scratch, ch, 0, num);
        for (int ch = 0; ch < 2; ++ch)
            SpaceReverb::addToBus (ch, done, sendScratch.getReadPointer (ch), num, 1.0f);

        pl = juce::jmax (pl, scratch.getMagnitude (0, 0, num));
        pr = juce::jmax (pr, scratch.getMagnitude (1, 0, num));
        done += num;
    }

    peakL = juce::jmax (peakL.load(), pl);
    peakR = juce::jmax (peakR.load(), pr);
}

bool LoopChannel::startLoop (int owner, LoopClipPtr clip, double seconds, float voiceGain, juce::int64 anchor, juce::int64 startAt)
{
    // Use a voice nobody can hear: replacing an audible clip would click.
    for (size_t i = 0; i < loops.size(); ++i)
    {
        auto& p = loops[i];
        if (p.isAudible())
            continue;

        loopOwners[i] = owner;
        ownerVolume[owner] = voiceGain;
        if (const int k = fxSlotFor (owner); k >= 0)
            channelFxVolume[(size_t) k] = voiceGain;
        p.setLooping (true);
        p.setVoiceGain (voiceGainFor (owner), true);
        p.setClip (std::move (clip), anchor);
        p.fadeTo (1.0f, seconds, startAt);
        return true;
    }

    return false;
}

void LoopChannel::stopLoop (int owner, double seconds, juce::int64 startAt)
{
    // The voice keeps its owner while it fades, so it stays in that channel's effects.
    for (size_t i = 0; i < loops.size(); ++i)
        if (loopOwners[i] == owner && loops[i].isAudible() && loops[i].getFadeTarget() > 0.0f)
            loops[i].fadeTo (0.0f, seconds, startAt);
}

void LoopChannel::realignLoop (int owner, juce::int64 anchor)
{
    // Only the voice that's playing: one fading out after a stop is left to finish.
    for (size_t i = 0; i < loops.size(); ++i)
        if (loopOwners[i] == owner && loops[i].getFadeTarget() > 0.0f)
            loops[i].realign (anchor);
}

bool LoopChannel::isFadingIn (int owner) const
{
    for (size_t i = 0; i < loops.size(); ++i)
        if (loopOwners[i] == owner && loops[i].getFadeTarget() > 0.0f && loops[i].getFadeLevel() < 0.999f)
            return true;
    return false;
}

LoopChannel::Cue LoopChannel::cueFor (int owner) const
{
    for (size_t i = 0; i < loops.size(); ++i)
    {
        const auto& p = loops[i];
        if (loopOwners[i] != owner || ! p.isWaiting())
            continue;
        if (p.getFadeTarget() > 0.0f)
            return Cue::start;
        if (p.getFadeLevel() > 0.0f)
            return Cue::stop;
    }
    return Cue::none;
}

void LoopChannel::triggerShot (int owner, LoopClipPtr clip, float voiceGain)
{
    // Prefer a free voice; otherwise take over the next one in turn.
    int voice = -1;
    for (int i = 0; i < (int) shots.size(); ++i)
        if (! shots[(size_t) i].isAudible()) { voice = i; break; }

    if (voice < 0)
    {
        voice = nextShot;
        nextShot = (nextShot + 1) % (int) shots.size();
    }

    auto& p = shots[(size_t) voice];
    shotOwners[(size_t) voice] = owner;
    ownerVolume[owner] = voiceGain;
    if (const int k = fxSlotFor (owner); k >= 0)
        channelFxVolume[(size_t) k] = voiceGain;
    p.setLooping (false);
    p.setVoiceGain (voiceGainFor (owner), true);
    p.setClip (std::move (clip));
    p.fadeTo (1.0f, 0.0);
}

void LoopChannel::setOwnerGain (int owner, float voiceGain)
{
    ownerVolume[owner] = voiceGain;
    if (const int k = fxSlotFor (owner); k >= 0)
    {
        channelFxVolume[(size_t) k] = voiceGain;
        return;
    }

    for (size_t i = 0; i < loops.size(); ++i)
        if (loopOwners[i] == owner && loops[i].getFadeTarget() > 0.0f)
            loops[i].setVoiceGain (voiceGain);

    for (size_t i = 0; i < shots.size(); ++i)
        if (shotOwners[i] == owner)
            shots[i].setVoiceGain (voiceGain);
}

// The owner's voices at the gain they should have now (after its effects were added or removed).
void LoopChannel::refreshOwnerVoices (int owner)
{
    const float g = voiceGainFor (owner);
    for (size_t i = 0; i < loops.size(); ++i)
        if (loopOwners[i] == owner)
            loops[i].setVoiceGain (g);
    for (size_t i = 0; i < shots.size(); ++i)
        if (shotOwners[i] == owner)
            shots[i].setVoiceGain (g);
}

bool LoopChannel::setOwnerFx (int owner, const FxSettings& settings)
{
    int slot = fxSlotFor (owner);

    if (! settings.isActive())
    {
        if (slot >= 0)
        {
            channelFx[(size_t) slot].setSettings (settings);
            fxOwners[(size_t) slot] = -1;
            refreshOwnerVoices (owner);   // its volume goes back onto the voices
        }
        return true;
    }

    if (slot < 0)
    {
        for (int k = 0; k < (int) fxOwners.size() && slot < 0; ++k)
            if (fxOwners[(size_t) k] < 0)
                slot = k;

        if (slot < 0)
            return false;

        // A fresh owner starts with empty tails, not whatever the slot last played, and
        // its volume moves to after the effects.
        channelFx[(size_t) slot].requestClear();
        const auto it = ownerVolume.find (owner);
        channelFxVolume[(size_t) slot] = it != ownerVolume.end() ? it->second : 1.0f;
        channelFxVolumeSnap[(size_t) slot] = true;
        channelFx[(size_t) slot].setSettings (settings);
        fxOwners[(size_t) slot] = owner;
        refreshOwnerVoices (owner);
        return true;
    }

    channelFx[(size_t) slot].setSettings (settings);
    fxOwners[(size_t) slot] = owner;
    return true;
}

float LoopChannel::takeOwnerPeak (int owner)
{
    // A channel with its own effects is metered after them (tails included).
    const int k = fxSlotFor (owner);
    float p = k >= 0 ? channelFxPeaks[(size_t) k].exchange (0.0f) : 0.0f;

    for (size_t i = 0; i < loops.size(); ++i)
        if (loopOwners[i] == owner)
        {
            const float played = loops[i].takePeak();
            if (k < 0) p = juce::jmax (p, played);
        }

    for (size_t i = 0; i < shots.size(); ++i)
        if (shotOwners[i] == owner)
        {
            const float played = shots[i].takePeak();
            if (k < 0) p = juce::jmax (p, played);
        }

    return p;
}

void LoopChannel::stopAll (double seconds)
{
    for (auto& p : loops)
        if (p.isAudible())
            p.fadeTo (0.0f, seconds);

    for (auto& p : shots)
        if (p.isAudible())
            p.fadeTo (0.0f, seconds);
}
