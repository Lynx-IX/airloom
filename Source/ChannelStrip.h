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
        if (title.isNotEmpty())
            Theme::drawSurface (g, getLocalBounds().toFloat(), Theme::panel, 8.0f);

        g.setColour (Theme::text);
        g.setFont (Theme::heading (15.0f, 0.12f));
        g.drawText (title.toUpperCase(), titleArea, juce::Justification::centred);

        if (cuedText.isNotEmpty())
        {
            g.setColour (Theme::warn);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText ("NEXT: " + cuedText, cuedArea, juce::Justification::centred, 1);
        }

        if (hardwarePos >= 0.0f && ! muteOnly)
        {
            // Small amber tick beside the fader: "move the MidiMix fader to here".
            g.setColour (Theme::warn);

            if (horizontal)
            {
                auto track = fader.getBounds().withTrimmedRight (52).reduced (10, 0);
                const float x = (float) track.getX() + hardwarePos * (float) track.getWidth();
                g.fillRect (x - 1.5f, (float) track.getY() - 2.0f, 3.0f, 6.0f);
            }
            else
            {
                auto track = fader.getBounds().withTrimmedBottom (28).reduced (0, 10);
                const float y = (float) track.getBottom() - hardwarePos * (float) track.getHeight();
                g.fillRect ((float) track.getX() - 2.0f, y - 1.5f, 10.0f, 3.0f);
            }
        }
    }

    // Lays the strip out sideways (fader, then mute) for short spaces such as a row header.
    void setHorizontal (bool shouldBeHorizontal)
    {
        horizontal = shouldBeHorizontal;
        fader.setSliderStyle (horizontal ? juce::Slider::LinearHorizontal : juce::Slider::LinearVertical);
        fader.setTextBoxStyle (horizontal ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow, false,
                               horizontal ? 50 : 70, horizontal ? 20 : 22);
        resized();
    }

    // Just the MUTE button (a collapsed group's header).
    void setMuteOnly (bool shouldBeMuteOnly)
    {
        muteOnly = shouldBeMuteOnly;
        fader.setVisible (! muteOnly);
        meter.setVisible (! muteOnly);
        resized();
    }

    void resized() override
    {
        if (muteOnly)
        {
            muteButton.setBounds (getLocalBounds());
            return;
        }

        auto area = getLocalBounds().reduced (8);

        if (horizontal)
        {
            meter.setBounds (area.removeFromRight (14));
            area.removeFromRight (4);
            fader.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            muteButton.setBounds (area.removeFromTop (26).withWidth (juce::jmin (area.getWidth(), 70)));
            return;
        }
        titleArea = title.isNotEmpty() ? area.removeFromTop (24) : juce::Rectangle<int>();

        if (fileButton.isVisible())
        {
            fileButton.setBounds (area.removeFromTop (28));
            cuedArea = area.removeFromTop (20);
        }
        else
        {
            cuedArea = {};
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
    bool horizontal = false, muteOnly = false;
};
