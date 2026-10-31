#pragma once

#include <JuceHeader.h>
#include "Effects.h"

// How the Akai Fire's 4x16 pads map to notes: a scale laid out left to right,
// each row up starting a few scale steps higher (like an in-key Push layout).
struct PadLayout
{
    static constexpr int numScales = 7;
    static inline const char* const scaleNames[numScales] = {
        "Minor pentatonic", "Major pentatonic", "Natural minor", "Major", "Dorian", "Hijaz", "Chromatic"
    };
    static inline const std::array<std::vector<int>, numScales> scaleSteps {{
        { 0, 3, 5, 7, 10 },
        { 0, 2, 4, 7, 9 },
        { 0, 2, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 5, 7, 9, 11 },
        { 0, 2, 3, 5, 7, 9, 10 },
        { 0, 1, 4, 5, 7, 8, 10 },
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
    }};
    // Spelled the way they're usually named for these instruments: flats for Bb, Eb and Ab.
    static inline const char* const noteNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    int root = 9;      // A
    int scale = 0;     // minor pentatonic
    int octave = 3;    // the bottom-left pad is the root in this octave (A3 by default)

    // Pads are numbered 0-63 from the top-left, as the Fire sends them (note 54 + pad).
    int degreeForPad (int pad) const
    {
        const auto& steps = scaleSteps[(size_t) juce::jlimit (0, numScales - 1, scale)];
        const int row = 3 - pad / 16, column = pad % 16;
        return column + row * (steps.size() == 12 ? 5 : 3);
    }

    int noteForPad (int pad) const
    {
        const auto& steps = scaleSteps[(size_t) juce::jlimit (0, numScales - 1, scale)];
        const int size = (int) steps.size();
        const int degree = degreeForPad (pad);
        return juce::jlimit (0, 127, 12 * (octave + 1) + root + 12 * (degree / size) + steps[(size_t) (degree % size)]);
    }

    bool isRootPad (int pad) const
    {
        return degreeForPad (pad) % (int) scaleSteps[(size_t) juce::jlimit (0, numScales - 1, scale)].size() == 0;
    }

    // The key that shares these notes with a different home: the relative major of a
    // minor scale (A minor -> C major), the relative minor of a major one (Bb major -> G minor).
    // -1 for scales without one (Hijaz, chromatic).
    int relativeRoot() const
    {
        switch (scale)
        {
            case 0: case 2: return (root + 3) % 12;    // minor -> relative major
            case 1: case 3: return (root + 9) % 12;    // major -> relative minor
            case 4:         return (root + 10) % 12;   // Dorian -> the major scale it comes from
            default:        return -1;
        }
    }

    bool isRelativeRootPad (int pad) const
    {
        const int relative = relativeRoot();
        return relative >= 0 && ! isRootPad (pad) && noteForPad (pad) % 12 == relative;
    }

    juce::String describeRelative() const
    {
        const int relative = relativeRoot();
        if (relative < 0)
            return {};
        return juce::String (noteNames[relative]) + (scale == 1 || scale == 3 ? " minor" : " major");
    }

    // The key as people say it: "A minor", "Bb major", "E Hijaz". Empty for chromatic.
    juce::String keyName() const
    {
        const juce::String note = noteNames[root % 12];
        switch (scale)
        {
            case 0: case 2: case 4: return note + " minor";
            case 1: case 3:         return note + " major";
            case 5:                 return note + " Hijaz";
            default:                return {};
        }
    }

    juce::String describe() const
    {
        return juce::String (noteNames[root % 12]) + " " + scaleNames[juce::jlimit (0, numScales - 1, scale)];
    }

    // A key button's label: "A min" and "Bb maj" for the pentatonics, "A minor", "D dorian" for the rest.
    static juce::String shortName (int root, int scale)
    {
        static const char* const shortScales[numScales] = { "min", "maj", "minor", "major", "dorian", "hijaz", "chrom" };
        return juce::String (noteNames[juce::jlimit (0, 11, root)]) + " " + shortScales[juce::jlimit (0, numScales - 1, scale)];
    }

    // Every note: the chromatic layout from C (rows a fourth apart), for instruments in no key in particular.
    static PadLayout anyKey (int octave)
    {
        PadLayout l;
        l.root = 0;
        l.scale = numScales - 1;
        l.octave = octave;
        return l;
    }
};

// A hosted instrument plugin: its MIDI comes in from controllers (the Fire's
// pads translated through a PadLayout, other keyboards passed straight
// through), and its audio runs through the same channel chain as an input.
//
// Thread model: setPlugin/takePlugin and the setters are called from the
// message thread, handleMidi() from MIDI threads, prepare()/process() from
// the audio thread (prepare while the device is stopped).
class InstrumentChannel
{
public:
    InstrumentChannel();

    void prepare (double sampleRate, int blockSize);
    void release();

    // Its Space send goes to 'sendBus' (its live group's, from sample 0) when given, else straight to the shared reverb.
    // 'silencedAfter': nothing after it can be heard (its group is muted and faded out). Then, as when the
    // instrument itself is muted and faded out, the plug-in rests to save CPU - unless it's armed to record.
    void process (juce::AudioBuffer<float>& out, int startSample, int numSamples, juce::AudioBuffer<float>& recordBus,
                  juce::AudioBuffer<float>* sendBus = nullptr, bool silencedAfter = false);

    // True while the plug-in is resting (muted, not armed).
    bool isResting() const   { return resting.load(); }

    // Installs a plugin (prepared here for the current device), returning the old one.
    std::unique_ptr<juce::AudioPluginInstance> setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin);
    std::unique_ptr<juce::AudioPluginInstance> takePlugin()   { return setPlugin (nullptr); }
    juce::AudioPluginInstance* getPlugin() const              { return plugin.get(); }

    void setGain (float linearGain)   { targetGain = linearGain; }
    void setMuted (bool shouldMute)   { muted = shouldMute; }
    void setArmed (bool shouldArm)    { armed = shouldArm; }
    void setRecordWithFx (bool wet)   { recordWithFx = wet; }
    // Which MIDI device plays it. A device is known by its USB path ('id'), which stays the same
    // when Windows renames it - a replugged Fire can come back as "3 - FL STUDIO FIRE".
    struct MidiSource
    {
        juce::String name, id;          // both empty: every keyboard and Fire
        bool connected = false;         // that exact device is plugged in (same USB port)
        juce::StringArray takenIds;     // devices other instruments are set to
    };
    void setMidiSource (const MidiSource& source);

    // "3 - FL STUDIO FIRE" -> "FL STUDIO FIRE": the name without Windows' number.
    static juce::String baseName (const juce::String& deviceName)
    {
        const auto t = deviceName.trim();
        const auto number = t.upToFirstOccurrenceOf (" - ", false, false);
        return number.isNotEmpty() && number.containsOnly ("0123456789") && t.contains (" - ")
                   ? t.fromFirstOccurrenceOf (" - ", false, false).trim() : t;
    }

    // True if notes from this device play an instrument set to 'source'. Its own device when
    // that's plugged in; otherwise (moved to another USB port, or a set from another computer)
    // the same kind of device by name - one no other instrument is set to.
    static bool playsFrom (const MidiSource& source, const juce::String& deviceName, const juce::String& deviceId)
    {
        if (source.name.isEmpty() && source.id.isEmpty())
            return true;
        if (source.id.isNotEmpty() && source.connected)
            return deviceId == source.id;
        if (source.takenIds.contains (deviceId))
            return false;
        return baseName (deviceName).equalsIgnoreCase (baseName (source.name));
    }
    void setLayout (const PadLayout& newLayout);
    FxChain& getFx()                  { return fx; }

    // Called for every incoming MIDI message, from the MIDI thread.
    void handleMidi (const juce::String& deviceName, const juce::String& deviceId, const juce::MidiMessage& m);

    std::pair<float, float> takePeaks()  { return { peakL.exchange (0.0f), peakR.exchange (0.0f) }; }

    static constexpr int firstFirePadNote = 54;

private:
    // Gives plugins the set's tempo, so their own synced effects follow it.
    struct TempoPlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            // Plug-ins count quarter notes, so an eighth-note BPM (6/8) is halved for them.
            info.setBpm (FxChain::tempoBpm.load() * 4.0 / FxChain::beatUnit.load());
            info.setTimeSignature (TimeSignature { FxChain::beatsPerBar.load(), FxChain::beatUnit.load() });
            info.setIsPlaying (false);
            return info;
        }
    };

    static inline TempoPlayHead playHead;

    std::unique_ptr<juce::AudioPluginInstance> plugin;   // swapped under pluginLock
    juce::SpinLock pluginLock;
    juce::AudioBuffer<float> pluginBuffer, stereo;       // audio thread (swapped under pluginLock)
    juce::MidiMessageCollector collector;
    juce::MidiBuffer midi;
    double sampleRate = 48000.0;
    int blockSize = 512;

    juce::SpinLock configLock;                           // guards the two below for the MIDI thread
    MidiSource midiSource;
    PadLayout layout;
    std::array<int, 64> padNotes;                        // the note each held pad started (Fire's MIDI thread)

    FxChain fx;
    juce::SmoothedValue<float> gain;
    std::atomic<float> targetGain { 1.0f };
    std::atomic<bool> muted { false }, armed { false }, recordWithFx { true };
    std::atomic<bool> resting { false };   // audio thread writes: the plug-in isn't being run
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentChannel)
};
