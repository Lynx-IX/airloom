#pragma once

#include <JuceHeader.h>
#include "LoopPlayer.h"
#include "Theme.h"

// Edit sample: a channel's sample as a waveform, with handles to trim its start and end and
// shape its fade in and out, its gain, Normalise and Reset. Nothing is written into the file:
// each change goes to onChange, which saves it with the channel and plays the new version.
// The waveform is the sample as it plays (reversed if the channel is), before its trim.
class SampleEditor : public juce::Component, private juce::Timer
{
public:
    std::function<void (const SampleShape&)> onChange;
    std::function<void()> onPlay;          // PLAY / STOP: the channel itself
    std::function<bool()> isPlaying;

    // beatSeconds > 0 and isLoop: the end can snap so the loop is a whole number of beats.
    SampleEditor (const juce::String& name, LoopClipPtr sample, const SampleShape& initial, juce::Colour accent,
                  bool isLoop, double beatSeconds, int beatsPerBar)
        : title (name), clip (std::move (sample)), shape (initial), colour (accent),
          loop (isLoop), beat (beatSeconds), barBeats (juce::jmax (1, beatsPerBar))
    {
        length = clip != nullptr ? clip->data.getNumSamples() / clip->sampleRate : 0.0;

        for (auto* b : { &playButton, &normaliseButton, &resetButton })
        {
            addAndMakeVisible (b);
            b->setWantsKeyboardFocus (false);
        }
        playButton.onClick = [this] { if (onPlay) onPlay(); };
        normaliseButton.setTooltip ("Gain that brings the loudest moment to just under full scale");
        normaliseButton.onClick = [this] { normalise(); };
        resetButton.setTooltip ("Back to the whole sample, no fades, no gain");
        resetButton.onClick = [this] { shape = {}; gain.setValue (0.0, juce::dontSendNotification); changed (true); };

        addAndMakeVisible (snapButton);
        snapButton.setButtonText ("Snap to beats");
        snapButton.setToggleState (loop && beat > 0.0, juce::dontSendNotification);
        snapButton.setVisible (loop && beat > 0.0);
        snapButton.setWantsKeyboardFocus (false);
        snapButton.setColour (juce::ToggleButton::tickColourId, colour);

        addAndMakeVisible (gainLabel);
        gainLabel.setText ("Gain", juce::dontSendNotification);
        gainLabel.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (gain);
        gain.setSliderStyle (juce::Slider::LinearHorizontal);
        gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        gain.setRange (-24.0, 18.0, 0.1);
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
        gain.setColour (juce::Slider::trackColourId, colour);
        gain.setValue (shape.gainDb, juce::dontSendNotification);
        gain.onValueChange = [this] { shape.gainDb = gain.getValue(); repaint(); if (! gain.isMouseButtonDown()) changed (true); };
        gain.onDragEnd = [this] { changed (true); };

        buildPeaks();
        setSize (760, 300);
        startTimerHz (8);
    }

    const SampleShape& getShape() const   { return shape; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);

        // Title and what the trim adds up to.
        g.setColour (colour);
        g.setFont (Theme::heading (14.0f, 0.1f));
        g.drawText ("EDIT SAMPLE  -  " + title.toUpperCase(), titleArea, juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (describeLength(), titleArea, juce::Justification::centredRight);

        // The waveform: dim outside the trim, inside it as it will sound (gain and fades).
        const auto r = wave.toFloat();
        Theme::drawSurface (g, r, Theme::background, 6.0f);
        const float mid = r.getCentreY(), half = r.getHeight() * 0.46f;
        const float gainNow = juce::Decibels::decibelsToGain ((float) shape.gainDb);
        const double s0 = shape.start, s1 = endSeconds();

        for (int x = 0; x < (int) peaks.size(); ++x)
        {
            const double t = secondsAt ((float) wave.getX() + (float) x + 0.5f);
            const bool inside = t >= s0 && t <= s1;
            float level = peaks[(size_t) x];
            if (inside)
                level = juce::jmin (1.0f, level * gainNow * envelopeAt (t));
            g.setColour (inside ? colour.withAlpha (0.85f) : Theme::textDim.withAlpha (0.25f));
            g.fillRect ((float) wave.getX() + (float) x, mid - level * half, 1.0f, juce::jmax (1.0f, 2.0f * level * half));
        }

        // Beat lines for loops, from the trim's start.
        if (loop && beat > 0.0)
            for (int n = 1; s0 + n * beat < length; ++n)
            {
                const float x = xAt (s0 + n * beat);
                g.setColour (Theme::panelEdge.withAlpha (n % barBeats == 0 ? 0.9f : 0.4f));
                g.fillRect (x, r.getY() + 2.0f, 1.0f, r.getHeight() - 4.0f);
            }

        // Fade curves, then the handles.
        juce::Path fades;
        fades.startNewSubPath (xAt (s0), r.getBottom() - 2.0f);
        for (int i = 0; i <= 24; ++i)
        {
            const double t = s0 + shape.fadeIn * i / 24.0;
            fades.lineTo (xAt (t), r.getBottom() - 2.0f - (r.getHeight() - 4.0f) * envelopeAt (t));
        }
        for (int i = 0; i <= 24; ++i)
        {
            const double t = s1 - shape.fadeOut + shape.fadeOut * i / 24.0;
            fades.lineTo (xAt (t), r.getBottom() - 2.0f - (r.getHeight() - 4.0f) * envelopeAt (t));
        }
        g.setColour (Theme::text.withAlpha (0.55f));
        g.strokePath (fades, juce::PathStrokeType (1.2f));

        auto drawEdge = [&] (double t, bool active)
        {
            const float x = xAt (t);
            g.setColour (active ? Theme::text : colour);
            g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
            g.fillRoundedRectangle (x - 5.0f, r.getBottom() - 14.0f, 10.0f, 14.0f, 3.0f);
        };
        drawEdge (s0, dragging == Handle::start);
        drawEdge (s1, dragging == Handle::end);

        auto drawFadeHandle = [&] (double t, bool active)
        {
            g.setColour (active ? Theme::text : Theme::text.withAlpha (0.75f));
            g.fillEllipse (xAt (t) - 5.0f, r.getY() + 3.0f, 10.0f, 10.0f);
        };
        drawFadeHandle (s0 + shape.fadeIn, dragging == Handle::fadeIn);
        drawFadeHandle (s1 - shape.fadeOut, dragging == Handle::fadeOut);

        // The hint under the waveform.
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Drag the bars at the edges to trim, the dots at the top to fade. The file itself is never changed.",
                    hintArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        titleArea = area.removeFromTop (24);
        area.removeFromTop (6);
        auto controls = area.removeFromBottom (30);
        area.removeFromBottom (4);
        hintArea = area.removeFromBottom (18);
        area.removeFromBottom (4);
        wave = area;

        playButton.setBounds (controls.removeFromLeft (80));
        controls.removeFromLeft (12);
        resetButton.setBounds (controls.removeFromRight (80));
        controls.removeFromRight (8);
        normaliseButton.setBounds (controls.removeFromRight (100));
        controls.removeFromRight (16);
        if (snapButton.isVisible())
        {
            snapButton.setBounds (controls.removeFromLeft (130));
            controls.removeFromLeft (8);
        }
        gainLabel.setBounds (controls.removeFromLeft (40));
        gain.setBounds (controls);

        buildPeaks();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = handleAt (e.position);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == Handle::none || clip == nullptr)
            return;

        const double t = juce::jlimit (0.0, length, secondsAt (e.position.x));
        const double minLength = 0.05;

        switch (dragging)
        {
            case Handle::start:
                shape.start = juce::jlimit (0.0, endSeconds() - minLength, nearZero (t));
                break;
            case Handle::end:
            {
                double end = juce::jlimit (shape.start + minLength, length, t);
                if (snapButton.getToggleState() && snapButton.isVisible())
                {
                    // A whole number of beats from the start, never past the sample's end.
                    const double beats = juce::jmax (1.0, std::round ((end - shape.start) / beat));
                    end = shape.start + beats * beat;
                    while (end > length + 0.0005 && end - beat > shape.start)
                        end -= beat;
                    end = juce::jmin (end, length);
                }
                else
                {
                    end = nearZero (end);
                }
                shape.end = end >= length - 0.0005 ? 0.0 : end;
                break;
            }
            case Handle::fadeIn:   shape.fadeIn = juce::jlimit (0.0, juce::jmax (0.0, trimmedLength() - shape.fadeOut), t - shape.start); break;
            case Handle::fadeOut:  shape.fadeOut = juce::jlimit (0.0, juce::jmax (0.0, trimmedLength() - shape.fadeIn), endSeconds() - t); break;
            case Handle::none:     break;
        }

        // The fades never add up to more than what's left.
        shape.fadeIn = juce::jmin (shape.fadeIn, trimmedLength());
        shape.fadeOut = juce::jmin (shape.fadeOut, trimmedLength() - shape.fadeIn);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging != Handle::none)
        {
            dragging = Handle::none;
            changed (true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = handleAt (e.position);
        setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
    }

private:
    enum class Handle { none, start, end, fadeIn, fadeOut };

    void timerCallback() override
    {
        const bool on = isPlaying && isPlaying();
        playButton.setButtonText (on ? "STOP" : "PLAY");
        playButton.setColour (juce::TextButton::buttonColourId, on ? colour : Theme::raised);
        playButton.setColour (juce::TextButton::textColourOffId, on ? Theme::background : Theme::text);
    }

    void changed (bool notify)
    {
        repaint();
        if (notify && onChange && ! (shape == lastSent))
        {
            lastSent = shape;
            onChange (shape);
        }
    }

    void normalise()
    {
        if (clip == nullptr)
            return;
        const double sr = clip->sampleRate;
        const int from = (int) (shape.start * sr), to = (int) (endSeconds() * sr);
        float peak = 0.0f;
        for (int ch = 0; ch < clip->data.getNumChannels(); ++ch)
            peak = juce::jmax (peak, clip->data.getMagnitude (ch, from, juce::jmax (1, to - from)));
        if (peak <= 0.0f)
            return;
        shape.gainDb = juce::jlimit (-24.0, 18.0, std::round ((-0.3 - juce::Decibels::gainToDecibels (peak)) * 10.0) / 10.0);
        gain.setValue (shape.gainDb, juce::dontSendNotification);
        changed (true);
    }

    double endSeconds() const      { return shape.end > 0.0 ? juce::jmin (shape.end, length) : length; }
    double trimmedLength() const   { return juce::jmax (0.0, endSeconds() - shape.start); }

    // Fade level at a time inside the trim (raised cosine, as the sound will have it).
    float envelopeAt (double t) const
    {
        float e = 1.0f;
        if (shape.fadeIn > 0.0 && t < shape.start + shape.fadeIn)
            e *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) ((t - shape.start) / shape.fadeIn));
        if (shape.fadeOut > 0.0 && t > endSeconds() - shape.fadeOut)
            e *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) ((endSeconds() - t) / shape.fadeOut));
        return juce::jlimit (0.0f, 1.0f, e);
    }

    float xAt (double t) const        { return (float) wave.getX() + (float) (t / juce::jmax (0.001, length)) * (float) wave.getWidth(); }
    double secondsAt (float x) const  { return (double) (x - (float) wave.getX()) / juce::jmax (1, wave.getWidth()) * length; }

    // The nearest point within 3 ms where the sound crosses zero, so a cut doesn't click.
    double nearZero (double t) const
    {
        if (clip == nullptr)
            return t;
        const double sr = clip->sampleRate;
        const auto* d = clip->data.getReadPointer (0);
        const int n = clip->data.getNumSamples(), centre = juce::jlimit (0, n - 1, (int) (t * sr)), reach = (int) (0.003 * sr);
        for (int k = 0; k <= reach; ++k)
            for (int i : { centre - k, centre + k })
                if (i > 0 && i < n && (d[i - 1] <= 0.0f) != (d[i] <= 0.0f))
                    return i / sr;
        return t;
    }

    Handle handleAt (juce::Point<float> p) const
    {
        if (! wave.toFloat().expanded (6.0f).contains (p))
            return Handle::none;
        const float tolerance = 8.0f;
        if (p.y < (float) wave.getY() + 18.0f)
        {
            if (std::abs (p.x - xAt (shape.start + shape.fadeIn)) < tolerance)  return Handle::fadeIn;
            if (std::abs (p.x - xAt (endSeconds() - shape.fadeOut)) < tolerance) return Handle::fadeOut;
        }
        const float toStart = std::abs (p.x - xAt (shape.start)), toEnd = std::abs (p.x - xAt (endSeconds()));
        if (juce::jmin (toStart, toEnd) < tolerance)
            return toStart <= toEnd ? Handle::start : Handle::end;
        return Handle::none;
    }

    juce::String describeLength() const
    {
        juce::String text = "Length " + juce::String (trimmedLength(), 2) + " s";
        if (loop && beat > 0.0)
        {
            const double beats = trimmedLength() / beat;
            const bool whole = std::abs (beats - std::round (beats)) < 0.02;
            text << "   -   " << (whole ? juce::String ((int) std::round (beats)) : juce::String (beats, 1)) << " beats";
            if (whole && (int) std::round (beats) % barBeats == 0)
                text << " (" << (int) std::round (beats) / barBeats << (std::round (beats) == barBeats ? " bar)" : " bars)");
        }
        if (shape.isSet())
            text << "   -   of " << juce::String (length, 2) << " s";
        return text;
    }

    void buildPeaks()
    {
        peaks.assign ((size_t) juce::jmax (0, wave.getWidth()), 0.0f);
        if (clip == nullptr || peaks.empty())
            return;
        const int n = clip->data.getNumSamples();
        for (size_t x = 0; x < peaks.size(); ++x)
        {
            const int from = (int) ((double) x / (double) peaks.size() * n);
            const int to = juce::jmax (from + 1, (int) ((double) (x + 1) / (double) peaks.size() * n));
            float peak = 0.0f;
            for (int ch = 0; ch < clip->data.getNumChannels(); ++ch)
                peak = juce::jmax (peak, clip->data.getMagnitude (ch, from, juce::jmin (to, n) - from));
            peaks[x] = peak;
        }
    }

    juce::String title;
    LoopClipPtr clip;
    SampleShape shape, lastSent = shape;
    juce::Colour colour;
    bool loop;
    double beat, length = 0.0;
    int barBeats;

    juce::Rectangle<int> titleArea, hintArea, wave;
    std::vector<float> peaks;
    Handle dragging = Handle::none;

    juce::TextButton playButton { "PLAY" }, normaliseButton { "NORMALISE" }, resetButton { "RESET" };
    juce::ToggleButton snapButton;
    juce::Label gainLabel;
    juce::Slider gain;
};
