#include "LoopLibrary.h"

namespace
{
    template <typename Array>
    void sortByName (Array& files)
    {
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    }

    juce::Array<juce::File> audioFilesIn (const juce::File& folder, const juce::String& wildcard)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, wildcard);
        sortByName (files);
        return files;
    }
}

void LoopLibrary::scan (const juce::File& rootFolder, const juce::String& audioWildcard)
{
    root = rootFolder;
    keys.clear();

    auto keyFolders = root.getChildFile ("Keys").findChildFiles (juce::File::findDirectories, false);
    sortByName (keyFolders);

    for (auto& folder : keyFolders)
        if (auto files = audioFilesIn (folder, audioWildcard); ! files.isEmpty())
            keys.push_back ({ folder.getFileName(), files });

    nature = audioFilesIn (root.getChildFile ("Nature"), audioWildcard);
    tones = audioFilesIn (root.getChildFile ("Tones"), audioWildcard);
}

int LoopLibrary::indexOfKey (const juce::String& name) const
{
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i].name == name)
            return (int) i;
    return -1;
}

void LoopLibrary::createFolders (const juce::File& rootFolder)
{
    for (auto* sub : { "Keys", "Nature", "Tones" })
        rootFolder.getChildFile (sub).createDirectory();
}
