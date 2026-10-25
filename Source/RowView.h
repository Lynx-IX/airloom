#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
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
    LevelMeter meter;

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

        fxButton.setColour (juce::TextButton::buttonColourId, hasFx ? rowColour.withAlpha (0.7f) : Theme::panelEdge);
        fxButton.setColour (juce::TextButton::textColourOffId, hasFx ? Theme::background : Theme::textDim);

        if (! volume.isMouseButtonDown() && std::abs (volume.getValue() - volumeDb) > 0.05)
            volume.setValue (volumeDb, juce::dontSendNotification);

        const bool lit = state == State::playing || state == State::fadingIn;
        playButton.setButtonText (oneShot ? "PLAY" : state == State::loading ? "..." : lit ? "STOP" : "PLAY");
        playButton.setColour (juce::TextButton::buttonColourId,
                              state == State::playing ? rowColour : state == State::fadingIn ? Theme::warn : Theme::panelEdge);
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
        g.setColour (Theme::background.interpolatedWith (rowColour, lit ? 0.18f : 0.06f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (lit ? rowColour : state == State::fadingIn || state == State::loading ? Theme::warn
                                                                                           : rowColour.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 5.0f, state == State::idle ? 1.0f : 2.0f);

        // Small tags: plays once, reversed, smooth loop point.
        juce::StringArray tags;
        if (oneShot)               tags.add ("1x");
        if (reversed)              tags.add ("REV");
        if (smoothed && ! oneShot) tags.add ("~");

        auto top = nameArea;
        if (! tags.isEmpty())
        {
            g.setColour (rowColour);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            const auto text = tags.joinIntoString (" ");
            g.drawText (text, top.removeFromBottom (11), juce::Justification::centredRight);
        }

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (label, top, juce::Justification::centred, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        nameArea = area.removeFromTop (30);
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
    bool oneShot = false, smoothed = false, reversed = false;
    State state = State::empty;
    juce::Colour rowColour = Theme::accent;
    juce::Rectangle<int> nameArea;

    juce::TextButton playButton { "PLAY" }, xfButton { "XF" }, fxButton { "FX" };
    juce::Slider volume;
};

// A group of channels: its name, FX and group fader on the left, then its
// channels in a grid just big enough for them. Small groups sit side by side;
// big ones wrap their channels onto more lines. Drag the name to move it.
class RowView : public juce::Component
{
public:
    static constexpr int headerWidth = 110, cardWidth = 112, cardHeight = 170, cardGap = 6, padding = 6;
    static constexpr const char* dragPrefix = "group:";

    int index = 0;
    ChannelStrip strip { {}, false };

    std::function<void (int)> onPlay, onCrossfade, onMenu, onChannelFx;
    std::function<void (int, double)> onVolume;
    std::function<void()> onAddChannel, onHeaderMenu, onFx, onLayoutChanged;

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
    }

    // The size a group with this many channels wants, given the widest it may be.
    static juce::Point<int> preferredSize (int numChannels, int maxWidth)
    {
        const int items = numChannels + 1;   // the channels, then the + button
        const int cell = cardWidth + cardGap;
        const int columns = juce::jlimit (1, items, (maxWidth - headerWidth - 2 * padding + cardGap) / cell);
        const int lines = (items + columns - 1) / columns;
        return { headerWidth + 2 * padding + columns * cell - cardGap,
                 juce::jmax (cardHeight, lines * (cardHeight + cardGap) - cardGap) + 2 * padding };
    }

    juce::Component* getCard (int slot)   { return juce::isPositiveAndBelow (slot, cards.size()) ? cards[slot] : nullptr; }

    void update (const LiveSet::Row& row, const std::function<bool (int slotId)>& isFadingIn, juce::uint32 now)
    {
        const auto newColour = Theme::rowColour (row.colour);
        fxButton.setColour (juce::TextButton::buttonColourId, row.fx.anyOn() ? newColour.withAlpha (0.6f) : Theme::panelEdge);

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
        g.setColour (Theme::panel.interpolatedWith (colour, 0.05f));
        g.fillRoundedRectangle (bounds, 6.0f);

        // Coloured edge on the left identifies the group at a glance.
        g.setColour (colour);
        g.fillRoundedRectangle (bounds.withWidth (6.0f), 3.0f);

        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawFittedText (name.toUpperCase(), nameArea.reduced (8, 0), juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromLeft (headerWidth).withTrimmedLeft (6);
        nameArea = header.removeFromTop (34);
        fxButton.setBounds (header.removeFromTop (24).reduced (8, 0));
        strip.setBounds (header);

        gridArea = area.reduced (padding);
        columns = juce::jmax (1, (gridArea.getWidth() + cardGap) / (cardWidth + cardGap));

        for (int i = 0; i < cards.size(); ++i)
            cards[i]->setBounds (cellBounds (i));

        addButton.setBounds (cellBounds (cards.size()).withWidth (36));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Dragging the group's name picks the whole group up.
        if (! nameArea.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
                container->startDragging (dragPrefix + juce::String (index), this);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
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
    juce::OwnedArray<ChannelCard> cards;
    juce::TextButton addButton { "+" };
    juce::TextButton fxButton { "FX" };
};

