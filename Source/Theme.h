#pragma once

#include <JuceHeader.h>

// Dark, high-contrast palettes meant to be readable at a glance on a dim stage.
// Surfaces step up in brightness: background < panel < raised < panelEdge.
// The colours below are the current theme's; switchTo() changes them (Set > Theme).
namespace Theme
{
    struct Palette
    {
        const char* name;
        juce::uint32 background, panel, raised, panelEdge, text, textDim, accent, warn, danger;

        // The theme's take on the 16 group colours: each blended a little towards a tint, then
        // its saturation and brightness scaled. Every colour keeps its own hue, so a group's
        // colour is recognisably the same in every theme and they stay distinct from each other.
        juce::uint32 groupTint;
        float tintAmount, groupSaturation, groupBrightness;
    };

    constexpr int numPalettes = 6;
    inline const Palette palettes[numPalettes] {
        //                  background  panel       raised      edge        text        textDim     accent      warn        danger      group tint  amount sat    bright
        { "Graphite",      0xff0b0c0f, 0xff121418, 0xff1a1d23, 0xff2c3039, 0xffeceef2, 0xff8d93a0, 0xff4fd1b5, 0xfff2b54e, 0xffff5d5d, 0xff000000, 0.0f,  1.0f,  1.0f },
        { "Midnight",      0xff0a0d16, 0xff101523, 0xff181f31, 0xff2a3550, 0xffe8eefa, 0xff8794b0, 0xff6cb6ff, 0xfff2b54e, 0xffff6070, 0xff7f9cff, 0.22f, 0.9f,  1.0f },
        { "Ember",         0xff100b09, 0xff18110f, 0xff211915, 0xff3a2c25, 0xfff4ebe4, 0xffa39283, 0xffe9915e, 0xfff5cf57, 0xffff5d5d, 0xfff0a060, 0.2f,  0.95f, 0.98f },
        { "Forest",        0xff090d0b, 0xff0f1613, 0xff17201b, 0xff293a31, 0xffe9f2ec, 0xff8a9d91, 0xff7fd18b, 0xfff2b54e, 0xffff6b5d, 0xff9cc48a, 0.18f, 0.9f,  0.96f },
        { "Night",         0xff050506, 0xff0a0b0d, 0xff111316, 0xff1f2228, 0xffb9bdc6, 0xff6b707b, 0xff3c9f8a, 0xffc28f3e, 0xffcc4a4a, 0xff000000, 0.0f,  0.85f, 0.72f },
        { "High contrast", 0xff020202, 0xff0a0a0a, 0xff1a1a1a, 0xff4c4c4c, 0xfffdfdfd, 0xffc8c8c8, 0xff3dffd2, 0xffffc233, 0xffff4545, 0xff000000, 0.0f,  1.12f, 1.12f },
    };

    inline int currentPalette = 0;

    inline juce::Colour background  { palettes[0].background };
    inline juce::Colour panel       { palettes[0].panel };
    inline juce::Colour raised      { palettes[0].raised };
    inline juce::Colour panelEdge   { palettes[0].panelEdge };
    inline juce::Colour text        { palettes[0].text };
    inline juce::Colour textDim     { palettes[0].textDim };
    inline juce::Colour accent      { palettes[0].accent };   // calm teal: playing / OK
    inline juce::Colour warn        { palettes[0].warn };     // amber: attention
    inline juce::Colour danger      { palettes[0].danger };   // red: clipping / armed

    // Group colours: soft, distinct hues that stay readable on the dark background.
    // The first eight are the originals (sets refer to them by number), then eight more.
    constexpr int numRowColours = 16;
    inline const juce::Colour baseRowColours[numRowColours] {
        juce::Colour (0xff9d8cff),   // violet
        juce::Colour (0xffa6d160),   // green
        juce::Colour (0xff5eaef0),   // blue
        juce::Colour (0xfff2b54e),   // amber
        juce::Colour (0xfff07c9e),   // rose
        juce::Colour (0xff4fd1c4),   // teal
        juce::Colour (0xfff08c5c),   // coral
        juce::Colour (0xffd2bd8c),   // sand
        juce::Colour (0xffd9e85c),   // lime
        juce::Colour (0xff8fd8ff),   // sky
        juce::Colour (0xffd98ae8),   // orchid
        juce::Colour (0xff7c86f5),   // indigo
        juce::Colour (0xff86e8b6),   // mint
        juce::Colour (0xffe85fd0),   // magenta
        juce::Colour (0xffa7b6cb),   // slate
        juce::Colour (0xffb2a65c),   // olive
    };
    inline juce::Colour rowColours[numRowColours] {
        baseRowColours[0],  baseRowColours[1],  baseRowColours[2],  baseRowColours[3],
        baseRowColours[4],  baseRowColours[5],  baseRowColours[6],  baseRowColours[7],
        baseRowColours[8],  baseRowColours[9],  baseRowColours[10], baseRowColours[11],
        baseRowColours[12], baseRowColours[13], baseRowColours[14], baseRowColours[15],
    };

    inline int paletteIndex (const juce::String& name)
    {
        for (int i = 0; i < numPalettes; ++i)
            if (name.equalsIgnoreCase (palettes[i].name))
                return i;
        return 0;
    }

    // Every theme colour, in a fixed order (to map one theme's colours onto another's).
    inline std::vector<juce::Colour> allColours()
    {
        std::vector<juce::Colour> all { background, panel, raised, panelEdge, text, textDim, accent, warn, danger };
        all.insert (all.end(), std::begin (rowColours), std::end (rowColours));
        return all;
    }

    // Sets the colours above. Components built afterwards pick them up; switchTo() also
    // recolours what's already on screen.
    inline void applyPalette (int index)
    {
        currentPalette = juce::jlimit (0, numPalettes - 1, index);
        const auto& p = palettes[currentPalette];
        background = juce::Colour (p.background);
        panel = juce::Colour (p.panel);
        raised = juce::Colour (p.raised);
        panelEdge = juce::Colour (p.panelEdge);
        text = juce::Colour (p.text);
        textDim = juce::Colour (p.textDim);
        accent = juce::Colour (p.accent);
        warn = juce::Colour (p.warn);
        danger = juce::Colour (p.danger);

        for (int i = 0; i < numRowColours; ++i)
            rowColours[i] = baseRowColours[i].interpolatedWith (juce::Colour (p.groupTint), p.tintAmount)
                                             .withMultipliedSaturation (p.groupSaturation)
                                             .withMultipliedBrightness (p.groupBrightness);
    }
    inline const char* const rowColourNames[numRowColours] {
        "Violet", "Green", "Blue", "Amber", "Rose", "Teal", "Coral", "Sand",
        "Lime", "Sky", "Orchid", "Indigo", "Mint", "Magenta", "Slate", "Olive"
    };

    inline juce::Colour rowColour (int index)
    {
        return rowColours[(size_t) juce::jlimit (0, numRowColours - 1, index)];
    }

    // Small caps-style headings: bold with a little extra letter spacing.
    inline juce::Font heading (float size, float spacing = 0.08f)
    {
        return juce::Font (juce::FontOptions (size, juce::Font::bold)).withExtraKerningFactor (spacing);
    }

    // A surface with a soft top-lit gradient, a hairline highlight and a dark outline.
    inline void drawSurface (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float corner,
                             juce::Colour outline = juce::Colours::black.withAlpha (0.4f))
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.04f), 0.0f, r.getY(),
                                                 base.darker (0.08f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);

        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + corner, r.getRight() - corner);

        g.setColour (outline);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }

    // A group's coloured spine on the left: a thin rail with the faintest wash into the panel,
    // so the colour names the group without lighting it up.
    inline void drawSpine (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.07f), bounds.getX(), 0.0f,
                                                 colour.withAlpha (0.0f), bounds.getX() + 40.0f, 0.0f, false));
        g.fillRoundedRectangle (bounds.withWidth (40.0f), 6.0f);
        g.setColour (colour.withAlpha (0.85f));
        g.fillRoundedRectangle (bounds.withWidth (2.0f).reduced (0.0f, 6.0f), 1.0f);
    }

    // Draws a name as large as it can be (up to maxSize) while fitting the area on up
    // to two lines, so a short name reads big and a long one shrinks to fit.
    inline void drawFittedName (juce::Graphics& g, const juce::String& name, juce::Rectangle<int> area, juce::Colour colour,
                                float maxSize, float minSize = 11.0f)
    {
        const auto width = (float) area.getWidth();

        for (float size = maxSize; size >= minSize; size -= 0.5f)
        {
            juce::AttributedString styled;
            styled.setText (name);
            styled.setFont (juce::Font (juce::FontOptions (size, juce::Font::bold)));
            styled.setColour (colour);
            styled.setJustification (juce::Justification::centred);
            styled.setWordWrap (juce::AttributedString::byWord);

            juce::TextLayout layout;
            layout.createLayout (styled, width);

            bool fits = layout.getNumLines() <= 2 && layout.getHeight() <= (float) area.getHeight();
            for (int i = 0; fits && i < layout.getNumLines(); ++i)
                fits = layout.getLine (i).getLineBoundsX().getLength() <= width + 0.5f;

            // Don't break a single word across lines just to make it bigger.
            if (fits && layout.getNumLines() > 1 && ! name.trim().containsChar (' '))
                fits = false;

            if (fits)
            {
                const float top = (float) area.getY() + ((float) area.getHeight() - layout.getHeight()) / 2.0f;
                layout.draw (g, juce::Rectangle<float> ((float) area.getX(), top, width, layout.getHeight()));
                return;
            }
        }

        g.setColour (colour);
        g.setFont (juce::FontOptions (minSize, juce::Font::bold));
        g.drawFittedText (name, area, juce::Justification::centred, 2, 0.9f);
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            // Segoe UI ships with every copy of Windows 10 and 11, so the laptop looks the same.
            setDefaultSansSerifTypefaceName ("Segoe UI");
            refreshColours();
        }

        // Takes the current theme's colours.
        void refreshColours()
        {
            setColourScheme ({ background, panel, raised, panelEdge, text, accent,
                               background, accent, text });

            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::TextButton::buttonColourId, raised);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text.interpolatedWith (textDim, 0.45f));   // quiet until on
            setColour (juce::TextButton::textColourOnId, background);
            setColour (juce::Slider::thumbColourId, text);
            setColour (juce::Slider::trackColourId, accent);
            setColour (juce::Slider::backgroundColourId, panelEdge);
            setColour (juce::Slider::rotarySliderFillColourId, accent);
            setColour (juce::Slider::textBoxTextColourId, text.withAlpha (0.85f));
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.35f));
            setColour (juce::Label::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::ComboBox::backgroundColourId, raised);
            setColour (juce::ComboBox::outlineColourId, juce::Colours::black.withAlpha (0.55f));
            setColour (juce::ComboBox::arrowColourId, textDim);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::headerTextColourId, textDim);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.22f));
            setColour (juce::PopupMenu::highlightedTextColourId, text);
            setColour (juce::TextEditor::backgroundColourId, background);
            setColour (juce::TextEditor::outlineColourId, panelEdge);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
            setColour (juce::CaretComponent::caretColourId, accent);
            setColour (juce::TooltipWindow::backgroundColourId, raised);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, panelEdge);
            setColour (juce::AlertWindow::backgroundColourId, panel);
            setColour (juce::AlertWindow::textColourId, text);
            setColour (juce::AlertWindow::outlineColourId, panelEdge);
            setColour (juce::ListBox::backgroundColourId, panel);
            setColour (juce::ScrollBar::thumbColourId, text.withAlpha (0.2f));
        }

        //==============================================================================
        // Buttons: top-lit pads. Coloured (lit) buttons get a brighter highlight.
        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override
        {
            auto r = button.getLocalBounds().toFloat();
            const float corner = juce::jmin (5.0f, r.getHeight() * 0.22f);

            auto base = backgroundColour;
            if (! button.isEnabled())  base = base.withMultipliedAlpha (0.45f);
            else if (down)             base = base.darker (0.3f);
            else if (highlighted)      base = base.brighter (0.15f);

            const bool lit = base.getAlpha() > 200 && base.getPerceivedBrightness() > 0.35f;
            drawSurface (g, r, base, corner);

            if (lit)
            {
                // A light sheen across the top half, like a backlit pad.
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0.0f, r.getY(),
                                                         juce::Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false));
                g.fillRoundedRectangle (r.reduced (1.0f), corner);
            }
        }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return heading (juce::jmin (13.5f, (float) buttonHeight * 0.52f), 0.06f);
        }

        //==============================================================================
        // Faders: a recessed groove with dB ticks and a console-style cap.
        int getSliderThumbRadius (juce::Slider& s) override
        {
            return s.isBar() || s.isRotary() ? 0 : 8;
        }

        juce::Label* createSliderTextBox (juce::Slider& s) override
        {
            auto* l = LookAndFeel_V4::createSliderTextBox (s);
            l->setFont (s.isBar() ? heading (17.0f, 0.02f) : juce::Font (juce::FontOptions (12.5f)));
            return l;
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            if (s.isBar())
            {
                // A readout box with the value as a fill (the BPM).
                auto r = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
                g.setColour (juce::Colour (0xff08090b));
                g.fillRoundedRectangle (r, 4.0f);
                auto fill = style == juce::Slider::LinearBarVertical ? r.withTop (sliderPos) : r.withRight (sliderPos);
                g.setColour (s.findColour (juce::Slider::trackColourId).withAlpha (0.55f));
                g.fillRoundedRectangle (fill.reduced (1.0f), 3.0f);
                g.setColour (panelEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
                return;
            }

            if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
            {
                LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, s);
                return;
            }

            const bool vertical = style == juce::Slider::LinearVertical;
            const auto colour = s.findColour (juce::Slider::trackColourId);
            const bool dbFader = s.getMinimum() <= -59.0 && s.getMaximum() >= 5.0;
            const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
            const float centre = vertical ? area.getCentreX() : area.getCentreY();
            const float alpha = s.isEnabled() ? 1.0f : 0.4f;

            // dB scale marks either side of the groove.
            if (dbFader && (vertical ? width : height) >= 24)
            {
                for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -40.0, -60.0 })
                {
                    const float p = (float) s.valueToProportionOfLength (db);
                    const float len = db == 0.0 ? 7.0f : 4.0f;
                    g.setColour ((db == 0.0 ? text.withAlpha (0.45f) : textDim.withAlpha (0.3f)).withMultipliedAlpha (alpha));

                    if (vertical)
                    {
                        const float py = area.getBottom() - p * area.getHeight();
                        g.fillRect (centre - 9.0f - len, py - 0.5f, len, 1.0f);
                        g.fillRect (centre + 9.0f, py - 0.5f, len, 1.0f);
                    }
                    else
                    {
                        const float px = area.getX() + p * area.getWidth();
                        g.fillRect (px - 0.5f, centre - 9.0f - len, 1.0f, len);
                        g.fillRect (px - 0.5f, centre + 9.0f, 1.0f, len);
                    }
                }
            }

            // The groove, then the level up to the cap.
            auto groove = vertical ? juce::Rectangle<float> (centre - 2.5f, area.getY() - 4.0f, 5.0f, area.getHeight() + 8.0f)
                                   : juce::Rectangle<float> (area.getX() - 4.0f, centre - 2.5f, area.getWidth() + 8.0f, 5.0f);
            g.setColour (juce::Colour (0xff050608));
            g.fillRoundedRectangle (groove, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (groove.expanded (0.5f), 3.0f, 1.0f);

            auto filled = vertical ? groove.withTop (sliderPos).reduced (1.0f, 0.0f)
                                   : groove.withRight (sliderPos).reduced (0.0f, 1.0f);
            g.setGradientFill (vertical ? juce::ColourGradient (colour.withAlpha (0.35f * alpha), 0.0f, groove.getBottom(),
                                                                colour.withAlpha (alpha), 0.0f, sliderPos, false)
                                        : juce::ColourGradient (colour.withAlpha (0.35f * alpha), groove.getX(), 0.0f,
                                                                colour.withAlpha (alpha), sliderPos, 0.0f, false));
            g.fillRoundedRectangle (filled, 1.5f);

            // The cap: a ridged block with a coloured line marking its position.
            const float capLong = 16.0f;
            const float capWide = juce::jmin (vertical ? (float) width - 2.0f : (float) height - 2.0f, 30.0f);
            auto cap = vertical ? juce::Rectangle<float> (centre - capWide / 2.0f, sliderPos - capLong / 2.0f, capWide, capLong)
                                : juce::Rectangle<float> (sliderPos - capLong / 2.0f, centre - capWide / 2.0f, capLong, capWide);

            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);   // drop shadow

            const bool active = s.isMouseOverOrDragging();
            const auto top = juce::Colour (active ? 0xff6a707d : 0xff5a606c), bottom = juce::Colour (0xff272a31);
            g.setGradientFill (vertical ? juce::ColourGradient (top, 0.0f, cap.getY(), bottom, 0.0f, cap.getBottom(), false)
                                        : juce::ColourGradient (top, cap.getX(), 0.0f, bottom, cap.getRight(), 0.0f, false));
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.12f));
            for (float offset : { -4.5f, 4.5f })
            {
                if (vertical) g.fillRect (cap.getX() + 4.0f, cap.getCentreY() + offset - 0.5f, cap.getWidth() - 8.0f, 1.0f);
                else          g.fillRect (cap.getCentreX() + offset - 0.5f, cap.getY() + 4.0f, 1.0f, cap.getHeight() - 8.0f);
            }

            g.setColour (colour.withMultipliedAlpha (alpha).brighter (0.2f));
            if (vertical) g.fillRect (cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
            else          g.fillRect (cap.getCentreX() - 1.0f, cap.getY() + 2.0f, 2.0f, cap.getHeight() - 4.0f);
        }

        //==============================================================================
        // Knobs: a value arc around a dark cap with a pointer. Knobs with a centre
        // (pan, EQ gain) draw their arc from the middle.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float proportion,
                               float startAngle, float endAngle, juce::Slider& s) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
            const auto c = bounds.getCentre();
            const float arcRadius = radius - 3.0f, thickness = 3.5f;
            const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (s.isEnabled() ? 1.0f : 0.4f);
            const float angle = startAngle + proportion * (endAngle - startAngle);

            const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
            const float from = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;

            juce::Path track, value;
            track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colour (0xff050608));
            g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (std::abs (angle - from) > 0.01f)
            {
                value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
                g.setColour (colour.withAlpha (0.25f));
                g.strokePath (value, juce::PathStrokeType (thickness + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (colour);
                g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const float capRadius = arcRadius - 7.0f;
            if (capRadius > 4.0f)
            {
                auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (c);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillEllipse (cap.translated (0.0f, 2.0f));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff474c57), c.x, cap.getY(),
                                                         juce::Colour (0xff1f2228), c.x, cap.getBottom(), false));
                g.fillEllipse (cap);
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.drawEllipse (cap.reduced (0.5f), 1.0f);

                juce::Path pointer;
                pointer.addRoundedRectangle (-1.25f, -capRadius + 2.5f, 2.5f, capRadius * 0.55f, 1.0f);
                g.setColour (text);
                g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
            }
        }

        //==============================================================================
        // Tick boxes become small LEDs: lit in the control's colour when on.
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            const float size = juce::jmin (14.0f, r.getHeight() - 4.0f);
            const auto led = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - size / 2.0f, size, size);
            const bool on = b.getToggleState();
            const auto colour = b.findColour (juce::ToggleButton::tickColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f);

            g.setColour (juce::Colour (0xff050608));
            g.fillRoundedRectangle (led, 3.0f);

            if (on)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.fillRoundedRectangle (led.expanded (2.0f), 5.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (led.reduced (2.5f), 2.0f);
            }

            g.setColour (on ? colour : panelEdge.brighter (highlighted ? 0.4f : 0.0f));
            g.drawRoundedRectangle (led.reduced (0.5f), 3.0f, 1.0f);

            g.setColour (on ? text : (highlighted ? text.withAlpha (0.8f) : textDim));
            g.setFont (heading (juce::jmin (13.5f, r.getHeight() * 0.6f), 0.04f));
            g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (size + 10.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1);
        }

        //==============================================================================
        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<float> ((float) width, (float) height);
            drawSurface (g, r, box.findColour (juce::ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.1f : 0.0f), 4.0f);

            juce::Path chevron;
            const float cx = r.getRight() - 14.0f, cy = r.getCentreY();
            chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
            chevron.lineTo (cx, cy + 2.0f);
            chevron.lineTo (cx + 4.0f, cy - 2.0f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
            g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override   { return juce::Font (juce::FontOptions (13.5f)); }

        //==============================================================================
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
            g.setColour (panelEdge);
            g.drawRect (0, 0, width, height);
        }

        juce::Font getPopupMenuFont() override   { return juce::Font (juce::FontOptions (15.0f)); }

        //==============================================================================
        // Tabs (the Settings window): plain labels, the open one in full text with an accent line under it.
        int getTabButtonBestWidth (juce::TabBarButton& button, int) override
        {
            return juce::GlyphArrangement::getStringWidthInt (heading (13.0f, 0.1f), button.getButtonText().toUpperCase()) + 44;
        }

        void drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool over, bool) override
        {
            const auto r = button.getActiveArea();
            const bool front = button.isFrontTab();
            g.setColour (front ? panel : (over ? raised : background.interpolatedWith (panel, 0.5f)));
            g.fillRect (r);
            g.setColour (front ? text : (over ? text.withAlpha (0.85f) : textDim));
            g.setFont (heading (13.0f, 0.1f));
            g.drawText (button.getButtonText().toUpperCase(), r, juce::Justification::centred);
            if (front)
            {
                g.setColour (accent);
                g.fillRect (r.withTop (r.getBottom() - 2));
            }
        }

        void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h) override
        {
            g.setColour (panelEdge);
            g.fillRect (0, h - 1, w, 1);
        }

        //==============================================================================
        // Dialogs (rename, confirm): the panel colour with a thin accent line across the top,
        // a heading for the title, no icon.
        juce::Font getAlertWindowTitleFont() override     { return heading (18.0f, 0.06f); }
        juce::Font getAlertWindowMessageFont() override   { return juce::Font (juce::FontOptions (15.0f)); }
        juce::Font getAlertWindowFont() override          { return juce::Font (juce::FontOptions (15.0f)); }
        int getAlertWindowButtonHeight() override         { return 34; }

        void drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                           juce::TextLayout& textLayout) override
        {
            auto r = alert.getLocalBounds();
            g.fillAll (panel);
            g.setColour (accent);
            g.fillRect (r.removeFromTop (3));
            g.setColour (panelEdge);
            g.drawRect (alert.getLocalBounds(), 1);
            textLayout.draw (g, textArea.toFloat().withTrimmedTop (4.0f));
        }

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                            int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override
        {
            auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                  : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
            g.setColour (text.withAlpha (mouseDown ? 0.4f : mouseOver ? 0.3f : 0.16f));
            g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), juce::jmin (width, height) * 0.5f - 2.0f);
        }

        void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillPath (path, juce::AffineTransform::translation (0.0f, 3.0f));
            g.setColour (panel);
            g.fillPath (path);
            g.setColour (panelEdge.brighter (0.2f));
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }
    };

    // Changes theme while running: new colours, the look-and-feel's defaults, and every
    // colour a component on screen was given from the old theme (matched by RGB, keeping
    // its transparency) moves to the new theme's colour in the same role.
    inline void recolour (juce::Component& c, const std::vector<juce::Colour>& from, const std::vector<juce::Colour>& to)
    {
        const auto& props = c.getProperties();
        std::vector<std::pair<int, juce::Colour>> changes;

        for (int i = 0; i < props.size(); ++i)
        {
            const auto name = props.getName (i).toString();
            if (! name.startsWith ("jcclr_"))
                continue;

            const auto colour = juce::Colour ((juce::uint32) (int) props.getValueAt (i));
            for (size_t k = 0; k < from.size(); ++k)
                if ((colour.getARGB() & 0x00ffffff) == (from[k].getARGB() & 0x00ffffff))
                {
                    changes.push_back ({ (int) name.substring (6).getHexValue32(), to[k].withAlpha (colour.getAlpha()) });
                    break;
                }
        }

        for (const auto& [id, colour] : changes)
            c.setColour (id, colour);

        for (auto* child : c.getChildren())
            recolour (*child, from, to);
    }

    // Called after a theme change (the app uses it to colour the window's title bar).
    inline std::function<void()> onSwitched;

    inline void switchTo (int index)
    {
        const auto from = allColours();
        applyPalette (index);
        const auto to = allColours();

        if (auto* lf = dynamic_cast<LookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
            lf->refreshColours();

        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* top = desktop.getComponent (i))
            {
                recolour (*top, from, to);
                top->sendLookAndFeelChange();
                top->repaint();
            }

        if (onSwitched)
            onSwitched();
    }
}
