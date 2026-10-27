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

private:
    std::map<juce::String, MidiBinding> bindings;
};
