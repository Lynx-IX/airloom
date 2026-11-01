#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// Header readout of how hard the computer is working: audio CPU load and dropouts (blocks
// the audio couldn't be ready in time - heard as clicks or gaps). Click to reset the count.
class PerformanceMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onReset;

    PerformanceMeter()
    {
        setTooltip ("Audio CPU load and dropouts (clicks or gaps) since the last reset. Over about 70% or any "
                    "dropouts: raise the buffer size in Settings > Audio, or use fewer reverbs and plug-ins. Click to reset.");
    }

    // 'cpu' is 0..1 (or more when overloaded); 'recentDropout' flashes the count red.
    void setValues (double cpu, int dropoutCount, bool recentDropout)
    {
        const int percent = juce::roundToInt (juce::jlimit (0.0, 9.99, cpu) * 100.0);
        if (percent != shownPercent || dropoutCount != dropouts || recentDropout != recent)
        {
            shownPercent = percent;
            dropouts = dropoutCount;
            recent = recentDropout;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 26.0f);
        const auto load = shownPercent >= 90 ? Theme::danger : shownPercent >= 70 ? Theme::warn : Theme::accent;
        const auto edge = recent ? Theme::danger : load;

        g.setColour (edge.withAlpha (recent ? 0.25f : 0.08f));
        g.fillRoundedRectangle (r, 13.0f);
        g.setColour (edge.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);

        r.removeFromLeft (10.0f);
        r.removeFromRight (10.0f);

        g.setFont (Theme::heading (9.5f, 0.12f));
        g.setColour (Theme::textDim);
        g.drawText ("CPU", r.removeFromLeft (28.0f), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.setColour (load);
        g.drawText (juce::String (shownPercent) + "%", r.removeFromLeft (42.0f), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (dropouts > 0 ? (recent ? Theme::danger : Theme::warn) : Theme::textDim);
        g.drawText (dropouts == 1 ? "1 dropout" : juce::String (dropouts) + " dropouts", r, juce::Justification::centredRight);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && onReset)
            onReset();
    }

private:
    int shownPercent = -1, dropouts = 0;
    bool recent = false;
};

// A red bar across the top of the groups for something that needs attention right now
// (the audio interface going missing). Hidden while there's no message.
class AlertBanner : public juce::Component
{
public:
    void setMessage (const juce::String& newMessage)
    {
        if (newMessage != message)
        {
            message = newMessage;
            setVisible (message.isNotEmpty());
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Theme::danger.darker (0.6f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Theme::danger);
        g.drawRoundedRectangle (r, 8.0f, 2.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawFittedText (message, getLocalBounds().reduced (18, 4), juce::Justification::centred, 2);
    }

private:
    juce::String message;
};

// The count-in, big in the middle of the window: the beat of the bar (1 2 3 4...) as
// it's counted, then REC as recording starts. Doesn't take clicks; hidden otherwise.
class CountInOverlay : public juce::Component
{
public:
    CountInOverlay()    { setInterceptsMouseClicks (false, false); }

    // 'text' is the big word ("3", "REC"); 'through' is how far through the beat (0..1),
    // so each count lands bright and fades. Empty text hides it.
    void show (const juce::String& newText, const juce::String& newCaption, float through, bool last)
    {
        const float level = 1.0f - 0.6f * juce::jlimit (0.0f, 1.0f, through);
        if (newText != text || newCaption != caption || std::abs (level - brightness) > 0.03f || last != isLast)
        {
            text = newText;
            caption = newCaption;
            brightness = level;
            isLast = last;
            setVisible (text.isNotEmpty());
            repaint();
        }
    }

    void hide()     { show ({}, {}, 0.0f, false); }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float size = juce::jmin (bounds.getHeight() * 0.7f, bounds.getWidth() * 0.5f, 380.0f);
        auto box = bounds.withSizeKeepingCentre (size * (text.length() > 2 ? 1.6f : 1.0f), size);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (box, size * 0.12f);
        const auto colour = text == "REC" || isLast ? Theme::danger : juce::Colours::white;
        g.setColour (colour.withAlpha (0.5f * brightness));
        g.drawRoundedRectangle (box.reduced (2.0f), size * 0.12f, 4.0f);

        auto captionArea = box.removeFromBottom (size * 0.2f);
        g.setColour (colour.withAlpha (brightness));
        g.setFont (juce::FontOptions (size * (text.length() > 2 ? 0.42f : 0.66f), juce::Font::bold));
        g.drawText (text, box, juce::Justification::centred, false);

        g.setColour (Theme::text.withAlpha (0.85f));
        g.setFont (juce::FontOptions (juce::jmax (13.0f, size * 0.075f), juce::Font::bold));
        g.drawText (caption, captionArea.withTrimmedBottom (size * 0.04f), juce::Justification::centred, true);
    }

private:
    juce::String text, caption;
    float brightness = 0.0f;
    bool isLast = false;
};


// Notifications: confirmations and errors as short-lived cards in the top-right corner, newest at
// the top. Errors (red edge) stay longer than confirmations (accent edge); a click dismisses one.
class ToastStack : public juce::Component, private juce::Timer
{
public:
    enum class Kind { info, error };

    ToastStack()   { setInterceptsMouseClicks (true, false); }

    void post (const juce::String& text, Kind kind)
    {
        if (text.isEmpty())
            return;
        // The same message again just stays up longer.
        for (auto& t : toasts)
            if (t.text == text)
            {
                t.born = juce::Time::getMillisecondCounter();
                return;
            }
        toasts.insert (toasts.begin(), { text, kind, juce::Time::getMillisecondCounter() });
        if (toasts.size() > 4)
            toasts.pop_back();
        startTimerHz (30);
        repaint();
    }

    int count() const   { return (int) toasts.size(); }

    // Whether a point (in this component) is on a card: the rest of the area lets clicks through.
    bool hitTest (int x, int y) override
    {
        for (int i = 0; i < (int) toasts.size(); ++i)
            if (cardBounds (i).contains (x, y))
                return true;
        return false;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) toasts.size(); ++i)
            if (cardBounds (i).contains (e.getPosition()))
            {
                toasts.erase (toasts.begin() + i);
                repaint();
                return;
            }
    }

    void paint (juce::Graphics& g) override
    {
        const auto now = juce::Time::getMillisecondCounter();
        for (int i = 0; i < (int) toasts.size(); ++i)
        {
            const auto& t = toasts[(size_t) i];
            const auto age = (float) (now - t.born), life = (float) lifeOf (t);
            const float alpha = juce::jlimit (0.0f, 1.0f, juce::jmin (age / 150.0f, (life - age) / 400.0f));
            const auto r = cardBounds (i).toFloat();

            g.setOpacity (alpha);
            g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 8.0f);
            Theme::drawSurface (g, r, Theme::raised, 8.0f);
            const auto edge = t.kind == Kind::error ? Theme::danger : Theme::accent;
            g.setColour (edge.withAlpha (alpha));
            g.fillRoundedRectangle (r.withWidth (4.0f).reduced (0.0f, 6.0f), 2.0f);

            g.setColour ((t.kind == Kind::error ? Theme::danger.brighter (0.4f) : Theme::text).withAlpha (alpha));
            g.setFont (juce::FontOptions (14.5f));
            g.drawFittedText (t.text, r.toNearestInt().reduced (16, 8), juce::Justification::centredLeft, 3, 0.9f);
        }
    }

private:
    struct Toast { juce::String text; Kind kind; juce::uint32 born; };

    static juce::uint32 lifeOf (const Toast& t)
    {
        // Long enough to read: longer for errors and long messages.
        return (t.kind == Kind::error ? 7000u : 4000u) + (juce::uint32) t.text.length() * 30u;
    }

    juce::Rectangle<int> cardBounds (int i) const
    {
        return { getWidth() - cardWidth, i * (cardHeight + 8), cardWidth, cardHeight };
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        toasts.erase (std::remove_if (toasts.begin(), toasts.end(), [now] (const Toast& t) { return now - t.born > lifeOf (t); }),
                      toasts.end());
        if (toasts.empty())
            stopTimer();
        repaint();
    }

    static constexpr int cardWidth = 400, cardHeight = 62;
    std::vector<Toast> toasts;
};
