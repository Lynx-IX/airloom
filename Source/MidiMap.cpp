#include "MidiMap.h"

bool MidiBinding::matches (const juce::String& deviceName, const juce::MidiMessage& m) const
{
    if (! isValid())
        return false;

    if (device.isNotEmpty()
        && ! deviceName.removeCharacters (" ").containsIgnoreCase (device.removeCharacters (" ")))
        return false;

    if (channel != 0 && m.getChannel() != channel)
        return false;

    if (isNote)
        return (m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() == number;

    return m.isController() && m.getControllerNumber() == number;
}

juce::String MidiBinding::describe() const
{
    if (! isValid())
        return "not assigned";

    return (isNote ? "Note " : "CC ") + juce::String (number)
         + (device.isNotEmpty() ? " on " + device : juce::String());
}

//==============================================================================
void MidiMap::setDefaults()
{
    bindings.clear();

    // Akai MidiMix factory settings.
    const int faderCCs[] = { 19, 23, 27, 31, 49, 53, 57, 61 };
    const int muteNotes[] = { 1, 4, 7, 10, 13, 16, 19, 22 };

    for (int i = 0; i < 8; ++i)
    {
        bind ("fader." + juce::String (i), { midiMixName, false, faderCCs[i], 0 });
        bind ("mute." + juce::String (i), { midiMixName, true, muteNotes[i], 0 });
    }

    bind ("fader.master", { midiMixName, false, 62, 0 });
    bind ("keyPrev",      { midiMixName, true, 25, 0 });   // BANK LEFT
    bind ("keyNext",      { midiMixName, true, 26, 0 });   // BANK RIGHT
    bind ("go",           { midiMixName, true, 27, 0 });   // SOLO
}

void MidiMap::bind (const juce::String& target, MidiBinding binding)
{
    // One hardware control drives one target, so drop any other use of it.
    for (auto it = bindings.begin(); it != bindings.end();)
    {
        const auto& b = it->second;
        if (b.isNote == binding.isNote && b.number == binding.number
            && b.channel == binding.channel && b.device == binding.device)
            it = bindings.erase (it);
        else
            ++it;
    }

    bindings[target] = std::move (binding);
}

MidiBinding MidiMap::get (const juce::String& target) const
{
    auto it = bindings.find (target);
    return it != bindings.end() ? it->second : MidiBinding();
}

juce::StringArray MidiMap::findTargets (const juce::String& deviceName, const juce::MidiMessage& m) const
{
    juce::StringArray targets;
    for (auto& [target, binding] : bindings)
        if (binding.matches (deviceName, m))
            targets.add (target);
    return targets;
}

std::unique_ptr<juce::XmlElement> MidiMap::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("MidiMap");
    for (auto& [target, b] : bindings)
    {
        auto* e = xml->createNewChildElement ("Bind");
        e->setAttribute ("target", target);
        e->setAttribute ("device", b.device);
        e->setAttribute ("note", b.isNote);
        e->setAttribute ("number", b.number);
        e->setAttribute ("channel", b.channel);
    }
    return xml;
}

void MidiMap::fromXml (const juce::XmlElement& xml)
{
    bindings.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("Bind"))
        bindings[e->getStringAttribute ("target")] = { e->getStringAttribute ("device"),
                                                       e->getBoolAttribute ("note"),
                                                       e->getIntAttribute ("number", -1),
                                                       e->getIntAttribute ("channel") };
}
