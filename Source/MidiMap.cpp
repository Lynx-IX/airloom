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

    // Akai MidiMix factory settings for the master fader.
    bind ("fader.master", { midiMixName, false, 62, 0 });

    // The first two REC ARM buttons arm the two inputs.
    bind ("arm.0", { midiMixName, true, 3, 0 });
    bind ("arm.1", { midiMixName, true, 6, 0 });

    // The Fire's REC button records a loop.
    bind ("rec", { fireName, true, fireRecNote, 0 });
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

void MidiMap::removeIf (const std::function<bool (const juce::String&)>& shouldRemove)
{
    for (auto it = bindings.begin(); it != bindings.end();)
        it = shouldRemove (it->first) ? bindings.erase (it) : std::next (it);
}

std::unique_ptr<juce::XmlElement> MidiMap::toXml() const
{
    return toXml ([] (const juce::String&) { return true; });
}

std::unique_ptr<juce::XmlElement> MidiMap::toXml (const std::function<bool (const juce::String&)>& include) const
{
    auto xml = std::make_unique<juce::XmlElement> ("MidiMap");
    for (auto& [target, b] : bindings)
    {
        if (! include (target))
            continue;

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
    addFromXml (xml);
}

void MidiMap::addFromXml (const juce::XmlElement& xml)
{
    for (auto* e : xml.getChildWithTagNameIterator ("Bind"))
        bindings[e->getStringAttribute ("target")] = { e->getStringAttribute ("device"),
                                                       e->getBoolAttribute ("note"),
                                                       e->getIntAttribute ("number", -1),
                                                       e->getIntAttribute ("channel") };
}
