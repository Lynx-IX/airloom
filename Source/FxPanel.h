#pragma once

#include <JuceHeader.h>
#include "Effects.h"
#include "Theme.h"

// Pop-up editor for one input's or row's effects chain. Every change is
// reported at once through onChange.
class FxPanel : public juce::Component
{
public:
    FxPanel (const juce::String& panelTitle, const FxSettings& initial, juce::Colour accentColour,
             std::function<void (const FxSettings&)> onSettingsChanged)
        : title (panelTitle), settings (initial), accent (accentColour), onChange (std::move (onSettingsChanged))
    {
        using F = FxSettings;

        auto& channel = addSection ("Channel", nullptr);
        addKnob (channel, "Gain", &F::gainDb, -24.0, 24.0, 0.1, formatDb);
        addKnob (channel, "Pan", &F::pan, -1.0, 1.0, 0.01, formatPan);

        auto& gate = addSection ("Gate", &F::gateOn);
        addKnob (gate, "Threshold", &F::gateThresholdDb, -80.0, -10.0, 0.5, formatDb);

        auto& lowCut = addSection ("Low cut", &F::lowCutOn);
        addKnob (lowCut, "Freq", &F::lowCutHz, 20.0, 400.0, 1.0, formatHz, 100.0);

        auto& eq = addSection ("EQ", &F::eqOn);
        addKnob (eq, "Low", &F::eqLowDb, -15.0, 15.0, 0.1, formatDb);
        addKnob (eq, "Mid", &F::eqMidDb, -15.0, 15.0, 0.1, formatDb);
        addKnob (eq, "Mid freq", &F::eqMidHz, 200.0, 6000.0, 1.0, formatHz, 1000.0);
        addKnob (eq, "High", &F::eqHighDb, -15.0, 15.0, 0.1, formatDb);

        auto& comp = addSection ("Compressor", &F::compOn);
        addKnob (comp, "Threshold", &F::compThresholdDb, -50.0, 0.0, 0.5, formatDb);
        addKnob (comp, "Ratio", &F::compRatio, 1.0, 10.0, 0.1, [] (double v) { return juce::String (v, 1) + ":1"; }, 3.0);
        addKnob (comp, "Release", &F::compReleaseMs, 20.0, 1000.0, 1.0, formatMs, 150.0);
        addKnob (comp, "Makeup", &F::compMakeupDb, 0.0, 18.0, 0.1, formatDb);

        delaySection = &addSection ("Delay", &F::delayOn);
        timeKnob = &addKnob (*delaySection, "Time", &F::delayMs, 30.0, 2000.0, 1.0, formatMs, 400.0);
        addKnob (*delaySection, "Feedback", &F::delayFeedback, 0.0, 0.9, 0.01, formatPercent);
        addKnob (*delaySection, "Tone", &F::delayTone, 0.0, 1.0, 0.01, formatPercent);
        addKnob (*delaySection, "Mix", &F::delayMix, 0.0, 1.0, 0.01, formatPercent);

        syncButton.setButtonText ("Sync to BPM");
        syncButton.setToggleState (settings.delaySync, juce::dontSendNotification);
        syncButton.onClick = [this] { settings.delaySync = syncButton.getToggleState(); updateDelayControls(); changed(); };
        pingPongButton.setButtonText ("Ping-pong");
        pingPongButton.setToggleState (settings.delayPingPong, juce::dontSendNotification);
        pingPongButton.onClick = [this] { settings.delayPingPong = pingPongButton.getToggleState(); changed(); };

        for (int i = 0; i < numDelayDivisions; ++i)
            divisionBox.addItem (delayDivisions[i].name, i + 1);
        divisionBox.setSelectedId (settings.delayDivision + 1, juce::dontSendNotification);
        divisionBox.onChange = [this] { settings.delayDivision = divisionBox.getSelectedId() - 1; changed(); };

        for (auto* c : std::initializer_list<juce::Component*> { &syncButton, &pingPongButton, &divisionBox })
            addAndMakeVisible (c);
        for (auto* t : { &syncButton, &pingPongButton })
            t->setColour (juce::ToggleButton::tickColourId, accent);

        auto& reverb = addSection ("Reverb", &F::reverbOn);
        addKnob (reverb, "Decay", &F::reverbDecay, 0.5, 20.0, 0.1, [] (double v) { return juce::String (v, 1) + " s"; }, 4.0);
        addKnob (reverb, "Size", &F::reverbSize, 0.0, 1.0, 0.01, formatPercent);
        addKnob (reverb, "Tone", &F::reverbDamping, 0.0, 1.0, 0.01, [] (double v) { return v < 0.34 ? juce::String ("Bright") : v < 0.67 ? juce::String ("Warm") : juce::String ("Dark"); });
        addKnob (reverb, "Pre-delay", &F::reverbPredelayMs, 0.0, 200.0, 1.0, formatMs);
        addKnob (reverb, "Movement", &F::reverbMod, 0.0, 1.0, 0.01, formatPercent);
        addKnob (reverb, "Mix", &F::reverbMix, 0.0, 1.0, 0.01, formatPercent);

        updateDelayControls();
        setSize (900, 360);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (accent);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title.toUpperCase() + "  EFFECTS", getLocalBounds().removeFromTop (34).reduced (14, 0),
                    juce::Justification::centredLeft);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Signal flows left to right, top row then bottom row. Double-click a knob to reset it.",
                    getLocalBounds().removeFromTop (34).reduced (14, 0), juce::Justification::centredRight);

        for (auto* s : sections)
        {
            g.setColour (Theme::background.interpolatedWith (accent, 0.05f));
            g.fillRoundedRectangle (s->area.toFloat().expanded (4.0f), 5.0f);

            if (s->toggle == nullptr)
            {
                g.setColour (Theme::text);
                g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                g.drawText (s->name, s->area.withHeight (24), juce::Justification::centredLeft);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        area.removeFromTop (26);
        const int rowHeight = (area.getHeight() - 14) / 2;

        // Top row: everything up to the compressor. Bottom row: delay and reverb.
        layoutRow (area.removeFromTop (rowHeight), 0, 5);
        area.removeFromTop (14);
        layoutRow (area, 5, 7);
    }

private:
    struct Section
    {
        juce::String name;
        std::unique_ptr<juce::ToggleButton> toggle;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label> labels;
        juce::Rectangle<int> area;
        int extraWidth = 0;   // room for non-knob controls (the delay's sync options)
    };

    static juce::String formatDb (double v)      { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    static juce::String formatMs (double v)      { return juce::String (juce::roundToInt (v)) + " ms"; }
    static juce::String formatPercent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
    static juce::String formatHz (double v)      { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    static juce::String formatPan (double v)
    {
        const int amount = juce::roundToInt (std::abs (v) * 100.0);
        return amount == 0 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (amount);
    }

    Section& addSection (const juce::String& name, bool FxSettings::* onFlag)
    {
        auto* s = sections.add (new Section());
        s->name = name;

        if (onFlag != nullptr)
        {
            s->toggle = std::make_unique<juce::ToggleButton> (name);
            s->toggle->setToggleState (settings.*onFlag, juce::dontSendNotification);
            s->toggle->setColour (juce::ToggleButton::tickColourId, accent);
            s->toggle->onClick = [this, s, onFlag] { settings.*onFlag = s->toggle->getToggleState(); changed(); };
            addAndMakeVisible (*s->toggle);
        }

        return *s;
    }

    juce::Slider& addKnob (Section& s, const juce::String& name, double FxSettings::* member,
                           double min, double max, double step, std::function<juce::String (double)> format,
                           double skewMidPoint = 0.0)
    {
        auto* label = s.labels.add (new juce::Label ({}, name));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, Theme::textDim);
        label->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (label);

        auto* knob = s.knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
        knob->setRange (min, max, step);
        if (skewMidPoint > min && skewMidPoint < max)
            knob->setSkewFactorFromMidPoint (skewMidPoint);
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        knob->setColour (juce::Slider::rotarySliderFillColourId, accent);
        knob->textFromValueFunction = format;
        knob->setDoubleClickReturnValue (true, FxSettings{}.*member);
        knob->setValue (settings.*member, juce::dontSendNotification);
        knob->onValueChange = [this, knob, member] { settings.*member = knob->getValue(); changed(); };
        addAndMakeVisible (knob);
        return *knob;
    }

    void layoutRow (juce::Rectangle<int> area, int first, int last)
    {
        constexpr int knobWidth = 70, gap = 18;

        if (delaySection != nullptr)
            delaySection->extraWidth = 120;

        int totalWidth = 0;
        for (int i = first; i < last; ++i)
            totalWidth += juce::jmax (1, sections[i]->knobs.size()) * knobWidth + sections[i]->extraWidth;
        const float scale = (float) (area.getWidth() - gap * (last - first - 1)) / (float) totalWidth;

        for (int i = first; i < last; ++i)
        {
            auto* s = sections[i];
            const int width = (int) ((float) (juce::jmax (1, s->knobs.size()) * knobWidth + s->extraWidth) * scale);
            s->area = area.removeFromLeft (width);
            area.removeFromLeft (gap);

            auto a = s->area;
            auto header = a.removeFromTop (24);
            if (s->toggle != nullptr)
                s->toggle->setBounds (header);
            a.removeFromTop (6);

            if (s == delaySection)
            {
                auto extra = a.removeFromLeft ((int) (s->extraWidth * scale));
                syncButton.setBounds (extra.removeFromTop (24));
                extra.removeFromTop (4);
                divisionBox.setBounds (extra.removeFromTop (24).reduced (0, 1).withTrimmedRight (8));
                extra.removeFromTop (8);
                pingPongButton.setBounds (extra.removeFromTop (24));
            }

            const int each = a.getWidth() / juce::jmax (1, s->knobs.size());
            for (int k = 0; k < s->knobs.size(); ++k)
            {
                auto cell = a.removeFromLeft (each);
                s->labels[k]->setBounds (cell.removeFromTop (16));
                s->knobs[k]->setBounds (cell.removeFromTop (juce::jmin (cell.getHeight(), 96)));
            }
        }
    }

    void updateDelayControls()
    {
        // Synced: pick a note length. Free: set the time in milliseconds.
        divisionBox.setEnabled (settings.delaySync);
        timeKnob->setEnabled (! settings.delaySync);
        timeKnob->setAlpha (settings.delaySync ? 0.35f : 1.0f);
    }

    void changed()
    {
        if (onChange)
            onChange (settings);
    }

    juce::String title;
    FxSettings settings;
    juce::Colour accent;
    std::function<void (const FxSettings&)> onChange;

    juce::OwnedArray<Section> sections;
    Section* delaySection = nullptr;
    juce::Slider* timeKnob = nullptr;
    juce::ToggleButton syncButton, pingPongButton;
    juce::ComboBox divisionBox;
};
