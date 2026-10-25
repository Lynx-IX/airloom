#pragma once

#include <JuceHeader.h>
#include "LevelMeter.h"
#include "Theme.h"

// Wraps a control so a right-click calls onRightClick (for MIDI learn)
// instead of being treated as a normal click or drag.
template <class Base>
class RightClickable : public Base
{
public:
    using Base::Base;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        Base::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! (e.mods.isPopupMenu() && onRightClick)) Base::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! (e.mods.isPopupMenu() && onRightClick)) Base::mouseUp (e);
    }
};

// One mixer column: title, current/next file, fader, meter and mute.
class ChannelStrip : public juce::Component
{
public:
    explicit ChannelStrip (const juce::String& stripTitle, bool showFile = true)
        : title (stripTitle)
    {
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 22);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setTextValueSuffix (" dB");
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
        addAndMakeVisible (fader);

        muteButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, Theme::danger);
        addAndMakeVisible (muteButton);

        addAndMakeVisible (meter);

        if (showFile)
        {
            fileButton.setColour (juce::TextButton::buttonColourId, Theme::background);
            addAndMakeVisible (fileButton);
        }
    }

    void setFileText (const juce::String& current, const juce::String& cued)
    {
        fileButton.setButtonText (current.isEmpty() ? "(empty)" : current);
        if (cued != cuedText) { cuedText = cued; repaint(); }
    }

    // Where the hardware fader sits while it hasn't yet caught the on-screen
    // fader (soft takeover). Pass a negative position to hide the marker.
    void setHardwarePosition (float proportion)
    {
        if (proportion != hardwarePos) { hardwarePos = proportion; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (title.toUpperCase(), titleArea, juce::Justification::centred);

        if (cuedText.isNotEmpty())
        {
            g.setColour (Theme::warn);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText ("NEXT: " + cuedText, cuedArea, juce::Justification::centred, 1);
        }

        if (hardwarePos >= 0.0f)
        {
            // Small amber tick beside the fader: "move the MidiMix fader to here".
            auto track = fader.getBounds().withTrimmedBottom (28).reduced (0, 10);
            const float y = (float) track.getBottom() - hardwarePos * (float) track.getHeight();
            g.setColour (Theme::warn);
            g.fillRect ((float) track.getX() - 2.0f, y - 1.5f, 10.0f, 3.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        titleArea = area.removeFromTop (24);

        if (fileButton.isVisible())
        {
            fileButton.setBounds (area.removeFromTop (28));
            cuedArea = area.removeFromTop (20);
        }
        else
        {
            cuedArea = {};
            area.removeFromTop (48);
        }

        muteButton.setBounds (area.removeFromBottom (34));
        area.removeFromBottom (8);
        meter.setBounds (area.removeFromRight (22).withTrimmedBottom (28));
        area.removeFromRight (4);
        fader.setBounds (area);
    }

    RightClickable<juce::Slider> fader;
    RightClickable<juce::TextButton> muteButton { "MUTE" };
    juce::TextButton fileButton;
    LevelMeter meter;

private:
    juce::String title, cuedText;
    juce::Rectangle<int> titleArea, cuedArea;
    float hardwarePos = -1.0f;
};
