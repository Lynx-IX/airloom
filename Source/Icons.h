#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

// Button icons: the SVGs in Resources/Icons (24 x 24 line drawings in "currentColor"), built
// into the app. A button shows one beside its label once given a name - Icons::set (button,
// "record") - and the icon takes the label's colour, so it follows the theme and lit states.
namespace Icons
{
    // The icon called name (its file name without .svg) in a colour; null if there's no such icon.
    inline juce::Drawable* get (const juce::String& name, juce::Colour colour)
    {
        static std::map<juce::String, std::unique_ptr<juce::Drawable>> cache;
        const auto key = name + "/" + colour.toString();
        if (auto it = cache.find (key); it != cache.end())
            return it->second.get();

        std::unique_ptr<juce::Drawable> icon;
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])) == name + ".svg")
            {
                int size = 0;
                const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                const auto svg = juce::String::fromUTF8 (data, size).replace ("currentColor", "#" + colour.toDisplayString (false));
                if (auto xml = juce::parseXML (svg))
                    icon = juce::Drawable::createFromSVG (*xml);
                break;
            }
        return (cache[key] = std::move (icon)).get();
    }

    inline void draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour)
    {
        if (auto* icon = get (name, colour.withAlpha (1.0f)))
            icon->drawWithin (g, area, juce::RectanglePlacement::centred, colour.getFloatAlpha());
    }

    // Gives a button an icon (or takes it away with an empty name).
    inline void set (juce::Button& button, const juce::String& name)
    {
        if (button.getProperties()["icon"].toString() != name)
        {
            button.getProperties().set ("icon", name);
            button.repaint();
        }
    }

    inline juce::String of (const juce::Button& button)   { return button.getProperties()["icon"].toString(); }
}
