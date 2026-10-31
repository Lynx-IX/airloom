#include "LiveSet.h"

void LiveSet::PluginSlot::addToXml (juce::XmlElement& parent) const
{
    if (isEmpty())
        return;

    auto* pe = parent.createNewChildElement ("Insert");
    pe->setAttribute ("name", name);
    pe->setAttribute ("state", state);
    if (auto plugin = juce::parseXML (xml))
        pe->addChildElement (plugin.release());
}

LiveSet::PluginSlot LiveSet::PluginSlot::fromXml (const juce::XmlElement* parent)
{
    PluginSlot slot;
    if (auto* pe = parent != nullptr ? parent->getChildByName ("Insert") : nullptr)
    {
        slot.name = pe->getStringAttribute ("name");
        slot.state = pe->getStringAttribute ("state");
        if (auto* plugin = pe->getChildByName ("PLUGIN"))
            slot.xml = plugin->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    }
    return slot;
}

LiveSet::Row LiveSet::makeRow (const juce::String& name, bool oneShot, int numSlots)
{
    Row r;
    r.name = name;
    r.newSlotsOneShot = oneShot;
    r.slots.resize ((size_t) numSlots);
    for (auto& s : r.slots)
        s.oneShot = oneShot;
    return r;
}

std::vector<LiveSet::Input> LiveSet::defaultInputs()
{
    std::vector<Input> in (2);

    in[0].name = "Mic";
    in[0].channel = 0;
    in[0].fx.lowCutOn = true;     // clear rumble and handling noise
    in[0].fx.reverbOn = true;

    in[1].name = "Instrument";
    in[1].channel = 1;
    return in;
}

LiveSet::LiveGroup* LiveSet::findLiveGroup (const juce::String& uid)
{
    for (auto& g : liveGroups)
        if (g.uid == uid)
            return &g;
    return nullptr;
}

LiveSet::LiveGroup* LiveSet::firstLiveGroup (bool wantInstruments)
{
    for (auto& g : liveGroups)
        if (g.instruments == wantInstruments)
            return &g;
    return nullptr;
}

void LiveSet::ensureLiveGroups()
{
    auto ensure = [this] (bool wantInstruments) -> juce::String
    {
        if (auto* g = firstLiveGroup (wantInstruments))
            return g->uid;

        LiveGroup g;
        g.instruments = wantInstruments;
        g.name = wantInstruments ? "Instruments" : "Inputs";
        g.colour = wantInstruments ? 5 : 4;   // teal, rose
        liveGroups.push_back (g);
        return g.uid;
    };

    const auto inputsUid = ensure (false);
    const auto instrumentsUid = ensure (true);

    for (auto& in : inputs)
        if (findLiveGroup (in.group) == nullptr)
            in.group = inputsUid;
    for (auto& inst : instruments)
        if (findLiveGroup (inst.group) == nullptr)
            inst.group = instrumentsUid;
}

LiveSet LiveSet::makeDefault()
{
    LiveSet set;
    set.ensureLiveGroups();
    set.rows.push_back (makeRow ("Pads", false));
    set.rows.push_back (makeRow ("Ambient", false));
    set.rows.push_back (makeRow ("Tones", false));
    set.rows.push_back (makeRow ("One-shots", true));

    for (size_t i = 0; i < set.rows.size(); ++i)
        set.rows[i].colour = (int) i;   // violet, green, blue, amber

    return set;
}

std::pair<LiveSet::Row*, LiveSet::Slot*> LiveSet::findSlot (int id)
{
    for (auto& r : rows)
        for (auto& s : r.slots)
            if (s.id == id)
                return { &r, &s };
    return { nullptr, nullptr };
}

juce::Array<juce::File> LiveSet::allFiles() const
{
    juce::Array<juce::File> files;

    for (auto& r : rows)
        for (auto& s : r.slots)
            if (s.file != juce::File())
                files.addIfNotAlreadyThere (s.file);

    for (auto& f : pool)
        files.addIfNotAlreadyThere (f);

    return files;
}

int LiveSet::addToPool (const juce::Array<juce::File>& files)
{
    int added = 0;
    for (auto& f : files)
        if (f != juce::File() && ! pool.contains (f))
        {
            pool.add (f);
            ++added;
        }

    std::sort (pool.begin(), pool.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return added;
}

juce::String LiveSet::describeUsage (const juce::File& f) const
{
    juce::StringArray uses;
    for (auto& r : rows)
        for (auto& s : r.slots)
            if (s.file == f)
                uses.addIfNotAlreadyThere (r.name);

    return uses.joinIntoString (", ");
}

void LiveSet::relocate (const std::map<juce::String, juce::File>& moved)
{
    auto update = [&] (juce::File& f)
    {
        if (auto it = moved.find (f.getFullPathName()); it != moved.end())
            f = it->second;
    };

    for (auto& r : rows)
        for (auto& s : r.slots)
            update (s.file);

    for (auto& f : pool)
        update (f);

    // Samples moved to the panel's own folders stay there wherever their files went.
    std::map<juce::String, juce::String> remapped;
    for (const auto& [path, folder] : poolFolders.moved)
    {
        const auto it = moved.find (path);
        remapped[it != moved.end() ? it->second.getFullPathName() : path] = folder;
    }
    poolFolders.moved = std::move (remapped);
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

juce::String LiveSet::editText (const juce::XmlElement& setXml)
{
    juce::XmlElement copy (setXml);
    for (const auto* a : { "bpm", "master", "fade", "loopStart" })
        copy.removeAttribute (a);

    std::function<void (juce::XmlElement&)> strip = [&strip] (juce::XmlElement& e)
    {
        const auto tag = e.getTagName();
        if (tag == "Scene")
            e.deleteAllChildElements();   // a scene's mix changes as it's played (keep changes)
        if (tag == "Row" || tag == "LiveGroup")
            for (const auto* a : { "fader", "mute" }) e.removeAttribute (a);
        if (tag == "Input")
            for (const auto* a : { "fader", "mute", "monitor" }) e.removeAttribute (a);
        if (tag == "Instrument")
            for (const auto* a : { "fader", "mute", "state", "padRoot", "padScale", "padOctave", "anyKey" }) e.removeAttribute (a);
        for (auto* child : e.getChildIterator())
            strip (*child);
    };
    strip (copy);

    // Channel volumes sit on Row's children; scenes' channels were emptied above.
    for (auto* row : copy.getChildWithTagNameIterator ("Row"))
        for (auto* ch : row->getChildWithTagNameIterator ("Channel"))
            ch->removeAttribute ("volume");

    return copy.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

std::unique_ptr<juce::XmlElement> LiveSet::toXml (const juce::File& baseFolder) const
{
    auto xml = std::make_unique<juce::XmlElement> ("LiveRigSet");
    xml->setAttribute ("version", 3);
    xml->setAttribute ("master", masterDb);
    xml->setAttribute ("fade", fadeSeconds);
    xml->setAttribute ("loopStart", (int) loopStart);
    xml->setAttribute ("bpm", bpm);
    xml->setAttribute ("beatsPerBar", beatsPerBar);
    xml->setAttribute ("beatUnit", beatUnit);
    xml->setAttribute ("loopLength", (int) loopLength);
    xml->setAttribute ("layoutWidth", layoutWidth);
    space.addToXml (*xml->createNewChildElement ("Space"));

    auto* scenesXml = xml->createNewChildElement ("Scenes");
    scenesXml->setAttribute ("keepChanges", scenesKeepChanges);
    for (const auto& sc : scenes)
    {
        auto* e = scenesXml->createNewChildElement ("Scene");
        e->setAttribute ("name", sc.name);
        e->setAttribute ("uid", sc.uid);
        e->setAttribute ("morph", sc.morphSeconds);
        for (const auto& c : sc.channels)
        {
            auto* ce = e->createNewChildElement ("Channel");
            ce->setAttribute ("uid", c.uid);
            ce->setAttribute ("playing", c.playing);
            ce->setAttribute ("volume", c.volumeDb);
            if (c.hasFx) c.fx.addToXml (*ce);
        }
        for (const auto& gr : sc.groups)
        {
            auto* ge = e->createNewChildElement ("Group");
            ge->setAttribute ("uid", gr.uid);
            ge->setAttribute ("fader", gr.faderDb);
            ge->setAttribute ("muted", gr.muted);
            if (gr.hasFx) gr.fx.addToXml (*ge);
        }
        for (const auto& st : sc.strips)
        {
            auto* se = e->createNewChildElement ("Strip");
            se->setAttribute ("kind", st.kind == Scene::Strip::instrument ? "instrument" : "input");
            se->setAttribute ("index", st.index);
            se->setAttribute ("fader", st.faderDb);
            se->setAttribute ("muted", st.muted);
            if (st.hasFx) st.fx.addToXml (*se);
        }
        for (const auto& k : sc.keys)
        {
            auto* ke = e->createNewChildElement ("Key");
            ke->setAttribute ("instrument", k.instrument);
            ke->setAttribute ("root", k.root);
            ke->setAttribute ("scale", k.scale);
            ke->setAttribute ("octave", k.octave);
            ke->setAttribute ("anyKey", k.anyKey);
        }
    }
    masterInsert.addToXml (*xml);

    for (auto& r : rows)
    {
        auto* re = xml->createNewChildElement ("Row");
        re->setAttribute ("name", r.name);
        re->setAttribute ("colour", r.colour);
        re->setAttribute ("newSlotsOneShot", r.newSlotsOneShot);
        re->setAttribute ("fader", r.faderDb);
        re->setAttribute ("mute", r.muted);
        re->setAttribute ("columns", r.columns);
        re->setAttribute ("collapsed", r.collapsed);
        re->setAttribute ("uid", r.uid);
        re->setAttribute ("x", r.position.x);
        re->setAttribute ("y", r.position.y);
        r.fx.addToXml (*re);
        r.insert.addToXml (*re);

        for (auto& s : r.slots)
        {
            auto* se = re->createNewChildElement ("Channel");
            se->setAttribute ("path", encode (s.file, baseFolder));
            se->setAttribute ("label", s.label);
            se->setAttribute ("oneShot", s.oneShot);
            se->setAttribute ("volume", s.volumeDb);
            se->setAttribute ("smooth", s.smoothSeconds);
            se->setAttribute ("reverse", s.reverse);
            if (s.shape.isSet())
            {
                se->setAttribute ("trimStart", s.shape.start);
                se->setAttribute ("trimEnd", s.shape.end);
                se->setAttribute ("fadeIn", s.shape.fadeIn);
                se->setAttribute ("fadeOut", s.shape.fadeOut);
                se->setAttribute ("sampleGain", s.shape.gainDb);
            }
            se->setAttribute ("uid", s.uid);
            if (s.fx.isActive())
                s.fx.addToXml (*se);
        }
    }

    if (midi != nullptr)
        xml->addChildElement (new juce::XmlElement (*midi));

    auto* keysXml = xml->createNewChildElement ("KeyButtons");
    for (const auto& k : keyButtons)
    {
        auto* ke = keysXml->createNewChildElement ("KeyButton");
        ke->setAttribute ("root", k.root);
        ke->setAttribute ("scale", k.scale);
    }

    auto* liveXml = xml->createNewChildElement ("LiveGroups");
    for (const auto& g : liveGroups)
    {
        auto* ge = liveXml->createNewChildElement ("LiveGroup");
        ge->setAttribute ("name", g.name);
        ge->setAttribute ("uid", g.uid);
        ge->setAttribute ("instruments", g.instruments);
        ge->setAttribute ("colour", g.colour);
        ge->setAttribute ("fader", g.faderDb);
        ge->setAttribute ("mute", g.muted);
        ge->setAttribute ("columns", g.columns);
        ge->setAttribute ("collapsed", g.collapsed);
        ge->setAttribute ("x", g.position.x);
        ge->setAttribute ("y", g.position.y);
        g.fx.addToXml (*ge);
    }

    for (auto& in : inputs)
    {
        auto* ie = xml->createNewChildElement ("Input");
        ie->setAttribute ("name", in.name);
        ie->setAttribute ("group", in.group);
        ie->setAttribute ("order", in.order);
        ie->setAttribute ("channel", in.channel);
        ie->setAttribute ("stereo", in.stereo);
        ie->setAttribute ("fader", in.faderDb);
        ie->setAttribute ("mute", in.muted);
        ie->setAttribute ("monitor", in.monitor);
        ie->setAttribute ("recordWithFx", in.recordWithFx);
        in.fx.addToXml (*ie);

        if (in.insertXml.isNotEmpty())
        {
            auto* pe = ie->createNewChildElement ("Insert");
            pe->setAttribute ("name", in.insertName);
            pe->setAttribute ("state", in.insertState);
            if (auto plugin = juce::parseXML (in.insertXml))
                pe->addChildElement (plugin.release());
        }
    }

    for (auto& inst : instruments)
    {
        auto* e = xml->createNewChildElement ("Instrument");
        e->setAttribute ("name", inst.name);
        e->setAttribute ("label", inst.label);
        e->setAttribute ("group", inst.group);
        e->setAttribute ("order", inst.order);
        e->setAttribute ("fader", inst.faderDb);
        e->setAttribute ("mute", inst.muted);
        e->setAttribute ("recordWithFx", inst.recordWithFx);
        e->setAttribute ("midiSource", inst.midiSource);
        e->setAttribute ("midiSourceId", inst.midiSourceId);
        e->setAttribute ("anyKey", inst.anyKey);
        e->setAttribute ("padRoot", inst.padRoot);
        e->setAttribute ("padScale", inst.padScale);
        e->setAttribute ("padOctave", inst.padOctave);
        e->setAttribute ("state", inst.state);
        if (auto plugin = juce::parseXML (inst.pluginXml))
            e->addChildElement (plugin.release());
        inst.fx.addToXml (*e);
    }

    auto* pe = xml->createNewChildElement ("Pool");
    for (auto& f : pool)
        pe->createNewChildElement ("File")->setAttribute ("path", encode (f, baseFolder));

    auto* fe = xml->createNewChildElement ("PoolFolders");
    for (const auto& [path, folder] : poolFolders.moved)
    {
        auto* m = fe->createNewChildElement ("Moved");
        m->setAttribute ("path", encode (juce::File (path), baseFolder));
        m->setAttribute ("folder", folder);
    }
    for (const auto& name : poolFolders.made)
        fe->createNewChildElement ("Made")->setAttribute ("name", name);
    for (const auto& name : poolFolders.closed)
        fe->createNewChildElement ("Closed")->setAttribute ("name", name);

    return xml;
}

void LiveSet::loadFromXml (const juce::XmlElement& xml, const juce::File& baseFolder)
{
    rows.clear();
    pool.clear();
    masterDb = xml.getDoubleAttribute ("master", 0.0);
    fadeSeconds = xml.getDoubleAttribute ("fade", 8.0);
    loopStart = (LoopStart) juce::jlimit (0, 2, xml.getIntAttribute ("loopStart", 0));
    bpm = juce::jlimit (30.0, 240.0, xml.getDoubleAttribute ("bpm", 72.0));
    beatsPerBar = juce::jlimit (1, 16, xml.getIntAttribute ("beatsPerBar", 4));
    beatUnit = xml.getIntAttribute ("beatUnit", 4) == 8 ? 8 : 4;
    loopLength = (LoopLength) juce::jlimit (0, 2, xml.getIntAttribute ("loopLength", 0));
    layoutWidth = juce::jmax (0, xml.getIntAttribute ("layoutWidth", 0));
    midi.reset();
    if (auto* m = xml.getChildByName ("MidiMap"))
        midi = std::make_shared<juce::XmlElement> (*m);

    keyButtons.clear();
    if (auto* keysXml = xml.getChildByName ("KeyButtons"))
    {
        for (auto* ke : keysXml->getChildWithTagNameIterator ("KeyButton"))
            keyButtons.push_back ({ juce::jlimit (0, 11, ke->getIntAttribute ("root")), juce::jmax (0, ke->getIntAttribute ("scale")) });
    }
    else
    {
        // A set from before key buttons were its own: the three flute keys it always had.
        keyButtons = { { 9, 0 }, { 10, 1 }, { 4, 1 } };   // A minor, Bb major, E major pentatonic
    }

    liveGroups.clear();
    if (auto* liveXml = xml.getChildByName ("LiveGroups"))
    {
        for (auto* ge : liveXml->getChildWithTagNameIterator ("LiveGroup"))
        {
            if ((int) liveGroups.size() >= maxLiveGroups)
                break;
            LiveGroup g;
            g.name = ge->getStringAttribute ("name", "Live");
            g.uid = ge->getStringAttribute ("uid", g.uid);
            g.instruments = ge->getBoolAttribute ("instruments");
            g.colour = juce::jmax (0, ge->getIntAttribute ("colour", g.colour));
            g.faderDb = ge->getDoubleAttribute ("fader");
            g.muted = ge->getBoolAttribute ("mute");
            g.columns = juce::jmax (0, ge->getIntAttribute ("columns"));
            g.collapsed = ge->getBoolAttribute ("collapsed");
            g.position = { ge->getIntAttribute ("x", -1), ge->getIntAttribute ("y", -1) };
            g.fx = FxSettings::fromXml (ge->getChildByName ("Fx"));
            liveGroups.push_back (std::move (g));
        }
    }
    else
    {
        // A set from before live groups: the old Inputs panel's place and width go to its
        // inputs; the instruments get a group of their own beside it.
        LiveGroup inputsGroup;
        inputsGroup.name = "Inputs";
        inputsGroup.colour = 4;
        inputsGroup.position = { xml.getIntAttribute ("inputsX", -1), xml.getIntAttribute ("inputsY", -1) };
        inputsGroup.columns = juce::jmax (0, xml.getIntAttribute ("inputsColumns", 0));
        liveGroups.push_back (inputsGroup);
    }
    {
        const auto* se = xml.getChildByName ("Space");
        space = FxSettings::fromXml (se != nullptr ? se->getChildByName ("Fx") : nullptr, SpaceReverb::defaults());
    }
    masterInsert = PluginSlot::fromXml (&xml);

    scenes.clear();
    const auto* scenesXml = xml.getChildByName ("Scenes");
    scenesKeepChanges = scenesXml == nullptr || scenesXml->getBoolAttribute ("keepChanges", true);
    if (scenesXml != nullptr)
        for (auto* e : scenesXml->getChildWithTagNameIterator ("Scene"))
        {
            Scene sc;
            sc.name = e->getStringAttribute ("name", "Scene");
            sc.uid = e->getStringAttribute ("uid", sc.uid);
            sc.morphSeconds = juce::jlimit (0.0, 120.0, e->getDoubleAttribute ("morph", 8.0));
            auto fxOf = [] (const juce::XmlElement* x, bool& has)
            {
                const auto* f = x->getChildByName ("Fx");
                has = f != nullptr;
                return FxSettings::fromXml (f);
            };
            for (auto* ce : e->getChildWithTagNameIterator ("Channel"))
            {
                Scene::Channel c { ce->getStringAttribute ("uid"), ce->getBoolAttribute ("playing"), ce->getDoubleAttribute ("volume") };
                c.fx = fxOf (ce, c.hasFx);
                sc.channels.push_back (c);
            }
            for (auto* ge : e->getChildWithTagNameIterator ("Group"))
            {
                Scene::Group g { ge->getStringAttribute ("uid"), ge->getDoubleAttribute ("fader"), ge->getBoolAttribute ("muted") };
                g.fx = fxOf (ge, g.hasFx);
                sc.groups.push_back (g);
            }
            for (auto* se : e->getChildWithTagNameIterator ("Strip"))
            {
                Scene::Strip st { se->getStringAttribute ("kind") == "instrument" ? Scene::Strip::instrument : Scene::Strip::input,
                                  se->getIntAttribute ("index"), se->getDoubleAttribute ("fader"), se->getBoolAttribute ("muted") };
                st.fx = fxOf (se, st.hasFx);
                sc.strips.push_back (st);
            }
            for (auto* ke : e->getChildWithTagNameIterator ("Key"))
                sc.keys.push_back ({ ke->getIntAttribute ("instrument"), ke->getIntAttribute ("root", 9),
                                     ke->getIntAttribute ("scale"), ke->getIntAttribute ("octave", 3),
                                     ke->getBoolAttribute ("anyKey", false) });
            scenes.push_back (std::move (sc));
        }

    for (auto* re : xml.getChildWithTagNameIterator ("Row"))
    {
        if ((int) rows.size() >= maxRows)
            break;

        Row r;
        r.name = re->getStringAttribute ("name", "Row");
        r.colour = re->getIntAttribute ("colour", (int) rows.size());
        r.newSlotsOneShot = re->getBoolAttribute ("newSlotsOneShot");
        r.faderDb = re->getDoubleAttribute ("fader", 0.0);
        r.muted = re->getBoolAttribute ("mute");
        r.columns = juce::jmax (0, re->getIntAttribute ("columns", 0));
        r.collapsed = re->getBoolAttribute ("collapsed", false);
        r.uid = re->getStringAttribute ("uid", r.uid);
        r.position = { re->getIntAttribute ("x", -1), re->getIntAttribute ("y", -1) };
        r.fx = FxSettings::fromXml (re->getChildByName ("Fx"));
        r.insert = PluginSlot::fromXml (re);

        for (auto* se : re->getChildWithTagNameIterator ("Channel"))
        {
            Slot s;
            s.file = decode (se->getStringAttribute ("path"), baseFolder);
            s.label = se->getStringAttribute ("label");
            s.oneShot = se->getBoolAttribute ("oneShot");
            s.volumeDb = se->getDoubleAttribute ("volume", 0.0);
            s.smoothSeconds = se->getDoubleAttribute ("smooth", 0.0);
            s.reverse = se->getBoolAttribute ("reverse");
            s.shape = { juce::jmax (0.0, se->getDoubleAttribute ("trimStart")), juce::jmax (0.0, se->getDoubleAttribute ("trimEnd")),
                        juce::jmax (0.0, se->getDoubleAttribute ("fadeIn")), juce::jmax (0.0, se->getDoubleAttribute ("fadeOut")),
                        se->getDoubleAttribute ("sampleGain") };
            s.uid = se->getStringAttribute ("uid", s.uid);
            s.fx = FxSettings::fromXml (se->getChildByName ("Fx"));
            r.slots.push_back (std::move (s));
        }

        rows.push_back (std::move (r));
    }

    if (rows.empty())
        rows = makeDefault().rows;

    // As many inputs as the set was saved with (the first two start from the defaults).
    inputs = defaultInputs();
    int saved = 0;
    for (auto* ie : xml.getChildWithTagNameIterator ("Input"))
        saved += ie != nullptr ? 1 : 0;
    if (saved > 0)
        inputs.resize ((size_t) juce::jmin (saved, maxInputs));

    int i = 0;
    for (auto* ie : xml.getChildWithTagNameIterator ("Input"))
    {
        if (i >= maxInputs)
            break;

        auto& in = inputs[(size_t) i++];
        in.name = ie->getStringAttribute ("name", in.name);
        in.group = ie->getStringAttribute ("group");
        in.order = ie->getIntAttribute ("order");
        if (ie->hasAttribute ("channel"))
        {
            in.channel = juce::jlimit (0, maxDeviceInputs - 1, ie->getIntAttribute ("channel"));
            in.stereo = ie->getBoolAttribute ("stereo");
        }
        else
        {
            // Sets from before any input could be picked: 0 = input 1, 1 = input 2, 2 = inputs 1+2 stereo.
            const int source = juce::jlimit (0, 2, ie->getIntAttribute ("source", in.channel));
            in.channel = source == 1 ? 1 : 0;
            in.stereo = source == 2;
        }
        in.faderDb = ie->getDoubleAttribute ("fader", in.faderDb);
        in.muted = ie->getBoolAttribute ("mute", in.muted);
        in.monitor = ie->getBoolAttribute ("monitor", in.monitor);
        in.recordWithFx = ie->getBoolAttribute ("recordWithFx", in.recordWithFx);
        in.fx = FxSettings::fromXml (ie->getChildByName ("Fx"), in.fx);

        if (auto* pe = ie->getChildByName ("Insert"))
        {
            in.insertName = pe->getStringAttribute ("name");
            in.insertState = pe->getStringAttribute ("state");
            if (auto* plugin = pe->getChildByName ("PLUGIN"))
                in.insertXml = plugin->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
        }
    }

    instruments.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("Instrument"))
    {
        if ((int) instruments.size() >= maxInstruments)
            break;

        Instrument inst;
        inst.name = e->getStringAttribute ("name");
        inst.label = e->getStringAttribute ("label");
        inst.group = e->getStringAttribute ("group");
        inst.order = e->getIntAttribute ("order");
        inst.faderDb = e->getDoubleAttribute ("fader");
        inst.muted = e->getBoolAttribute ("mute");
        inst.recordWithFx = e->getBoolAttribute ("recordWithFx", true);
        inst.midiSource = e->getStringAttribute ("midiSource");
        inst.midiSourceId = e->getStringAttribute ("midiSourceId");
        inst.anyKey = e->getBoolAttribute ("anyKey", false);   // older sets: their instruments were in a key
        inst.padRoot = juce::jlimit (0, 11, e->getIntAttribute ("padRoot", 9));
        inst.padScale = e->getIntAttribute ("padScale", 0);
        inst.padOctave = juce::jlimit (0, 7, e->getIntAttribute ("padOctave", 3));
        inst.state = e->getStringAttribute ("state");
        if (auto* plugin = e->getChildByName ("PLUGIN"))
            inst.pluginXml = plugin->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
        inst.fx = FxSettings::fromXml (e->getChildByName ("Fx"));
        instruments.push_back (std::move (inst));
    }

    poolFolders = {};
    if (auto* fe = xml.getChildByName ("PoolFolders"))
    {
        for (auto* m : fe->getChildWithTagNameIterator ("Moved"))
            poolFolders.moved[decode (m->getStringAttribute ("path"), baseFolder).getFullPathName()] = m->getStringAttribute ("folder");
        for (auto* m : fe->getChildWithTagNameIterator ("Made"))
            poolFolders.made.addIfNotAlreadyThere (m->getStringAttribute ("name"));
        for (auto* m : fe->getChildWithTagNameIterator ("Closed"))
            poolFolders.closed.addIfNotAlreadyThere (m->getStringAttribute ("name"));
    }

    juce::Array<juce::File> pooled;
    if (auto* pe = xml.getChildByName ("Pool"))
        for (auto* f : pe->getChildWithTagNameIterator ("File"))
            pooled.add (decode (f->getStringAttribute ("path"), baseFolder));

    // Everything on a channel also belongs in the pool.
    pooled.addArray (allFiles());
    addToPool (pooled);

    ensureLiveGroups();
}
