#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// One light per beat of the bar, pulsing in time: the current beat lights up and
// fades through the beat; beat 1 is brighter. Red while recording. The time signature
// sits beside them; clicking opens onClick (to change it).
class BeatIndicator : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;

    BeatIndicator()     { setTooltip ("Beat lights. Click to change the time signature."); }

    void setTimeSignature (int beats, int unit)
    {
        if (beats != beatsPerBar || unit != beatUnit)
        {
            beatsPerBar = juce::jmax (1, beats);
            beatUnit = unit;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override   { if (onClick) onClick(); }

    // 'beats' counts from beat 1 of some bar (fractions are part-way through a beat).
    void setPosition (double beats, bool recording)
    {
        const int beat = (int) std::floor (beats) % beatsPerBar;
        const float through = (float) (beats - std::floor (beats));
        const float level = juce::jlimit (0.25f, 1.0f, 1.0f - through * 0.75f);   // bright on the beat, then fades

        if (beat != currentBeat || std::abs (level - currentLevel) > 0.04f || recording != isRecording)
        {
            currentBeat = beat;
            currentLevel = level;
            isRecording = recording;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (beatsPerBar) + "/" + juce::String (beatUnit), area.removeFromLeft (30.0f),
                    juce::Justification::centredLeft);

        const int n = beatsPerBar;
        const float gap = n > 5 ? 3.0f : 6.0f;
        const float size = juce::jmin (area.getHeight(), (area.getWidth() - (float) (n - 1) * gap) / (float) n);
        const float startX = area.getCentreX() - ((float) n * size + (float) (n - 1) * gap) / 2.0f;
        const auto on = isRecording ? Theme::danger : Theme::accent;

        for (int i = 0; i < n; ++i)
        {
            auto dot = juce::Rectangle<float> (startX + (float) i * (size + gap), area.getCentreY() - size / 2.0f, size, size);

            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillEllipse (dot);

            if (i == currentBeat)
            {
                // Beat 1 at full strength, the others a little softer.
                const float strength = currentLevel * (i == 0 ? 1.0f : 0.7f);
                g.setColour (on.withAlpha (strength * 0.3f));
                g.fillEllipse (dot.expanded (3.0f));
                g.setColour (on.withAlpha (strength));
                g.fillEllipse (dot.reduced (1.0f));
            }

            g.setColour (i == 0 ? on.withAlpha (0.6f) : Theme::panelEdge.brighter (0.2f));
            g.drawEllipse (dot.reduced (0.5f), i == 0 ? 1.5f : 1.0f);
        }
    }

private:
    int beatsPerBar = 4, beatUnit = 4;
    int currentBeat = -1;
    float currentLevel = 0.0f;
    bool isRecording = false;
};
