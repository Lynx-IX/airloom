#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// Lets a group be resized by dragging its right edge. The width snaps to whole
// columns of channels; double-clicking the edge goes back to automatic (0).
// The owning component forwards its mouse events here first.
class ColumnResizer
{
public:
    static constexpr int edgeWidth = 8;

    std::function<int (int width)> columnsForWidth;
    std::function<void (int columns)> onColumnsChanged;

    bool isOverEdge (const juce::Component& c, juce::Point<int> pos) const
    {
        return pos.x >= c.getWidth() - edgeWidth;
    }

    void mouseMove (juce::Component& c, const juce::MouseEvent& e)
    {
        c.setMouseCursor (isOverEdge (c, e.getPosition()) ? juce::MouseCursor::LeftRightResizeCursor
                                                          : juce::MouseCursor::NormalCursor);
    }

    bool mouseDown (juce::Component& c, const juce::MouseEvent& e)
    {
        dragging = ! e.mods.isPopupMenu() && isOverEdge (c, e.getPosition());
        startWidth = c.getWidth();
        lastColumns = -1;
        return dragging;
    }

    bool mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging)
            return false;

        const int columns = columnsForWidth ? columnsForWidth (startWidth + e.getDistanceFromDragStartX()) : 0;
        if (columns != lastColumns)
        {
            lastColumns = columns;
            if (onColumnsChanged)
                onColumnsChanged (columns);
        }
        return true;
    }

    bool mouseUp()
    {
        const bool was = dragging;
        dragging = false;
        return was;
    }

    bool mouseDoubleClick (juce::Component& c, const juce::MouseEvent& e)
    {
        if (! isOverEdge (c, e.getPosition()))
            return false;
        if (onColumnsChanged)
            onColumnsChanged (0);
        return true;
    }

    // A small grip on the right edge so it's clear the group can be stretched.
    static void paintGrip (juce::Graphics& g, const juce::Component& c, juce::Colour colour, bool fixedColumns)
    {
        const float x = (float) c.getWidth() - 4.0f, y = (float) c.getHeight() / 2.0f;
        g.setColour (colour.withAlpha (fixedColumns ? 0.8f : 0.35f));
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (x - 1.5f, y + (float) i * 6.0f - 1.5f, 3.0f, 3.0f);
    }

private:
    bool dragging = false;
    int startWidth = 0, lastColumns = -1;
};
