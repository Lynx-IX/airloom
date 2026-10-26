#pragma once

#include <JuceHeader.h>

// Tempo-synced delay lengths, in beats.
struct DelayDivision
{
    const char* name;
    double beats;
};

inline constexpr DelayDivision delayDivisions[] = {
    { "1/16", 0.25 }, { "1/8 triplet", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8 dotted", 0.75 },
    { "1/4 triplet", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4 dotted", 1.5 }, { "1/2", 2.0 }, { "1 bar", 4.0 },
};
inline constexpr int numDelayDivisions = (int) std::size (delayDivisions);

// Safety limiter for the master output. Below the ceiling it does nothing; above it
// the level comes down at once and recovers over 150 ms, so nothing ever hard-clips
// the speakers. Non-finite samples (a plug-in gone wrong) are silenced.
class OutputLimiter
{
public:
    static constexpr float ceiling = 0.944f;   // -0.5 dBFS

    void prepare (double sampleRate)
    {
        release = std::exp (-1.0f / (0.15f * (float) sampleRate));
        gain = 1.0f;
    }

    // Returns the deepest gain this block used (1 = untouched).
    float process (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels)
    {
        float lowest = 1.0f;

        for (int i = start; i < start + numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& x = buffer.getWritePointer (ch)[i];
                if (! std::isfinite (x))
                    x = 0.0f;
                peak = juce::jmax (peak, std::abs (x));
            }

            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            gain = target < gain ? target : target + (gain - target) * release;
            lowest = juce::jmin (lowest, gain);

            if (gain < 1.0f)
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.getWritePointer (ch)[i] *= gain;
        }

        return lowest;
    }

private:
    float release = 0.999f, gain = 1.0f;
};

// The effect settings for one input or row, as plain values that can be saved.
struct FxSettings
{
    double gainDb = 0.0;            // channel gain before everything else
    double pan = 0.0;               // -1 left .. +1 right

    bool gateOn = false;
    double gateThresholdDb = -50.0;

    bool lowCutOn = false;
    double lowCutHz = 100.0;

    bool eqOn = false;
    double eqLowDb = 0.0, eqMidDb = 0.0, eqMidHz = 1000.0, eqHighDb = 0.0;

    bool compOn = false;
    double compThresholdDb = -18.0, compRatio = 3.0, compReleaseMs = 150.0, compMakeupDb = 0.0;

    bool delayOn = false;
    bool delaySync = true;
    int delayDivision = 5;          // index into delayDivisions (1/4)
    double delayMs = 450.0;         // used when not synced
    double delayFeedback = 0.35;
    double delayTone = 0.6;         // 0 dark .. 1 bright repeats
    double delayMix = 0.25;
    bool delayPingPong = false;

    bool reverbOn = false;
    double reverbDecay = 4.0;       // seconds
    double reverbSize = 0.7;
    double reverbDamping = 0.5;     // 0 bright .. 1 dark
    double reverbPredelayMs = 20.0;
    double reverbMod = 0.3;
    double reverbMix = 0.3;

    bool hasTail() const    { return delayOn || reverbOn; }
    bool anyOn() const      { return gateOn || lowCutOn || eqOn || compOn || delayOn || reverbOn; }
    bool isActive() const   { return anyOn() || gainDb != 0.0 || pan != 0.0; }

    double delayTimeMs (double bpm) const;

    void addToXml (juce::XmlElement& parent) const;
    static FxSettings fromXml (const juce::XmlElement* fx, const FxSettings& defaults = {});
};

// A lush stereo reverb for long ambient tails: pre-delay, input diffusion,
// then an 8-line feedback delay network with gently modulated line lengths.
class AmbientReverb
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParameters (float decaySeconds, float size, float damping, float predelayMs, float modulation);

    // Processes in place, blending the reverb under the dry signal by 'mix'.
    void process (float* left, float* right, int numSamples, float mix);

    // One (mono) sample in, the stereo tail out.
    void tick (float input, float& wetL, float& wetR);

    // While frozen the tail holds almost indefinitely: no damping, next to no loss.
    // The change glides over about 50 ms so it never clicks.
    void setFrozen (bool shouldFreeze)   { frozen = shouldFreeze; }

private:
    static constexpr int numLines = 8;

    struct Allpass
    {
        std::vector<float> buffer;
        int pos = 0;

        float process (float x)
        {
            constexpr float g = 0.6f;
            const float delayed = buffer[(size_t) pos];
            const float y = delayed - g * x;
            buffer[(size_t) pos] = x + g * y;
            if (++pos >= (int) buffer.size())
                pos = 0;
            return y;
        }
    };

    double sampleRate = 44100.0;
    std::array<std::vector<float>, numLines> lines;
    std::array<int, numLines> writePos {};
    std::array<float, numLines> lineLength {}, feedback {}, damp {}, lfoPhase {}, lfoStep {};
    std::array<Allpass, 4> diffusers;
    std::vector<float> predelay;
    int predelayWrite = 0, predelaySamples = 0;
    float dampCoeff = 0.5f, modDepth = 0.0f;
    float currentDecay = -1.0f, currentSize = -1.0f;
    bool frozen = false;
    float freezeAmount = 0.0f, freezeStep = 0.0f;
};

// FREEZE: holds what was just played as an endless pad to play over. A reverb
// listens to the live inputs and instruments all the time; pressing hold
// freezes its tail and stops it listening, releasing fades the pad out.
//
// Thread model: held and releaseSeconds are set from anywhere, prepare() and
// process() run on the audio thread.
class FreezeHold
{
public:
    std::atomic<bool> held { false };
    std::atomic<float> releaseSeconds { 4.0f };

    void prepare (double sampleRate);

    // Adds the held pad to the buffer, listening to what's in it first.
    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    // True while the pad can be heard (held, or fading out).
    bool isSounding() const   { return sounding; }

private:
    AmbientReverb reverb;
    double sampleRate = 44100.0;
    float level = 0.0f, listen = 1.0f;
    std::atomic<bool> sounding { false };
};

// Gain -> gate -> low cut -> EQ -> compressor -> delay -> reverb -> pan,
// processed in place on a stereo buffer.
//
// Thread model: setSettings() is called from the message thread, prepare()
// and process() from the audio thread.
class FxChain
{
public:
    // The set's tempo, shared by every synced delay.
    static inline std::atomic<double> tempoBpm { 72.0 };

    void prepare (double sampleRate, int maxBlockSize);
    void setSettings (const FxSettings& settings);

    bool isActive() const   { return active; }
    // True while delay or reverb is on: callers keep processing silence so tails ring out.
    bool needsTail() const  { return tail; }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

        void set (float nb0, float nb1, float nb2, float a0, float na1, float na2)
        {
            b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
        }

        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void updateFilters();
    void processDelay (float* left, float* right, int numSamples);

    // Settings handed over from the message thread.
    juce::SpinLock settingsLock;
    FxSettings pending;
    std::atomic<bool> settingsChanged { false }, active { false }, tail { false };

    // Audio thread only.
    FxSettings current, filtersBuiltFor;
    bool filtersValid = false;
    double sampleRate = 44100.0;

    juce::SmoothedValue<float> gain, pan;
    juce::dsp::NoiseGate<float> gate;
    juce::dsp::Compressor<float> compressor;
    juce::SmoothedValue<float> makeup;
    std::array<Biquad, 2> lowCut, eqLow, eqMid, eqHigh;

    juce::AudioBuffer<float> delayBuffer;
    int delayWrite = 0;
    juce::SmoothedValue<float> delaySamples;
    std::array<float, 2> delayToneState {};
    bool delayWasOn = false, reverbWasOn = false;

    AmbientReverb reverb;
};
