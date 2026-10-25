#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// Four lights, one per beat of a 4/4 bar, pulsing in time: the current beat
// lights up and fades through the beat; beat 1 is brighter. Red while recording.
class BeatIndicator : public juce::Component
{
public:
    BeatIndicator()     { setInterceptsMouseClicks (false, false); }

    // 'beats' counts from beat 1 of some bar (fractions are part-way through a beat).
    void setPosition (double beats, bool recording)
    {
        const int beat = (int) std::floor (beats) % 4;
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
        const auto area = getLocalBounds().toFloat();
        const float size = juce::jmin (area.getHeight(), (area.getWidth() - 3.0f * gap) / 4.0f);
        const float startX = area.getCentreX() - (4.0f * size + 3.0f * gap) / 2.0f;
        const auto on = isRecording ? Theme::danger : Theme::accent;

        for (int i = 0; i < 4; ++i)
        {
            auto dot = juce::Rectangle<float> (startX + (float) i * (size + gap), area.getCentreY() - size / 2.0f, size, size);

            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillEllipse (dot);

            if (i == currentBeat)
            {
                // Beat 1 at full strength, the others a little softer.
                const float strength = currentLevel * (i == 0 ? 1.0f : 0.7f);
                g.setColour (on.withAlpha (strength));
                g.fillEllipse (dot.reduced (1.0f));
            }

            g.setColour (i == 0 ? on.withAlpha (0.6f) : Theme::panelEdge);
            g.drawEllipse (dot.reduced (0.5f), i == 0 ? 1.5f : 1.0f);
        }
    }

private:
    static constexpr float gap = 6.0f;
    int currentBeat = -1;
    float currentLevel = 0.0f;
    bool isRecording = false;
};
