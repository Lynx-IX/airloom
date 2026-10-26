#include "Effects.h"

namespace
{
    // Settings saved by name; each entry maps an XML attribute to a field.
    template <typename T>
    struct Field { const char* name; T FxSettings::* member; };

    const Field<bool> boolFields[] = {
        { "gateOn", &FxSettings::gateOn }, { "lowCutOn", &FxSettings::lowCutOn }, { "eqOn", &FxSettings::eqOn },
        { "compOn", &FxSettings::compOn }, { "delayOn", &FxSettings::delayOn }, { "delaySync", &FxSettings::delaySync },
        { "delayPingPong", &FxSettings::delayPingPong }, { "reverbOn", &FxSettings::reverbOn },
    };

    const Field<double> doubleFields[] = {
        { "gainDb", &FxSettings::gainDb }, { "pan", &FxSettings::pan }, { "gateThresholdDb", &FxSettings::gateThresholdDb },
        { "lowCutHz", &FxSettings::lowCutHz }, { "eqLowDb", &FxSettings::eqLowDb }, { "eqMidDb", &FxSettings::eqMidDb },
        { "eqMidHz", &FxSettings::eqMidHz }, { "eqHighDb", &FxSettings::eqHighDb },
        { "compThresholdDb", &FxSettings::compThresholdDb }, { "compRatio", &FxSettings::compRatio },
        { "compReleaseMs", &FxSettings::compReleaseMs }, { "compMakeupDb", &FxSettings::compMakeupDb },
        { "delayMs", &FxSettings::delayMs }, { "delayFeedback", &FxSettings::delayFeedback },
        { "delayTone", &FxSettings::delayTone }, { "delayMix", &FxSettings::delayMix },
        { "reverbDecay", &FxSettings::reverbDecay }, { "reverbSize", &FxSettings::reverbSize },
        { "reverbDamping", &FxSettings::reverbDamping }, { "reverbPredelayMs", &FxSettings::reverbPredelayMs },
        { "reverbMod", &FxSettings::reverbMod }, { "reverbMix", &FxSettings::reverbMix },
    };

    float onePoleCoeff (float cutoffHz, double sampleRate)
    {
        return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate);
    }
}

double FxSettings::delayTimeMs (double bpm) const
{
    if (! delaySync)
        return delayMs;

    const auto& division = delayDivisions[(size_t) juce::jlimit (0, numDelayDivisions - 1, delayDivision)];
    return 60000.0 / juce::jlimit (20.0, 300.0, bpm) * division.beats;
}

void FxSettings::addToXml (juce::XmlElement& parent) const
{
    auto* e = parent.createNewChildElement ("Fx");
    for (auto& f : boolFields)   e->setAttribute (f.name, this->*f.member);
    for (auto& f : doubleFields) e->setAttribute (f.name, this->*f.member);
    e->setAttribute ("delayDivision", delayDivision);
}

FxSettings FxSettings::fromXml (const juce::XmlElement* e, const FxSettings& defaults)
{
    FxSettings s = defaults;
    if (e == nullptr)
        return s;

    for (auto& f : boolFields)   s.*f.member = e->getBoolAttribute (f.name, defaults.*f.member);
    for (auto& f : doubleFields) s.*f.member = e->getDoubleAttribute (f.name, defaults.*f.member);
    s.delayDivision = juce::jlimit (0, numDelayDivisions - 1, e->getIntAttribute ("delayDivision", defaults.delayDivision));
    return s;
}

//==============================================================================
void AmbientReverb::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    for (auto& line : lines)
        line.assign ((size_t) (sampleRate * 0.16) + 4, 0.0f);   // longest line at the largest size, plus modulation

    // Short allpasses smear the input into a dense wash before it reaches the network.
    const float diffuserMs[] = { 4.77f, 3.59f, 12.73f, 9.30f };
    for (size_t i = 0; i < diffusers.size(); ++i)
        diffusers[i].buffer.assign ((size_t) (diffuserMs[i] * 0.001 * sampleRate) + 1, 0.0f);

    predelay.assign ((size_t) (sampleRate * 0.25) + 1, 0.0f);

    // Slow, unrelated LFO rates keep the tail moving without obvious wobble.
    for (int i = 0; i < numLines; ++i)
    {
        lfoPhase[(size_t) i] = (float) i * 0.79f;
        lfoStep[(size_t) i] = juce::MathConstants<float>::twoPi * (0.07f + 0.11f * (float) i) / (float) sampleRate;
    }

    currentDecay = currentSize = -1.0f;
    freezeStep = 1.0f / (0.05f * (float) sampleRate);
    freezeAmount = 0.0f;
    reset();
}

void AmbientReverb::reset()
{
    for (auto& line : lines) std::fill (line.begin(), line.end(), 0.0f);
    for (auto& d : diffusers) std::fill (d.buffer.begin(), d.buffer.end(), 0.0f);
    std::fill (predelay.begin(), predelay.end(), 0.0f);
    damp.fill (0.0f);
}

void AmbientReverb::setParameters (float decaySeconds, float size, float damping, float predelayMs, float modulation)
{
    if (size != currentSize || decaySeconds != currentDecay)
    {
        // Mutually prime-ish lengths avoid metallic ringing; size scales them all.
        const float baseMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.3f, 67.1f, 79.3f };
        const float scale = 0.4f + 1.2f * juce::jlimit (0.0f, 1.0f, size);

        for (int i = 0; i < numLines; ++i)
        {
            lineLength[(size_t) i] = baseMs[i] * scale * 0.001f * (float) sampleRate;
            // Each pass round a line loses enough to fall 60 dB over the decay time. The
            // damping filters take some energy too, so the target is stretched to compensate
            // (measured: without this the tail is about 30% shorter than the knob says).
            const float seconds = lineLength[(size_t) i] / (float) sampleRate;
            feedback[(size_t) i] = std::pow (10.0f, -3.0f * seconds / (juce::jmax (0.2f, decaySeconds) * 1.42f));
        }

        currentSize = size;
        currentDecay = decaySeconds;
    }

    dampCoeff = onePoleCoeff (16000.0f * std::pow (0.08f, juce::jlimit (0.0f, 1.0f, damping)), sampleRate);
    modDepth = juce::jlimit (0.0f, 1.0f, modulation) * 0.0025f * (float) sampleRate;
    predelaySamples = juce::jlimit (0, (int) predelay.size() - 1, (int) (predelayMs * 0.001f * (float) sampleRate));
}

void AmbientReverb::process (float* left, float* right, int numSamples, float mix)
{
    const float dryGain = 1.0f - 0.5f * mix;
    const float wetGain = mix * 0.6f;

    for (int n = 0; n < numSamples; ++n)
    {
        float wetL, wetR;
        tick (0.5f * (left[n] + right[n]), wetL, wetR);
        left[n] = left[n] * dryGain + wetL * wetGain;
        right[n] = right[n] * dryGain + wetR * wetGain;
    }
}

void AmbientReverb::tick (float input, float& wetL, float& wetR)
{
    // Pre-delay, then diffusion.
    const int predelaySize = (int) predelay.size();
    predelay[(size_t) predelayWrite] = input;
    int readPos = predelayWrite - predelaySamples;
    if (readPos < 0)
        readPos += predelaySize;
    float x = predelay[(size_t) readPos];
    if (++predelayWrite >= predelaySize)
        predelayWrite = 0;

    for (auto& d : diffusers)
        x = d.process (x);

    // Read each line at its (modulated) length.
    float out[numLines];
    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[(size_t) i];
        const int size = (int) line.size();
        const float length = lineLength[(size_t) i] + modDepth * (1.0f + std::sin (lfoPhase[(size_t) i]));
        lfoPhase[(size_t) i] += lfoStep[(size_t) i];
        if (lfoPhase[(size_t) i] > juce::MathConstants<float>::twoPi)
            lfoPhase[(size_t) i] -= juce::MathConstants<float>::twoPi;

        float pos = (float) writePos[(size_t) i] - length;
        while (pos < 0.0f)
            pos += (float) size;
        const int p0 = (int) pos;
        const int p1 = (p0 + 1) % size;
        const float frac = pos - (float) p0;
        out[i] = line[(size_t) p0] + frac * (line[(size_t) p1] - line[(size_t) p0]);
    }

    // Frozen: glide to no damping and (almost) no loss, so the tail holds.
    freezeAmount = frozen ? juce::jmin (1.0f, freezeAmount + freezeStep) : juce::jmax (0.0f, freezeAmount - freezeStep);
    const float dampNow = dampCoeff + (1.0f - dampCoeff) * freezeAmount;

    // Damp (darker repeats) and scale each line for the decay time.
    float fb[numLines];
    for (int i = 0; i < numLines; ++i)
    {
        damp[(size_t) i] += dampNow * (out[i] - damp[(size_t) i]);
        fb[i] = damp[(size_t) i] * (feedback[(size_t) i] + (0.9998f - feedback[(size_t) i]) * freezeAmount);
    }

    // Mix all lines into each other (8-point Hadamard): energy-preserving and dense.
    for (int len = 1; len < numLines; len <<= 1)
        for (int i = 0; i < numLines; i += len << 1)
            for (int j = i; j < i + len; ++j)
            {
                const float a = fb[j], b = fb[j + len];
                fb[j] = a + b;
                fb[j + len] = a - b;
            }

    constexpr float norm = 0.35355339f;   // 1 / sqrt (8)
    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[(size_t) i];
        line[(size_t) writePos[(size_t) i]] = fb[i] * norm + x * ((i & 1) ? 0.5f : -0.5f);
        if (++writePos[(size_t) i] >= (int) line.size())
            writePos[(size_t) i] = 0;
    }

    wetL = out[0] - out[2] + out[4] - out[6];
    wetR = out[1] - out[3] + out[5] - out[7];
}

//==============================================================================
void FreezeHold::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reverb.prepare (sampleRate);
    // Big and fairly bright, with a slow shimmer of modulation; a 5 s tail while listening.
    reverb.setParameters (5.0f, 0.9f, 0.3f, 0.0f, 0.5f);
    reverb.setFrozen (false);
    level = 0.0f;
    listen = 1.0f;
    sounding = false;
}

void FreezeHold::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    const bool hold = held.load();
    const float sr = (float) sampleRate;
    const float listenStep = 1.0f / (0.25f * sr);     // keeps listening for a moment, to catch the note being played
    const float fadeInStep = 1.0f / (0.3f * sr);
    const float fadeOutStep = 1.0f / (juce::jmax (0.3f, releaseSeconds.load()) * sr);
    constexpr float outputGain = 0.25f;   // sits a little under the playing

    // Stays frozen until the pad has faded right out.
    reverb.setFrozen (hold || level > 0.0f);

    float* left = buffer.getWritePointer (0, startSample);
    float* right = buffer.getWritePointer (1, startSample);

    for (int n = 0; n < numSamples; ++n)
    {
        if (hold)
        {
            listen = juce::jmax (0.0f, listen - listenStep);
            level = juce::jmin (1.0f, level + fadeInStep);
        }
        else
        {
            level = juce::jmax (0.0f, level - fadeOutStep);
            if (level <= 0.0f)
                listen = juce::jmin (1.0f, listen + listenStep);
        }

        float wetL, wetR;
        reverb.tick (0.5f * (left[n] + right[n]) * listen, wetL, wetR);

        const float g = level * level * (3.0f - 2.0f * level) * outputGain;   // smooth fades both ways
        left[n] += wetL * g;
        right[n] += wetR * g;
    }

    sounding = hold || level > 0.0f;
}

//==============================================================================
void FxChain::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlockSize), 2 };

    gate.prepare (spec);
    gate.setRatio (10.0f);
    gate.setAttack (1.0f);
    gate.setRelease (120.0f);
    compressor.prepare (spec);
    compressor.setAttack (10.0f);

    gain.reset (sampleRate, 0.05);
    pan.reset (sampleRate, 0.05);
    makeup.reset (sampleRate, 0.05);

    for (auto* filters : { &lowCut, &eqLow, &eqMid, &eqHigh })
        for (auto& f : *filters)
            f.z1 = f.z2 = 0.0f;
    filtersValid = false;

    delayBuffer.setSize (2, (int) (sampleRate * 4.1) + 1);   // up to 4 s (a bar at 60 BPM)
    delayBuffer.clear();
    delayWrite = 0;
    delaySamples.reset (sampleRate, 0.1);
    delayToneState.fill (0.0f);

    reverb.prepare (sampleRate);

    // Start from the latest settings without ramps.
    settingsChanged = true;
    process (delayBuffer, 0, 0);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) current.gainDb));
    pan.setCurrentAndTargetValue ((float) current.pan);
    makeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) current.compMakeupDb));
    delaySamples.setCurrentAndTargetValue ((float) (current.delayTimeMs (tempoBpm) * 0.001 * sampleRate));
}

void FxChain::setSettings (const FxSettings& settings)
{
    {
        const juce::SpinLock::ScopedLockType sl (settingsLock);
        pending = settings;
    }

    settingsChanged = true;
    active = settings.isActive();
    tail = settings.hasTail();
}

void FxChain::updateFilters()
{
    const auto sr = (float) sampleRate;
    const auto& s = current;

    auto shelfOrPeak = [sr] (std::array<Biquad, 2>& filters, int type, float hz, float db, float q)
    {
        const float A = std::pow (10.0f, db / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * juce::jlimit (20.0f, sr * 0.45f, hz) / sr;
        const float cosW = std::cos (w0), sinW = std::sin (w0);
        const float alpha = sinW / (2.0f * q);
        const float rootA = 2.0f * std::sqrt (A) * alpha;

        for (auto& f : filters)
        {
            if (type == 0)        // low shelf
                f.set (A * ((A + 1) - (A - 1) * cosW + rootA), 2 * A * ((A - 1) - (A + 1) * cosW), A * ((A + 1) - (A - 1) * cosW - rootA),
                       (A + 1) + (A - 1) * cosW + rootA, -2 * ((A - 1) + (A + 1) * cosW), (A + 1) + (A - 1) * cosW - rootA);
            else if (type == 1)   // peak
                f.set (1 + alpha * A, -2 * cosW, 1 - alpha * A, 1 + alpha / A, -2 * cosW, 1 - alpha / A);
            else if (type == 2)   // high shelf
                f.set (A * ((A + 1) + (A - 1) * cosW + rootA), -2 * A * ((A - 1) + (A + 1) * cosW), A * ((A + 1) + (A - 1) * cosW - rootA),
                       (A + 1) - (A - 1) * cosW + rootA, 2 * ((A - 1) - (A + 1) * cosW), (A + 1) - (A - 1) * cosW - rootA);
            else                  // high-pass (low cut)
                f.set ((1 + cosW) * 0.5f, -(1 + cosW), (1 + cosW) * 0.5f, 1 + alpha, -2 * cosW, 1 - alpha);
        }
    };

    shelfOrPeak (lowCut, 3, (float) s.lowCutHz, 0.0f, 0.7071f);
    shelfOrPeak (eqLow, 0, 150.0f, (float) s.eqLowDb, 0.7071f);
    shelfOrPeak (eqMid, 1, (float) s.eqMidHz, (float) s.eqMidDb, 0.8f);
    shelfOrPeak (eqHigh, 2, 6000.0f, (float) s.eqHighDb, 0.7071f);

    filtersBuiltFor = s;
    filtersValid = true;
}

void FxChain::processDelay (float* left, float* right, int numSamples)
{
    const int size = delayBuffer.getNumSamples();
    delaySamples.setTargetValue (juce::jlimit (1.0f, (float) size - 2.0f,
                                               (float) (current.delayTimeMs (tempoBpm) * 0.001 * sampleRate)));
    const float fbAmount = juce::jlimit (0.0f, 0.95f, (float) current.delayFeedback);
    const float mix = (float) current.delayMix;
    const float toneCoeff = onePoleCoeff (1000.0f * std::pow (16.0f, (float) current.delayTone), sampleRate);
    const bool pingPong = current.delayPingPong;
    float* dl = delayBuffer.getWritePointer (0);
    float* dr = delayBuffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        // Read with interpolation so tempo or time changes glide instead of clicking.
        float readPos = (float) delayWrite - delaySamples.getNextValue();
        if (readPos < 0.0f)
            readPos += (float) size;

        const int r0 = (int) readPos;
        const int r1 = (r0 + 1) % size;
        const float frac = readPos - (float) r0;
        const float echoL = dl[r0] + frac * (dl[r1] - dl[r0]);
        const float echoR = dr[r0] + frac * (dr[r1] - dr[r0]);

        // Each repeat passes through a gentle low-pass, so echoes darken as they fade.
        delayToneState[0] += toneCoeff * (echoL - delayToneState[0]);
        delayToneState[1] += toneCoeff * (echoR - delayToneState[1]);

        if (pingPong)
        {
            // Mono into the left side; each repeat crosses to the other side.
            dl[delayWrite] = 0.5f * (left[i] + right[i]) + delayToneState[1] * fbAmount;
            dr[delayWrite] = delayToneState[0] * fbAmount;
        }
        else
        {
            dl[delayWrite] = left[i] + delayToneState[0] * fbAmount;
            dr[delayWrite] = right[i] + delayToneState[1] * fbAmount;
        }

        left[i] += echoL * mix;
        right[i] += echoR * mix;

        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

void FxChain::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;

    if (settingsChanged)
    {
        const juce::SpinLock::ScopedTryLockType sl (settingsLock);
        if (sl.isLocked())
        {
            current = pending;
            settingsChanged = false;
        }
    }

    if (numSamples <= 0 || buffer.getNumChannels() < 2)
        return;

    const auto& s = current;
    float* left = buffer.getWritePointer (0, startSample);
    float* right = buffer.getWritePointer (1, startSample);
    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, 2)
                     .getSubBlock ((size_t) startSample, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);

    gain.setTargetValue (juce::Decibels::decibelsToGain ((float) s.gainDb));
    if (gain.isSmoothing() || gain.getTargetValue() != 1.0f)
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }

    if (s.gateOn)
    {
        gate.setThreshold ((float) s.gateThresholdDb);
        gate.process (context);
    }

    if (! filtersValid || s.lowCutHz != filtersBuiltFor.lowCutHz || s.eqLowDb != filtersBuiltFor.eqLowDb
        || s.eqMidDb != filtersBuiltFor.eqMidDb || s.eqMidHz != filtersBuiltFor.eqMidHz || s.eqHighDb != filtersBuiltFor.eqHighDb)
        updateFilters();

    if (s.lowCutOn)
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = lowCut[0].process (left[i]);
            right[i] = lowCut[1].process (right[i]);
        }

    if (s.eqOn)
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = eqHigh[0].process (eqMid[0].process (eqLow[0].process (left[i])));
            right[i] = eqHigh[1].process (eqMid[1].process (eqLow[1].process (right[i])));
        }

    if (s.compOn)
    {
        compressor.setThreshold ((float) s.compThresholdDb);
        compressor.setRatio ((float) juce::jmax (1.0, s.compRatio));
        compressor.setRelease ((float) s.compReleaseMs);
        compressor.process (context);

        makeup.setTargetValue (juce::Decibels::decibelsToGain ((float) s.compMakeupDb));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = makeup.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }
    }

    if (s.delayOn)
    {
        if (! delayWasOn)
        {
            delayBuffer.clear();   // don't replay whatever was left from last time
            delayToneState.fill (0.0f);
            delaySamples.setCurrentAndTargetValue ((float) (s.delayTimeMs (tempoBpm) * 0.001 * sampleRate));
        }
        processDelay (left, right, numSamples);
    }
    delayWasOn = s.delayOn;

    if (s.reverbOn)
    {
        if (! reverbWasOn)
            reverb.reset();
        reverb.setParameters ((float) s.reverbDecay, (float) s.reverbSize, (float) s.reverbDamping,
                              (float) s.reverbPredelayMs, (float) s.reverbMod);
        reverb.process (left, right, numSamples, (float) s.reverbMix);
    }
    reverbWasOn = s.reverbOn;

    // Constant-power pan: centre leaves both sides untouched.
    pan.setTargetValue ((float) s.pan);
    if (pan.isSmoothing() || pan.getTargetValue() != 0.0f)
        for (int i = 0; i < numSamples; ++i)
        {
            const float angle = (pan.getNextValue() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            left[i] *= std::cos (angle) * juce::MathConstants<float>::sqrt2;
            right[i] *= std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }
}
