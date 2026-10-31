#pragma once

#include <JuceHeader.h>

// Assignment of one hardware control (a CC or a note) to an app control.
struct MidiBinding
{
    juce::String device;    // substring of the MIDI device name; empty = any device
    bool isNote = false;
    int number = -1;        // CC or note number
    int channel = 0;        // 1-16, or 0 = any

    bool isValid() const    { return number >= 0; }
    bool matches (const juce::String& deviceName, const juce::MidiMessage& m) const;
    juce::String describe() const;
};

// Maps MIDI controls to named targets such as "fader.master", "stopAll" or
// "ch.<uid>.play". Nothing is assigned to groups or channels automatically:
// right-click a control to learn one. Only the MidiMix master fader, the
// first two REC ARM buttons and the Fire's REC button start assigned.
//
// Targets for a set's channels and groups ("ch." and "grp.") are saved with
// the set, as are effect knobs on them and on Space ("fx.ch.<uid>.reverbMix",
// "fx.grp.<uid>...", "fx.space..."); the rest (master, inputs and their effects,
// instruments, looper, tempo) belong to the computer.
class MidiMap
{
public:
    MidiMap() { setDefaults(); }

    void setDefaults();

    static bool isSetTarget (const juce::String& target)
    {
        return target.startsWith ("ch.") || target.startsWith ("grp.")
            || target.startsWith ("fx.ch.") || target.startsWith ("fx.grp.") || target.startsWith ("fx.space.")
            || (target.startsWith ("scene.") && target != "scene.next" && target != "scene.prev");
    }

    void removeIf (const std::function<bool (const juce::String&)>& shouldRemove);
    std::unique_ptr<juce::XmlElement> toXml (const std::function<bool (const juce::String&)>& include) const;
    void addFromXml (const juce::XmlElement& xml);

    void bind (const juce::String& target, MidiBinding binding);
    void clear (const juce::String& target)    { bindings.erase (target); }
    MidiBinding get (const juce::String& target) const;
    juce::StringArray getTargets() const
    {
        juce::StringArray targets;
        for (auto& [target, binding] : bindings)
            targets.add (target);
        return targets;
    }

    juce::StringArray findTargets (const juce::String& deviceName, const juce::MidiMessage& m) const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);

    static constexpr const char* midiMixName = "MIDI Mix";
    static constexpr const char* fireName = "FIRE";
    static constexpr int fireRecNote = 53;   // the Akai Fire's REC button (its light is CC 53)
    static constexpr int firePlayNote = 51;  // its PLAY button: Airloom's beat light (CC 51)
    // With SHIFT held, these run the pads' light show: ALT on/off, GRID < > pattern, PATTERN up/down palette.
    static constexpr int fireShiftNote = 48, fireAltNote = 49, fireGridLeftNote = 34, fireGridRightNote = 35,
                         firePatternUpNote = 31, firePatternDownNote = 32,
                         fireBrowserNote = 33;   // SHIFT + BROWSER: where this Fire sits beside the other (joined light show)

    // The Fire's top-left knob (VOLUME). Its knobs turn endlessly and send steps, not positions:
    // 1..63 clockwise, 127..65 anticlockwise (bigger numbers for a faster turn).
    static constexpr int fireVolumeKnobCC = 16;
    static int relativeSteps (int ccValue)   { return ccValue < 64 ? ccValue : ccValue - 128; }

    // A fader moved by knob steps: half a dB a step, bigger steps down where the fader is quiet
    // (so it doesn't take forever to come up from silence). -60 is off.
    static double nudgeDb (double db, int steps)
    {
        for (int i = 0; i < std::abs (steps); ++i)
            db += (steps > 0 ? 1.0 : -1.0) * (db < -30.0 ? 2.0 : 0.5);
        return juce::jlimit (-60.0, 6.0, db);
    }

    // Moves several levels by the same dB, set by the loudest, so their balance holds. Levels
    // may sit below off (-60): one held at off comes back at its old distance on the way up.
    // The loudest stops at +6 and at off. Show each as jlimit (-60, 6, level).
    static void nudgeTogether (std::vector<double>& levels, int steps)
    {
        if (levels.empty())
            return;
        const double loudest = juce::jmax (-60.0, *std::max_element (levels.begin(), levels.end()));
        const double change = nudgeDb (loudest, steps) - loudest;
        for (auto& l : levels)
            l += change;
    }

private:
    std::map<juce::String, MidiBinding> bindings;
};
