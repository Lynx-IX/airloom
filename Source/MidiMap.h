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

// Maps MIDI controls to named targets such as "fader.0", "mute.2", "go".
// Defaults match the Akai MidiMix factory layout; anything can be re-learned.
class MidiMap
{
public:
    MidiMap() { setDefaults(); }

    void setDefaults();

    void bind (const juce::String& target, MidiBinding binding);
    void clear (const juce::String& target)    { bindings.erase (target); }
    MidiBinding get (const juce::String& target) const;

    juce::StringArray findTargets (const juce::String& deviceName, const juce::MidiMessage& m) const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);

    static constexpr const char* midiMixName = "MIDI Mix";

private:
    std::map<juce::String, MidiBinding> bindings;
};
