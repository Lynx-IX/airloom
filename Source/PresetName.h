#pragma once

#include <JuceHeader.h>

// Finds the name of the preset loaded in an instrument, from the settings it saves.
// Most synths don't tell the host their preset (Vital, Zyklop and Serum don't), but many
// write its name into their saved state:
//   - JSON, as Vital does:                    "preset_name":"Flume Pad"
//   - JUCE ValueTree binary, as Zyklop does:  PATCH_NAME \0 <size> 05 "SPACE OF HERE" \0
//   - XML attributes:                         presetName="Glass Pad"
// A hosted VST3's state arrives wrapped (VST3PluginState XML, the plug-in's data in base64);
// that's unwrapped first. Empty if no name is found.
namespace PresetName
{
    namespace Detail
    {
        inline bool isNameKey (const juce::String& key)
        {
            const auto k = key.toLowerCase().removeCharacters ("_- ");
            return k == "presetname" || k == "patchname" || k == "programname";
        }

        inline juce::String clean (const juce::String& s)
        {
            const auto t = s.trim();
            if (t.isEmpty() || t.length() > 64 || ! t.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.,'&()#+!/:"))
                return {};
            return t;
        }

        // "key":"value" or key="value" (JSON or XML), for any of the name keys.
        // Searches the raw bytes: states often hold binary (zeros included) around the text.
        inline juce::String fromText (const char* data, size_t size)
        {
            for (const char* key : { "preset_name", "presetName", "PresetName", "patch_name", "patchName", "PATCH_NAME", "PRESET_NAME" })
            {
                for (const auto& pattern : { juce::String ("\"") + key + "\":\"", juce::String ("\"") + key + "\": \"",
                                             juce::String (key) + "=\"" })
                {
                    const auto* p = pattern.toRawUTF8();
                    const size_t n = strlen (p);
                    for (size_t i = 0; i + n < size; ++i)
                    {
                        if (memcmp (data + i, p, n) != 0)
                            continue;
                        size_t end = i + n;
                        while (end < size && end - (i + n) < 80 && data[end] != '"')
                            ++end;
                        const auto value = clean (juce::String (juce::CharPointer_UTF8 (data + i + n), end - (i + n)));
                        if (value.isNotEmpty())
                            return value;
                    }
                }
            }
            return {};
        }

        // A ValueTree property in binary: name \0, then the value's size as a compressed int
        // (a byte count, then that many little-endian bytes), then type 5 (string) and the text.
        inline juce::String fromValueTree (const juce::uint8* data, size_t size)
        {
            for (const char* key : { "PATCH_NAME", "PRESET_NAME", "presetName", "patchName", "preset_name", "patch_name" })
            {
                const size_t keyLength = strlen (key);
                for (size_t i = 0; i + keyLength + 4 < size; ++i)
                {
                    if (memcmp (data + i, key, keyLength) != 0 || data[i + keyLength] != 0)
                        continue;
                    if (i > 0 && data[i - 1] >= 32 && data[i - 1] < 127)
                        continue;   // part of a longer name

                    size_t p = i + keyLength + 1;
                    const int countBytes = data[p++];
                    if (countBytes < 1 || countBytes > 4 || p + (size_t) countBytes >= size)
                        continue;
                    size_t valueSize = 0;
                    for (int b = 0; b < countBytes; ++b)
                        valueSize |= (size_t) data[p++] << (8 * b);
                    if (valueSize < 2 || p + valueSize > size || data[p] != 5)
                        continue;

                    const auto* text = reinterpret_cast<const char*> (data + p + 1);
                    const auto value = clean (juce::String (juce::CharPointer_UTF8 (text), valueSize - 2));
                    if (value.isNotEmpty())
                        return value;
                }
            }
            return {};
        }

        inline juce::String fromBytes (const juce::MemoryBlock& block)
        {
            const auto* data = static_cast<const juce::uint8*> (block.getData());
            const auto found = fromValueTree (data, block.getSize());
            return found.isNotEmpty() ? found : fromText (static_cast<const char*> (block.getData()), block.getSize());
        }
    }

    inline juce::String fromState (const juce::MemoryBlock& state)
    {
        if (auto xml = juce::AudioPluginInstance::getXmlFromBinary (state.getData(), (int) state.getSize()))
        {
            if (xml->hasTagName ("VST3PluginState"))
            {
                for (auto* part : xml->getChildIterator())
                {
                    juce::MemoryBlock inner;
                    if (inner.fromBase64Encoding (part->getAllSubText().trim()))
                        if (const auto name = Detail::fromBytes (inner); name.isNotEmpty())
                            return name;
                }
                return {};
            }

            // A plug-in that saves XML: a name attribute anywhere in it.
            juce::String found;
            std::function<void (const juce::XmlElement&)> search = [&] (const juce::XmlElement& e)
            {
                for (int a = 0; a < e.getNumAttributes() && found.isEmpty(); ++a)
                    if (Detail::isNameKey (e.getAttributeName (a)))
                        found = Detail::clean (e.getAttributeValue (a));
                for (auto* child : e.getChildIterator())
                    if (found.isEmpty())
                        search (*child);
            };
            search (*xml);
            return found;
        }

        return Detail::fromBytes (state);
    }

    // The preset of a plug-in that's running: from its state, or else its current program,
    // if that's a real name rather than a numbered slot ("Prog 3", "ProgramChange 12").
    inline juce::String of (juce::AudioPluginInstance& plugin)
    {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        if (auto name = fromState (state); name.isNotEmpty())
            return name;

        if (plugin.getNumPrograms() > 1)
        {
            const auto program = Detail::clean (plugin.getProgramName (plugin.getCurrentProgram()));
            const auto generic = program.trimCharactersAtEnd ("0123456789 ").toLowerCase();
            if (program.isNotEmpty() && generic != "prog" && generic != "program" && generic != "programchange"
                && generic != "default" && generic != "preset")
                return program;
        }
        return {};
    }
}
