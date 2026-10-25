#pragma once

#include <JuceHeader.h>

// Everything loaded for a performance: the keys (each with two pad files) and
// the lists of nature and tone loops. Files can live anywhere; saving a set
// gathers them into the set's own folder.
struct LiveSet
{
    struct Key
    {
        juce::String name;
        std::array<juce::File, 2> pads;   // Pad 1, Pad 2
    };

    std::vector<Key> keys;
    std::array<juce::Array<juce::File>, 2> bins;   // Nature, Tones
    juce::File file;                                // the .liverig file; empty until saved

    static constexpr const char* fileExtension = ".liverig";

    juce::String getName() const    { return file == juce::File() ? "Untitled set" : file.getFileNameWithoutExtension(); }
    juce::File getFolder() const    { return file == juce::File() ? juce::File() : file.getParentDirectory(); }

    juce::Array<juce::File> allFiles() const;

    // Points every reference to an old path (full path name) at its new file.
    void relocate (const std::map<juce::String, juce::File>& moved);

    void addToXml (juce::XmlElement& root, const juce::File& baseFolder) const;
    void loadFromXml (const juce::XmlElement& root, const juce::File& baseFolder);

    // Paths inside the set folder are stored relative to it so the folder can be moved.
    static juce::String encode (const juce::File& f, const juce::File& baseFolder);
    static juce::File decode (const juce::String& path, const juce::File& baseFolder);
};
