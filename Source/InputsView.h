#pragma once

#include <JuceHeader.h>
#include "BeatIndicator.h"
#include "ChannelStrip.h"
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
            choiceButtons[i]->setColour (juce::TextButton::buttonColourId, on ? accent : Theme::panelEdge);
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
            b.setColour (juce::TextButton::buttonColourId, on ? c : Theme::panelEdge);
            b.setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::textDim);
        };
        light (firstButton, firstOn, colour);
        light (armButton, armed, Theme::danger);
        light (fxButton, fxOn, colour.withAlpha (0.7f));
        strip.fader.setColour (juce::Slider::trackColourId, colour);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Theme::background.interpolatedWith (accent, 0.06f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
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

// The INPUTS section above the rows: the looper controls, then the interface
// inputs and hosted instruments side by side.
class InputsView : public juce::Component
{
public:
    static constexpr int headerWidth = 230, stripWidth = 160;
    static inline const juce::Colour colour { 0xffe07a9a };            // rose: live inputs and recorded loops
    static inline const juce::Colour instrumentColour { 0xff5fc9c0 };  // teal: hosted instruments

    std::array<std::unique_ptr<SourceStrip>, LiveSet::numInputs> inputStrips;
    juce::OwnedArray<SourceStrip> instrumentStrips;
    juce::TextButton addInstrumentButton { "+ INSTRUMENT" };
    RightClickable<juce::TextButton> recButton { "REC" }, dubButton { "DUB" }, undoButton { "UNDO" };
    juce::ComboBox lengthBox;
    BeatIndicator beats;   // counts from REC while recording, otherwise follows the tempo

    // Called whenever instrument strips are created, so the owner can wire them up.
    std::function<void (int index, SourceStrip&)> onInstrumentStripCreated;

    InputsView()
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (8);

        for (auto& s : inputStrips)
        {
            s = std::make_unique<SourceStrip> ("MON", "Monitor: hear this input through the speakers. Off by default so a mic can't feed back.");
            content.addAndMakeVisible (*s);
        }

        content.addAndMakeVisible (addInstrumentButton);
        addInstrumentButton.setTooltip ("Host a VST3 instrument, played from the Fire's pads or any keyboard");
        addInstrumentButton.setWantsKeyboardFocus (false);

        for (auto* b : { &recButton, &dubButton, &undoButton })
        {
            addAndMakeVisible (b);
            b->setWantsKeyboardFocus (false);
        }

        recButton.setTooltip ("Record a new loop from the armed sources. Press again to stop - it starts looping straight away.");
        dubButton.setTooltip ("Overdub: record on top of the latest loop (or the one chosen from a loop's menu). Press again to stop.");
        undoButton.setTooltip ("Remove the last overdub");

        lengthBox.addItem ("Free length", 1);
        lengthBox.addItem ("Snap to bars (BPM)", 2);
        lengthBox.addItem ("Match first loop", 3);
        lengthBox.setTooltip ("How long a new loop is when you stop recording");
        lengthBox.setWantsKeyboardFocus (false);
        addAndMakeVisible (lengthBox);
        addAndMakeVisible (beats);
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

    // Shows the looper's state: what's recording, for how long, and what's armed.
    void setLooperStatus (const juce::String& text, bool recording, bool overdubbing)
    {
        if (text != status)
        {
            status = text;
            repaint (statusArea);
        }

        recButton.setButtonText (recording ? "STOP" : "REC");
        recButton.setColour (juce::TextButton::buttonColourId, recording ? Theme::danger : Theme::danger.withAlpha (0.35f));
        dubButton.setColour (juce::TextButton::buttonColourId, overdubbing ? Theme::warn : Theme::panelEdge);
        dubButton.setColour (juce::TextButton::textColourOffId, overdubbing ? Theme::background : Theme::text);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::panel.interpolatedWith (colour, 0.05f));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (bounds.withWidth (6.0f), 3.0f);

        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("INPUTS  +  LOOPER", titleArea, juce::Justification::centredLeft);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (status, statusArea, juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 6);
        auto header = area.removeFromLeft (headerWidth).withTrimmedLeft (10).reduced (4, 0);

        titleArea = header.removeFromTop (24);
        header.removeFromTop (4);
        recButton.setBounds (header.removeFromTop (40));
        header.removeFromTop (6);
        auto row = header.removeFromTop (28);
        dubButton.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (3));
        undoButton.setBounds (row.withTrimmedLeft (3));
        header.removeFromTop (6);
        lengthBox.setBounds (header.removeFromTop (26));
        header.removeFromTop (8);
        beats.setBounds (header.removeFromTop (22));
        header.removeFromTop (6);
        statusArea = header;

        viewport.setBounds (area);
        const int height = juce::jmax (100, area.getHeight() - viewport.getScrollBarThickness() - 2);
        int x = 0;

        for (auto& s : inputStrips)
        {
            s->setBounds (x, 0, stripWidth, height);
            x += stripWidth + 6;
        }

        x += 10;   // a little gap between the interface inputs and the instruments
        for (auto* s : instrumentStrips)
        {
            s->setBounds (x, 0, stripWidth, height);
            x += stripWidth + 6;
        }

        addInstrumentButton.setBounds (x, 0, 120, 36);
        content.setSize (x + 120, height);
    }

private:
    juce::String status;
    juce::Rectangle<int> titleArea, statusArea;
    juce::Viewport viewport;
    juce::Component content;
};
