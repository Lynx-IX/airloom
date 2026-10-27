#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "Effects.h"
#include "Theme.h"

// The range a knob covers, shared by the panel and MIDI control.
inline juce::NormalisableRange<double> rangeOf (const FxParameter& p)
{
    juce::NormalisableRange<double> range (p.min, p.max, p.step);
    if (p.skewMidPoint > p.min && p.skewMidPoint < p.max)
        range.setSkewForCentre (p.skewMidPoint);
    return range;
}

// Pop-up editor for one channel's effects chain - or, as the Space panel, for the shared
// reverb. Every change is reported at once through onChange; right-clicking a knob asks
// (through onKnobRightClick) to assign it to a MIDI control.
class FxPanel : public juce::Component
{
public:
    enum class Kind { channel, space };

    std::function<void (const juce::String& parameter, const juce::String& label)> onKnobRightClick;

    FxPanel (const juce::String& panelTitle, const FxSettings& initial, juce::Colour accentColour,
             std::function<void (const FxSettings&)> onSettingsChanged, Kind panelKind = Kind::channel)
        : title (panelTitle), settings (initial), accent (accentColour), onChange (std::move (onSettingsChanged)), kind (panelKind)
    {
        using F = FxSettings;

        if (kind == Kind::space)
        {
            auto& space = addSection ("Space reverb", nullptr);
            addKnob (space, "reverbDecay", [] (double v) { return juce::String (v, 1) + " s"; });
            addKnob (space, "reverbSize", formatPercent);
            addKnob (space, "reverbDamping", formatTone);
            addKnob (space, "reverbPredelayMs", formatMs);
            addKnob (space, "reverbMod", formatPercent);
            addKnob (space, "reverbShimmer", formatPercent);
            addKnob (space, "reverbMix", formatPercent, "Return");
            rows = { { 0, 1 } };
            updateLooks();
            setSize (720, 210);
            return;
        }

        auto& channel = addSection ("Channel", nullptr);
        addKnob (channel, "gainDb", formatDb);
        addKnob (channel, "pan", formatPan);
        addKnob (channel, "spaceSend", formatPercent);

        auto& gate = addSection ("Gate", &F::gateOn);
        addKnob (gate, "gateThresholdDb", formatDb);

        auto& swell = addSection ("Swell", &F::swellOn);
        addKnob (swell, "swellAttackMs", formatMs);
        addKnob (swell, "swellThresholdDb", formatDb);

        auto& lowCut = addSection ("Low cut", &F::lowCutOn);
        addKnob (lowCut, "lowCutHz", formatHz);

        auto& eq = addSection ("EQ", &F::eqOn);
        addKnob (eq, "eqLowDb", formatDb);
        addKnob (eq, "eqMidDb", formatDb);
        addKnob (eq, "eqMidHz", formatHz);
        addKnob (eq, "eqHighDb", formatDb);

        auto& comp = addSection ("Compressor", &F::compOn);
        addKnob (comp, "compThresholdDb", formatDb);
        addKnob (comp, "compRatio", [] (double v) { return juce::String (v, 1) + ":1"; });
        addKnob (comp, "compReleaseMs", formatMs);
        addKnob (comp, "compMakeupDb", formatDb);

        auto& tape = addSection ("Tape", &F::tapeOn);
        addKnob (tape, "tapeDrive", formatPercent);
        addKnob (tape, "tapeWow", formatPercent);
        addKnob (tape, "tapeTone", formatTone);

        auto& filter = addSection ("Filter", &F::filterOn);
        highPassButton.setButtonText ("High-pass");
        addToggle (filter, highPassButton, &F::filterHighPass);
        addKnob (filter, "filterCutoffHz", formatHz);
        addKnob (filter, "filterResonance", formatPercent);
        addKnob (filter, "filterLfoRate", [] (double v) { return v <= 0.0 ? juce::String ("Still") : juce::String (v, 2) + " Hz"; });
        addKnob (filter, "filterLfoDepth", [] (double v) { return juce::String (v, 1) + " oct"; });

        auto& octave = addSection ("Octave", &F::octaveOn);
        for (int i = 0; i < numShiftIntervals; ++i)
            intervalBox.addItem (shiftIntervals[i].name, i + 1);
        intervalBox.setSelectedId (settings.octaveInterval + 1, juce::dontSendNotification);
        intervalBox.onChange = [this, &octave] { settings.octaveInterval = intervalBox.getSelectedId() - 1; switchOn (octave); changed(); };
        addExtra (octave, intervalBox);
        addKnob (octave, "octaveMix", formatPercent);

        auto& chorus = addSection ("Chorus", &F::chorusOn);
        addKnob (chorus, "chorusRate", [] (double v) { return juce::String (v, 2) + " Hz"; });
        addKnob (chorus, "chorusDepth", formatPercent);
        addKnob (chorus, "chorusMix", formatPercent);

        auto& motion = addSection ("Motion", &F::motionOn);
        addKnob (motion, "motionWidth", formatPercent);
        addKnob (motion, "motionPanRate", [] (double v) { return juce::String (v, 2) + " Hz"; });
        addKnob (motion, "motionPanDepth", formatPercent);

        auto& delay = addSection ("Delay", &F::delayOn);
        syncButton.setButtonText ("Sync to BPM");
        addToggle (delay, syncButton, &F::delaySync);
        for (int i = 0; i < numDelayDivisions; ++i)
            divisionBox.addItem (delayDivisions[i].name, i + 1);
        divisionBox.setSelectedId (settings.delayDivision + 1, juce::dontSendNotification);
        divisionBox.onChange = [this, &delay] { settings.delayDivision = divisionBox.getSelectedId() - 1; switchOn (delay); changed(); };
        addExtra (delay, divisionBox);
        pingPongButton.setButtonText ("Ping-pong");
        addToggle (delay, pingPongButton, &F::delayPingPong);
        timeKnob = &addKnob (delay, "delayMs", formatMs);
        addKnob (delay, "delayFeedback", formatPercent);
        addKnob (delay, "delayTone", formatPercent);
        addKnob (delay, "delayMix", formatPercent);

        auto& reverb = addSection ("Reverb", &F::reverbOn);
        addKnob (reverb, "reverbDecay", [] (double v) { return juce::String (v, 1) + " s"; });
        addKnob (reverb, "reverbSize", formatPercent);
        addKnob (reverb, "reverbDamping", formatTone);
        addKnob (reverb, "reverbPredelayMs", formatMs);
        addKnob (reverb, "reverbMod", formatPercent);
        addKnob (reverb, "reverbShimmer", formatPercent);
        addKnob (reverb, "reverbMix", formatPercent);
        addKnob (reverb, "reverbDry", formatPercent);

        // Channel, gate, swell, low cut, EQ, compressor / tape, filter, octave, chorus, motion / delay, reverb.
        rows = { { 0, 6 }, { 6, 11 }, { 11, 13 } };
        updateLooks();
        setSize (1180, 560);
    }

    // Shows settings changed from elsewhere (a MIDI knob), without reporting them back.
    void setSettings (const FxSettings& newSettings)
    {
        settings = newSettings;

        for (auto* s : sections)
        {
            if (s->toggle != nullptr)
                s->toggle->setToggleState (settings.*(s->onFlag), juce::dontSendNotification);
            for (int k = 0; k < s->knobs.size(); ++k)
                s->knobs[k]->setValue (settings.*(s->parameters[k]->member), juce::dontSendNotification);
        }
        for (auto& [button, member] : toggles)
            button->setToggleState (settings.*member, juce::dontSendNotification);
        intervalBox.setSelectedId (settings.octaveInterval + 1, juce::dontSendNotification);
        divisionBox.setSelectedId (settings.delayDivision + 1, juce::dontSendNotification);
        updateLooks();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (accent);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title.toUpperCase() + (kind == Kind::space ? juce::String() : "  EFFECTS"),
                    getLocalBounds().removeFromTop (34).reduced (14, 0), juce::Justification::centredLeft);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (kind == Kind::space ? "Channels send to it with their Space knob. Right-click a knob for MIDI."
                                        : "Tick a section (or turn its knobs) to switch it on. Double-click resets a knob, right-click assigns MIDI.",
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
        constexpr int rowGap = 14;
        const int rowHeight = (area.getHeight() - rowGap * ((int) rows.size() - 1)) / (int) rows.size();

        for (size_t r = 0; r < rows.size(); ++r)
        {
            layoutRow (area.removeFromTop (rowHeight), rows[r].first, rows[r].second);
            area.removeFromTop (rowGap);
        }
    }

private:
    struct Section
    {
        juce::String name;
        std::unique_ptr<juce::ToggleButton> toggle;
        bool FxSettings::* onFlag = nullptr;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label> labels;
        std::vector<const FxParameter*> parameters;
        std::vector<juce::Component*> extras;   // toggles and choices, stacked in a column before the knobs
        juce::Rectangle<int> area;
    };

    static juce::String formatDb (double v)      { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    static juce::String formatMs (double v)      { return juce::String (juce::roundToInt (v)) + " ms"; }
    static juce::String formatPercent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
    static juce::String formatHz (double v)      { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    static juce::String formatTone (double v)    { return v < 0.34 ? juce::String ("Bright") : v < 0.67 ? juce::String ("Warm") : juce::String ("Dark"); }
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
            s->onFlag = onFlag;
            s->toggle->onClick = [this, s, onFlag] { settings.*onFlag = s->toggle->getToggleState(); updateLooks(); changed(); };
            addAndMakeVisible (*s->toggle);
        }

        return *s;
    }

    juce::Slider& addKnob (Section& s, const char* parameterName, std::function<juce::String (double)> format,
                           const juce::String& labelOverride = {})
    {
        const auto* p = findFxParameter (parameterName);
        jassert (p != nullptr);
        const auto labelText = labelOverride.isNotEmpty() ? labelOverride : juce::String (p->label);

        auto* label = s.labels.add (new juce::Label ({}, labelText));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, Theme::textDim);
        label->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (label);

        auto* knob = new RightClickable<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        s.knobs.add (knob);
        s.parameters.push_back (p);
        knob->setNormalisableRange (rangeOf (*p));
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        knob->setColour (juce::Slider::rotarySliderFillColourId, accent);
        knob->textFromValueFunction = format;
        knob->setDoubleClickReturnValue (true, (kind == Kind::space ? SpaceReverb::defaults() : FxSettings{}).*(p->member));
        knob->setValue (settings.*(p->member), juce::dontSendNotification);
        knob->onValueChange = [this, &s, knob, p] { settings.*(p->member) = knob->getValue(); switchOn (s); changed(); };
        knob->onRightClick = [this, p, labelText, &s]
        {
            if (onKnobRightClick)
                onKnobRightClick (p->name, s.name + " " + labelText.toLowerCase());
        };
        addAndMakeVisible (knob);
        return *knob;
    }

    void addToggle (Section& s, juce::ToggleButton& button, bool FxSettings::* member)
    {
        button.setToggleState (settings.*member, juce::dontSendNotification);
        button.setColour (juce::ToggleButton::tickColourId, accent);
        button.onClick = [this, &s, &button, member] { settings.*member = button.getToggleState(); switchOn (s); updateLooks(); changed(); };
        toggles.push_back ({ &button, member });
        addExtra (s, button);
    }

    void addExtra (Section& s, juce::Component& c)
    {
        s.extras.push_back (&c);
        addAndMakeVisible (c);
    }

    void layoutRow (juce::Rectangle<int> area, int first, int last)
    {
        constexpr int knobWidth = 70, extraWidth = 112, gap = 18;

        auto widthOf = [&] (const Section& s) { return juce::jmax (1, s.knobs.size()) * knobWidth + (s.extras.empty() ? 0 : extraWidth); };

        int totalWidth = 0;
        for (int i = first; i < last; ++i)
            totalWidth += widthOf (*sections[i]);
        const float scale = juce::jmin (1.4f, (float) (area.getWidth() - gap * (last - first - 1)) / (float) totalWidth);

        for (int i = first; i < last; ++i)
        {
            auto* s = sections[i];
            s->area = area.removeFromLeft ((int) ((float) widthOf (*s) * scale));
            area.removeFromLeft (gap);

            auto a = s->area;
            auto header = a.removeFromTop (24);
            if (s->toggle != nullptr)
                s->toggle->setBounds (header);
            a.removeFromTop (6);

            if (! s->extras.empty())
            {
                auto column = a.removeFromLeft ((int) ((float) extraWidth * scale));
                for (auto* c : s->extras)
                {
                    c->setBounds (column.removeFromTop (24).withTrimmedRight (8));
                    column.removeFromTop (6);
                }
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

    // Turning a knob means you want to hear it: switch its section on if it was off.
    void switchOn (Section& s)
    {
        if (s.onFlag == nullptr || settings.*(s.onFlag))
            return;

        settings.*(s.onFlag) = true;
        s.toggle->setToggleState (true, juce::dontSendNotification);
        updateLooks();
    }

    // Sections that are off fade back, so it's obvious they aren't doing anything.
    void updateLooks()
    {
        for (auto* s : sections)
        {
            const float alpha = (s->onFlag == nullptr || settings.*(s->onFlag)) ? 1.0f : 0.4f;
            for (auto* k : s->knobs)  k->setAlpha (alpha);
            for (auto* l : s->labels) l->setAlpha (alpha);
            for (auto* c : s->extras) c->setAlpha (alpha);
        }

        // Synced delay: pick a note length. Free: set the time in milliseconds.
        if (timeKnob != nullptr)
        {
            divisionBox.setEnabled (settings.delaySync);
            timeKnob->setEnabled (! settings.delaySync);
            if (settings.delaySync)
                timeKnob->setAlpha (0.35f);
        }
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
    Kind kind;

    juce::OwnedArray<Section> sections;
    std::vector<std::pair<int, int>> rows;   // which sections go on each line: [first, last)
    std::vector<std::pair<juce::ToggleButton*, bool FxSettings::*>> toggles;
    juce::Slider* timeKnob = nullptr;
    juce::ToggleButton syncButton, pingPongButton, highPassButton;
    juce::ComboBox divisionBox, intervalBox;
};
