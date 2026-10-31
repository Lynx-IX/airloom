#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "GroupResize.h"
#include "LiveSet.h"
#include "Theme.h"

// One live source - an interface input or a hosted instrument: name, a line of
// detail, three buttons (MON or EDIT, ARM, FX), then a fader with its level
// meter and a mute.
class SourceStrip : public juce::Component
{
public:
    std::function<void()> onMenu, onFirstButton, onArm, onFx;
    juce::String dragId;   // what a drag of this strip carries ("strip:in:<i>" or "strip:inst:<i>")
    static constexpr const char* dragPrefix = "strip:";
    ChannelStrip strip { {}, false };

    explicit SourceStrip (const juce::String& firstButtonText, const juce::String& firstButtonTip)
        : firstButton (firstButtonText)
    {
        for (auto* b : { &firstButton, &armButton, &fxButton })
        {
            addAndMakeVisible (b);
            b->setWantsKeyboardFocus (false);
        }
        addAndMakeVisible (strip);

        firstButton.setTooltip (firstButtonTip);
        armButton.setTooltip ("Arm: feeds the looper when you press REC or DUB");
        firstButton.onClick = [this] { if (onFirstButton) onFirstButton(); };
        armButton.onClick = [this] { if (onArm) onArm(); };
        fxButton.onClick = [this] { if (onFx) onFx(); };
    }

    // A row of one-click choices under the buttons (the instrument's keys); the active one lights up.
    std::function<void (int)> onChoice, onChoiceMenu;   // onChoiceMenu: a key button right-clicked

    void setChoices (const juce::StringArray& labels)
    {
        choiceButtons.clear();
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = choiceButtons.add (new RightClickable<juce::TextButton> (labels[i]));
            b->setWantsKeyboardFocus (false);
            b->onClick = [this, i] { if (onChoice) onChoice (i); };
            b->onRightClick = [this, i] { if (onChoiceMenu) onChoiceMenu (i); };
            addAndMakeVisible (b);
        }
        resized();
    }

    void setActiveChoice (int index)
    {
        for (int i = 0; i < choiceButtons.size(); ++i)
        {
            const bool on = i == index;
            choiceButtons[i]->setColour (juce::TextButton::buttonColourId, on ? accent : Theme::raised);
            choiceButtons[i]->setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::text);
        }
    }

    void setContent (const juce::String& newName, const juce::String& newDetail, bool firstOn, bool armed, bool fxOn,
                     juce::Colour colour)
    {
        name = newName;
        detail = newDetail;
        accent = colour;

        auto light = [] (juce::TextButton& b, bool on, juce::Colour c)
        {
            b.setColour (juce::TextButton::buttonColourId, on ? c : Theme::raised);
            b.setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::textDim);
        };
        light (firstButton, firstOn, colour);
        light (armButton, armed, Theme::danger);
        light (fxButton, fxOn, colour.darker (0.15f));
        strip.fader.setColour (juce::Slider::trackColourId, colour);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Theme::drawSurface (g, r, Theme::raised.interpolatedWith (accent, 0.035f), 6.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (r.reduced (10.0f, 0.0f).withHeight (3.0f), 1.5f);

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        g.drawFittedText (name, nameArea, juce::Justification::centred, 1);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (detail, detailArea, juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (5);
        nameArea = area.removeFromTop (18);
        detailArea = area.removeFromTop (15);
        area.removeFromTop (3);

        auto buttons = area.removeFromTop (24);
        const int w = (buttons.getWidth() - 6) / 3;
        firstButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (3);
        armButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (3);
        fxButton.setBounds (buttons);

        if (! choiceButtons.isEmpty())
        {
            area.removeFromTop (3);
            auto row = area.removeFromTop (24);
            const int cw = (row.getWidth() - 3 * (choiceButtons.size() - 1)) / choiceButtons.size();
            for (auto* b : choiceButtons)
            {
                b->setBounds (row.removeFromLeft (cw));
                row.removeFromLeft (3);
            }
        }

        strip.setBounds (area);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (strip.isMutedShown())
        {
            // Muted: everything but the MUTE key fades back.
            g.setColour (Theme::raised.withAlpha (0.5f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().withBottom ((float) strip.getY() + (float) strip.muteButton.getY() - 2.0f), 6.0f);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Dragging the strip (by its name or background) picks it up to move it.
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6 || dragId.isEmpty())
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
                container->startDragging (dragId, this);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && (e.mods.isPopupMenu() || nameArea.getUnion (detailArea).contains (e.getPosition())) && onMenu)
            onMenu();
    }

private:
    juce::String name, detail;
    juce::Colour accent = Theme::accent;
    juce::Rectangle<int> nameArea, detailArea;
    juce::TextButton firstButton, armButton { "ARM" }, fxButton { "FX" };
    juce::OwnedArray<RightClickable<juce::TextButton>> choiceButtons;
};

// Owns every input and instrument strip, made and refreshed from the set. The strips
// are shown by the live groups (LiveGroupView) they belong to, which take them as children.
class InputsView
{
public:
    static constexpr int stripWidth = 160, stripHeight = 272;

    juce::OwnedArray<SourceStrip> inputStrips;
    juce::OwnedArray<SourceStrip> instrumentStrips;

    // Called whenever input or instrument strips are created, so the owner can wire them up.
    std::function<void (int index, SourceStrip&)> onInputStripCreated, onInstrumentStripCreated;
    std::function<void()> onStripsChanged;   // strips were made or removed: the groups need laying out again

    int getNumStrips() const   { return inputStrips.size() + instrumentStrips.size(); }

    // colourOf (isInstrument, index): the colour of the group the strip is in.
    void update (const LiveSet& set, const std::function<juce::String (int)>& instrumentName,
                 const std::function<juce::String (int)>& instrumentDetail,
                 const std::function<juce::Colour (bool, int)>& colourOf)
    {
        bool changed = false;

        if (inputStrips.size() != (int) set.inputs.size())
        {
            inputStrips.clear();
            for (int i = 0; i < (int) set.inputs.size(); ++i)
            {
                auto* s = inputStrips.add (new SourceStrip ("MON", "Monitor: hear this input through the speakers. Off by default so a mic can't feed back."));
                s->strip.fader.setValue (set.inputs[(size_t) i].faderDb, juce::dontSendNotification);
                s->strip.muteButton.setToggleState (set.inputs[(size_t) i].muted, juce::dontSendNotification);
                if (onInputStripCreated)
                    onInputStripCreated (i, *s);
            }
            changed = true;
        }

        for (int i = 0; i < inputStrips.size(); ++i)
        {
            const auto& in = set.inputs[(size_t) i];
            const auto source = "In " + juce::String (in.channel + 1) + (in.stereo ? "+" + juce::String (in.channel + 2) + " stereo" : juce::String());
            const auto plugin = in.insertName.isNotEmpty() ? "  -  " + in.insertName : juce::String();
            inputStrips[i]->setContent (in.name, source + plugin + (in.recordWithFx ? juce::String() : "  -  records dry"),
                                        in.monitor, in.armed, in.fx.anyOn(), colourOf (false, i));
        }

        if (instrumentStrips.size() != (int) set.instruments.size())
        {
            instrumentStrips.clear();
            for (int i = 0; i < (int) set.instruments.size(); ++i)
            {
                auto* s = instrumentStrips.add (new SourceStrip ("EDIT", "Open the instrument's own window"));
                if (onInstrumentStripCreated)
                    onInstrumentStripCreated (i, *s);
            }
            changed = true;
        }

        for (int i = 0; i < instrumentStrips.size(); ++i)
        {
            const auto& inst = set.instruments[(size_t) i];
            instrumentStrips[i]->setContent (instrumentName (i), instrumentDetail (i), false, inst.armed, inst.fx.anyOn(),
                                             colourOf (true, i));
        }

        if (changed && onStripsChanged)
            onStripsChanged();
    }
};
