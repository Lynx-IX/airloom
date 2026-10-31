#pragma once

#include <JuceHeader.h>

// Where the groups go on the page. Each stays exactly where it was put; the only thing that
// moves one is an overlap: going top to bottom, a group that would overlap one above it slides
// down until it's clear. So a group dropped onto another, or one that grows into the next,
// pushes just what's in the way down. Groups not placed yet go in along the bottom.
namespace GroupLayout
{
    // The space between groups. Inside a group, lines of channels are spaced so that a group
    // n lines tall is exactly as tall as n one-line groups stacked (its padding, this gap, the
    // next one's padding), so short groups line up beside a tall one.
    constexpr int gap = 8;
    constexpr int lineGap (int padding)   { return 2 * padding + gap; }

    // want.x < 0: not placed yet. size: as it is now; fullSize: expanded (0 = the same as size).
    struct Group { int item; juce::Point<int> want; juce::Point<int> size; juce::Point<int> fullSize {}; };

    inline std::map<int, juce::Rectangle<int>> arrange (std::vector<Group> groups, int width, int gap,
                                                        int moving = std::numeric_limits<int>::min(),
                                                        juce::Point<int> movingTo = {})
    {
        for (auto& g : groups)
            if (g.item == moving)
                g.want = movingTo;

        std::vector<Group> unplaced;
        for (auto it = groups.begin(); it != groups.end();)
        {
            if (it->want.x < 0) { unplaced.push_back (*it); it = groups.erase (it); }
            else                ++it;
        }

        // Top to bottom, then left to right; a group being dropped wins a tie.
        std::stable_sort (groups.begin(), groups.end(), [moving] (const Group& a, const Group& b)
        {
            if (a.want.y != b.want.y) return a.want.y < b.want.y;
            if ((a.item == moving) != (b.item == moving)) return a.item == moving;
            return a.want.x < b.want.x;
        });

        std::map<int, juce::Rectangle<int>> placed;
        auto place = [&] (const Group& g, int x, int y)
        {
            x = juce::jlimit (0, juce::jmax (0, width - g.size.x), x);
            y = juce::jmax (0, y);
            for (bool moved = true; moved;)
            {
                moved = false;
                const juce::Rectangle<int> mine (x, y, g.size.x, g.size.y);
                for (const auto& [other, r] : placed)
                    if (mine.intersects (r.expanded (gap - 1)))
                    {
                        y = r.getBottom() + gap;
                        moved = true;
                    }
            }
            placed[g.item] = { x, y, g.size.x, g.size.y };
        };

        for (const auto& g : groups)
            place (g, g.want.x, g.want.y);

        for (const auto& g : unplaced)
        {
            int bottom = 0;
            for (const auto& [other, r] : placed)
                bottom = juce::jmax (bottom, r.getBottom() + gap);
            place (g, 0, bottom);
        }

        return placed;
    }

    // The page with collapsed groups closed up. First everything is arranged as if expanded (the
    // layout you make by dragging); then, top to bottom, a group moves up by as much as the group
    // just above it shrank (or moved up), so collapsing a group pulls up what's under it and
    // expanding it pushes them back. It never moves up into anything else above it. A group being
    // dropped lands where it's dropped. 'wants' (if given) gets the position to keep for each group
    // so the next layout comes out the same: its place in the expanded layout.
    inline std::map<int, juce::Rectangle<int>> arrangeCollapsed (const std::vector<Group>& groups, int width, int gap,
                                                                 int moving = std::numeric_limits<int>::min(),
                                                                 juce::Point<int> movingTo = {},
                                                                 std::map<int, juce::Point<int>>* wants = nullptr)
    {
        std::vector<Group> expanded;
        std::map<int, juce::Point<int>> actual;
        for (const auto& g : groups)
        {
            const auto full = g.fullSize.x > 0 ? g.fullSize : g.size;
            expanded.push_back ({ g.item, g.want, full });
            actual[g.item] = g.size;
        }
        const auto full = arrange (std::move (expanded), width, gap, moving, movingTo);

        std::vector<std::pair<int, juce::Rectangle<int>>> order (full.begin(), full.end());
        std::stable_sort (order.begin(), order.end(), [] (const auto& a, const auto& b) { return a.second.getY() < b.second.getY(); });

        std::map<int, juce::Rectangle<int>> placed;
        auto sideBySide = [gap] (const juce::Rectangle<int>& a, const juce::Rectangle<int>& b)
        {
            return a.getX() < b.getRight() + gap && b.getX() < a.getRight() + gap;
        };

        for (const auto& [item, f] : order)
        {
            // The nearest group above it (at full size) sets how far it moves up.
            int nearestBottom = -1, pull = 0;
            for (const auto& [other, done] : placed)
            {
                const auto& otherFull = full.at (other);
                if (otherFull.getBottom() <= f.getY() + gap / 2 && sideBySide (otherFull, f) && otherFull.getBottom() > nearestBottom)
                {
                    nearestBottom = otherFull.getBottom();
                    pull = otherFull.getBottom() - done.getBottom();
                }
            }

            pull = juce::jmax (0, pull);
            const auto size = actual[item];
            const bool dropped = item == moving;
            int y = juce::jmax (0, f.getY() - (dropped ? 0 : pull));
            if (wants != nullptr)
                (*wants)[item] = { f.getX(), f.getY() + (dropped ? pull : 0) };
            for (bool moved = true; moved;)
            {
                moved = false;
                const juce::Rectangle<int> mine (f.getX(), y, size.x, size.y);
                for (const auto& [other, r] : placed)
                    if (mine.intersects (r.expanded (gap - 1)))
                    {
                        y = r.getBottom() + gap;
                        moved = true;
                    }
            }
            placed[item] = { f.getX(), y, size.x, size.y };
        }
        return placed;
    }

    // A dragged group's top-left corner, snapped on each axis to a nearby edge of another group
    // (or the page's top and left): lined up with its side or top, just beside or below it, or
    // flush with its far side. Further than 'distance' from any edge, it stays where it is.
    inline juce::Point<int> snap (juce::Point<int> pos, juce::Point<int> size, const juce::Array<juce::Rectangle<int>>& others,
                                  int gap, int distance = 24)
    {
        juce::Array<int> xs { 0 }, ys { 0 };
        for (const auto& c : others)
        {
            xs.addArray ({ c.getX(), c.getRight() + gap, c.getRight() - size.x, c.getX() - gap - size.x });
            ys.addArray ({ c.getY(), c.getBottom() + gap, c.getBottom() - size.y, c.getY() - gap - size.y });
        }

        auto nearest = [distance] (int value, const juce::Array<int>& candidates)
        {
            int best = value, bestDistance = distance + 1;
            for (int candidate : candidates)
                if (std::abs (value - candidate) < bestDistance) { bestDistance = std::abs (value - candidate); best = candidate; }
            return best;
        };

        return { juce::jmax (0, nearest (pos.x, xs)), juce::jmax (0, nearest (pos.y, ys)) };
    }
}
