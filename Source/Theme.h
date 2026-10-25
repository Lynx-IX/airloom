#pragma once

#include <JuceHeader.h>

// Dark, high-contrast palette meant to be readable at a glance on a dim stage.
namespace Theme
{
    inline const juce::Colour background  { 0xff0e0f12 };
    inline const juce::Colour panel       { 0xff181a1f };
    inline const juce::Colour panelEdge   { 0xff2a2d35 };
    inline const juce::Colour text        { 0xffe8e6e1 };
    inline const juce::Colour textDim     { 0xff8a8d96 };
    inline const juce::Colour accent      { 0xff5fb3a1 };   // calm teal: playing / OK
    inline const juce::Colour warn        { 0xffe0a84f };   // amber: attention
    inline const juce::Colour danger      { 0xffe0605a };   // red: clipping / armed

    // Row colours: soft, distinct hues that stay readable on the dark background.
    constexpr int numRowColours = 8;
    inline const juce::Colour rowColours[numRowColours] {
        juce::Colour (0xff9b8cff),   // violet
        juce::Colour (0xffa3c95f),   // green
        juce::Colour (0xff5fa8e0),   // blue
        juce::Colour (0xffe0a84f),   // amber
        juce::Colour (0xffe07a9a),   // rose
        juce::Colour (0xff5fc9c0),   // teal
        juce::Colour (0xffe08a5f),   // coral
        juce::Colour (0xffc9b58a),   // sand
    };
    inline const char* const rowColourNames[numRowColours] {
        "Violet", "Green", "Blue", "Amber", "Rose", "Teal", "Coral", "Sand"
    };

    inline juce::Colour rowColour (int index)
    {
        return rowColours[(size_t) juce::jlimit (0, numRowColours - 1, index)];
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColourScheme ({ background, panel, panel, panelEdge, text, accent,
                               background, accent, text });

            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::TextButton::buttonColourId, panel);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, background);
            setColour (juce::Slider::thumbColourId, text);
            setColour (juce::Slider::trackColourId, accent);
            setColour (juce::Slider::backgroundColourId, panelEdge);
            setColour (juce::Label::textColourId, text);
        }
    };
}
