#pragma once

#include <JuceHeader.h>

// The loop library on disk:
//
//   <Library>/Keys/<key name>/   pads for that key; the first two audio files
//                                (sorted by name) go to Pad 1 and Pad 2
//   <Library>/Nature/            nature soundscapes
//   <Library>/Tones/             isochronic tones
struct LoopLibrary
{
    struct Key
    {
        juce::String name;
        juce::Array<juce::File> files;
    };

    juce::File root;
    std::vector<Key> keys;
    juce::Array<juce::File> nature, tones;

    void scan (const juce::File& rootFolder, const juce::String& audioWildcard);
    int indexOfKey (const juce::String& name) const;

    // Creates the Keys / Nature / Tones subfolders if they're missing.
    static void createFolders (const juce::File& rootFolder);
};
