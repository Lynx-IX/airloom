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

// Saved with the set, so MIDI assignments still find their channel or group
// after it is moved, renamed or the set is reopened on another computer.
inline juce::String newUid()
{
    return juce::Uuid().toString().substring (0, 12);
}

// A performance set: rows of channels (one sample each), plus the pool of every
// sample loaded. Files can live anywhere; saving a set gathers them into its folder.
struct LiveSet
{
    // An effect plug-in placed on a group or the master: which one, and its settings.
    struct PluginSlot
    {
        juce::String xml;              // the plug-in's description
        juce::String state;            // its settings, base64
        juce::String name;             // shown while it's missing or loading

        bool isEmpty() const    { return xml.isEmpty(); }
        void addToXml (juce::XmlElement& parent) const;
        static PluginSlot fromXml (const juce::XmlElement* parent);
    };

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
        juce::String uid = newUid();   // what MIDI assignments refer to

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
        int columns = 0;               // grid width in channels; 0 = automatic (one line)
        juce::Point<int> position { -1, -1 };   // where the group was placed; -1 = not placed yet
        juce::String uid = newUid();   // what MIDI assignments refer to
        FxSettings fx;
        PluginSlot insert;             // an effect plug-in on the whole group, after its effects
        std::vector<Slot> slots;

        int engine = -1;               // runtime: which audio engine plays this row
    };

    // A live input from the audio interface.
    struct Input
    {
        juce::String name;
        int channel = 0;               // the interface input it takes (0 = input 1)
        bool stereo = false;           // true: that input and the next, as a stereo pair
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
        juce::String midiSource;       // the MIDI device that plays it; empty = every keyboard and Fire
        int padRoot = 9, padScale = 0, padOctave = 3;

        bool armed = false;            // runtime, like the inputs
    };

    static constexpr int maxInstruments = 4;

    // A saved mix: which loops play and every channel and group level. Recalling one morphs
    // there over 'morphSeconds'. Channels and groups are found by uid; ones added since are
    // left alone, and ones removed since are skipped.
    struct Scene
    {
        juce::String name;
        juce::String uid = newUid();   // what MIDI assignments refer to
        double morphSeconds = 8.0;

        struct Channel { juce::String uid; bool playing = false; double volumeDb = 0.0; };
        struct Group   { juce::String uid; double faderDb = 0.0; bool muted = false; };
        struct Key     { int instrument = 0, root = 9, scale = 0, octave = 3; };   // an instrument's Fire key, by index
        struct Strip   { enum Kind { input, instrument } kind = input; int index = 0; double faderDb = 0.0; bool muted = false; };
        std::vector<Channel> channels;
        std::vector<Group> groups;
        std::vector<Key> keys;
        std::vector<Strip> strips;     // inputs' and instruments' faders and mutes, by index
    };

    static constexpr int maxInputs = 8;          // input strips in a set
    static constexpr int maxDeviceInputs = 16;   // interface inputs opened
    static std::vector<Input> defaultInputs();

    std::vector<Row> rows;
    std::vector<Input> inputs = defaultInputs();
    std::vector<Instrument> instruments;
    std::vector<Scene> scenes;
    bool scenesKeepChanges = true;     // leaving a scene saves the mix you made in it
    juce::Array<juce::File> pool;      // every sample loaded into the set, used or not
    juce::File file;                   // the .liverig file; empty until saved
    double masterDb = 0.0;
    FxSettings space = SpaceReverb::defaults();   // the shared reverb channels send to
    PluginSlot masterInsert;                       // an effect plug-in on the master
    double fadeSeconds = 8.0;
    enum class LoopStart { now, beat, bar };
    LoopStart loopStart = LoopStart::now;   // when a pressed loop comes in
    double bpm = 72.0;                 // for synced delays and snapping recorded loops to bars

    // Beats in a bar, and the note that's one beat (BPM counts these: in 6/8 it counts eighths).
    int beatsPerBar = 4, beatUnit = 4;
    struct TimeSignature { int beats, unit; };
    static constexpr TimeSignature timeSignatures[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 } };
    LoopLength loopLength = LoopLength::free;
    juce::Point<int> inputsPosition { -1, -1 };   // where the Inputs + Looper group was placed
    std::shared_ptr<juce::XmlElement> midi;       // MIDI assigned to this set's channels and groups
    int inputsColumns = 0;             // its grid width in strips; 0 = automatic

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
