#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "GroupLayout.h"
#include "GroupResize.h"
#include "InputsView.h"
#include "Theme.h"

// A live group on screen: like a loop group, its name, FX, + and group fader on the left,
// then its input or instrument strips in a grid. Drag the name to move it, click the name
// for its menu; the right edge sets how many strips sit on a line. The arrow by the name
// collapses it to a short bar: name, MUTE and FX, and a tile per strip (lit while the strip
// is live; a click mutes or unmutes it).
class LiveGroupView : public juce::Component
{
public:
    static constexpr int headerWidth = 110, stripGap = 6, padding = 6;
    static constexpr int stripWidth = InputsView::stripWidth, stripHeight = InputsView::stripHeight;
    static constexpr int tileHeight = 28, collapsedHeight = 66;   // a collapsed group
    static constexpr int lineGap = GroupLayout::lineGap (padding); // between lines of strips

    struct Tile { juce::String name; bool live = false; };
    static constexpr const char* dragPrefix = "group:live";

    int index = 0;
    ChannelStrip strip { {}, false };
    juce::TextButton fxButton { "FX" }, addButton { "+" };
    std::function<void()> onHeaderMenu, onFx, onAdd, onToggleCollapsed;
    std::function<void (int tile)> onTile;
    ColumnResizer resizer;          // its onColumnsChanged is set by the owner
    juce::Point<int> grabOffset;    // where in the group the mouse picked it up

    LiveGroupView()
    {
        addAndMakeVisible (strip);
        addAndMakeVisible (fxButton);
        addAndMakeVisible (addButton);

        fxButton.setTooltip ("Effects for everything in this group");
        fxButton.setWantsKeyboardFocus (false);
        fxButton.onClick = [this] { if (onFx) onFx(); };
        addButton.setWantsKeyboardFocus (false);
        addButton.onClick = [this] { if (onAdd) onAdd(); };

        resizer.columnsForWidth = [this] (int width)
        {
            const int cell = stripWidth + stripGap;
            const int fitting = (width - headerWidth - 2 * padding + stripGap + cell / 2) / cell;
            return juce::jlimit (1, juce::jmax (1, (int) strips.size()), fitting);
        };
    }

    // The size a group with this many strips wants, given the widest it may be.
    // columns 0 means automatic: everything on one line if it fits.
    // A collapsed group keeps its width (so the layout doesn't jump) but is only as tall as its tiles.
    static juce::Point<int> preferredSize (int numStrips, int maxWidth, int wantedColumns = 0, bool isCollapsed = false)
    {
        const int items = juce::jmax (1, numStrips);
        const int cell = stripWidth + stripGap;
        const int fitting = juce::jmax (1, (maxWidth - headerWidth - 2 * padding + stripGap) / cell);
        const int columns = juce::jlimit (1, items, juce::jmin (fitting, wantedColumns > 0 ? wantedColumns : items));
        const int lines = (items + columns - 1) / columns;
        const int height = isCollapsed ? juce::jmax (collapsedHeight - 2 * padding, lines * (tileHeight + stripGap) - stripGap)
                                       : lines * (stripHeight + lineGap) - lineGap;
        return { headerWidth + 2 * padding + columns * cell - stripGap, height + 2 * padding };
    }

    void setCollapsed (bool shouldCollapse)
    {
        if (shouldCollapse == collapsed)
            return;
        collapsed = shouldCollapse;
        strip.setMuteOnly (collapsed);
        addButton.setVisible (! collapsed);
        resized();
        repaint();
    }

    bool isCollapsed() const   { return collapsed; }

    void setTiles (const std::vector<Tile>& newTiles)
    {
        const bool same = newTiles.size() == tiles.size()
                       && std::equal (newTiles.begin(), newTiles.end(), tiles.begin(),
                                      [] (const Tile& a, const Tile& b) { return a.name == b.name && a.live == b.live; });
        if (! same)
        {
            tiles = newTiles;
            if (collapsed)
                repaint();
        }
    }

    void setContent (const juce::String& newName, juce::Colour newColour, bool fxOn, bool fixed, bool holdsInstruments)
    {
        fxButton.setColour (juce::TextButton::buttonColourId, fxOn ? newColour.darker (0.15f) : Theme::raised);
        addButton.setTooltip (holdsInstruments ? "Add an instrument to this group" : "Add an interface input to this group");

        if (newName != name || newColour != colour || fixed != fixedColumns || holdsInstruments != instruments)
        {
            name = newName;
            colour = newColour;
            fixedColumns = fixed;
            instruments = holdsInstruments;
            strip.fader.setColour (juce::Slider::trackColourId, colour);
            repaint();
        }
    }

    // The strips this group shows (owned by InputsView); they become this view's children.
    void setStrips (const juce::Array<juce::Component*>& members)
    {
        if (members == strips)
            return;

        // Let go of strips no longer here. Goes by the current children, not 'strips': a strip
        // that's been deleted (its input removed) has already left and mustn't be touched.
        for (auto* c : juce::Array<juce::Component*> (getChildren()))
            if (dynamic_cast<SourceStrip*> (c) != nullptr && ! members.contains (c))
                removeChildComponent (c);

        strips = members;
        for (auto* s : strips)
            addChildComponent (s);   // shown by resized() unless collapsed
        resized();
        repaint();
    }

    int getNumStrips() const   { return strips.size(); }

    // Where a strip dropped at this point (in this view's coordinates) would be inserted.
    int dropIndexAt (juce::Point<int> pos) const
    {
        if (collapsed)
            return strips.size();
        const int cell = stripWidth + stripGap;
        const int column = juce::jlimit (0, columns, (pos.x - gridArea.getX() + cell / 2) / cell);
        const int line = juce::jmax (0, (pos.y - gridArea.getY()) / (stripHeight + lineGap));
        return juce::jlimit (0, strips.size(), line * columns + column);
    }

    // Shows (index >= 0) or hides the insertion line while a strip is dragged.
    void setDropMarker (int insertAt)
    {
        if (insertAt != dropMarker) { dropMarker = insertAt; repaint(); }
    }

    // Shows which side a dragged group would land on: -1 before, 1 after, 0 hidden.
    void setGroupDropSide (int side)
    {
        if (side != groupDropSide) { groupDropSide = side; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        Theme::drawSurface (g, bounds, Theme::panel, 8.0f);
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
        g.drawFittedText (name.toUpperCase(), nameArea.withLeft (arrowArea.getRight()).reduced (2, 0),
                          juce::Justification::centredLeft, collapsed ? 1 : 2);

        if (collapsed)
        {
            for (int i = 0; i < (int) tiles.size(); ++i)
            {
                const auto r = cellBounds (i).toFloat().reduced (1.0f);
                const bool live = tiles[(size_t) i].live;
                Theme::drawSurface (g, r, live ? colour : Theme::raised.interpolatedWith (colour, 0.06f), 5.0f);
                g.setColour (live ? Theme::background : Theme::textDim);
                g.setFont (Theme::heading (12.0f));
                g.drawFittedText (tiles[(size_t) i].name, r.toNearestInt().reduced (7, 2), juce::Justification::centred, 1, 0.8f);
            }
            return;
        }

        if (strips.isEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (instruments ? "Empty - press + to add an instrument" : "Empty - press + to add an input",
                              gridArea.reduced (8), juce::Justification::centred, 3);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (strip.isMutedShown())
        {
            // Muted: its strips fade back.
            g.setColour (Theme::panel.withAlpha (0.55f));
            g.fillRoundedRectangle (gridArea.toFloat().expanded (2.0f), 6.0f);
        }

        ColumnResizer::paintGrip (g, *this, colour, fixedColumns);

        if (dropMarker >= 0)
        {
            g.setColour (colour);
            if (collapsed || strips.isEmpty())
            {
                g.drawRoundedRectangle (gridArea.toFloat().reduced (1.0f), 6.0f, 2.0f);   // goes in here
            }
            else
            {
                const auto cell = cellBounds (dropMarker);
                g.fillRoundedRectangle ((float) (cell.getX() - stripGap / 2) - 2.0f, (float) cell.getY(), 4.0f, (float) cell.getHeight(), 2.0f);
            }
        }

        if (groupDropSide != 0)
        {
            g.setColour (colour);
            const float x = groupDropSide < 0 ? 0.0f : (float) getWidth() - 5.0f;
            g.fillRoundedRectangle (x, 0.0f, 5.0f, (float) getHeight(), 2.0f);
        }
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

        gridArea = area.reduced (padding);
        columns = juce::jmax (1, (gridArea.getWidth() + stripGap) / (stripWidth + stripGap));
        if (collapsed)
        {
            // Tiles sit in the middle of the bar.
            const int lines = (juce::jmax (1, (int) tiles.size()) + columns - 1) / columns;
            const int used = lines * (tileHeight + stripGap) - stripGap;
            gridArea = gridArea.withTrimmedTop (juce::jmax (0, (gridArea.getHeight() - used) / 2));
        }

        for (int i = 0; i < strips.size(); ++i)
        {
            strips[i]->setVisible (! collapsed);
            strips[i]->setBounds (cellBounds (i));
        }
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
    void mouseDown (const juce::MouseEvent& e) override       { resizer.mouseDown (*this, e); }

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

        if (! e.mouseWasClicked() || e.mods.isPopupMenu())
        {
            if (e.mouseWasClicked() && nameArea.contains (e.getPosition()) && onHeaderMenu)
                onHeaderMenu();
            return;
        }

        if (arrowArea.contains (e.getPosition()))
        {
            if (onToggleCollapsed) onToggleCollapsed();
            return;
        }

        if (nameArea.contains (e.getPosition()))
        {
            if (onHeaderMenu) onHeaderMenu();
            return;
        }

        if (collapsed)
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (cellBounds (i).contains (e.getPosition()) && onTile)
                {
                    onTile (i);
                    return;
                }
    }

private:
    juce::Rectangle<int> cellBounds (int i) const
    {
        const int h = collapsed ? tileHeight : stripHeight;
        return { gridArea.getX() + (i % columns) * (stripWidth + stripGap),
                 gridArea.getY() + (i / columns) * (h + (collapsed ? stripGap : lineGap)), stripWidth, h };
    }

    juce::String name;
    juce::Colour colour = Theme::accent;
    juce::Rectangle<int> nameArea, gridArea, arrowArea;
    juce::Array<juce::Component*> strips;
    std::vector<Tile> tiles;
    bool fixedColumns = false, instruments = false, collapsed = false, arrowHover = false;
    int groupDropSide = 0, columns = 1, dropMarker = -1;
};
