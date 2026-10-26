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
        const float gap = juce::jmin (2.0f, area.getWidth() * 0.12f);
        const float barWidth = (area.getWidth() - gap) / 2.0f;

        // Tall meters are drawn as LED segments; short ones as a smooth bar.
        const float segment = 3.0f, segmentGap = 1.0f;
        const bool segmented = area.getHeight() >= 60.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (area.getX() + ch * (barWidth + gap), area.getY(),
                                               barWidth, area.getHeight());

            g.setColour (juce::Colour (0xff050608));
            g.fillRoundedRectangle (bar, 1.5f);

            const auto inner = bar.reduced (1.0f);
            const float level = toProportion (display[(size_t) ch]);
            const float litTop = inner.getBottom() - inner.getHeight() * level;

            if (segmented)
            {
                for (float y = inner.getBottom() - segment; y >= inner.getY(); y -= segment + segmentGap)
                {
                    const float proportion = (inner.getBottom() - y) / inner.getHeight();
                    const bool lit = y + segment * 0.5f >= litTop && level > 0.0f;
                    g.setColour (colourAt (proportion).withAlpha (lit ? 1.0f : 0.07f));
                    g.fillRect (inner.getX(), y, inner.getWidth(), segment);
                }
            }
            else if (level > 0.0f)
            {
                g.setGradientFill (juce::ColourGradient (colourAt (0.0f), 0.0f, inner.getBottom(),
                                                         colourAt (1.0f), 0.0f, inner.getY(), false));
                g.fillRect (inner.withTop (litTop));
            }

            if (hold[(size_t) ch] > 0.001f)
            {
                const float p = toProportion (hold[(size_t) ch]);
                const float holdY = inner.getBottom() - inner.getHeight() * p;
                g.setColour (colourAt (p).brighter (0.3f));
                g.fillRect (inner.getX(), holdY - 1.0f, inner.getWidth(), 2.0f);
            }
        }
    }

private:
    // Green-teal up to -12 dB, amber to -3 dB, red above (positions on the -60..0 dB scale).
    static juce::Colour colourAt (float proportion)
    {
        return proportion >= 0.95f ? Theme::danger : proportion >= 0.8f ? Theme::warn : Theme::accent;
    }

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
