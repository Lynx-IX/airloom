#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "GroupLayout.h"
#include "GroupResize.h"
#include "LevelMeter.h"
#include "LiveSet.h"
#include "Theme.h"

// One channel, laid out like a mixer strip: name, PLAY/STOP (or a trigger for
// one-shots), XF to crossfade to it, then a vertical fader with a level meter.
class ChannelCard : public juce::Component
{
public:
    enum class State { empty, idle, loading, fadingIn, playing, cuedStart, cuedStop };

    static constexpr const char* dragPrefix = "channel:";

    int index = 0;
    int slotId = 0;
    std::function<void()> onPlay, onCrossfade, onMenu, onFx, onDub;
    std::function<void (double db)> onVolume;
    std::function<void (const juce::String& control)> onLearn;   // right-click: "play", "xf", "dub" or "vol"
    LevelMeter meter;

    juce::Slider& getVolumeSlider()   { return volume; }

    // A collapsed group shows its channels as small tiles: the name, lit while playing.
    // Click plays or stops; right-click opens the channel's menu.
    void setCompact (bool shouldBeCompact)
    {
        if (shouldBeCompact == compact)
            return;
        compact = shouldBeCompact;
        updateVisibility();
        resized();
        repaint();
    }

    // Shows a small MIDI tag when any of this channel's controls has a MIDI assignment.
    void setMidiAssigned (bool assigned)
    {
        if (assigned != midiAssigned) { midiAssigned = assigned; repaint(); }
    }

    // DUB: amber text while waiting for its loop to come in, lit amber while overdubbing.
    // Either way, pressing it again calls it off.
    void setDubState (bool isWaiting, bool isOverdubbing)
    {
        if (isWaiting == dubWaiting && isOverdubbing == overdubbing)
            return;

        dubWaiting = isWaiting;
        overdubbing = isOverdubbing;
        dubButton.setColour (juce::TextButton::buttonColourId, overdubbing ? Theme::warn : Theme::raised);
        dubButton.setColour (juce::TextButton::textColourOffId, overdubbing ? Theme::background : dubWaiting ? Theme::warn : Theme::textDim);
        dubButton.setTooltip (overdubbing ? "Overdubbing onto this loop - press to stop (UNDO removes it)"
                            : dubWaiting  ? "Waiting for the loop to come in - press to cancel"
                                          : "Overdub onto this loop: plays it if needed and records the armed inputs on top");
    }

    ChannelCard()
    {
        addAndMakeVisible (playButton);
        addAndMakeVisible (xfButton);
        addAndMakeVisible (fxButton);
        addAndMakeVisible (dubButton);
        addAndMakeVisible (dotsButton);
        dotsButton.onClick = [this] { if (onMenu) onMenu(); };
        addAndMakeVisible (volume);
        addAndMakeVisible (meter);

        playButton.onClick = [this] { if (onPlay) onPlay(); };
        xfButton.onClick = [this] { if (onCrossfade) onCrossfade(); };
        fxButton.onClick = [this] { if (onFx) onFx(); };
        dubButton.onClick = [this] { if (onDub) onDub(); };
        dubButton.onRightClick = [this] { if (onLearn) onLearn ("dub"); };
        playButton.onRightClick = [this] { if (onLearn) onLearn ("play"); };
        xfButton.onRightClick = [this] { if (onLearn) onLearn ("xf"); };
        volume.onRightClick = [this] { if (onLearn) onLearn ("vol"); };
        xfButton.setTooltip ("Crossfade to this channel: it fades in while the others in this group fade out");
        fxButton.setTooltip ("This channel's own effects (the group's FX still apply after them)");
        dubButton.setTooltip ("Overdub onto this loop: plays it if needed and records the armed inputs on top");
        dubButton.setColour (juce::TextButton::textColourOffId, Theme::textDim);

        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 18);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
        volume.onValueChange = [this] { if (onVolume) onVolume (volume.getValue()); };

        for (auto* c : getChildren())
            c->setWantsKeyboardFocus (false);
    }

    void setContent (const juce::String& newLabel, bool isOneShot, bool isSmoothed, bool isReversed, bool hasFx,
                     State newState, double volumeDb, juce::Colour colour)
    {
        // Cued channels blink, about twice a second, until their beat comes.
        const bool cued = newState == State::cuedStart || newState == State::cuedStop;
        const bool blink = cued && (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        const bool changed = newLabel != label || isOneShot != oneShot || isSmoothed != smoothed || isReversed != reversed
                          || newState != state || colour != rowColour || blink != blinkOn;
        blinkOn = blink;
        label = newLabel;
        oneShot = isOneShot;
        smoothed = isSmoothed;
        reversed = isReversed;
        state = newState;
        rowColour = colour;
        volume.setColour (juce::Slider::trackColourId, rowColour);

        updateVisibility();

        fxButton.setColour (juce::TextButton::buttonColourId, hasFx ? rowColour.darker (0.15f) : Theme::raised);
        fxButton.setColour (juce::TextButton::textColourOffId, hasFx ? Theme::background : Theme::textDim);

        if (! volume.isMouseButtonDown() && std::abs (volume.getValue() - volumeDb) > 0.05)
            volume.setValue (volumeDb, juce::dontSendNotification);

        // Cued to start: blinks amber. Cued to stop: blinks between its colour and off.
        const bool lit = state == State::playing || state == State::fadingIn
                      || (state == State::cuedStart && blinkOn) || (state == State::cuedStop && blinkOn);
        playButton.setButtonText (oneShot ? "PLAY" : state == State::loading ? "..." : cued ? "CUED" : lit ? "STOP" : "PLAY");
        playButton.setColour (juce::TextButton::buttonColourId,
                              ! lit ? Theme::raised
                                    : state == State::fadingIn || state == State::cuedStart ? Theme::warn : rowColour);
        playButton.setColour (juce::TextButton::textColourOffId, lit ? Theme::background : Theme::textDim);
        playButton.setTooltip (state == State::cuedStart ? "Waiting for the next beat or bar to come in (press to cancel)"
                             : state == State::cuedStop  ? "Stops on the next beat or bar"
                                                         : juce::String());

        if (changed)
        {
            resized();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        if (compact)
        {
            const bool lit = state == State::playing || state == State::fadingIn
                          || ((state == State::cuedStart || state == State::cuedStop) && blinkOn);
            const auto fill = state == State::empty ? Theme::panel
                            : lit ? (state == State::fadingIn || state == State::cuedStart ? Theme::warn : rowColour)
                                  : Theme::raised.interpolatedWith (rowColour, 0.06f);
            Theme::drawSurface (g, r, fill, 5.0f);
            if (state == State::empty)
            {
                g.setColour (Theme::panelEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            }

            g.setColour (lit ? Theme::background : state == State::empty ? Theme::textDim : Theme::text);
            g.setFont (Theme::heading (12.0f));
            const auto text = state == State::empty ? juce::String ("empty") : (oneShot ? "1x " : "") + label;
            g.drawFittedText (text, getLocalBounds().reduced (7, 2), juce::Justification::centred, 1, 0.8f);
            return;
        }

        if (state == State::empty)
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r, 5.0f);
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::panelEdge);
            g.fillPath (dashed);

            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (oneShot ? "Drop a one-shot or click to load" : "Drop a loop or click to load",
                              getLocalBounds().reduced (8), juce::Justification::centred, 4);
            return;
        }

        const bool lit = state == State::playing || state == State::cuedStop;
        const bool pending = state == State::fadingIn || state == State::loading || state == State::cuedStart;
        const auto edge = lit ? rowColour : pending ? Theme::warn : juce::Colour();

        Theme::drawSurface (g, r, Theme::raised.interpolatedWith (rowColour, lit ? 0.16f : 0.035f), 6.0f);

        if (! edge.isTransparent())
        {
            // Playing: a glow from the top and a coloured outline.
            g.setGradientFill (juce::ColourGradient (edge.withAlpha (0.28f), 0.0f, r.getY(),
                                                     edge.withAlpha (0.0f), 0.0f, r.getY() + 60.0f, false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.5f);
            g.fillRoundedRectangle (r.reduced (10.0f, 0.0f).withHeight (3.0f), 1.5f);
        }

        // Small tags: plays once, reversed, smooth loop point.
        juce::StringArray tags;
        if (midiAssigned)          tags.add ("MIDI");
        if (oneShot)               tags.add ("1x");
        if (reversed)              tags.add ("REV");
        if (smoothed && ! oneShot) tags.add ("~");

        auto top = nameArea;
        if (! tags.isEmpty())
        {
            g.setColour (rowColour);
            g.setFont (Theme::heading (10.0f));
            const auto text = tags.joinIntoString (" ");
            g.drawText (text, top.removeFromBottom (11), juce::Justification::centredRight);
        }

        // Clear of the ... button in the corner (both sides, so the name stays centred).
        Theme::drawFittedName (g, label, top.reduced (14, 0), lit ? Theme::text : Theme::text.withAlpha (0.88f), 24.0f);
    }

    void resized() override
    {
        if (compact)
        {
            nameArea = getLocalBounds();
            return;
        }

        auto area = getLocalBounds().reduced (5);
        nameArea = area.removeFromTop (38);
        dotsButton.setBounds (getWidth() - 23, 3, 20, 14);
        area.removeFromTop (2);

        // PLAY and FX, then (loops only) a smaller DUB and XF underneath.
        auto buttons = area.removeFromTop (26);
        fxButton.setBounds (buttons.removeFromRight (28));
        buttons.removeFromRight (2);
        playButton.setBounds (buttons);

        if (dubButton.isVisible())
        {
            area.removeFromTop (2);
            auto second = area.removeFromTop (20);
            xfButton.setBounds (second.removeFromRight (28));
            second.removeFromRight (2);
            dubButton.setBounds (second);
        }
        area.removeFromTop (4);

        meter.setBounds (area.removeFromRight (16).withTrimmedBottom (22).reduced (0, 4));
        volume.setBounds (area);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Dragging the strip (not its fader or buttons) picks it up to move it.
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
                container->startDragging (dragPrefix + juce::String (slotId), this);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;

        // Empty: click loads. Otherwise the name (or a right-click anywhere) opens the menu.
        // A compact tile plays or stops on a click instead.
        if ((state == State::empty || compact) && ! e.mods.isPopupMenu())
        {
            if (onPlay) onPlay();
        }
        else if ((e.mods.isPopupMenu() || nameArea.contains (e.getPosition())) && onMenu)
        {
            onMenu();
        }
    }

private:
    void updateVisibility()
    {
        dotsButton.setVisible (! compact);
        const bool hasSample = state != State::empty && ! compact;
        playButton.setVisible (hasSample);
        xfButton.setVisible (hasSample && ! oneShot);
        dubButton.setVisible (hasSample && ! oneShot);
        fxButton.setVisible (hasSample);
        volume.setVisible (hasSample);
        meter.setVisible (hasSample);
    }

    juce::String label;
    bool oneShot = false, smoothed = false, reversed = false, midiAssigned = false, compact = false;
    bool blinkOn = false;   // the current half of a cued channel's blink
    bool dubWaiting = false, overdubbing = false;
    State state = State::empty;
    juce::Colour rowColour = Theme::accent;
    juce::Rectangle<int> nameArea;

    RightClickable<juce::TextButton> playButton { "PLAY" }, xfButton { "XF" }, dubButton { "DUB" };
    DotsButton dotsButton;
    juce::TextButton fxButton { "FX" };
    RightClickable<juce::Slider> volume;
};

// A group of channels: its name, FX and group fader on the left, then its
// channels in a grid. By default the grid is one line (wrapping if the window is
// too narrow); dragging the right edge picks a number of columns instead.
// Drag the name to move the group. The arrow by the name collapses it to a short
// bar: name, MUTE and FX, and a small tile per channel.
class RowView : public juce::Component
{
public:
    static constexpr int headerWidth = 110, cardWidth = 112, cardHeight = 186, cardGap = 6, padding = 6;
    static constexpr int tileHeight = 28, collapsedHeight = 66;   // a collapsed group
    static constexpr int lineGap = GroupLayout::lineGap (padding); // between lines of channels
    static constexpr const char* dragPrefix = "group:";

    int index = 0;
    ChannelStrip strip { {}, false };

    std::function<void (int)> onPlay, onCrossfade, onMenu, onChannelFx, onDub;
    std::function<void (int, const juce::String&)> onChannelLearn;
    std::function<void (int, double)> onVolume;
    std::function<void()> onAddChannel, onHeaderMenu, onFx, onLayoutChanged, onToggleCollapsed;
    ColumnResizer resizer;   // its onColumnsChanged is set by the owner
    juce::Point<int> grabOffset;   // where in the group the mouse picked it up

    RowView()
    {
        addAndMakeVisible (strip);
        addAndMakeVisible (fxButton);
        addAndMakeVisible (addButton);
        addAndMakeVisible (dotsButton);
        dotsButton.onClick = [this] { if (onHeaderMenu) onHeaderMenu(); };

        fxButton.setTooltip ("Effects for everything in this group");
        fxButton.setWantsKeyboardFocus (false);
        fxButton.onClick = [this] { if (onFx) onFx(); };

        addButton.setTooltip ("Add a channel");
        addButton.setWantsKeyboardFocus (false);
        addButton.onClick = [this] { if (onAddChannel) onAddChannel(); };

        resizer.columnsForWidth = [this] (int width)
        {
            const int fitting = (width - headerWidth - 2 * padding + cardGap + (cardWidth + cardGap) / 2) / (cardWidth + cardGap);
            return juce::jlimit (1, juce::jmax (1, cards.size()), fitting);
        };
    }

    // The size a group with this many channels wants, given the widest it may be.
    // columns 0 means automatic: everything on one line if it fits.
    // A collapsed group keeps its width (so the layout doesn't jump) but is only as tall as its tiles.
    static juce::Point<int> preferredSize (int numChannels, int maxWidth, int wantedColumns = 0, bool isCollapsed = false)
    {
        const int items = juce::jmax (1, numChannels);
        const int cell = cardWidth + cardGap;
        const int fitting = juce::jmax (1, (maxWidth - headerWidth - 2 * padding + cardGap) / cell);
        const int columns = juce::jlimit (1, items, juce::jmin (fitting, wantedColumns > 0 ? wantedColumns : items));
        const int lines = (items + columns - 1) / columns;
        const int height = isCollapsed ? juce::jmax (collapsedHeight - 2 * padding, lines * (tileHeight + cardGap) - cardGap)
                                       : juce::jmax (cardHeight, lines * (cardHeight + lineGap) - lineGap);
        return { headerWidth + 2 * padding + columns * cell - cardGap, height + 2 * padding };
    }

    bool isCollapsed() const   { return collapsed; }

    juce::Component* getCard (int slot)   { return juce::isPositiveAndBelow (slot, cards.size()) ? cards[slot] : nullptr; }
    ChannelCard* getChannelCard (int slot) { return juce::isPositiveAndBelow (slot, cards.size()) ? cards[slot] : nullptr; }

    void update (const LiveSet::Row& row, const std::function<bool (int slotId)>& isFadingIn,
                 const std::function<ChannelCard::State (int slotId)>& cueState, juce::uint32 now)
    {
        const auto newColour = Theme::rowColour (row.colour);
        fixedColumns = row.columns > 0;
        fxButton.setColour (juce::TextButton::buttonColourId, row.fx.anyOn() ? newColour.darker (0.15f) : Theme::raised);

        if (row.collapsed != collapsed)
        {
            collapsed = row.collapsed;
            strip.setMuteOnly (collapsed);
            addButton.setVisible (! collapsed);
            for (auto* c : cards)
                c->setCompact (collapsed);
            if (onLayoutChanged)
                onLayoutChanged();
            resized();
            repaint();
        }

        if (row.name != name || newColour != colour)
        {
            name = row.name;
            colour = newColour;
            strip.fader.setColour (juce::Slider::trackColourId, colour);
            repaint();
        }

        if (cards.size() != (int) row.slots.size())
        {
            cards.clear();
            for (int i = 0; i < (int) row.slots.size(); ++i)
            {
                auto* c = cards.add (new ChannelCard());
                c->index = i;
                c->onPlay = [this, i] { if (onPlay) onPlay (i); };
                c->onCrossfade = [this, i] { if (onCrossfade) onCrossfade (i); };
                c->onMenu = [this, i] { if (onMenu) onMenu (i); };
                c->onFx = [this, i] { if (onChannelFx) onChannelFx (i); };
                c->onDub = [this, i] { if (onDub) onDub (i); };
                c->onVolume = [this, i] (double db) { if (onVolume) onVolume (i, db); };
                c->onLearn = [this, i] (const juce::String& control) { if (onChannelLearn) onChannelLearn (i, control); };
                c->setCompact (collapsed);
                addAndMakeVisible (c);
            }

            // A different number of channels means a different size.
            if (onLayoutChanged)
                onLayoutChanged();
            resized();
        }

        for (int i = 0; i < cards.size(); ++i)
        {
            const auto& slot = row.slots[(size_t) i];
            using State = ChannelCard::State;

            const State cue = slot.file == juce::File() || slot.loading ? State::idle : cueState (slot.id);
            const State state = slot.file == juce::File() ? State::empty
                              : slot.loading              ? State::loading
                              : cue != State::idle        ? cue
                              : slot.playing              ? (isFadingIn (slot.id) ? State::fadingIn : State::playing)
                              : slot.flashUntil > now     ? State::playing
                                                          : State::idle;

            const auto label = slot.label.isNotEmpty() ? slot.label : slot.file.getFileNameWithoutExtension();
            cards[i]->slotId = slot.id;
            cards[i]->setContent (label, slot.oneShot, slot.smoothSeconds > 0.0, slot.reverse, slot.fx.isActive(),
                                  state, slot.volumeDb, colour);
        }
    }

    // Where a channel dropped at this point (in this view's coordinates) would be inserted.
    int dropIndexAt (juce::Point<int> pos) const
    {
        const int cell = cardWidth + cardGap;
        const int column = juce::jlimit (0, columns, (pos.x - gridArea.getX() + cell / 2) / cell);
        const int line = juce::jmax (0, (pos.y - gridArea.getY()) / (cellHeight() + linePitchGap()));
        return juce::jlimit (0, cards.size(), line * columns + column);
    }

    // Shows (index >= 0) or hides the insertion line while a channel is dragged.
    void setDropMarker (int insertAt)
    {
        if (insertAt != dropMarker)
        {
            dropMarker = insertAt;
            repaint();
        }
    }

    // Shows which side a dragged group would land on: -1 before, 1 after, 0 hidden.
    void setGroupDropSide (int side)
    {
        if (side != groupDropSide)
        {
            groupDropSide = side;
            repaint();
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (strip.isMutedShown())
        {
            // Muted: its channels fade back.
            g.setColour (Theme::panel.withAlpha (0.55f));
            g.fillRoundedRectangle (gridArea.toFloat().expanded (2.0f), 6.0f);
        }

        g.setColour (colour);
        ColumnResizer::paintGrip (g, *this, colour, fixedColumns);

        if (dropMarker >= 0)
        {
            const auto cell = cellBounds (dropMarker);
            g.fillRoundedRectangle ((float) (cell.getX() - cardGap / 2) - 2.0f, (float) cell.getY(), 4.0f, (float) cell.getHeight(), 2.0f);
        }

        if (groupDropSide != 0)
        {
            const float x = groupDropSide < 0 ? 0.0f : (float) getWidth() - 5.0f;
            g.fillRoundedRectangle (x, 0.0f, 5.0f, (float) getHeight(), 2.0f);
        }
    }

    void pushChannelPeak (int slot, float peak)
    {
        if (juce::isPositiveAndBelow (slot, cards.size()))
        {
            cards[slot]->meter.pushPeak (0, peak);
            cards[slot]->meter.pushPeak (1, peak);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        Theme::drawSurface (g, bounds, Theme::panel, 8.0f);

        // Coloured spine on the left identifies the group at a glance.
        Theme::drawSpine (g, bounds, colour);

        // The collapse arrow: pointing down when open, right when collapsed.
        {
            const auto c = arrowArea.toFloat().getCentre();
            juce::Path arrow;
            if (collapsed) arrow.addTriangle (c.x - 3.0f, c.y - 5.0f, c.x - 3.0f, c.y + 5.0f, c.x + 4.0f, c.y);
            else           arrow.addTriangle (c.x - 5.0f, c.y - 3.0f, c.x + 5.0f, c.y - 3.0f, c.x, c.y + 4.0f);
            g.setColour (arrowHover ? Theme::text : colour.withAlpha (0.85f));
            g.fillPath (arrow);
        }

        g.setColour (colour);
        g.setFont (Theme::heading (14.0f, 0.1f));
        g.drawFittedText (name.toUpperCase(), nameArea.withLeft (arrowArea.getRight()).withTrimmedRight (24).reduced (2, 0),
                          juce::Justification::centredLeft, collapsed ? 1 : 2);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromLeft (headerWidth).withTrimmedLeft (6);

        if (collapsed)
        {
            // Name on top, then MUTE and FX side by side.
            header.removeFromTop (padding - 2);
            nameArea = header.removeFromTop (28);
            auto buttons = header.removeFromTop (24).reduced (8, 0);
            fxButton.setBounds (buttons.removeFromRight (34));
            buttons.removeFromRight (4);
            strip.setBounds (buttons);
        }
        else
        {
            nameArea = header.removeFromTop (34);
            auto buttons = header.removeFromTop (24).reduced (8, 0);
            addButton.setBounds (buttons.removeFromRight (28));
            buttons.removeFromRight (4);
            fxButton.setBounds (buttons);
            strip.setBounds (header);
        }
        arrowArea = nameArea.withWidth (24);
        dotsButton.setBounds (nameArea.getRight() - 24, nameArea.getY() + (collapsed ? 7 : 10), 22, 14);

        gridArea = area.reduced (padding);
        columns = juce::jmax (1, (gridArea.getWidth() + cardGap) / (cardWidth + cardGap));

        if (collapsed)
        {
            // Tiles sit in the middle of the bar.
            const int lines = (juce::jmax (1, cards.size()) + columns - 1) / columns;
            const int used = lines * (tileHeight + cardGap) - cardGap;
            gridArea = gridArea.withTrimmedTop (juce::jmax (0, (gridArea.getHeight() - used) / 2));
        }

        for (int i = 0; i < cards.size(); ++i)
            cards[i]->setBounds (cellBounds (i));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        resizer.mouseMove (*this, e);
        const bool over = arrowArea.contains (e.getPosition());
        if (over != arrowHover)
        {
            arrowHover = over;
            repaint (arrowArea);
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (arrowHover) { arrowHover = false; repaint (arrowArea); }
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { resizer.mouseDoubleClick (*this, e); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        resizer.mouseDown (*this, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (resizer.mouseDrag (e))
            return;

        // Dragging the group's name picks the whole group up.
        if (! nameArea.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
            {
                grabOffset = e.getMouseDownPosition();
                container->startDragging (dragPrefix + juce::String (index), this);
            }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (resizer.mouseUp())
            return;

        if (! e.mouseWasClicked())
            return;

        if (arrowArea.contains (e.getPosition()) && ! e.mods.isPopupMenu())
        {
            if (onToggleCollapsed) onToggleCollapsed();
        }
        else if (nameArea.contains (e.getPosition()) && onHeaderMenu)
        {
            onHeaderMenu();
        }
    }

private:
    juce::Rectangle<int> cellBounds (int i) const
    {
        return { gridArea.getX() + (i % columns) * (cardWidth + cardGap),
                 gridArea.getY() + (i / columns) * (cellHeight() + linePitchGap()),
                 cardWidth, cellHeight() };
    }

    int cellHeight() const     { return collapsed ? tileHeight : cardHeight; }
    int linePitchGap() const   { return collapsed ? cardGap : lineGap; }

    juce::String name;
    juce::Colour colour = Theme::accent;
    juce::Rectangle<int> nameArea, gridArea, arrowArea;
    int columns = 1, dropMarker = -1, groupDropSide = 0;
    bool collapsed = false, arrowHover = false;
    bool fixedColumns = false;
    juce::OwnedArray<ChannelCard> cards;
    juce::TextButton addButton { "+" };
    juce::TextButton fxButton { "FX" };
    DotsButton dotsButton;
};

