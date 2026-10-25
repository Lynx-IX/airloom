#include "LiveSet.h"

namespace
{
    const char* const binTags[] = { "Nature", "Tones" };
}

juce::Array<juce::File> LiveSet::allFiles() const
{
    juce::Array<juce::File> files;

    for (auto& k : keys)
        for (auto& f : k.pads)
            if (f != juce::File())
                files.addIfNotAlreadyThere (f);

    for (auto& bin : bins)
        for (auto& f : bin)
            files.addIfNotAlreadyThere (f);

    return files;
}

void LiveSet::relocate (const std::map<juce::String, juce::File>& moved)
{
    auto update = [&] (juce::File& f)
    {
        if (auto it = moved.find (f.getFullPathName()); it != moved.end())
            f = it->second;
    };

    for (auto& k : keys)
        for (auto& f : k.pads)
            update (f);

    for (auto& bin : bins)
        for (auto& f : bin)
            update (f);
}

juce::String LiveSet::encode (const juce::File& f, const juce::File& baseFolder)
{
    if (f == juce::File())
        return {};

    if (baseFolder.isDirectory() && f.isAChildOf (baseFolder))
        return f.getRelativePathFrom (baseFolder).replaceCharacter ('\\', '/');

    return f.getFullPathName();
}

juce::File LiveSet::decode (const juce::String& path, const juce::File& baseFolder)
{
    if (path.isEmpty())
        return {};

    if (juce::File::isAbsolutePath (path) || baseFolder == juce::File())
        return juce::File (path);

    return baseFolder.getChildFile (path);
}

void LiveSet::addToXml (juce::XmlElement& root, const juce::File& baseFolder) const
{
    for (auto& k : keys)
    {
        auto* e = root.createNewChildElement ("Key");
        e->setAttribute ("name", k.name);
        e->setAttribute ("pad1", encode (k.pads[0], baseFolder));
        e->setAttribute ("pad2", encode (k.pads[1], baseFolder));
    }

    for (size_t b = 0; b < bins.size(); ++b)
    {
        auto* e = root.createNewChildElement (binTags[b]);
        for (auto& f : bins[b])
            e->createNewChildElement ("File")->setAttribute ("path", encode (f, baseFolder));
    }
}

void LiveSet::loadFromXml (const juce::XmlElement& root, const juce::File& baseFolder)
{
    keys.clear();
    for (auto& bin : bins)
        bin.clear();

    for (auto* e : root.getChildWithTagNameIterator ("Key"))
        keys.push_back ({ e->getStringAttribute ("name"),
                          { decode (e->getStringAttribute ("pad1"), baseFolder),
                            decode (e->getStringAttribute ("pad2"), baseFolder) } });

    for (size_t b = 0; b < bins.size(); ++b)
        if (auto* e = root.getChildByName (binTags[b]))
            for (auto* f : e->getChildWithTagNameIterator ("File"))
                bins[b].add (decode (f->getStringAttribute ("path"), baseFolder));
}
