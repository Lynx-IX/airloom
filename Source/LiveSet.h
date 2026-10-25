#pragma once

#include <JuceHeader.h>
#include "Effects.h"
#include "LoopPlayer.h"

// Unique for the life of the app, so a channel can be found again after
// channels around it are moved or removed.
inline int newSlotId()
{
    static std::atomic<int> next { 1 };
    return next++;
}

// A performance set: rows of channels (one sample each), plus the pool of every
// sample loaded. Files can live anywhere; saving a set gathers them into its folder.
struct LiveSet
{
    // One channel: a sample, how it plays, and its own volume.
    struct Slot
    {
        juce::File file;
        juce::String label;
        bool oneShot = false;
        double volumeDb = 0.0;
        double smoothSeconds = 0.0;    // > 0: blend the end into the start so a non-loop repeats seamlessly
        bool reverse = false;          // play the sample backwards
        FxSettings fx;                 // this channel's own effects, before the row's

        // Runtime state, not saved.
        int id = newSlotId();
        bool playing = false;          // loop is on (or fading in)
        bool loading = false;          // loop is being read before it fades in
        int request = 0;               // discards loads superseded by a newer click
        juce::uint32 flashUntil = 0;   // a one-shot is sounding until this time
        juce::int64 anchor = -1;       // clock time the playing loop is phase-locked to
        std::shared_ptr<LoopClip> liveClip;   // editable audio once overdubbed
        std::shared_ptr<LoopClip> undoClip;   // the audio before the last overdub
    };

    struct Row
    {
        juce::String name;
        int colour = 0;                // index into Theme::rowColours
        bool newSlotsOneShot = false;  // what channels added to this row default to
        double faderDb = 0.0;
        bool muted = false;
        FxSettings fx;
        std::vector<Slot> slots;

        int engine = -1;               // runtime: which audio engine plays this row
    };

    // A live input from the audio interface.
    struct Input
    {
        juce::String name;
        int source = 0;                // InputChannel::Source: input 1, input 2, or 1+2 stereo
        double faderDb = 0.0;
        bool muted = false;
        bool monitor = false;          // off until switched on, so a mic can't feed back by surprise
        bool recordWithFx = true;      // false: loops are recorded dry, so their effects can change later
        FxSettings fx;

        // An effect plug-in that runs first (e.g. pitch correction).
        juce::String insertXml;        // which plug-in
        juce::String insertState;      // its settings, base64
        juce::String insertName;

        bool armed = false;            // runtime: feeds the looper; never restored, for safety
    };

    enum class LoopLength { free, snapToBars, matchFirstLoop };

    // A hosted instrument plugin and how it's played.
    struct Instrument
    {
        juce::String pluginXml;        // the plugin's description (which plugin to load)
        juce::String state;            // the plugin's own settings, base64
        juce::String name;             // shown while the plugin is missing or loading
        double faderDb = 0.0;
        bool muted = false;
        bool recordWithFx = true;
        FxSettings fx;
        juce::String midiSource;       // part of a MIDI device name; empty = every keyboard and the Fire
        int padRoot = 9, padScale = 0, padOctave = 3;

        bool armed = false;            // runtime, like the inputs
    };

    static constexpr int maxInstruments = 4;

    static constexpr int numInputs = 2;
    static std::array<Input, numInputs> defaultInputs();

    std::vector<Row> rows;
    std::array<Input, numInputs> inputs = defaultInputs();
    std::vector<Instrument> instruments;
    juce::Array<juce::File> pool;      // every sample loaded into the set, used or not
    juce::File file;                   // the .liverig file; empty until saved
    double masterDb = 0.0;
    double fadeSeconds = 8.0;
    double bpm = 72.0;                 // for synced delays and snapping recorded loops to bars
    LoopLength loopLength = LoopLength::free;

    static constexpr int maxRows = 8;  // one per MidiMix fader
    static constexpr const char* fileExtension = ".liverig";
    static constexpr const char* packExtension = ".liverigpack";   // a set and its samples in one file (a zip)

    static LiveSet makeDefault();
    static Row makeRow (const juce::String& name, bool oneShot, int numSlots = 4);

    // Finds a channel by id; both pointers are null if it no longer exists.
    std::pair<Row*, Slot*> findSlot (int id);

    juce::String getName() const    { return file == juce::File() ? "Untitled set" : file.getFileNameWithoutExtension(); }
    juce::File getFolder() const    { return file == juce::File() ? juce::File() : file.getParentDirectory(); }

    juce::Array<juce::File> allFiles() const;

    // Adds files to the pool (keeping it sorted by name) and returns how many were new.
    int addToPool (const juce::Array<juce::File>& files);

    // Names of the rows a file is used in; empty if it's only in the pool.
    juce::String describeUsage (const juce::File& f) const;

    // Points every reference to an old path (full path name) at its new file.
    void relocate (const std::map<juce::String, juce::File>& moved);

    std::unique_ptr<juce::XmlElement> toXml (const juce::File& baseFolder) const;
    void loadFromXml (const juce::XmlElement& xml, const juce::File& baseFolder);

    // Paths inside the set folder are stored relative to it so the folder can be moved.
    static juce::String encode (const juce::File& f, const juce::File& baseFolder);
    static juce::File decode (const juce::String& path, const juce::File& baseFolder);
};
