#pragma once

#include <JuceHeader.h>

// Where Airloom keeps this computer's settings, session, recordings and crash log:
// %APPDATA%\Airloom. Computers that ran it as Live Rig keep %APPDATA%\LiveRig - the session and
// unsaved recordings there are referred to by full path, so the folder isn't moved.
namespace AppFolder
{
    inline juce::String name()
    {
        const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
        return appData.getChildFile ("LiveRig").isDirectory() && ! appData.getChildFile ("Airloom").isDirectory()
                   ? "LiveRig" : "Airloom";
    }

    inline juce::File get()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (name());
    }
}
