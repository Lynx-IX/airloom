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

// The intervals the octave effect can add.
struct ShiftInterval
{
    const char* name;
    float semitones;
};

inline constexpr ShiftInterval shiftIntervals[] = {
    { "Octave down", -12.0f }, { "Octave up", 12.0f }, { "Fifth up", 7.0f }, { "Fifth down", -7.0f }, { "Two octaves down", -24.0f },
};
inline constexpr int numShiftIntervals = (int) std::size (shiftIntervals);

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
    double spaceSend = 0.0;         // 0..1 sent to the shared Space reverb, after the fader

    bool swellOn = false;           // fades each note in, like a volume pedal
    double swellAttackMs = 600.0;
    double swellThresholdDb = -45.0;

    bool gateOn = false;
    double gateThresholdDb = -50.0;

    bool lowCutOn = false;
    double lowCutHz = 100.0;

    bool eqOn = false;
    double eqLowDb = 0.0, eqMidDb = 0.0, eqMidHz = 1000.0, eqHighDb = 0.0;

    bool compOn = false;
    double compThresholdDb = -18.0, compRatio = 3.0, compReleaseMs = 150.0, compMakeupDb = 0.0;

    bool tapeOn = false;            // saturation, wow and flutter, rolled-off highs
    double tapeDrive = 0.3, tapeWow = 0.25, tapeTone = 0.5;

    bool filterOn = false;          // a resonant sweep, optionally moving by itself
    bool filterHighPass = false;
    double filterCutoffHz = 2000.0, filterResonance = 0.2;
    double filterLfoRate = 0.0;     // Hz; 0 = still
    double filterLfoDepth = 1.0;    // octaves either side

    bool octaveOn = false;          // adds a pitch-shifted copy
    int octaveInterval = 0;         // index into shiftIntervals
    double octaveMix = 0.4;

    bool chorusOn = false;
    double chorusRate = 0.4, chorusDepth = 0.35, chorusMix = 0.4;

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
    double reverbShimmer = 0.0;     // 0..1: the tail rises an octave as it fades
    double reverbDry = 1.0;         // how much of the original stays under the reverb (0 = only reverb)

    bool motionOn = false;          // stereo width and a slow auto-pan
    double motionWidth = 1.0;       // 0 mono .. 1 as is .. 2 extra wide
    double motionPanRate = 0.1;     // Hz
    double motionPanDepth = 0.5;

    bool hasTail() const    { return delayOn || reverbOn; }
    bool anyOn() const
    {
        return gateOn || swellOn || lowCutOn || eqOn || compOn || tapeOn || filterOn || octaveOn || chorusOn
            || delayOn || reverbOn || motionOn;
    }
    bool isActive() const   { return anyOn() || gainDb != 0.0 || pan != 0.0 || spaceSend > 0.0; }

    double delayTimeMs (double bpm) const;

    void addToXml (juce::XmlElement& parent) const;
    static FxSettings fromXml (const juce::XmlElement* fx, const FxSettings& defaults = {});

    // A knob's setting by its saved name ("reverbMix"), for MIDI control. Null if there's no such knob.
    double* findParameter (const juce::String& name);
};

// Every effect knob: its saved name, range and centre (for a skewed knob), so the
// panel and MIDI control agree on what a knob covers.
struct FxParameter
{
    const char* name;
    const char* label;
    double FxSettings::* member;
    double min, max, step, skewMidPoint;
    bool FxSettings::* section;     // the switch it turns on when moved (null: always on)
};

const FxParameter* findFxParameter (const juce::String& name);

// Part-way from one set of effects to another (t 0..1), for scene morphs. Knobs glide; an effect
// switched on or off along the way is on throughout, its mix fading in from nothing or out to
// nothing, so it arrives and leaves smoothly; other choices (sync, divisions) change half-way.
// At t = 1 it is exactly 'to'.
FxSettings blendFx (const FxSettings& from, const FxSettings& to, float t);

// True if the two would sound the same (every setting equal).
bool sameFx (const FxSettings& a, const FxSettings& b);

// The pitch shifter in the octave effect and the shimmer: two read heads sweep through
// a short delay line at the new speed, crossfading so neither's jump back is heard.
class PitchShifter
{
public:
    void prepare (double sampleRate, float windowMs);
    void reset();
    void setSemitones (float semitones)   { ratio = std::pow (2.0f, semitones / 12.0f); }
    float process (float input);

private:
    std::vector<float> buffer;
    int write = 0;
    float window = 1000.0f, phase = 0.0f, ratio = 1.0f;
};

// A lush stereo reverb for long ambient tails: pre-delay, input diffusion,
// then an 8-line feedback delay network with gently modulated line lengths.
class AmbientReverb
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParameters (float decaySeconds, float size, float damping, float predelayMs, float modulation);

    // 0..1: feeds the tail back an octave up, so it rises into a shimmer as it fades.
    void setShimmer (float amount)   { shimmer = juce::jlimit (0.0f, 1.0f, amount); }

    // Processes in place, blending the reverb under the dry signal by 'mix'; 'dry' turns
    // the original down on top of that (0 leaves only the reverb).
    void process (float* left, float* right, int numSamples, float mix, float dry = 1.0f);

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

    PitchShifter shimmerShifter;
    float shimmer = 0.0f, shimmerLast = 0.0f, shimmerLowCut = 0.0f, shimmerLowCutCoeff = 0.0f;
};

// The shared reverb, "Space": any channel can send some of itself to it (after its fader),
// so everything sits in one room. Sources add their sends to the bus as they mix; then
// it's played through one big reverb into the master.
//
// Thread model: setSettings() from the message thread; everything else on the audio thread.
class SpaceReverb
{
public:
    // The sends for this block: sample 0 is the block's first sample.
    static inline juce::AudioBuffer<float> bus;
    static void addToBus (int channel, int offset, const float* source, int numSamples, float gain);

    void prepare (double sampleRate, int maxBlockSize);
    void setSettings (const FxSettings& settings);

    // Adds the reverb of what was sent into 'out', then clears the bus for the next block.
    void process (juce::AudioBuffer<float>& out, int startSample, int numSamples);

    // The Space defaults: a big, slow room that sits behind everything.
    static FxSettings defaults();

private:
    AmbientReverb reverb;
    juce::SpinLock settingsLock;
    FxSettings pending, current;
    std::atomic<bool> changed { true };
    juce::SmoothedValue<float> returnGain;
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

// Gain -> gate -> swell -> low cut -> EQ -> compressor -> tape -> filter -> octave ->
// chorus -> delay -> reverb -> motion -> pan, processed in place on a stereo buffer.
//
// Thread model: setSettings() is called from the message thread, prepare()
// and process() from the audio thread.
class FxChain
{
public:
    // The set's tempo, shared by every synced delay, and its time signature: BPM counts
    // beats of 1/beatUnit (a quarter in 4/4, an eighth in 6/8).
    static inline std::atomic<double> tempoBpm { 72.0 };
    static inline std::atomic<int> beatsPerBar { 4 }, beatUnit { 4 };

    void prepare (double sampleRate, int maxBlockSize);
    void setSettings (const FxSettings& settings);

    bool isActive() const   { return active; }
    // How much this channel sends to the shared Space reverb (0..1).
    float getSpaceSend() const   { return spaceSend; }
    // True while delay or reverb is on: callers keep processing silence so tails ring out.
    bool needsTail() const  { return tail; }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    // Empties every tail and filter on the next block (a chain handed to another channel
    // shouldn't play the last one's reverb).
    void requestClear()   { clearRequested = true; }

    // Safety net: a block that comes out non-finite or far beyond full scale means something
    // ran away; the chain is emptied, the block silenced and this count goes up.
    static inline std::atomic<int> runaways { 0 };

private:
    void clearState();
    std::atomic<bool> clearRequested { false };

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
    void processSwell (float* left, float* right, int numSamples);
    void processTape (float* left, float* right, int numSamples);
    void processFilter (float* left, float* right, int numSamples);
    void processOctave (float* left, float* right, int numSamples);
    void processMotion (float* left, float* right, int numSamples);

    // Settings handed over from the message thread.
    juce::SpinLock settingsLock;
    FxSettings pending;
    std::atomic<bool> settingsChanged { false }, active { false }, tail { false };
    std::atomic<float> spaceSend { 0.0f };

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

    float swellEnvelope = 0.0f, swellGain = 0.0f;

    std::vector<float> tapeLine;     // stereo, interleaved: the wow and flutter delay
    int tapeWrite = 0;
    double tapeWowPhase = 0.0, tapeFlutterPhase = 0.0;
    std::array<float, 2> tapeToneState {};
    bool tapeWasOn = false;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> filterCutoff;
    double filterLfoPhase = 0.0;
    bool filterWasOn = false, filterWasHighPass = false;

    PitchShifter octaveShifter;
    bool octaveWasOn = false;

    juce::dsp::Chorus<float> chorus;
    bool chorusWasOn = false;

    double motionPhase = 0.0;
};
