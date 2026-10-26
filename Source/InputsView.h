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
    std::function<void (int)> onChoice;

    void setChoices (const juce::StringArray& labels)
    {
        choiceButtons.clear();
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = choiceButtons.add (new juce::TextButton (labels[i]));
            b->setWantsKeyboardFocus (false);
            b->onClick = [this, i] { if (onChoice) onChoice (i); };
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
    juce::OwnedArray<juce::TextButton> choiceButtons;
};

// The INPUTS group: the interface inputs and hosted instruments in a grid
// (the looper's controls are in the bottom bar). It flows among the other groups, can be dragged
// by its title, and its right edge sets how many strips sit on a line.
class InputsView : public juce::Component
{
public:
    // A slim title bar across the top (name, + INSTRUMENT); the strips fill the rest.
    static constexpr int headerWidth = 10, titleHeight = 30, stripWidth = 160, stripHeight = 272, stripGap = 6, margin = 6;
    static constexpr const char* dragId = "group:inputs";
    static inline const juce::Colour colour { 0xffe07a9a };            // rose: live inputs and recorded loops
    static inline const juce::Colour instrumentColour { 0xff5fc9c0 };  // teal: hosted instruments

    std::array<std::unique_ptr<SourceStrip>, LiveSet::numInputs> inputStrips;
    juce::OwnedArray<SourceStrip> instrumentStrips;
    juce::TextButton addInstrumentButton { "+ INSTRUMENT" };

    // Called whenever instrument strips are created, so the owner can wire them up.
    std::function<void (int index, SourceStrip&)> onInstrumentStripCreated;
    std::function<void()> onLayoutChanged;
    ColumnResizer resizer;   // its onColumnsChanged is set by the owner
    juce::Point<int> grabOffset;   // where in the group the mouse picked it up

    // The size this group wants for this many strips, given the widest it may be.
    // columns 0 means automatic: all the strips on one line if they fit.
    static juce::Point<int> preferredSize (int numStrips, int maxWidth, int wantedColumns = 0)
    {
        const int items = juce::jmax (1, numStrips);
        const int cell = stripWidth + stripGap;
        const int fitting = juce::jmax (1, (maxWidth - headerWidth - 2 * margin + stripGap) / cell);
        const int columns = juce::jlimit (1, items, juce::jmin (fitting, wantedColumns > 0 ? wantedColumns : items));
        const int lines = (items + columns - 1) / columns;
        return { headerWidth + 2 * margin + columns * cell - stripGap,
                 titleHeight + lines * (stripHeight + stripGap) - stripGap + 2 * margin };
    }

    int getNumStrips() const   { return (int) inputStrips.size() + instrumentStrips.size(); }

    void setFixedColumns (bool isFixed)
    {
        if (isFixed != fixedColumns) { fixedColumns = isFixed; repaint(); }
    }

    // Shows which side a dragged group would land on: -1 before, 1 after, 0 hidden.
    void setGroupDropSide (int side)
    {
        if (side != groupDropSide) { groupDropSide = side; repaint(); }
    }

    InputsView()
    {
        addAndMakeVisible (content);
        content.setInterceptsMouseClicks (false, true);

        resizer.columnsForWidth = [this] (int width)
        {
            const int cell = stripWidth + stripGap;
            const int fitting = (width - headerWidth - 2 * margin + stripGap + cell / 2) / cell;
            return juce::jlimit (1, juce::jmax (1, getNumStrips()), fitting);
        };

        for (auto& s : inputStrips)
        {
            s = std::make_unique<SourceStrip> ("MON", "Monitor: hear this input through the speakers. Off by default so a mic can't feed back.");
            content.addAndMakeVisible (*s);
        }

        addAndMakeVisible (addInstrumentButton);
        addInstrumentButton.setTooltip ("Host a VST3 instrument, played from the Fire's pads or any keyboard");
        addInstrumentButton.setWantsKeyboardFocus (false);

    }

    void update (const LiveSet& set, const std::function<juce::String (int)>& instrumentName,
                 const std::function<juce::String (int)>& instrumentDetail)
    {
        for (size_t i = 0; i < inputStrips.size(); ++i)
        {
            const auto& in = set.inputs[i];
            const auto source = juce::String (in.source == 0 ? "In 1" : in.source == 1 ? "In 2" : "In 1+2 stereo");
            const auto plugin = in.insertName.isNotEmpty() ? "  -  " + in.insertName : juce::String();
            inputStrips[i]->setContent (in.name, source + plugin + (in.recordWithFx ? juce::String() : "  -  records dry"),
                                        in.monitor, in.armed, in.fx.anyOn(), colour);
        }

        if (instrumentStrips.size() != (int) set.instruments.size())
        {
            instrumentStrips.clear();
            for (int i = 0; i < (int) set.instruments.size(); ++i)
            {
                auto* s = instrumentStrips.add (new SourceStrip ("EDIT", "Open the instrument's own window"));
                content.addAndMakeVisible (s);
                if (onInstrumentStripCreated)
                    onInstrumentStripCreated (i, *s);
            }
            if (onLayoutChanged)
                onLayoutChanged();   // more or fewer strips means a different size
            resized();
        }

        for (int i = 0; i < instrumentStrips.size(); ++i)
        {
            const auto& inst = set.instruments[(size_t) i];
            instrumentStrips[i]->setContent (instrumentName (i), instrumentDetail (i), false, inst.armed, inst.fx.anyOn(),
                                             instrumentColour);
        }

        addInstrumentButton.setEnabled ((int) set.instruments.size() < LiveSet::maxInstruments);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        Theme::drawSurface (g, bounds, Theme::panel, 8.0f);
        Theme::drawSpine (g, bounds, colour);

        g.setColour (colour);
        g.setFont (Theme::heading (14.0f, 0.1f));
        g.drawText ("INPUTS", titleArea, juce::Justification::centredLeft);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        ColumnResizer::paintGrip (g, *this, colour, fixedColumns);

        if (groupDropSide != 0)
        {
            g.setColour (colour);
            const float x = groupDropSide < 0 ? 0.0f : (float) getWidth() - 5.0f;
            g.fillRoundedRectangle (x, 0.0f, 5.0f, (float) getHeight(), 2.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override       { resizer.mouseMove (*this, e); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { resizer.mouseDoubleClick (*this, e); }
    void mouseDown (const juce::MouseEvent& e) override       { resizer.mouseDown (*this, e); }
    void mouseUp (const juce::MouseEvent&) override           { resizer.mouseUp(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (resizer.mouseDrag (e))
            return;

        // Dragging the title picks the whole group up.
        if (! titleArea.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
            {
                grabOffset = e.getMouseDownPosition();
                container->startDragging (dragId, this);
            }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, margin).withTrimmedRight (margin);
        area.removeFromLeft (headerWidth);

        auto titleBar = area.removeFromTop (titleHeight - 4);
        addInstrumentButton.setButtonText (titleBar.getWidth() >= 230 ? "+ INSTRUMENT" : "+ INST");
        addInstrumentButton.setBounds (titleBar.removeFromRight (titleBar.getWidth() >= 230 ? 124 : 72));
        titleArea = titleBar.withTrimmedLeft (4);
        area.removeFromTop (4);

        // Strips in a grid: the interface inputs first, then the instruments.
        content.setBounds (area);
        const int columns = juce::jmax (1, (area.getWidth() + stripGap) / (stripWidth + stripGap));
        int index = 0;
        auto place = [&] (juce::Component& s)
        {
            s.setBounds ((index % columns) * (stripWidth + stripGap), (index / columns) * (stripHeight + stripGap),
                         stripWidth, stripHeight);
            ++index;
        };

        for (auto& s : inputStrips)
            place (*s);
        for (auto* s : instrumentStrips)
            place (*s);
    }

private:
    juce::Rectangle<int> titleArea;
    juce::Component content;
    bool fixedColumns = false;
    int groupDropSide = 0;
};
