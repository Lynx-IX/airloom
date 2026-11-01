#pragma once

#include <JuceHeader.h>
#include "ChannelStrip.h"
#include "Theme.h"

// The looper's controls in the bottom bar: REC, DUB, UNDO, how long a new loop
// is, and a line or two saying what the looper is doing.
class LooperBar : public juce::Component
{
public:
    RightClickable<juce::TextButton> recButton { "REC" }, dubButton { "DUB" }, undoButton { "UNDO" };
    juce::ComboBox lengthBox;

    static constexpr int buttonsWidth = 100 + 6 + 64 + 6 + 64 + 10 + 156;   // REC to the length box

    LooperBar()
    {
        for (auto* b : { &recButton, &dubButton, &undoButton })
        {
            addAndMakeVisible (b);
            b->setWantsKeyboardFocus (false);
        }

        recButton.setTooltip ("Record a new loop from the armed sources. Press again to stop - it starts looping straight away.");
        dubButton.setTooltip ("Overdub: record on top of the latest loop (or the one chosen from a loop's menu). Press again to stop.");
        undoButton.setTooltip ("Remove the last overdub");
        Icons::set (recButton, "record");
        Icons::set (dubButton, "overdub");
        Icons::set (undoButton, "undo");

        lengthBox.addItem ("Free length", 1);
        lengthBox.addItem ("Snap to bars (BPM)", 2);
        lengthBox.addItem ("Match first loop", 3);
        lengthBox.setTooltip ("How long a new loop is when you stop recording");
        lengthBox.setWantsKeyboardFocus (false);
        addAndMakeVisible (lengthBox);

        setLooperStatus ({}, false, false);
    }

    // Shows the looper's state: what's recording, for how long, and what's armed.
    void setLooperStatus (const juce::String& text, bool isRecording, bool isOverdubbing)
    {
        if (text != status || isRecording != recording || isOverdubbing != overdubbing)
        {
            status = text;
            recording = isRecording;
            overdubbing = isOverdubbing;
            repaint (statusArea);
        }

        recButton.setButtonText (recording ? "STOP" : "REC");
        Icons::set (recButton, recording ? "stop-all" : "record");
        recButton.setColour (juce::TextButton::buttonColourId, recording ? Theme::danger : Theme::danger.darker (0.9f));
        dubButton.setColour (juce::TextButton::buttonColourId, overdubbing ? Theme::warn : Theme::raised);
        dubButton.setColour (juce::TextButton::textColourOffId, overdubbing ? Theme::background : Theme::text);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (recording ? Theme::danger : overdubbing ? Theme::warn : Theme::textDim);
        g.setFont (juce::FontOptions (13.0f, recording || overdubbing ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 2, 0.9f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        recButton.setBounds (area.removeFromLeft (100));
        area.removeFromLeft (6);
        dubButton.setBounds (area.removeFromLeft (64));
        area.removeFromLeft (6);
        undoButton.setBounds (area.removeFromLeft (64));
        area.removeFromLeft (10);
        lengthBox.setBounds (area.removeFromLeft (156).reduced (0, 4));
        area.removeFromLeft (12);
        statusArea = area;
    }

private:
    juce::String status;
    bool recording = false, overdubbing = false;
    juce::Rectangle<int> statusArea;
};
