#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"
#include "Theme.h"

// The manual, built into the app: the project's README from "How it works" on (its intro,
// roadmap and build notes are for developers), shown with simple formatting - headings, bullet
// points, **bold**, `names` and tables; code and diagram blocks are left out.
class HelpView : public juce::Component
{
public:
    HelpView()
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&page, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        parse (juce::String::fromUTF8 (BinaryData::README_md, BinaryData::README_mdSize));
        setSize (820, 760);
    }

    void paint (juce::Graphics& g) override   { g.fillAll (Theme::panel); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        page.layOut (getWidth() - viewport.getScrollBarThickness());
    }

private:
    struct Block { juce::AttributedString text; float indent = 0.0f, spaceBefore = 0.0f; };

    struct Page : public juce::Component
    {
        std::vector<Block> blocks;
        std::vector<std::pair<juce::Rectangle<float>, juce::TextLayout>> laid;

        void layOut (int width)
        {
            laid.clear();
            float y = 18.0f;
            const float margin = 28.0f;
            for (const auto& b : blocks)
            {
                y += b.spaceBefore;
                juce::TextLayout layout;
                const float w = (float) width - 2.0f * margin - b.indent;
                layout.createLayout (b.text, juce::jmax (100.0f, w));
                laid.push_back ({ { margin + b.indent, y, w, layout.getHeight() }, std::move (layout) });
                y += laid.back().second.getHeight() + 4.0f;
            }
            setSize (width, (int) y + 30);
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& [r, layout] : laid)
                if (g.clipRegionIntersects (r.toNearestInt()))
                    layout.draw (g, r);
        }
    };

    // Adds text with **bold** and `code` picked out.
    static void appendInline (juce::AttributedString& s, const juce::String& line, float size, juce::Colour colour, bool boldAll = false)
    {
        juce::String rest = line;
        bool bold = boldAll, italic = false;
        while (rest.isNotEmpty())
        {
            // The nearest marker: ** bold, * italic, ` name.
            const int b = rest.indexOf ("**"), c = rest.indexOf ("`");
            const int star = rest.indexOf ("*");
            int next = -1; enum { none, isBold, isItalic, isCode } kind = none;
            if (b >= 0 && b == star && (c < 0 || b < c))        { next = b; kind = isBold; }
            else if (star >= 0 && (c < 0 || star < c))          { next = star; kind = isItalic; }
            else if (c >= 0)                                    { next = c; kind = isCode; }

            const auto chunk = next < 0 ? rest : rest.substring (0, next);
            if (chunk.isNotEmpty())
                s.append (chunk, juce::Font (juce::FontOptions (size, (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0))),
                          colour);
            if (next < 0)
                break;

            if (kind == isBold)
            {
                bold = ! bold || boldAll;
                rest = rest.substring (next + 2);
            }
            else if (kind == isItalic)
            {
                italic = ! italic;
                rest = rest.substring (next + 1);
            }
            else
            {
                const int end = rest.indexOf (next + 1, "`");
                const auto code = end < 0 ? rest.substring (next + 1) : rest.substring (next + 1, end);
                s.append (code, juce::Font (juce::FontOptions (size, juce::Font::bold)), Theme::accent);
                rest = end < 0 ? juce::String() : rest.substring (end + 1);
            }
        }
    }

    void parse (const juce::String& markdown)
    {
        Block current;
        bool open = false, started = false, inCode = false;
        auto flush = [&]
        {
            if (open)
                page.blocks.push_back (current);
            current = {};
            open = false;
        };

        // The user's part starts at "How it works"; one heading of the user's own above it.
        current.text.append ("Airloom manual", juce::Font (juce::FontOptions (26.0f, juce::Font::bold)), Theme::accent);
        current.spaceBefore = 6.0f;
        open = true;
        flush();

        for (auto line : juce::StringArray::fromLines (markdown))
        {
            const auto trimmed = line.trim();
            if (trimmed.startsWith ("```"))       { flush(); inCode = ! inCode; continue; }
            if (inCode)                           continue;   // commands and text diagrams
            if (trimmed.startsWith ("## How it works"))
                started = true;
            if (! started)                        continue;
            if (trimmed.isEmpty())                { flush(); continue; }

            if (trimmed.startsWith ("#"))
            {
                flush();
                const int level = trimmed.initialSectionContainingOnly ("#").length();
                const float size = level == 1 ? 26.0f : level == 2 ? 20.0f : 16.5f;
                appendInline (current.text, trimmed.trimCharactersAtStart ("# "), size, level == 1 ? Theme::accent : Theme::text, true);
                current.spaceBefore = level == 1 ? 6.0f : 16.0f;
                open = true;
                flush();
                continue;
            }

            if (trimmed.startsWith ("|"))
            {
                if (trimmed.containsOnly ("|-: "))
                    continue;   // a table's divider line
                flush();
                auto cells = juce::StringArray::fromTokens (trimmed.substring (1, trimmed.length() - 1), "|", "");
                cells.trim();
                appendInline (current.text, cells.joinIntoString ("    -    "), 13.5f, Theme::text.withAlpha (0.9f));
                current.indent = 12.0f;
                open = true;
                flush();
                continue;
            }

            const bool bullet = trimmed.startsWith ("- ") || trimmed.startsWith ("* ");
            const bool numbered = trimmed.initialSectionContainingOnly ("0123456789").isNotEmpty() && trimmed.containsChar ('.')
                               && trimmed.fromFirstOccurrenceOf (".", false, false).startsWithChar (' ');
            const int leading = line.length() - line.trimStart().length();

            if (bullet || numbered)
            {
                flush();
                current.indent = 14.0f + (float) juce::jmin (3, leading / 2) * 16.0f;
                current.text.append (bullet ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2  ")) : trimmed.upToFirstOccurrenceOf (".", true, false) + "  ",
                                     juce::Font (juce::FontOptions (15.0f, juce::Font::bold)), Theme::textDim);
                appendInline (current.text, bullet ? trimmed.substring (2) : trimmed.fromFirstOccurrenceOf (".", false, false).trim(),
                              15.0f, Theme::text);
                open = true;
                continue;
            }

            // A wrapped line carries on the paragraph or bullet it belongs to.
            if (open)
                current.text.append (" ", juce::Font (juce::FontOptions (15.0f)), Theme::text);
            appendInline (current.text, trimmed, 15.0f, Theme::text);
            open = true;
        }
        flush();

        for (auto& b : page.blocks)
            b.text.setWordWrap (juce::AttributedString::byWord);
    }

    juce::Viewport viewport;
    Page page;
};

// The first time Airloom opens: three things to know, and buttons for each.
class WelcomePanel : public juce::Component
{
public:
    std::function<void()> onAudio, onManual, onClose;

    WelcomePanel()
    {
        for (auto* b : { &audioButton, &manualButton, &startButton })
        {
            addAndMakeVisible (b);
            b->setWantsKeyboardFocus (false);
        }
        audioButton.onClick = [this] { if (onAudio) onAudio(); };
        manualButton.onClick = [this] { if (onManual) onManual(); };
        startButton.onClick = [this] { if (onClose) onClose(); };
        startButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
        startButton.setColour (juce::TextButton::textColourOffId, Theme::background);
        setSize (620, 430);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        auto area = getLocalBounds().reduced (32, 26);

        g.setColour (Theme::text);
        g.setFont (Theme::heading (24.0f, 0.2f));
        g.drawText ("WELCOME TO AIRLOOM", area.removeFromTop (36), juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("An instrument for live, looping, ambient performance.", area.removeFromTop (26), juce::Justification::centredLeft);
        area.removeFromTop (18);

        const char* steps[][2] = {
            { "1   Pick your audio interface", "Settings > Audio (or the button below). Use the smallest buffer that plays without crackles." },
            { "2   Bring in sound", "Drop audio files onto the Samples group, or arm an input or instrument in the LIVE dock and press REC to record a loop." },
            { "3   Find your way around", "Click a name, or the  ...  button, for its options. Ctrl Z undoes. F1 opens the manual any time." },
        };
        for (auto& s : steps)
        {
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (16.5f, juce::Font::bold));
            g.drawText (s[0], area.removeFromTop (24), juce::Justification::centredLeft);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (14.5f));
            g.drawFittedText (s[1], area.removeFromTop (40).withTrimmedLeft (26), juce::Justification::topLeft, 2);
            area.removeFromTop (10);
        }
    }

    void resized() override
    {
        auto buttons = getLocalBounds().reduced (32, 26).removeFromBottom (40);
        startButton.setBounds (buttons.removeFromRight (140));
        buttons.removeFromRight (10);
        manualButton.setBounds (buttons.removeFromRight (150));
        buttons.removeFromRight (10);
        audioButton.setBounds (buttons.removeFromRight (170));
    }

private:
    juce::TextButton audioButton { "Audio settings" }, manualButton { "Open the manual" }, startButton { "Let's go" };
};
