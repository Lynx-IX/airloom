#pragma once

#include <JuceHeader.h>

// Working out a loop's tempo, so the set's BPM can follow it.
namespace TempoGuess
{
    // A tempo written in the file name, like "Drum Loop 72 BPM" or "pad_bpm90". 0 if none.
    inline double fromName (const juce::String& name)
    {
        const auto text = name.toLowerCase();

        for (int at = text.indexOf ("bpm"); at >= 0; at = text.indexOf (at + 3, "bpm"))
        {
            // The number just before "bpm" (allowing a space, dash or underscore)...
            int end = at;
            while (end > 0 && juce::String (" _-").containsChar (text[end - 1]))
                --end;
            int start = end;
            while (start > 0 && (juce::CharacterFunctions::isDigit (text[start - 1]) || text[start - 1] == '.'))
                --start;

            double bpm = text.substring (start, end).getDoubleValue();

            // ...or just after it.
            if (bpm <= 0.0)
            {
                int s = at + 3;
                while (s < text.length() && juce::String (" _-").containsChar (text[s]))
                    ++s;
                int e = s;
                while (e < text.length() && (juce::CharacterFunctions::isDigit (text[e]) || text[e] == '.'))
                    ++e;
                bpm = text.substring (s, e).getDoubleValue();
            }

            if (bpm >= 40.0 && bpm <= 250.0)
                return bpm;
        }

        return 0.0;
    }

    struct Option
    {
        int bars;
        double bpm;
    };

    // Tempos that fit a loop of this length exactly, as a whole number of bars of 'beatsPerBar'.
    inline std::vector<Option> fromLength (double seconds, int beatsPerBar = 4, double minBpm = 50.0, double maxBpm = 200.0)
    {
        std::vector<Option> options;
        if (seconds <= 0.0)
            return options;

        for (int bars : { 1, 2, 3, 4, 6, 8, 12, 16, 32 })
            if (const double bpm = 60.0 * beatsPerBar * bars / seconds; bpm >= minBpm && bpm <= maxBpm)
                options.push_back ({ bars, bpm });

        return options;
    }

    // Of the tempos a loop of this length fits, the one nearest 'currentBpm' (by ratio,
    // so 72 is as near 144 as it is 36). An empty option (0 bars) if none fits.
    inline Option nearest (double seconds, double currentBpm, int beatsPerBar = 4)
    {
        Option best { 0, 0.0 };
        double bestDistance = 1.0e9;
        for (const auto& o : fromLength (seconds, beatsPerBar))
            if (const double d = std::abs (std::log (o.bpm / currentBpm)); d < bestDistance)
            {
                bestDistance = d;
                best = o;
            }
        return best;
    }
}
