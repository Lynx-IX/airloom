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
        { "swellOn", &FxSettings::swellOn }, { "tapeOn", &FxSettings::tapeOn }, { "filterOn", &FxSettings::filterOn },
        { "filterHighPass", &FxSettings::filterHighPass }, { "octaveOn", &FxSettings::octaveOn },
        { "chorusOn", &FxSettings::chorusOn }, { "motionOn", &FxSettings::motionOn },
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
        { "spaceSend", &FxSettings::spaceSend }, { "swellAttackMs", &FxSettings::swellAttackMs },
        { "swellThresholdDb", &FxSettings::swellThresholdDb }, { "tapeDrive", &FxSettings::tapeDrive },
        { "tapeWow", &FxSettings::tapeWow }, { "tapeTone", &FxSettings::tapeTone },
        { "filterCutoffHz", &FxSettings::filterCutoffHz }, { "filterResonance", &FxSettings::filterResonance },
        { "filterLfoRate", &FxSettings::filterLfoRate }, { "filterLfoDepth", &FxSettings::filterLfoDepth },
        { "octaveMix", &FxSettings::octaveMix }, { "chorusRate", &FxSettings::chorusRate },
        { "chorusDepth", &FxSettings::chorusDepth }, { "chorusMix", &FxSettings::chorusMix },
        { "reverbShimmer", &FxSettings::reverbShimmer }, { "reverbDry", &FxSettings::reverbDry },
        { "motionWidth", &FxSettings::motionWidth },
        { "motionPanRate", &FxSettings::motionPanRate }, { "motionPanDepth", &FxSettings::motionPanDepth },
    };

    using F = FxSettings;
    const FxParameter parameters[] = {
        { "gainDb", "Gain", &F::gainDb, -24.0, 24.0, 0.1, 0.0, nullptr },
        { "pan", "Pan", &F::pan, -1.0, 1.0, 0.01, 0.0, nullptr },
        { "spaceSend", "Space", &F::spaceSend, 0.0, 1.0, 0.01, 0.0, nullptr },
        { "gateThresholdDb", "Threshold", &F::gateThresholdDb, -80.0, -10.0, 0.5, 0.0, &F::gateOn },
        { "swellAttackMs", "Attack", &F::swellAttackMs, 50.0, 3000.0, 1.0, 600.0, &F::swellOn },
        { "swellThresholdDb", "Threshold", &F::swellThresholdDb, -70.0, -20.0, 0.5, 0.0, &F::swellOn },
        { "lowCutHz", "Freq", &F::lowCutHz, 20.0, 400.0, 1.0, 100.0, &F::lowCutOn },
        { "eqLowDb", "Low", &F::eqLowDb, -15.0, 15.0, 0.1, 0.0, &F::eqOn },
        { "eqMidDb", "Mid", &F::eqMidDb, -15.0, 15.0, 0.1, 0.0, &F::eqOn },
        { "eqMidHz", "Mid freq", &F::eqMidHz, 200.0, 6000.0, 1.0, 1000.0, &F::eqOn },
        { "eqHighDb", "High", &F::eqHighDb, -15.0, 15.0, 0.1, 0.0, &F::eqOn },
        { "compThresholdDb", "Threshold", &F::compThresholdDb, -50.0, 0.0, 0.5, 0.0, &F::compOn },
        { "compRatio", "Ratio", &F::compRatio, 1.0, 10.0, 0.1, 3.0, &F::compOn },
        { "compReleaseMs", "Release", &F::compReleaseMs, 20.0, 1000.0, 1.0, 150.0, &F::compOn },
        { "compMakeupDb", "Makeup", &F::compMakeupDb, 0.0, 18.0, 0.1, 0.0, &F::compOn },
        { "tapeDrive", "Drive", &F::tapeDrive, 0.0, 1.0, 0.01, 0.0, &F::tapeOn },
        { "tapeWow", "Wow", &F::tapeWow, 0.0, 1.0, 0.01, 0.0, &F::tapeOn },
        { "tapeTone", "Tone", &F::tapeTone, 0.0, 1.0, 0.01, 0.0, &F::tapeOn },
        { "filterCutoffHz", "Cutoff", &F::filterCutoffHz, 40.0, 18000.0, 1.0, 1200.0, &F::filterOn },
        { "filterResonance", "Resonance", &F::filterResonance, 0.0, 1.0, 0.01, 0.0, &F::filterOn },
        { "filterLfoRate", "Sweep rate", &F::filterLfoRate, 0.0, 2.0, 0.01, 0.3, &F::filterOn },
        { "filterLfoDepth", "Sweep depth", &F::filterLfoDepth, 0.0, 3.0, 0.01, 0.0, &F::filterOn },
        { "octaveMix", "Mix", &F::octaveMix, 0.0, 1.0, 0.01, 0.0, &F::octaveOn },
        { "chorusRate", "Rate", &F::chorusRate, 0.05, 3.0, 0.01, 0.6, &F::chorusOn },
        { "chorusDepth", "Depth", &F::chorusDepth, 0.0, 1.0, 0.01, 0.0, &F::chorusOn },
        { "chorusMix", "Mix", &F::chorusMix, 0.0, 1.0, 0.01, 0.0, &F::chorusOn },
        { "delayMs", "Time", &F::delayMs, 30.0, 2000.0, 1.0, 400.0, &F::delayOn },
        { "delayFeedback", "Feedback", &F::delayFeedback, 0.0, 0.9, 0.01, 0.0, &F::delayOn },
        { "delayTone", "Tone", &F::delayTone, 0.0, 1.0, 0.01, 0.0, &F::delayOn },
        { "delayMix", "Mix", &F::delayMix, 0.0, 1.0, 0.01, 0.0, &F::delayOn },
        { "reverbDecay", "Decay", &F::reverbDecay, 0.5, 20.0, 0.1, 4.0, &F::reverbOn },
        { "reverbSize", "Size", &F::reverbSize, 0.0, 1.0, 0.01, 0.0, &F::reverbOn },
        { "reverbDamping", "Tone", &F::reverbDamping, 0.0, 1.0, 0.01, 0.0, &F::reverbOn },
        { "reverbPredelayMs", "Pre-delay", &F::reverbPredelayMs, 0.0, 200.0, 1.0, 0.0, &F::reverbOn },
        { "reverbMod", "Movement", &F::reverbMod, 0.0, 1.0, 0.01, 0.0, &F::reverbOn },
        { "reverbShimmer", "Shimmer", &F::reverbShimmer, 0.0, 1.0, 0.01, 0.0, &F::reverbOn },
        { "reverbMix", "Mix", &F::reverbMix, 0.0, 1.0, 0.01, 0.0, &F::reverbOn },
        { "reverbDry", "Dry", &F::reverbDry, 0.0, 1.0, 0.01, 0.0, &F::reverbOn },
        { "motionWidth", "Width", &F::motionWidth, 0.0, 2.0, 0.01, 0.0, &F::motionOn },
        { "motionPanRate", "Pan rate", &F::motionPanRate, 0.01, 1.0, 0.01, 0.15, &F::motionOn },
        { "motionPanDepth", "Pan depth", &F::motionPanDepth, 0.0, 1.0, 0.01, 0.0, &F::motionOn },
    };

    float onePoleCoeff (float cutoffHz, double sampleRate)
    {
        return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate);
    }
}

const FxParameter* findFxParameter (const juce::String& name)
{
    for (auto& p : parameters)
        if (name == p.name)
            return &p;
    return nullptr;
}

double* FxSettings::findParameter (const juce::String& name)
{
    if (auto* p = findFxParameter (name))
        return &(this->*(p->member));
    return nullptr;
}

double FxSettings::delayTimeMs (double bpm) const
{
    if (! delaySync)
        return delayMs;

    // Divisions are in quarter notes; "1 bar" is however long the set's bar is.
    const int index = juce::jlimit (0, numDelayDivisions - 1, delayDivision);
    const double beats = index == numDelayDivisions - 1 ? (double) FxChain::beatsPerBar.load()
                                                        : delayDivisions[(size_t) index].beats * FxChain::beatUnit.load() / 4.0;
    return 60000.0 / juce::jlimit (20.0, 300.0, bpm) * beats;
}

void FxSettings::addToXml (juce::XmlElement& parent) const
{
    auto* e = parent.createNewChildElement ("Fx");
    for (auto& f : boolFields)   e->setAttribute (f.name, this->*f.member);
    for (auto& f : doubleFields) e->setAttribute (f.name, this->*f.member);
    e->setAttribute ("delayDivision", delayDivision);
    e->setAttribute ("octaveInterval", octaveInterval);
}

FxSettings FxSettings::fromXml (const juce::XmlElement* e, const FxSettings& defaults)
{
    FxSettings s = defaults;
    if (e == nullptr)
        return s;

    for (auto& f : boolFields)   s.*f.member = e->getBoolAttribute (f.name, defaults.*f.member);
    for (auto& f : doubleFields) s.*f.member = e->getDoubleAttribute (f.name, defaults.*f.member);
    s.delayDivision = juce::jlimit (0, numDelayDivisions - 1, e->getIntAttribute ("delayDivision", defaults.delayDivision));
    s.octaveInterval = juce::jlimit (0, numShiftIntervals - 1, e->getIntAttribute ("octaveInterval", defaults.octaveInterval));
    return s;
}

//==============================================================================
void PitchShifter::prepare (double sampleRate, float windowMs)
{
    window = juce::jmax (8.0f, windowMs * 0.001f * (float) sampleRate);
    buffer.assign ((size_t) window * 2 + 8, 0.0f);
    reset();
}

void PitchShifter::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
    phase = 0.0f;
}

float PitchShifter::process (float input)
{
    const int size = (int) buffer.size();
    buffer[(size_t) write] = input;

    // The delay grows (lower) or shrinks (higher) by (1 - ratio) samples per sample,
    // wrapping round the window; the second head is half a window behind.
    phase += (1.0f - ratio) / window;
    phase -= std::floor (phase);

    auto read = [&] (float p)
    {
        float pos = (float) write - (2.0f + p * window);
        if (pos < 0.0f)
            pos += (float) size;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % size;
        const float frac = pos - (float) i0;
        return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
    };

    const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
    // Each head fades out as it nears its jump (sin windows, equal power between the two).
    const float out = read (phase) * std::sin (juce::MathConstants<float>::pi * phase)
                    + read (p2) * std::sin (juce::MathConstants<float>::pi * p2);

    if (++write >= size)
        write = 0;
    return out;
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

    shimmerShifter.prepare (sampleRate, 90.0f);
    shimmerShifter.setSemitones (12.0f);
    shimmerLowCutCoeff = onePoleCoeff (400.0f, sampleRate);
    reset();
}

void AmbientReverb::reset()
{
    for (auto& line : lines) std::fill (line.begin(), line.end(), 0.0f);
    for (auto& d : diffusers) std::fill (d.buffer.begin(), d.buffer.end(), 0.0f);
    std::fill (predelay.begin(), predelay.end(), 0.0f);
    damp.fill (0.0f);
    shimmerShifter.reset();
    shimmerLast = shimmerLowCut = 0.0f;
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

void AmbientReverb::process (float* left, float* right, int numSamples, float mix, float dry)
{
    const float dryGain = (1.0f - 0.5f * mix) * juce::jlimit (0.0f, 1.0f, dry);
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
    // Shimmer: the tail, an octave up and without its lows, goes round again. Soft-clipped
    // and kept well under unity so it rises and fades rather than building up for ever.
    if (shimmer > 0.0f)
    {
        shimmerLowCut += shimmerLowCutCoeff * (shimmerLast - shimmerLowCut);
        const float up = shimmerShifter.process (shimmerLast - shimmerLowCut);
        input += std::tanh (up * shimmer * 0.9f) * 0.5f;
    }

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
    shimmerLast = 0.25f * (wetL + wetR);
}

//==============================================================================
void SpaceReverb::addToBus (int channel, int offset, const float* source, int numSamples, float gain)
{
    if (gain <= 0.0f || channel >= bus.getNumChannels() || offset < 0)
        return;

    const int n = juce::jmin (numSamples, bus.getNumSamples() - offset);
    if (n > 0)
        bus.addFrom (channel, offset, source, n, gain);
}

FxSettings SpaceReverb::defaults()
{
    FxSettings s;
    s.reverbOn = true;
    s.reverbDecay = 9.0;
    s.reverbSize = 0.9;
    s.reverbDamping = 0.45;
    s.reverbPredelayMs = 40.0;
    s.reverbMod = 0.45;
    s.reverbShimmer = 0.0;
    s.reverbMix = 0.7;   // the return level
    return s;
}

void SpaceReverb::prepare (double sampleRate, int maxBlockSize)
{
    bus.setSize (2, juce::jmax (4096, maxBlockSize));
    bus.clear();
    reverb.prepare (sampleRate);
    returnGain.reset (sampleRate, 0.05);
    changed = true;
}

void SpaceReverb::setSettings (const FxSettings& settings)
{
    {
        const juce::SpinLock::ScopedLockType sl (settingsLock);
        pending = settings;
    }
    changed = true;
}

void SpaceReverb::process (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;

    if (changed)
    {
        const juce::SpinLock::ScopedTryLockType sl (settingsLock);
        if (sl.isLocked())
        {
            current = pending;
            changed = false;
            reverb.setParameters ((float) current.reverbDecay, (float) current.reverbSize, (float) current.reverbDamping,
                                  (float) current.reverbPredelayMs, (float) current.reverbMod);
            reverb.setShimmer ((float) current.reverbShimmer);
        }
    }

    const int n = juce::jmin (numSamples, bus.getNumSamples());
    if (n <= 0 || out.getNumChannels() < 2 || bus.getNumChannels() < 2)
        return;

    returnGain.setTargetValue (current.reverbOn ? (float) current.reverbMix * 0.6f : 0.0f);
    const float* sendL = bus.getReadPointer (0);
    const float* sendR = bus.getReadPointer (1);
    float* left = out.getWritePointer (0, startSample);
    float* right = out.getWritePointer (1, startSample);

    for (int i = 0; i < n; ++i)
    {
        float wetL, wetR;
        reverb.tick (0.5f * (sendL[i] + sendR[i]), wetL, wetR);
        const float g = returnGain.getNextValue();
        left[i] += wetL * g;
        right[i] += wetR * g;
    }

    bus.clear (0, n);
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

    swellEnvelope = swellGain = 0.0f;

    tapeLine.assign ((size_t) (sampleRate * 0.03) * 2 + 8, 0.0f);   // up to 30 ms, stereo interleaved
    tapeWrite = 0;
    tapeToneState.fill (0.0f);

    filter.prepare (spec);
    filter.reset();
    filterCutoff.reset (sampleRate, 0.05);
    filterCutoff.setCurrentAndTargetValue (2000.0f);

    octaveShifter.prepare (sampleRate, 60.0f);

    chorus.prepare (spec);
    chorus.setCentreDelay (12.0f);
    chorus.setFeedback (0.1f);

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
    spaceSend = (float) juce::jlimit (0.0, 1.0, settings.spaceSend);
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

void FxChain::processSwell (float* left, float* right, int numSamples)
{
    // A note starting from silence fades in over the attack time; a gap below the
    // threshold (between notes) closes it again, ready for the next.
    const float threshold = juce::Decibels::decibelsToGain ((float) current.swellThresholdDb);
    const float rise = 1.0f / (juce::jmax (10.0f, (float) current.swellAttackMs) * 0.001f * (float) sampleRate);
    const float fall = 1.0f / (0.12f * (float) sampleRate);
    const float envRelease = std::exp (-1.0f / (0.05f * (float) sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        swellEnvelope = level > swellEnvelope ? level : level + (swellEnvelope - level) * envRelease;
        swellGain = swellEnvelope > threshold ? juce::jmin (1.0f, swellGain + rise) : juce::jmax (0.0f, swellGain - fall);

        const float g = swellGain * swellGain;   // a slow start, like a bow
        left[i] *= g;
        right[i] *= g;
    }
}

void FxChain::processTape (float* left, float* right, int numSamples)
{
    // Saturation ceiling: high with no drive (only peaks round off), lower as it's driven.
    const float ceiling = 1.2f * std::pow (0.25f, (float) current.tapeDrive);
    const float wow = (float) current.tapeWow;
    const int size = (int) tapeLine.size() / 2;
    const float baseDelay = 0.012f * (float) sampleRate;
    const float wowDepth = wow * 0.0035f * (float) sampleRate, flutterDepth = wow * 0.0004f * (float) sampleRate;
    const double wowStep = juce::MathConstants<double>::twoPi * 0.55 / sampleRate;
    const double flutterStep = juce::MathConstants<double>::twoPi * 6.5 / sampleRate;
    const float toneCoeff = onePoleCoeff (2500.0f * std::pow (7.0f, (float) current.tapeTone), sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        // Saturation: unchanged when quiet, rounding off towards the ceiling as it gets loud.
        float l = ceiling * std::tanh (left[i] / ceiling);
        float r = ceiling * std::tanh (right[i] / ceiling);

        // Wow and flutter: a slowly wandering delay bends the pitch a little.
        tapeLine[(size_t) tapeWrite * 2] = l;
        tapeLine[(size_t) tapeWrite * 2 + 1] = r;
        const float delay = baseDelay + wowDepth * (float) std::sin (tapeWowPhase) + flutterDepth * (float) std::sin (tapeFlutterPhase);
        tapeWowPhase += wowStep;
        tapeFlutterPhase += flutterStep;
        if (tapeWowPhase > juce::MathConstants<double>::twoPi) tapeWowPhase -= juce::MathConstants<double>::twoPi;
        if (tapeFlutterPhase > juce::MathConstants<double>::twoPi) tapeFlutterPhase -= juce::MathConstants<double>::twoPi;

        float pos = (float) tapeWrite - delay;
        if (pos < 0.0f)
            pos += (float) size;
        const int p0 = (int) pos, p1 = (p0 + 1) % size;
        const float frac = pos - (float) p0;
        l = tapeLine[(size_t) p0 * 2] + frac * (tapeLine[(size_t) p1 * 2] - tapeLine[(size_t) p0 * 2]);
        r = tapeLine[(size_t) p0 * 2 + 1] + frac * (tapeLine[(size_t) p1 * 2 + 1] - tapeLine[(size_t) p0 * 2 + 1]);
        if (++tapeWrite >= size)
            tapeWrite = 0;

        // Worn highs.
        tapeToneState[0] += toneCoeff * (l - tapeToneState[0]);
        tapeToneState[1] += toneCoeff * (r - tapeToneState[1]);
        left[i] = tapeToneState[0];
        right[i] = tapeToneState[1];
    }
}

void FxChain::processFilter (float* left, float* right, int numSamples)
{
    if (current.filterHighPass != filterWasHighPass || ! filterWasOn)
    {
        filter.setType (current.filterHighPass ? juce::dsp::StateVariableTPTFilterType::highpass
                                               : juce::dsp::StateVariableTPTFilterType::lowpass);
        filterWasHighPass = current.filterHighPass;
    }

    // Q 0.7 .. 4.2, and the level comes down as it rises so the resonant peak doesn't jump out.
    const float q = 0.707f * std::pow (6.0f, (float) current.filterResonance);
    filter.setResonance (q);
    const float level = std::pow (q / 0.707f, -0.4f);
    filterCutoff.setTargetValue ((float) juce::jlimit (20.0, 20000.0, current.filterCutoffHz));
    const double lfoStep = juce::MathConstants<double>::twoPi * current.filterLfoRate / sampleRate;
    const float depth = current.filterLfoRate > 0.0 ? (float) current.filterLfoDepth : 0.0f;
    const float top = (float) sampleRate * 0.45f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float base = filterCutoff.getNextValue();

        // The sweep moves the cutoff up to 'depth' octaves either side; recalculated every 16 samples.
        if ((i & 15) == 0)
            filter.setCutoffFrequency (juce::jlimit (20.0f, top, base * std::pow (2.0f, depth * (float) std::sin (filterLfoPhase))));
        filterLfoPhase += lfoStep;
        if (filterLfoPhase > juce::MathConstants<double>::twoPi)
            filterLfoPhase -= juce::MathConstants<double>::twoPi;

        left[i] = filter.processSample (0, left[i]) * level;
        right[i] = filter.processSample (1, right[i]) * level;
    }
}

void FxChain::processOctave (float* left, float* right, int numSamples)
{
    octaveShifter.setSemitones (shiftIntervals[(size_t) juce::jlimit (0, numShiftIntervals - 1, current.octaveInterval)].semitones);
    const float mix = (float) current.octaveMix;

    for (int i = 0; i < numSamples; ++i)
    {
        const float shifted = octaveShifter.process (0.5f * (left[i] + right[i])) * mix;
        left[i] += shifted;
        right[i] += shifted;
    }
}

void FxChain::processMotion (float* left, float* right, int numSamples)
{
    const float width = (float) current.motionWidth;
    const float depth = (float) current.motionPanDepth;
    const double step = juce::MathConstants<double>::twoPi * current.motionPanRate / sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // Width: scale the difference between the sides (mid/side).
        const float mid = 0.5f * (left[i] + right[i]);
        const float side = 0.5f * (left[i] - right[i]) * width;
        float l = mid + side, r = mid - side;

        // Auto-pan: a slow constant-power drift from side to side.
        const float angle = (1.0f + depth * (float) std::sin (motionPhase)) * juce::MathConstants<float>::pi * 0.25f;
        motionPhase += step;
        if (motionPhase > juce::MathConstants<double>::twoPi)
            motionPhase -= juce::MathConstants<double>::twoPi;

        left[i] = l * std::cos (angle) * juce::MathConstants<float>::sqrt2;
        right[i] = r * std::sin (angle) * juce::MathConstants<float>::sqrt2;
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

    if (s.swellOn)
        processSwell (left, right, numSamples);
    else
        swellGain = 1.0f;   // switched on mid-note, it starts open rather than cutting out

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

    if (s.tapeOn)
    {
        if (! tapeWasOn)
            std::fill (tapeLine.begin(), tapeLine.end(), 0.0f);
        processTape (left, right, numSamples);
    }
    tapeWasOn = s.tapeOn;

    if (s.filterOn)
    {
        if (! filterWasOn)
        {
            filter.reset();
            filterCutoff.setCurrentAndTargetValue ((float) juce::jlimit (20.0, 20000.0, s.filterCutoffHz));
        }
        processFilter (left, right, numSamples);
    }
    filterWasOn = s.filterOn;

    if (s.octaveOn)
    {
        if (! octaveWasOn)
            octaveShifter.reset();
        processOctave (left, right, numSamples);
    }
    octaveWasOn = s.octaveOn;

    if (s.chorusOn)
    {
        if (! chorusWasOn)
            chorus.reset();
        chorus.setRate ((float) s.chorusRate);
        chorus.setDepth ((float) s.chorusDepth);
        chorus.setMix ((float) s.chorusMix);
        chorus.process (context);
    }
    chorusWasOn = s.chorusOn;

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
        reverb.setShimmer ((float) s.reverbShimmer);
        reverb.process (left, right, numSamples, (float) s.reverbMix, (float) s.reverbDry);
    }
    reverbWasOn = s.reverbOn;

    if (s.motionOn)
        processMotion (left, right, numSamples);

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
