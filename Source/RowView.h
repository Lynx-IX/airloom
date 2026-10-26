#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "GroupResize.h"
#include "LevelMeter.h"
#include "LiveSet.h"
#include "Theme.h"

// One channel, laid out like a mixer strip: name, PLAY/STOP (or a trigger for
// one-shots), XF to crossfade to it, then a vertical fader with a level meter.
class ChannelCard : public juce::Component
{
public:
    enum class State { empty, idle, loading, fadingIn, playing };

    static constexpr const char* dragPrefix = "channel:";

    int index = 0;
    int slotId = 0;
    std::function<void()> onPlay, onCrossfade, onMenu, onFx;
    std::function<void (double db)> onVolume;
    std::function<void (const juce::String& control)> onLearn;   // right-click: "play", "xf" or "vol"
    LevelMeter meter;

    juce::Slider& getVolumeSlider()   { return volume; }

    // Shows a small MIDI tag when any of this channel's controls has a MIDI assignment.
    void setMidiAssigned (bool assigned)
    {
        if (assigned != midiAssigned) { midiAssigned = assigned; repaint(); }
    }

    ChannelCard()
    {
        addAndMakeVisible (playButton);
        addAndMakeVisible (xfButton);
        addAndMakeVisible (fxButton);
        addAndMakeVisible (volume);
        addAndMakeVisible (meter);

        playButton.onClick = [this] { if (onPlay) onPlay(); };
        xfButton.onClick = [this] { if (onCrossfade) onCrossfade(); };
        fxButton.onClick = [this] { if (onFx) onFx(); };
        playButton.onRightClick = [this] { if (onLearn) onLearn ("play"); };
        xfButton.onRightClick = [this] { if (onLearn) onLearn ("xf"); };
        volume.onRightClick = [this] { if (onLearn) onLearn ("vol"); };
        xfButton.setTooltip ("Crossfade to this channel: it fades in while the others in this row fade out");
        fxButton.setTooltip ("This channel's own effects (the row's FX still apply after them)");

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
        const bool changed = newLabel != label || isOneShot != oneShot || isSmoothed != smoothed || isReversed != reversed
                          || newState != state || colour != rowColour;
        label = newLabel;
        oneShot = isOneShot;
        smoothed = isSmoothed;
        reversed = isReversed;
        state = newState;
        rowColour = colour;
        volume.setColour (juce::Slider::trackColourId, rowColour);

        const bool hasSample = state != State::empty;
        playButton.setVisible (hasSample);
        xfButton.setVisible (hasSample && ! oneShot);
        fxButton.setVisible (hasSample);
        volume.setVisible (hasSample);
        meter.setVisible (hasSample);

        fxButton.setColour (juce::TextButton::buttonColourId, hasFx ? rowColour.darker (0.15f) : Theme::raised);
        fxButton.setColour (juce::TextButton::textColourOffId, hasFx ? Theme::background : Theme::textDim);

        if (! volume.isMouseButtonDown() && std::abs (volume.getValue() - volumeDb) > 0.05)
            volume.setValue (volumeDb, juce::dontSendNotification);

        const bool lit = state == State::playing || state == State::fadingIn;
        playButton.setButtonText (oneShot ? "PLAY" : state == State::loading ? "..." : lit ? "STOP" : "PLAY");
        playButton.setColour (juce::TextButton::buttonColourId,
                              state == State::playing ? rowColour : state == State::fadingIn ? Theme::warn : Theme::raised);
        playButton.setColour (juce::TextButton::textColourOffId, lit ? Theme::background : Theme::text);

        if (changed)
        {
            resized();
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

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

        const bool lit = state == State::playing;
        const bool pending = state == State::fadingIn || state == State::loading;
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

        Theme::drawFittedName (g, label, top.reduced (2, 0), lit ? Theme::text : Theme::text.withAlpha (0.88f), 24.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        nameArea = area.removeFromTop (38);
        area.removeFromTop (2);

        auto buttons = area.removeFromTop (26);
        fxButton.setBounds (buttons.removeFromRight (28));
        buttons.removeFromRight (2);
        if (xfButton.isVisible())
        {
            xfButton.setBounds (buttons.removeFromRight (28));
            buttons.removeFromRight (2);
        }
        playButton.setBounds (buttons);
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
        if (state == State::empty && ! e.mods.isPopupMenu())
        {
            if (onPlay) onPlay();
        }
        else if ((e.mods.isPopupMenu() || nameArea.contains (e.getPosition())) && onMenu)
        {
            onMenu();
        }
    }

private:
    juce::String label;
    bool oneShot = false, smoothed = false, reversed = false, midiAssigned = false;
    State state = State::empty;
    juce::Colour rowColour = Theme::accent;
    juce::Rectangle<int> nameArea;

    RightClickable<juce::TextButton> playButton { "PLAY" }, xfButton { "XF" };
    juce::TextButton fxButton { "FX" };
    RightClickable<juce::Slider> volume;
};

// A group of channels: its name, FX and group fader on the left, then its
// channels in a grid. By default the grid is one line (wrapping if the window is
// too narrow); dragging the right edge picks a number of columns instead.
// Drag the name to move the group.
class RowView : public juce::Component
{
public:
    static constexpr int headerWidth = 110, cardWidth = 112, cardHeight = 170, cardGap = 6, padding = 6;
    static constexpr const char* dragPrefix = "group:";

    int index = 0;
    ChannelStrip strip { {}, false };

    std::function<void (int)> onPlay, onCrossfade, onMenu, onChannelFx;
    std::function<void (int, const juce::String&)> onChannelLearn;
    std::function<void (int, double)> onVolume;
    std::function<void()> onAddChannel, onHeaderMenu, onFx, onLayoutChanged;
    ColumnResizer resizer;   // its onColumnsChanged is set by the owner
    juce::Point<int> grabOffset;   // where in the group the mouse picked it up

    RowView()
    {
        addAndMakeVisible (strip);
        addAndMakeVisible (fxButton);
        addAndMakeVisible (addButton);

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
    static juce::Point<int> preferredSize (int numChannels, int maxWidth, int wantedColumns = 0)
    {
        const int items = juce::jmax (1, numChannels);
        const int cell = cardWidth + cardGap;
        const int fitting = juce::jmax (1, (maxWidth - headerWidth - 2 * padding + cardGap) / cell);
        const int columns = juce::jlimit (1, items, juce::jmin (fitting, wantedColumns > 0 ? wantedColumns : items));
        const int lines = (items + columns - 1) / columns;
        return { headerWidth + 2 * padding + columns * cell - cardGap,
                 juce::jmax (cardHeight, lines * (cardHeight + cardGap) - cardGap) + 2 * padding };
    }

    juce::Component* getCard (int slot)   { return juce::isPositiveAndBelow (slot, cards.size()) ? cards[slot] : nullptr; }
    ChannelCard* getChannelCard (int slot) { return juce::isPositiveAndBelow (slot, cards.size()) ? cards[slot] : nullptr; }

    void update (const LiveSet::Row& row, const std::function<bool (int slotId)>& isFadingIn, juce::uint32 now)
    {
        const auto newColour = Theme::rowColour (row.colour);
        fixedColumns = row.columns > 0;
        fxButton.setColour (juce::TextButton::buttonColourId, row.fx.anyOn() ? newColour.darker (0.15f) : Theme::raised);

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
                c->onVolume = [this, i] (double db) { if (onVolume) onVolume (i, db); };
                c->onLearn = [this, i] (const juce::String& control) { if (onChannelLearn) onChannelLearn (i, control); };
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

            const State state = slot.file == juce::File() ? State::empty
                              : slot.loading              ? State::loading
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
        const int line = juce::jmax (0, (pos.y - gridArea.getY()) / (cardHeight + cardGap));
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

        g.setColour (colour);
        g.setFont (Theme::heading (14.0f, 0.1f));
        g.drawFittedText (name.toUpperCase(), nameArea.reduced (8, 0), juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromLeft (headerWidth).withTrimmedLeft (6);
        nameArea = header.removeFromTop (34);
        auto buttons = header.removeFromTop (24).reduced (8, 0);
        addButton.setBounds (buttons.removeFromRight (28));
        buttons.removeFromRight (4);
        fxButton.setBounds (buttons);
        strip.setBounds (header);

        gridArea = area.reduced (padding);
        columns = juce::jmax (1, (gridArea.getWidth() + cardGap) / (cardWidth + cardGap));

        for (int i = 0; i < cards.size(); ++i)
            cards[i]->setBounds (cellBounds (i));
    }

    void mouseMove (const juce::MouseEvent& e) override       { resizer.mouseMove (*this, e); }
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

        if (e.mouseWasClicked() && nameArea.contains (e.getPosition()) && onHeaderMenu)
            onHeaderMenu();
    }

private:
    juce::Rectangle<int> cellBounds (int i) const
    {
        return { gridArea.getX() + (i % columns) * (cardWidth + cardGap),
                 gridArea.getY() + (i / columns) * (cardHeight + cardGap),
                 cardWidth, cardHeight };
    }

    juce::String name;
    juce::Colour colour = Theme::accent;
    juce::Rectangle<int> nameArea, gridArea;
    int columns = 1, dropMarker = -1, groupDropSide = 0;
    bool fixedColumns = false;
    juce::OwnedArray<ChannelCard> cards;
    juce::TextButton addButton { "+" };
    juce::TextButton fxButton { "FX" };
};

