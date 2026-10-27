#pragma once

#include <JuceHeader.h>
#include "PresetName.h"

// Development check: loads every VST3 instrument installed and reports what it tells a
// host about its presets (programs) - how many, which is current, and their names. Many
// synths keep presets to themselves; this shows which ones Airloom could name.
namespace PresetProbe
{
    inline juce::String run()
    {
        juce::StringArray report;
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            report.add ("MIDI IN  \"" + d.name + "\"  id: " + d.identifier);
        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);

        for (auto* format : formats.getFormats())
        {
            if (format->getName() != "VST3")
                continue;

            for (const auto& path : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
            {
                juce::OwnedArray<juce::PluginDescription> found;
                format->findAllTypesForFile (found, path);

                for (auto* d : found)
                {
                    if (! d->isInstrument)
                        continue;

                    juce::String error;
                    auto plugin = formats.createPluginInstance (*d, 48000.0, 512, error);
                    if (plugin == nullptr)
                    {
                        report.add (d->name + ": couldn't load (" + error + ")");
                        continue;
                    }

                    plugin->prepareToPlay (48000.0, 512);
                    const int n = plugin->getNumPrograms();
                    const int current = plugin->getCurrentProgram();
                    juce::StringArray names;
                    for (int i = 0; i < juce::jmin (n, 6); ++i)
                        names.add ("\"" + plugin->getProgramName (i) + "\"");

                    juce::String afterChange;
                    if (n > 1)
                    {
                        plugin->setCurrentProgram (1);
                        afterChange = "  | after choosing #1: current " + juce::String (plugin->getCurrentProgram())
                                    + " \"" + plugin->getProgramName (plugin->getCurrentProgram()) + "\"";
                    }

                    report.add (d->name + " (" + d->manufacturerName + "): " + juce::String (n) + " programs, current "
                                + juce::String (current) + " \"" + plugin->getProgramName (current) + "\"  first: "
                                + names.joinIntoString (", ") + afterChange);
                    plugin->releaseResources();
                }
            }
        }
        // The instruments in the current session: readable text inside their saved state.
        const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                      .getChildFile ("LiveRig").getChildFile ("LiveRig.settings");
        if (auto settings = juce::parseXML (settingsFile))
            for (auto* value : settings->getChildIterator())
                if (value->getStringAttribute ("name") == "session")
                {
                    std::unique_ptr<juce::XmlElement> session;
                    if (auto* inner = value->getFirstChildElement())
                        session = std::make_unique<juce::XmlElement> (*inner);
                    else
                        session = juce::parseXML (value->getStringAttribute ("val"));
                    if (session == nullptr)
                        continue;

                    for (auto* inst : session->getChildWithTagNameIterator ("Instrument"))
                    {
                        juce::MemoryBlock state;
                        state.fromBase64Encoding (inst->getStringAttribute ("state"));

                        // A hosted VST3's state is XML (VST3PluginState) holding the plug-in's own data, base64 again.
                        if (auto xml = juce::AudioPluginInstance::getXmlFromBinary (state.getData(), (int) state.getSize()))
                        {
                            juce::MemoryBlock inner;
                            for (auto* part : xml->getChildIterator())
                            {
                                juce::MemoryBlock piece;
                                piece.fromBase64Encoding (part->getAllSubText().trim());
                                inner.append (piece.getData(), piece.getSize());
                                report.add ("  part " + part->getTagName() + ": " + juce::String ((int) piece.getSize()) + " bytes");
                            }
                            if (inner.getSize() > 0)
                                state = inner;
                        }
                        juce::StringArray words;
                        juce::String run;
                        auto* bytes = static_cast<const char*> (state.getData());
                        for (size_t i = 0; i <= state.getSize(); ++i)
                        {
                            const char c = i < state.getSize() ? bytes[i] : 0;
                            if (c >= 32 && c < 127) { run += juce::String::charToString (c); continue; }
                            if (run.length() >= 5) words.add (run);
                            run.clear();
                        }
                        for (const char* key : { "PATCH_NAME", "PATCH_FILENAME" })
                        {
                            const auto text = juce::String::fromUTF8 (bytes, 0);   // unused; keep the raw search below
                            juce::ignoreUnused (text);
                            const auto* data = static_cast<const juce::uint8*> (state.getData());
                            const size_t keyLength = strlen (key);
                            for (size_t i = 0; i + keyLength + 60 < state.getSize(); ++i)
                                if (memcmp (data + i, key, keyLength) == 0)
                                {
                                    juce::String hex, chars;
                                    for (size_t j = i + keyLength; j < i + keyLength + 60; ++j)
                                    {
                                        hex << juce::String::toHexString ((int) data[j]).paddedLeft ('0', 2) << " ";
                                        chars << (data[j] >= 32 && data[j] < 127 ? juce::String::charToString ((char) data[j]) : juce::String ("."));
                                    }
                                    report.add ("  " + juce::String (key) + " -> " + hex + " | " + chars);
                                    break;
                                }
                        }

                        juce::StringArray interesting;
                        for (auto& w : words)
                            if (w.containsIgnoreCase ("preset") || w.containsIgnoreCase ("name") || w.containsIgnoreCase ("patch")
                                || w.containsIgnoreCase (".vital") || w.containsIgnoreCase ("program"))
                                interesting.add (w.substring (0, 400));
                        juce::MemoryBlock saved;
                        saved.fromBase64Encoding (inst->getStringAttribute ("state"));
                        report.add ({});
                        report.add ("PRESET " + inst->getStringAttribute ("name") + ": \"" + PresetName::fromState (saved) + "\"");
                        report.add ("SESSION " + inst->getStringAttribute ("name") + " (" + juce::String ((int) state.getSize())
                                    + " bytes of state): " + interesting.joinIntoString ("  ||  ").substring (0, 3000));
                    }
                }

        return report.joinIntoString ("\n");
    }
}
