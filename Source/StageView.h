#pragma once

#include <JuceHeader.h>
#include "RowView.h"
#include "Theme.h"

// The stage view: the same set, drawn big for performing. Scenes across the top; every
// group's loops as large tiles (press to play or stop, like their PLAY buttons) with the
// group's mute; the inputs' and instruments' mutes and MUTE OUT down the right. The mixer
// stays underneath - this only shows and presses the same things.
class StageView : public juce::Component
{
public:
    using State = ChannelCard::State;

    struct Tile  { juce::String label; int row = 0, slot = 0; State state = State::idle; bool oneShot = false; };
    struct Group { juce::String name; juce::Colour colour; bool muted = false; int row = 0; std::vector<Tile> tiles; };
    struct Live  { juce::String name; juce::Colour colour; bool muted = false; bool instrument = false; int index = 0; };

    struct Content
    {
        juce::StringArray scenes;
        int activeScene = -1;
        float morphProgress = -1.0f;   // 0..1 while morphing
        std::vector<Group> groups;
        std::vector<Live> live;
        bool outputMuted = false;
    };

    std::function<void (int scene)> onScene;
    std::function<void()> onAddScene, onMuteOut;
    std::function<void (int row, int slot)> onTile;
    std::function<void (int row)> onGroupMute;
    std::function<void (bool instrument, int index)> onLiveMute;

    StageView()     { setOpaque (true); }

    void setContent (Content newContent)
    {
        // Blinking (cued loops, MUTED) and the morph bar change the picture without changing the
        // content, so the signature includes the blink phase.
        const bool blinkPhase = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        const auto sig = signature (newContent) + (blinkPhase ? "b" : "");
        content = std::move (newContent);
        if (sig != lastSignature)
        {
            lastSignature = sig;
            blink = blinkPhase;
            layOut();
            repaint();
        }
    }

    void resized() override     { layOut(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::background);

        // Scenes
        for (int i = 0; i < (int) sceneRects.size(); ++i)
        {
            const auto r = sceneRects[(size_t) i];
            const bool on = i == content.activeScene;
            Theme::drawSurface (g, r, on ? Theme::accent.darker (0.25f) : Theme::raised, 10.0f);
            g.setColour (on ? Theme::background : Theme::text);
            g.setFont (juce::FontOptions (juce::jmin (26.0f, r.getHeight() * 0.36f), juce::Font::bold));
            g.drawFittedText (content.scenes[i], r.reduced (10.0f, 4.0f).toNearestInt(), juce::Justification::centred, 2);

            if (on && content.morphProgress >= 0.0f)
            {
                auto bar = r.reduced (12.0f, 0.0f).removeFromBottom (8.0f).withTrimmedBottom (3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRoundedRectangle (bar, 2.5f);
                g.setColour (Theme::warn);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * content.morphProgress), 2.5f);
            }
        }
        if (! addSceneRect.isEmpty())
        {
            Theme::drawSurface (g, addSceneRect, Theme::panel, 10.0f);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (content.scenes.isEmpty() ? "+ SCENE  (save the current mix)" : "+ SCENE", addSceneRect, juce::Justification::centred);
        }

        // Groups and their loops
        for (size_t gi = 0; gi < content.groups.size() && gi < groupRects.size(); ++gi)
        {
            const auto& grp = content.groups[gi];
            const auto& gr = groupRects[gi];

            g.setColour (grp.colour);
            g.setFont (Theme::heading (15.0f, 0.12f));
            g.drawText (grp.name.toUpperCase(), gr.header.withTrimmedRight (gr.mute.getWidth() + 8.0f), juce::Justification::centredLeft, true);
            drawToggle (g, gr.mute, grp.muted ? "MUTED" : "MUTE", grp.muted, Theme::panelEdge.brighter (0.3f));

            for (size_t ti = 0; ti < grp.tiles.size() && ti < gr.tiles.size(); ++ti)
                drawTile (g, gr.tiles[ti], grp.tiles[ti], grp.colour);
        }

        // Live inputs and instruments, then MUTE OUT
        if (! liveRects.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::heading (13.0f, 0.12f));
            g.drawText ("LIVE", liveHeader, juce::Justification::centredLeft);
        }
        for (size_t i = 0; i < content.live.size() && i < liveRects.size(); ++i)
        {
            const auto& l = content.live[i];
            const auto r = liveRects[i];
            Theme::drawSurface (g, r, l.muted ? Theme::panel : Theme::raised.interpolatedWith (l.colour, 0.25f), 8.0f);
            g.setColour (l.muted ? Theme::textDim : l.colour);
            g.fillRoundedRectangle (r.withWidth (6.0f).reduced (0.0f, 6.0f), 3.0f);
            g.setColour (l.muted ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
            g.drawFittedText (l.name, r.reduced (18.0f, 4.0f).withTrimmedRight (70.0f).toNearestInt(), juce::Justification::centredLeft, 1);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.setColour (l.muted ? Theme::textDim : Theme::accent);
            g.drawText (l.muted ? "MUTED" : "ON", r.withTrimmedRight (14.0f), juce::Justification::centredRight);
        }

        {
            const bool on = content.outputMuted && blink;
            Theme::drawSurface (g, muteOutRect, on ? Theme::danger : content.outputMuted ? Theme::danger.darker (1.0f) : Theme::raised, 10.0f);
            g.setColour (content.outputMuted ? juce::Colours::white : Theme::text);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText (content.outputMuted ? "MUTED" : "MUTE OUT", muteOutRect, juce::Justification::centred);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown())
            return;
        const auto p = e.position;

        for (int i = 0; i < (int) sceneRects.size(); ++i)
            if (sceneRects[(size_t) i].contains (p)) { if (onScene) onScene (i); return; }
        if (addSceneRect.contains (p)) { if (onAddScene) onAddScene(); return; }

        for (size_t gi = 0; gi < content.groups.size() && gi < groupRects.size(); ++gi)
        {
            const auto& grp = content.groups[gi];
            if (groupRects[gi].mute.contains (p)) { if (onGroupMute) onGroupMute (grp.row); return; }
            for (size_t ti = 0; ti < grp.tiles.size() && ti < groupRects[gi].tiles.size(); ++ti)
                if (groupRects[gi].tiles[ti].contains (p)) { if (onTile) onTile (grp.tiles[ti].row, grp.tiles[ti].slot); return; }
        }

        for (size_t i = 0; i < content.live.size() && i < liveRects.size(); ++i)
            if (liveRects[i].contains (p)) { if (onLiveMute) onLiveMute (content.live[i].instrument, content.live[i].index); return; }

        if (muteOutRect.contains (p) && onMuteOut)
            onMuteOut();
    }

private:
    struct GroupRects { juce::Rectangle<float> header, mute; std::vector<juce::Rectangle<float>> tiles; };

    Content content;
    juce::String lastSignature;
    bool blink = false;
    std::vector<juce::Rectangle<float>> sceneRects, liveRects;
    juce::Rectangle<float> addSceneRect, muteOutRect, liveHeader;
    std::vector<GroupRects> groupRects;

    static juce::String signature (const Content& c)
    {
        juce::String s = c.scenes.joinIntoString ("|") + "#" + juce::String (c.activeScene) + "#"
                       + juce::String (juce::roundToInt (c.morphProgress * 100.0f)) + (c.outputMuted ? "M" : "m");
        for (const auto& grp : c.groups)
        {
            s << "/" << grp.name << (grp.muted ? "1" : "0") << grp.colour.toString();
            for (const auto& t : grp.tiles)
                s << ";" << t.label << (int) t.state;
        }
        for (const auto& l : c.live)
            s << "^" << l.name << (l.muted ? "1" : "0");
        return s;
    }

    void layOut()
    {
        auto area = getLocalBounds().toFloat().reduced (4.0f);

        // Scenes: one row of big buttons.
        sceneRects.clear();
        auto sceneRow = area.removeFromTop (84.0f);
        area.removeFromTop (16.0f);
        {
            const int n = content.scenes.size();
            const float addWidth = n == 0 ? 340.0f : 130.0f, gap = 10.0f;
            const float each = n == 0 ? 0.0f : juce::jlimit (110.0f, 260.0f, (sceneRow.getWidth() - addWidth - gap * (float) n) / (float) n);
            for (int i = 0; i < n; ++i)
            {
                sceneRects.push_back (sceneRow.removeFromLeft (each));
                sceneRow.removeFromLeft (gap);
            }
            addSceneRect = sceneRow.removeFromLeft (juce::jmin (addWidth, sceneRow.getWidth()));
        }

        // Right column: live strips, MUTE OUT at the bottom.
        auto right = area.removeFromRight (280.0f);
        area.removeFromRight (20.0f);
        muteOutRect = right.removeFromBottom (80.0f);
        right.removeFromBottom (16.0f);
        liveRects.clear();
        if (! content.live.empty())
        {
            liveHeader = right.removeFromTop (24.0f);
            const float h = juce::jlimit (40.0f, 64.0f, (right.getHeight() - 8.0f * (float) content.live.size()) / (float) content.live.size());
            for (size_t i = 0; i < content.live.size(); ++i)
            {
                liveRects.push_back (right.removeFromTop (h));
                right.removeFromTop (8.0f);
            }
        }

        // Groups: a header line each, then their loops as tiles. Tiles shrink until everything fits.
        groupRects.clear();
        float tileH = 96.0f;
        for (; tileH > 40.0f; tileH -= 4.0f)
            if (groupsHeight (area.getWidth(), tileH) <= area.getHeight())
                break;
        const float tileW = tileH * 1.9f, gap = 8.0f;
        const int perLine = juce::jmax (1, (int) ((area.getWidth() + gap) / (tileW + gap)));

        for (const auto& grp : content.groups)
        {
            GroupRects gr;
            gr.header = area.removeFromTop (34.0f);
            gr.mute = gr.header.removeFromLeft (juce::jmin (area.getWidth(), (float) perLine * (tileW + gap) - gap))
                          .removeFromRight (96.0f).reduced (0.0f, 3.0f);
            gr.header = gr.header.withX (area.getX()).withWidth (gr.mute.getRight() - area.getX());
            for (size_t i = 0; i < grp.tiles.size(); ++i)
            {
                const int line = (int) i / perLine, col = (int) i % perLine;
                gr.tiles.push_back ({ area.getX() + (float) col * (tileW + gap), area.getY() + (float) line * (tileH + gap), tileW, tileH });
            }
            const int lines = grp.tiles.empty() ? 0 : ((int) grp.tiles.size() + perLine - 1) / perLine;
            area.removeFromTop ((float) lines * (tileH + gap) + 12.0f);
            groupRects.push_back (std::move (gr));
        }
    }

    float groupsHeight (float width, float tileH) const
    {
        const float tileW = tileH * 1.9f, gap = 8.0f;
        const int perLine = juce::jmax (1, (int) ((width + gap) / (tileW + gap)));
        float h = 0.0f;
        for (const auto& grp : content.groups)
            h += 34.0f + (float) ((int) grp.tiles.size() + perLine - 1) / perLine * (tileH + gap) + 12.0f;
        return h;
    }

    void drawToggle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, juce::Colour onColour) const
    {
        Theme::drawSurface (g, r, on ? onColour : Theme::raised, 6.0f);
        g.setColour (on ? juce::Colours::white : Theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (text, r, juce::Justification::centred);
    }

    void drawTile (juce::Graphics& g, juce::Rectangle<float> r, const Tile& t, juce::Colour colour) const
    {
        const bool playing = t.state == State::playing;
        const bool cued = t.state == State::cuedStart || t.state == State::cuedStop;
        const bool pending = t.state == State::fadingIn || t.state == State::loading || (t.state == State::cuedStart && blink);
        const bool lit = playing || (t.state == State::cuedStop && blink);

        const auto fill = lit ? colour.darker (0.2f) : pending ? Theme::warn.darker (0.4f) : Theme::raised.interpolatedWith (colour, 0.06f);
        Theme::drawSurface (g, r, fill, 10.0f);
        if (lit || pending)
        {
            g.setColour (lit ? colour.brighter (0.3f) : Theme::warn);
            g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 2.5f);
        }

        g.setColour (lit || pending ? Theme::background : Theme::text);
        g.setFont (juce::FontOptions (juce::jlimit (14.0f, 24.0f, r.getHeight() * 0.25f), juce::Font::bold));
        g.drawFittedText (t.label, r.reduced (12.0f, 8.0f).withTrimmedBottom (r.getHeight() * 0.22f).toNearestInt(),
                          juce::Justification::centred, 2);

        const juce::String status = t.oneShot ? "1x"
                                  : cued && t.state == State::cuedStart ? "CUED"
                                  : t.state == State::cuedStop ? "STOPPING"
                                  : t.state == State::fadingIn ? "FADING IN"
                                  : t.state == State::loading ? "..."
                                  : playing ? "PLAYING" : "";
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour ((lit || pending ? Theme::background : Theme::textDim).withAlpha (0.9f));
        g.drawText (status, r.reduced (10.0f, 6.0f), juce::Justification::centredBottom);
    }
};
