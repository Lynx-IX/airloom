#pragma once

#include <JuceHeader.h>

// The Akai Fire's 64 pads (16 x 4) as a light show, driven by what's going to the speakers.
// The audio thread feeds it samples; about 30 times a second the message thread calls update()
// (analysis, and the patterns that move by themselves) and then render() for each pattern and
// palette a Fire is showing - on one Fire (16 x 4), or two joined (32 x 4 side by side, 16 x 8
// stacked). Pad 0 is top-left, row by row. Levels adjust to the music, so a
// quiet ambient set still lights up, and silence goes dark.
class FireVisualizer
{
public:
    static constexpr int columns = 16, rows = 4;
    using Pads = std::array<juce::Colour, 64>;

    // ---- Patterns ----
    enum Pattern { spectrum, mirror, glow, plasma, waves, bursts, kaleidoscope, diamonds, stars, numPatterns };

    static juce::String patternName (int p)
    {
        static const char* names[] = { "Spectrum", "Mirror", "Glow", "Plasma", "Waves", "Bursts", "Kaleidoscope", "Diamonds", "Stars" };
        return names[juce::jlimit (0, numPatterns - 1, p)];
    }

    // ---- Palettes: gradients the patterns pick colours from (0 = start, 1 = end) ----
    static constexpr int numPalettes = 6;

    static juce::String paletteName (int p)
    {
        static const char* names[] = { "Ambient", "Aurora", "Ember", "Ocean", "Rainbow", "Moon" };
        return names[juce::jlimit (0, numPalettes - 1, p)];
    }

    static juce::Colour paletteColour (int palette, float t)
    {
        t = t - std::floor (t);   // patterns may run past the ends: wrap round
        if (palette == 4)
            return juce::Colour::fromHSV (t, 0.9f, 1.0f, 1.0f);

        static const juce::uint32 stops[numPalettes][4] = {
            { 0xff19c9b0, 0xff3a6cf0, 0xff8a4bf0, 0xfff0609a },   // Ambient: teal, blue, violet, rose
            { 0xff2bf07a, 0xff15c9c0, 0xff4b5cf0, 0xffb04bf0 },   // Aurora: green, teal, blue, violet
            { 0xff8a0f05, 0xfff04010, 0xfff0a020, 0xfffff0a0 },   // Ember: deep red, orange, gold, pale
            { 0xff051a6a, 0xff0a6af0, 0xff20d0f0, 0xffd0f8ff },   // Ocean: navy, blue, cyan, foam
            { 0, 0, 0, 0 },                                       // Rainbow (above)
            { 0xff203060, 0xff6080c0, 0xffb0c8f0, 0xffffffff },   // Moon: night blue to white
        };
        const auto& s = stops[juce::jlimit (0, numPalettes - 1, palette)];
        // Out and back (0 -> 1 -> 0 round the circle), so wrapping never jumps.
        const float u = (t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f) * 3.0f;
        const int i = juce::jlimit (0, 2, (int) u);
        return juce::Colour (s[i]).interpolatedWith (juce::Colour (s[i + 1]), u - (float) i);
    }

    void prepare (double newSampleRate)     { sampleRate = newSampleRate; }

    // Audio thread: the output, after the master.
    void push (const float* left, const float* right, int numSamples)
    {
        int w = writeIndex.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) w] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            w = (w + 1) & (ringSize - 1);
        }
        writeIndex.store (w, std::memory_order_release);
    }

    // Once per frame: listens, finds hits (a beat of the tempo grid while 'inTime', or a sudden
    // rise in level), and moves the patterns that have a life of their own.
    void update (double beats, bool inTime)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double dt = lastUpdate == 0.0 ? 1.0 / 30.0 : juce::jlimit (0.0, 0.2, now - lastUpdate);
        lastUpdate = now;
        time = now;

        analyse();
        phase += dt * (0.35 + 1.8 * overall);   // everything drifts faster when the music is fuller
        beatPosition = beats;
        pulsing = inTime;

        // Hits: the beat when loops play in time, otherwise jumps in level (a note, a drum).
        bool hit = false;
        if (inTime)
        {
            hit = (long long) std::floor (beats) != lastBeat;
            lastBeat = (long long) std::floor (beats);
        }
        const float jump = overall - slowOverall;
        slowOverall += (overall - slowOverall) * 0.05f;
        if (! inTime && jump > 0.08f && now - lastHit > 0.15)
            hit = true;
        if (hit)
        {
            lastHit = now;
            spawnBurst();
        }

        // Bursts expand and fade; stars twinkle where their column is loud.
        activeBursts.erase (std::remove_if (activeBursts.begin(), activeBursts.end(), [now] (const Burst& b) { return now - b.born > 1.6; }), activeBursts.end());
        for (int i = 0; i < maxPads; ++i)
        {
            auto& s = starLevel[(size_t) i];
            s *= 0.86f;
            const float chance = 0.02f + 0.3f * smoothed[(size_t) (i % columns)] * smoothed[(size_t) (i % columns)] + (hit ? 0.12f : 0.0f);
            if (random.nextFloat() < chance * overall * 1.5f + 0.004f)
            {
                s = 1.0f;
                starHue[(size_t) i] = random.nextFloat();
            }
        }
    }

    // The pads for one pattern and palette, as things stand after update(), on a canvas of
    // width x height pads (row by row from the top-left).
    std::vector<juce::Colour> render (int pattern, int palette, int width = columns, int height = rows) const
    {
        std::vector<juce::Colour> pads ((size_t) (width * height), juce::Colours::black);
        const float cx = (float) (width - 1) * 0.5f, cy = (float) (height - 1) * 0.5f;
        const float beatFlash = pulsing ? (float) std::exp (-5.0 * (beatPosition - std::floor (beatPosition))) : 0.0f;
        const float energy = juce::jlimit (0.0f, 1.0f, overall * 1.8f);   // typical music sits around 0.3: lift it

        for (int r = 0; r < height; ++r)
        {
            for (int c = 0; c < width; ++c)
            {
                const float x = (float) c, y = (float) r;
                const float dx = x - cx, dy = (y - cy) * 1.6f;   // rows sit a little apart
                const float dist = std::sqrt (dx * dx + dy * dy);
                const float band = smoothed[(size_t) (c * columns / width)];
                const float t = (float) phase;
                juce::Colour colour;

                switch (pattern)
                {
                    case spectrum:
                    {
                        // Bars rising from the bottom, lows on the left.
                        const float fill = juce::jlimit (0.0f, 1.0f, band * (float) height - (float) (height - 1 - r));
                        colour = paletteColour (palette, (float) c / (float) width * 0.5f).withMultipliedBrightness (fill > 0.02f ? 0.12f + 0.88f * fill : 0.0f);
                        break;
                    }
                    case mirror:
                    {
                        // Lows in the middle, highs at the edges, bars growing up and down from the centre line.
                        const float fromCentre = std::abs (x - cx) / (cx + 0.5f);   // 0 middle .. 1 edge
                        const float level = smoothed[(size_t) juce::jlimit (0, columns - 1, (int) (fromCentre * columns))];
                        const float rowFromMiddle = std::abs (y - cy) - 0.5f;   // 0 for the two middle rows, then 1, 2...
                        const float fill = juce::jlimit (0.0f, 1.0f, level * (float) height * 0.5f - rowFromMiddle);
                        colour = paletteColour (palette, 0.6f * fromCentre + t * 0.02f).withMultipliedBrightness (fill > 0.02f ? 0.1f + 0.9f * fill : 0.0f);
                        break;
                    }
                    case glow:
                    {
                        // Slow waves of colour, brighter with the music, pulsing on the beat.
                        const float wave = 0.5f + 0.5f * std::sin (x * 0.45f + y * 0.9f - t * 2.0f) * std::cos (x * 0.13f - y * 0.5f + t * 0.7f);
                        const float level = juce::jlimit (0.0f, 1.0f, 0.2f + 0.5f * energy + 0.3f * band) * (0.25f + 0.75f * wave);
                        colour = paletteColour (palette, t * 0.03f + 0.3f * x / (float) width + 0.08f * wave)
                                     .withMultipliedBrightness (juce::jlimit (0.0f, 1.0f, level + beatFlash * 0.3f));
                        break;
                    }
                    case plasma:
                    {
                        // Classic plasma: sums of sines, flowing; the music sets brightness and speed.
                        const float v = std::sin (x * 0.42f + t) + std::sin (y * 0.95f + t * 0.73f)
                                      + std::sin ((x + y) * 0.35f + t * 1.3f) + std::sin (dist * 0.9f - t * 2.1f);
                        const float shade = 0.4f + 0.6f * (0.5f + 0.5f * std::sin (v * 1.5f));
                        colour = paletteColour (palette, v * 0.125f + 0.5f + t * 0.04f)
                                     .withMultipliedBrightness (juce::jlimit (0.0f, 1.0f, (0.35f + 0.65f * energy) * shade + 0.2f * beatFlash));
                        break;
                    }
                    case waves:
                    {
                        // Two lines snaking across: the lows (slow, wide) and the highs (quick, tight).
                        const float low = average (0, 5), high = average (10, 16);
                        const float swing = 1.6f * (float) height / rows;
                        const float y1 = cy + swing * (0.25f + low) * std::sin (x * 0.55f - t * 2.6f);
                        const float y2 = cy + swing * (0.2f + high) * std::sin (x * 0.95f + t * 3.4f);
                        const float a = std::exp (-(y - y1) * (y - y1) * 2.2f) * (0.45f + 0.55f * low);
                        const float b = std::exp (-(y - y2) * (y - y2) * 2.2f) * (0.45f + 0.55f * high);
                        const auto ca = paletteColour (palette, 0.1f + t * 0.02f).withMultipliedBrightness (a);
                        const auto cb = paletteColour (palette, 0.6f + t * 0.02f).withMultipliedBrightness (b);
                        colour = juce::Colour::fromFloatRGBA (juce::jmin (1.0f, ca.getFloatRed() + cb.getFloatRed()),
                                                              juce::jmin (1.0f, ca.getFloatGreen() + cb.getFloatGreen()),
                                                              juce::jmin (1.0f, ca.getFloatBlue() + cb.getFloatBlue()), 1.0f);
                        break;
                    }
                    case bursts:
                    {
                        // Rings bursting outward from each hit, fading as they grow.
                        float r1 = 0.0f, g1 = 0.0f, b1 = 0.0f;
                        for (const auto& bu : activeBursts)
                        {
                            const float age = (float) (time - bu.born);
                            const float radius = age * 9.0f;
                            const float ddx = x - bu.x * (float) (width - 1), ddy = (y - bu.y * (float) (height - 1)) * 1.6f;
                            const float d = std::sqrt (ddx * ddx + ddy * ddy);
                            const float ringLight = std::exp (-(d - radius) * (d - radius) * 0.9f) * (1.0f - age / 1.6f) + (d < 1.0f && age < 0.15f ? 1.0f : 0.0f);
                            const auto bc = paletteColour (palette, bu.hue);
                            r1 += bc.getFloatRed() * ringLight;
                            g1 += bc.getFloatGreen() * ringLight;
                            b1 += bc.getFloatBlue() * ringLight;
                        }
                        const auto base = paletteColour (palette, 0.5f).withMultipliedBrightness (0.06f + 0.12f * energy);
                        colour = juce::Colour::fromFloatRGBA (juce::jmin (1.0f, r1 + base.getFloatRed()), juce::jmin (1.0f, g1 + base.getFloatGreen()),
                                                              juce::jmin (1.0f, b1 + base.getFloatBlue()), 1.0f);
                        break;
                    }
                    case kaleidoscope:
                    {
                        // Spokes turning round the centre, a ring breathing out on the beat.
                        const float angle = std::atan2 (dy, dx);
                        const float spokes = 0.5f + 0.5f * std::cos (3.0f * (angle - t * 1.2f));
                        const float breath = 0.5f + 0.5f * std::sin (dist * 1.1f - t * 3.0f);
                        const float level = juce::jlimit (0.0f, 1.0f, (0.35f + 0.65f * energy) * (0.06f + 0.94f * spokes * spokes * spokes * (0.6f + 0.4f * breath))
                                                                      + 0.3f * beatFlash);
                        colour = paletteColour (palette, angle / juce::MathConstants<float>::twoPi + t * 0.05f + dist * 0.04f).withMultipliedBrightness (level);
                        break;
                    }
                    case diamonds:
                    {
                        // Diamond rings flowing out from the centre; the beat sends a bright one.
                        const float d = std::abs (dx) + std::abs (dy);
                        const float flow = 0.5f + 0.5f * std::sin (d * 1.3f - t * 4.0f);
                        const float beatRing = pulsing ? std::exp (-std::pow (d - (float) (beatPosition - std::floor (beatPosition)) * 12.0f, 2.0f) * 0.5f) : 0.0f;
                        const float level = juce::jlimit (0.0f, 1.0f, (0.35f + 0.65f * energy) * flow * flow * flow + beatRing * (0.5f + 0.5f * energy));
                        colour = paletteColour (palette, d * 0.05f + t * 0.03f).withMultipliedBrightness (level);
                        break;
                    }
                    case stars:
                    default:
                    {
                        // Twinkles, more where the music is loud, over a dim sky.
                        const int i = (r * width + c) % maxPads;
                        const float s = starLevel[(size_t) i];
                        const auto sky = paletteColour (palette, 0.15f + 0.1f * y).withMultipliedBrightness (0.06f + 0.1f * energy);
                        const auto star = paletteColour (palette, starHue[(size_t) i]).withMultipliedBrightness (s);
                        colour = juce::Colour::fromFloatRGBA (juce::jmin (1.0f, sky.getFloatRed() + star.getFloatRed()),
                                                              juce::jmin (1.0f, sky.getFloatGreen() + star.getFloatGreen()),
                                                              juce::jmin (1.0f, sky.getFloatBlue() + star.getFloatBlue()), 1.0f);
                        break;
                    }
                }

                pads[(size_t) (r * width + c)] = colour;
            }
        }
        return pads;
    }

    // 0..1 per column (low to high), and the whole mix, after the last update().
    float bandLevel (int column) const      { return smoothed[(size_t) column]; }
    float overallLevel() const              { return overall; }
    int numBursts() const                   { return (int) activeBursts.size(); }

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, ringSize = 8192;

    struct Burst { float x, y, hue; double born; };   // x, y: 0..1 across the canvas
    static constexpr int maxPads = 128;                // two Fires

    double sampleRate = 48000.0;
    std::array<float, ringSize> ring {};
    std::atomic<int> writeIndex { 0 };

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, columns> smoothed {};
    float loudest = 1.0e-4f;     // recent loudest band, slowly forgotten: the scale
    float overall = 0.0f, slowOverall = 0.0f;
    double phase = 0.0, time = 0.0, lastUpdate = 0.0, lastHit = 0.0, beatPosition = 0.0;
    long long lastBeat = -1;
    bool pulsing = false;
    std::vector<Burst> activeBursts;
    std::array<float, maxPads> starLevel {}, starHue {};
    juce::Random random;

    float average (int from, int to) const
    {
        float sum = 0.0f;
        for (int c = from; c < to; ++c)
            sum += smoothed[(size_t) c];
        return sum / (float) (to - from);
    }

    void spawnBurst()
    {
        // From the centre, or somewhere along the middle; coloured by where the sound sits (low to high).
        float centroid = 0.0f, total = 1.0e-6f;
        for (int c = 0; c < columns; ++c)
        {
            centroid += smoothed[(size_t) c] * (float) c;
            total += smoothed[(size_t) c];
        }
        const bool centre = random.nextFloat() < 0.4f;
        activeBursts.push_back ({ centre ? 0.5f : 0.06f + random.nextFloat() * 0.88f, centre ? 0.5f : random.nextFloat(),
                            centroid / total / columns + random.nextFloat() * 0.15f, time });
        if (activeBursts.size() > 6)
            activeBursts.erase (activeBursts.begin());
    }

    void analyse()
    {
        // The latest fftSize samples (a block being written meanwhile only adds a little noise).
        const int end = writeIndex.load (std::memory_order_acquire);
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = ring[(size_t) ((end - fftSize + i) & (ringSize - 1))];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // 16 bands spaced evenly in pitch, 40 Hz to 12 kHz.
        std::array<float, columns> bands {};
        const double binHz = sampleRate / fftSize;
        float frameMax = 0.0f;
        for (int c = 0; c < columns; ++c)
        {
            const double lo = 40.0 * std::pow (300.0, (double) c / columns);
            const double hi = 40.0 * std::pow (300.0, (double) (c + 1) / columns);
            const int b0 = juce::jlimit (1, fftSize / 2 - 1, (int) (lo / binHz));
            const int b1 = juce::jlimit (b0 + 1, fftSize / 2, (int) (hi / binHz) + 1);
            float sum = 0.0f;
            for (int b = b0; b < b1; ++b)
                sum += fftData[(size_t) b];
            // Higher bands hold less energy in most music; lift them so the bars are even.
            bands[(size_t) c] = sum / (float) (b1 - b0) * (1.0f + 0.25f * (float) c);
            frameMax = juce::jmax (frameMax, bands[(size_t) c]);
        }

        // The scale follows the music: up at once, back down over a few seconds.
        loudest = juce::jmax (frameMax, loudest * 0.985f, 1.0e-4f);

        float total = 0.0f;
        for (int c = 0; c < columns; ++c)
        {
            const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (bands[(size_t) c] / loudest));
            auto& s = smoothed[(size_t) c];
            s = level > s ? s + (level - s) * 0.6f : s + (level - s) * 0.15f;   // quick rise, slow fall
            total += s;
        }

        // Silence reads as silence, however low the scale has sunk.
        if (frameMax < 1.0e-3f)
            for (auto& s : smoothed)
                s *= 0.8f;

        overall = overall + (total / columns - overall) * 0.2f;
    }
};
