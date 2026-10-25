#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// Vertical stereo peak meter with a falling bar and a held peak line.
// The audio thread writes peaks with pushPeak(); the meter polls them.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter() { startTimerHz (30); }

    // Audio thread: report the peak of the latest block for a channel (0 or 1).
    void pushPeak (int channel, float peak)
    {
        auto& p = pending[(size_t) channel];
        float prev = p.load();
        while (peak > prev && ! p.compare_exchange_weak (prev, peak)) {}
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const float barWidth = (area.getWidth() - 6.0f) / 2.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (area.getX() + ch * (barWidth + 6.0f), area.getY(),
                                               barWidth, area.getHeight());
            g.setColour (Theme::panel);
            g.fillRoundedRectangle (bar, 3.0f);

            const float level = toProportion (display[(size_t) ch]);
            auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * level);
            g.setColour (display[(size_t) ch] >= 0.99f ? Theme::danger
                       : display[(size_t) ch] >= 0.5f  ? Theme::warn
                                                       : Theme::accent);
            g.fillRoundedRectangle (fill, 3.0f);

            const float holdY = bar.getBottom() - bar.getHeight() * toProportion (hold[(size_t) ch]);
            g.setColour (Theme::text);
            g.fillRect (bar.getX(), holdY - 1.0f, bar.getWidth(), 2.0f);
        }
    }

private:
    // Map linear gain to 0..1 on a -60..0 dB scale.
    static float toProportion (float gain)
    {
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f);
        return juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
    }

    void timerCallback() override
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float peak = pending[ch].exchange (0.0f);
            display[ch] = juce::jmax (peak, display[ch] * 0.85f);

            if (peak >= hold[ch]) { hold[ch] = peak; holdFrames[ch] = 45; }
            else if (--holdFrames[ch] <= 0) hold[ch] *= 0.9f;
        }
        repaint();
    }

    std::array<std::atomic<float>, 2> pending { 0.0f, 0.0f };
    std::array<float, 2> display { 0.0f, 0.0f };
    std::array<float, 2> hold { 0.0f, 0.0f };
    std::array<int, 2> holdFrames { 0, 0 };
};
