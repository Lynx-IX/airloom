#pragma once

#include <JuceHeader.h>

// Soft takeover for a MIDI fader or knob: it only takes over an on-screen control once it
// reaches the control's position (or passes it), so a hardware control left somewhere else
// can't make the sound jump.
struct SoftTakeover
{
    bool locked = false;
    float last = -1.0f;   // where the hardware was last time; -1 = not heard from yet

    // Moves the control if the hardware has caught it, and says whether it did.
    bool apply (juce::Slider& control, float proportion)
    {
        const float current = (float) control.valueToProportionOfLength (control.getValue());

        // Moved on screen since the hardware last set it: the hardware has to catch up again.
        if (locked && std::abs (current - last) > 0.02f)
            locked = false;

        if (! locked)
        {
            const bool crossed = last >= 0.0f && (last - current) * (proportion - current) <= 0.0f;
            locked = std::abs (proportion - current) < 0.03f || crossed;
        }
        last = proportion;

        if (! locked)
            return false;

        // Told now, not later: a screen refresh in between would otherwise put the control back
        // to the value it's showing (a loop's volume did, so its MIDI fader never moved it).
        control.setValue (control.proportionOfLengthToValue (proportion), juce::sendNotificationSync);
        return true;
    }
};
