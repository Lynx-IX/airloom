#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "Theme.h"

// The strip of scenes above the groups: one button per saved scene (click to move the
// whole mix there), + SCENE to save the current mix as a new one. The scene last
// recalled is lit, with a bar filling along its bottom while it morphs.
class SceneBar : public juce::Component
{
public:
    std::function<void (int scene)> onRecall, onSceneMenu;
    std::function<void()> onAdd, onBarMenu;

    SceneBar()
    {
        addAndMakeVisible (addButton);
        addButton.setWantsKeyboardFocus (false);
        addButton.setTooltip ("Save the current mix - which loops play and every channel and group level - as a scene. "
                              "Right-click for MIDI buttons to step through the scenes.");
        addButton.onClick = [this] { if (onAdd) onAdd(); };
        addButton.onRightClick = [this] { if (onBarMenu) onBarMenu(); };
    }

    // 'progress' 0..1 while the active scene morphs in, or < 0 when it's done.
    void setScenes (const juce::StringArray& newNames, int newActive, float newProgress)
    {
        if (newNames != names)
        {
            names = newNames;
            buttons.clear();
            for (int i = 0; i < names.size(); ++i)
            {
                auto* b = buttons.add (new RightClickable<juce::TextButton> (names[i]));
                b->setWantsKeyboardFocus (false);
                b->setTooltip ("Click to move to this scene. Right-click to update, rename, set its morph time or assign MIDI.");
                b->onClick = [this, i] { if (onRecall) onRecall (i); };
                b->onRightClick = [this, i] { if (onSceneMenu) onSceneMenu (i); };
                addAndMakeVisible (b);
            }
            active = -2;   // recolour below
            resized();
        }

        if (newActive != active || std::abs (newProgress - progress) > 0.005f)
        {
            active = newActive;
            progress = newProgress;
            for (int i = 0; i < buttons.size(); ++i)
            {
                const bool on = i == active;
                buttons[i]->setColour (juce::TextButton::buttonColourId, on ? Theme::accent.darker (0.3f) : Theme::raised);
                buttons[i]->setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::text);
            }
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::heading (10.5f, 0.14f));
        g.drawText ("SCENES", getLocalBounds().removeFromLeft (labelWidth), juce::Justification::centredLeft);

        if (names.isEmpty())
        {
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Set up a mix, then + SCENE to save it. Scenes move every loop and level there in one press.",
                        getLocalBounds().withTrimmedLeft (addButton.getRight() + 14), juce::Justification::centredLeft, true);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (progress < 0.0f || ! juce::isPositiveAndBelow (active, buttons.size()))
            return;

        auto r = buttons[active]->getBounds().toFloat().removeFromBottom (4.0f).reduced (3.0f, 0.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (Theme::warn);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * progress), 2.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.x < labelWidth && onBarMenu)
            onBarMenu();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (labelWidth);

        // Buttons share what's left after + SCENE, up to 150 wide each.
        const int addWidth = 96, gap = 6;
        const int n = buttons.size();
        const int each = n == 0 ? 0 : juce::jlimit (60, 150, (r.getWidth() - addWidth - gap * n) / n);
        for (auto* b : buttons)
        {
            b->setBounds (r.removeFromLeft (each));
            r.removeFromLeft (gap);
        }
        addButton.setBounds (r.removeFromLeft (addWidth));
    }

private:
    static constexpr int labelWidth = 70;
    juce::StringArray names;
    juce::OwnedArray<RightClickable<juce::TextButton>> buttons;
    RightClickable<juce::TextButton> addButton { "+ SCENE" };
    int active = -1;
    float progress = -1.0f;
};
