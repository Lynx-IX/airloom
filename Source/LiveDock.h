#pragma once

#include <JuceHeader.h>
#include "LiveGroupView.h"
#include "Theme.h"

// The LIVE dock along the bottom: the input and instrument groups side by side, under the loop
// groups and the samples list. Folded (the button, or click the bar), every group shows as a
// collapsed bar of named tiles - one per input or instrument - so it takes little room but still
// says what's there; wider than the window, it scrolls sideways. Drag a group by its name to
// put it elsewhere in the row.
class LiveDock : public juce::Component
{
public:
    static constexpr int barHeight = 28, gap = 8, margin = 6;
    static constexpr const char* foldedTip = "Show the inputs and instruments";

    juce::Component content;   // the groups, laid out by arrange()
    std::function<void()> onToggleFold;

    LiveDock()
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (8);

        addAndMakeVisible (foldButton);
        foldButton.setWantsKeyboardFocus (false);
        foldButton.onClick = [this] { if (onToggleFold) onToggleFold(); };
        setFolded (false);
    }

    void setFolded (bool shouldFold)
    {
        folded = shouldFold;
        foldButton.setButtonText (folded ? "^" : "v");
        foldButton.setTooltip (folded ? foldedTip : "Fold the inputs and instruments away");
        resized();
        repaint();
    }

    bool isFolded() const   { return folded; }

    // What the bar says when folded (e.g. "Inputs 2  -  Instruments 3").
    void setSummary (const juce::String& text)
    {
        if (text != summary) { summary = text; repaint(); }
    }

    // Puts the groups (already sized) in a row; returns true if the dock wants a different height.
    bool arrange (const juce::Array<LiveGroupView*>& groups)
    {
        int x = margin, tallest = 0;
        for (auto* g : groups)
        {
            g->setTopLeftPosition (x, margin);
            x += g->getWidth() + gap;
            tallest = juce::jmax (tallest, g->getHeight());
        }
        content.setSize (juce::jmax (x - gap + margin, viewport.getMaximumVisibleWidth()), tallest + 2 * margin);

        const int wanted = barHeight + tallest + 2 * margin + (content.getWidth() > viewport.getWidth() ? 8 : 0);
        const bool changed = wanted != contentHeight;
        contentHeight = wanted;
        return changed;
    }

    int preferredHeight() const   { return juce::jmax (barHeight, contentHeight); }

    // The position in the row a group dropped at this point (dock coordinates) would take.
    int dropIndexAt (juce::Point<int> pos, const juce::Array<LiveGroupView*>& groups, const LiveGroupView* moving) const
    {
        const int x = content.getLocalPoint (this, pos).x;
        int index = 0;
        for (auto* g : groups)
            if (g != moving && g->getBounds().getCentreX() < x)
                ++index;
        return index;
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawSurface (g, getLocalBounds().toFloat(), Theme::panel.darker (0.15f), 8.0f);

        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (10, 0);
        g.setColour (Theme::text);
        g.setFont (Theme::heading (13.0f, 0.14f));
        g.drawText ("LIVE", bar, juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (folded ? summary + "   -   folded: click a name to mute it, ^ to open" : juce::String ("Inputs and instruments"),
                    bar.withTrimmedLeft (52).withTrimmedRight (36), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (barHeight);
        foldButton.setBounds (bar.removeFromRight (34).reduced (4, 3));
        viewport.setBounds (area);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // The bar itself folds and unfolds too.
        if (e.mouseWasClicked() && e.getPosition().y < barHeight && onToggleFold)
            onToggleFold();
    }

private:
    juce::Viewport viewport;
    juce::TextButton foldButton;
    juce::String summary;
    bool folded = false;
    int contentHeight = barHeight;
};
