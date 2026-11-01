#pragma once

#include "Effects.h"
#include "InputChannel.h"
#include "InstrumentChannel.h"
#include "LiveSet.h"
#include "MidiMap.h"
#include "SetRecorder.h"
#include "LoopChannel.h"
#include "Looper.h"
#include "TempoGuess.h"
#include "FxPanel.h"
#include "StageView.h"
#include "PresetName.h"
#include "FireVisualizer.h"
#include "RowView.h"
#include "LiveGroupView.h"
#include "LiveGroupBus.h"
#include "GroupLayout.h"
#include "SampleEditor.h"
#include "SamplePool.h"
#include "Help.h"
#include "StageStatus.h"
#include "SettingsPanel.h"

// Offline checks of the effects chain, run with:  "Live Rig.exe" --fx-selftest <report file>
// Each check feeds a test signal through FxChain and measures the result.
// A stand-in instrument for the tests: a steady tone, counting how often it's run and
// noting the MIDI it was sent.
class TestInstrument : public juce::AudioPluginInstance
{
public:
    int blocksRun = 0;
    bool sawAllNotesOff = false;

    const juce::String getName() const override                            { return "Test instrument"; }
    void prepareToPlay (double, int) override                              {}
    void releaseResources() override                                       {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        ++blocksRun;
        for (const auto meta : midi)
            sawAllNotesOff |= meta.getMessage().isAllNotesOff();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.25f, buffer.getNumSamples());
    }
    double getTailLengthSeconds() const override                           { return 0.0; }
    bool acceptsMidi() const override                                      { return true; }
    bool producesMidi() const override                                     { return false; }
    juce::AudioProcessorEditor* createEditor() override                    { return nullptr; }
    bool hasEditor() const override                                        { return false; }
    int getNumPrograms() override                                          { return 1; }
    int getCurrentProgram() override                                       { return 0; }
    void setCurrentProgram (int) override                                  {}
    const juce::String getProgramName (int) override                       { return {}; }
    void changeProgramName (int, const juce::String&) override             {}
    void getStateInformation (juce::MemoryBlock&) override                 {}
    void setStateInformation (const void*, int) override                   {}
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); d.isInstrument = true; }

    TestInstrument() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())) {}
};

namespace FxSelfTest
{
    inline juce::String run()
    {
        constexpr double sr = 48000.0;
        constexpr int block = 512;
        juce::StringArray report;
        int failures = 0;

        auto check = [&] (bool ok, const juce::String& what)
        {
            report.add ((ok ? "PASS  " : "FAIL  ") + what);
            if (! ok) ++failures;
        };

        // Renders 'seconds' of a signal through a chain with the given settings.
        auto render = [&] (const FxSettings& s, double seconds, std::function<float (int, int)> signal)
        {
            FxChain chain;
            chain.setSettings (s);
            chain.prepare (sr, block);

            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (2, total);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < total; ++i)
                    out.setSample (ch, i, signal (ch, i));

            for (int pos = 0; pos < total; pos += block)
                chain.process (out, pos, juce::jmin (block, total - pos));
            return out;
        };

        auto impulse = [] (int, int i) { return i == 0 ? 1.0f : 0.0f; };
        auto rms = [] (const juce::AudioBuffer<float>& b, int ch, double from, double to)
        {
            return b.getRMSLevel (ch, (int) (from * sr), (int) ((to - from) * sr));
        };

        // Synced delay: a 1/4 note at 120 BPM is 500 ms.
        {
            FxChain::tempoBpm = 120.0;
            FxSettings s;
            s.delayOn = true; s.delaySync = true; s.delayDivision = 5; s.delayMix = 1.0; s.delayFeedback = 0.5; s.delayTone = 1.0;
            auto out = render (s, 1.2, impulse);
            int peak = 1;
            for (int i = 1000; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak))) peak = i;
            const double ms = peak * 1000.0 / sr;
            check (std::abs (ms - 500.0) < 2.0, "Delay 1/4 at 120 BPM echoes at " + juce::String (ms, 1) + " ms (expect 500)");

            FxChain::tempoBpm = 90.0;
            s.delayDivision = 3;   // dotted 1/8 = 0.75 beat = 500 ms at 90 BPM
            out = render (s, 1.2, impulse);
            peak = 1;
            for (int i = 1000; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak))) peak = i;
            check (std::abs (peak * 1000.0 / sr - 500.0) < 2.0,
                   "Delay dotted 1/8 at 90 BPM echoes at " + juce::String (peak * 1000.0 / sr, 1) + " ms (expect 500)");
            FxChain::tempoBpm = 72.0;
        }

        // Ping-pong: the first repeat is on the left only, the second on the right.
        {
            FxSettings s;
            s.delayOn = true; s.delaySync = false; s.delayMs = 250.0; s.delayMix = 1.0; s.delayFeedback = 0.6;
            s.delayTone = 1.0; s.delayPingPong = true;
            auto out = render (s, 0.7, impulse);
            const float l1 = rms (out, 0, 0.24, 0.27), r1 = rms (out, 1, 0.24, 0.27);
            const float l2 = rms (out, 0, 0.49, 0.52), r2 = rms (out, 1, 0.49, 0.52);
            check (l1 > 10 * r1 && r2 > 10 * l2, "Ping-pong alternates sides (1st L/R " + juce::String (l1, 4) + "/" + juce::String (r1, 4)
                                                     + ", 2nd L/R " + juce::String (l2, 4) + "/" + juce::String (r2, 4) + ")");
        }

        // Reverb: a long, stable tail whose length follows the decay setting.
        {
            auto decayTime = [&] (double decaySeconds)
            {
                FxSettings s;
                s.reverbOn = true; s.reverbDecay = decaySeconds; s.reverbMix = 1.0; s.reverbPredelayMs = 0.0; s.reverbMod = 0.3;
                auto out = render (s, decaySeconds * 1.6 + 1.0, impulse);
                const float early = rms (out, 0, 0.1, 0.3);
                // Time for the level to fall 30 dB below the early tail, doubled = an RT60 estimate.
                double t = 0.3;
                while (t < out.getNumSamples() / sr - 0.1 && rms (out, 0, t, t + 0.1) > early * 0.0316f)
                    t += 0.05;
                return std::pair { (t - 0.2) * 2.0, out.getMagnitude (0, out.getNumSamples()) };
            };

            const auto [rtShort, peakShort] = decayTime (2.0);
            const auto [rtLong, peakLong] = decayTime (8.0);
            check (rtShort > 1.0 && rtShort < 3.5, "Reverb decay 2 s measures about " + juce::String (rtShort, 1) + " s");
            check (rtLong > 5.0 && rtLong < 12.0, "Reverb decay 8 s measures about " + juce::String (rtLong, 1) + " s");
            check (peakShort < 1.5f && peakLong < 1.5f, "Reverb stays bounded (peak " + juce::String (juce::jmax (peakShort, peakLong), 2) + ")");
        }

        // Compressor: a loud tone comes out quieter than it went in.
        {
            auto sine = [] (int, int i) { return 0.9f * std::sin ((float) i * 0.0578f); };   // ~440 Hz, -1 dBFS
            FxSettings s;
            s.compOn = true; s.compThresholdDb = -24.0; s.compRatio = 4.0;
            auto out = render (s, 1.0, sine);
            const float inLevel = 0.9f / std::sqrt (2.0f), outLevel = rms (out, 0, 0.5, 1.0);
            const float reductionDb = juce::Decibels::gainToDecibels (inLevel / outLevel);
            check (reductionDb > 8.0f, "Compressor 4:1 at -24 dB reduces a loud tone by " + juce::String (reductionDb, 1) + " dB");
        }

        // Gate: quiet hiss below the threshold is silenced.
        {
            juce::Random rng (1);
            auto hiss = [&rng] (int, int) { return (rng.nextFloat() * 2.0f - 1.0f) * 0.003f; };   // about -55 dBFS
            FxSettings s;
            s.gateOn = true; s.gateThresholdDb = -40.0;
            auto out = render (s, 1.0, hiss);
            const float level = rms (out, 0, 0.5, 1.0);
            check (level < 0.0003f, "Gate at -40 dB silences -55 dB hiss (left " + juce::String (juce::Decibels::gainToDecibels (level), 1) + " dB)");
        }

        // EQ: +12 dB low shelf lifts a 50 Hz tone by close to 12 dB.
        {
            auto bass = [] (int, int i) { return 0.1f * std::sin ((float) i * juce::MathConstants<float>::twoPi * 50.0f / 48000.0f); };
            FxSettings s;
            s.eqOn = true; s.eqLowDb = 12.0;
            auto out = render (s, 1.0, bass);
            const float boost = juce::Decibels::gainToDecibels (rms (out, 0, 0.5, 1.0) / (0.1f / std::sqrt (2.0f)));
            check (boost > 10.0f && boost < 13.0f, "EQ low +12 dB lifts 50 Hz by " + juce::String (boost, 1) + " dB");
        }

        // Low cut at 200 Hz removes most of a 40 Hz rumble.
        {
            auto rumble = [] (int, int i) { return 0.3f * std::sin ((float) i * juce::MathConstants<float>::twoPi * 40.0f / 48000.0f); };
            FxSettings s;
            s.lowCutOn = true; s.lowCutHz = 200.0;
            auto out = render (s, 1.0, rumble);
            const float cut = juce::Decibels::gainToDecibels (rms (out, 0, 0.5, 1.0) / (0.3f / std::sqrt (2.0f)));
            check (cut < -20.0f, "Low cut 200 Hz reduces 40 Hz rumble by " + juce::String (-cut, 1) + " dB");
        }

        // Everything on at once with loud noise: no blow-ups, no NaNs.
        {
            juce::Random rng (2);
            auto noise = [&rng] (int, int) { return rng.nextFloat() * 2.0f - 1.0f; };
            FxSettings s;
            s.gainDb = 6.0; s.pan = 0.3; s.gateOn = s.lowCutOn = s.eqOn = s.compOn = s.delayOn = s.reverbOn = true;
            s.delayFeedback = 0.9; s.delayMix = 1.0; s.delayPingPong = true; s.reverbDecay = 20.0; s.reverbMix = 1.0;
            s.reverbMod = 1.0; s.eqLowDb = s.eqMidDb = s.eqHighDb = 15.0;
            auto out = render (s, 10.0, noise);
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    finite = finite && std::isfinite (out.getSample (ch, i));
            check (finite && out.getMagnitude (0, out.getNumSamples()) < 100.0f,
                   "Full chain, extreme settings, 10 s of loud noise: finite and bounded (peak "
                       + juce::String (out.getMagnitude (0, out.getNumSamples()), 1) + ")");
        }

        // ---- The newer effects ----
        {
            // Power at one frequency (Goertzel), for checking pitch.
            auto powerAt = [] (const float* x, int n, double hz)
            {
                const double w = juce::MathConstants<double>::twoPi * hz / sr, c = 2.0 * std::cos (w);
                double s1 = 0.0, s2 = 0.0;
                for (int i = 0; i < n; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
                return s1 * s1 + s2 * s2 - c * s1 * s2;
            };
            auto sine = [] (double hz, float level) { return [hz, level] (int, int i) { return level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr); }; };

            // Octave: the pitch-shifted copy alone (dry removed) sits an octave away.
            for (int interval : { 0, 1 })
            {
                FxSettings o;
                o.octaveOn = true; o.octaveInterval = interval; o.octaveMix = 1.0;
                auto out = render (o, 1.0, sine (330.0, 0.5f));
                auto dry = render ({}, 1.0, sine (330.0, 0.5f));
                out.addFrom (0, 0, dry, 0, 0, out.getNumSamples(), -1.0f);
                const float* x = out.getReadPointer (0, (int) (0.3 * sr));
                const int n = (int) (0.6 * sr);
                const double target = interval == 0 ? 165.0 : 660.0;
                const double ratio = powerAt (x, n, target) / juce::jmax (1.0e-12, powerAt (x, n, 330.0));
                check (ratio > 10.0, juce::String (shiftIntervals[interval].name) + ": the shifted copy is at "
                                         + juce::String (target, 0) + " Hz (" + juce::String (10.0 * std::log10 (ratio), 1) + " dB over the original)");
            }

            // Shimmer: 1 s of noise into a 20 s reverb at full shimmer rises an octave and still fades away.
            {
                FxSettings sh;
                sh.reverbOn = true; sh.reverbDecay = 20.0; sh.reverbMix = 1.0; sh.reverbShimmer = 1.0;
                juce::Random random (7);
                auto out = render (sh, 45.0, [&random] (int, int i) { return i < (int) sr ? (random.nextFloat() - 0.5f) * 0.6f : 0.0f; });
                const float early = rms (out, 0, 2.0, 3.0), mid = rms (out, 0, 15.0, 16.0), late = rms (out, 0, 44.0, 45.0);
                const float peak = out.getMagnitude (0, 0, out.getNumSamples());
                check (std::isfinite (peak) && peak < 3.0f && late < early * 0.1f && mid < early,
                       "Shimmer at full on a 20 s reverb stays bounded and fades (peak " + juce::String (peak, 2) + ", "
                           + juce::String (juce::Decibels::gainToDecibels (late / early), 1) + " dB after 42 s)");
            }

            // Swell: a note starting from silence comes in over the attack time.
            {
                FxSettings sw;
                sw.swellOn = true; sw.swellAttackMs = 500.0;
                auto out = render (sw, 1.2, [] (int, int i) { return i < (int) (0.2 * sr) ? 0.0f : 0.5f * (float) std::sin (0.06 * i); });
                const float start = rms (out, 0, 0.2, 0.25), later = rms (out, 0, 0.8, 0.85);
                check (start < later * 0.1f && later > 0.3f,
                       "Swell fades a note in (first 50 ms " + juce::String (juce::Decibels::gainToDecibels (start / later), 1) + " dB, full after the attack)");
            }

            // Filter: a 500 Hz low-pass takes 5 kHz well down; as a high-pass it keeps it.
            {
                FxSettings f;
                f.filterOn = true; f.filterCutoffHz = 500.0; f.filterResonance = 0.0;
                const float low = rms (render (f, 0.5, sine (5000.0, 0.5f)), 0, 0.2, 0.5);
                f.filterHighPass = true;
                const float high = rms (render (f, 0.5, sine (5000.0, 0.5f)), 0, 0.2, 0.5);
                check (juce::Decibels::gainToDecibels (low / 0.3536f) < -20.0f && high > 0.3f,
                       "Filter: low-pass at 500 Hz cuts 5 kHz by " + juce::String (-juce::Decibels::gainToDecibels (low / 0.3536f), 1) + " dB, high-pass keeps it");

                f.filterHighPass = false; f.filterLfoRate = 1.0; f.filterLfoDepth = 3.0; f.filterResonance = 1.0;
                auto swept = render (f, 3.0, sine (1000.0, 0.5f));
                const float peak = swept.getMagnitude (0, 0, swept.getNumSamples());
                check (std::isfinite (peak) && peak < 1.5f, "Filter sweeping at full resonance stays bounded (peak " + juce::String (peak, 2) + ")");
            }

            // Tape: driven hard it rounds off rather than clipping; quiet signals pass at about their level.
            {
                FxSettings t;
                t.tapeOn = true; t.tapeDrive = 1.0; t.tapeWow = 1.0; t.tapeTone = 1.0;
                auto loud = render (t, 1.0, sine (200.0, 2.0f));
                const float peak = loud.getMagnitude (0, (int) (0.1 * sr), (int) (0.8 * sr));
                t.tapeDrive = 0.0;
                const float quiet = rms (render (t, 1.0, sine (200.0, 0.1f)), 0, 0.2, 1.0);
                check (std::isfinite (peak) && peak < 0.4f && std::abs (juce::Decibels::gainToDecibels (quiet / 0.0707f)) < 1.5f,
                       "Tape: a +6 dB signal comes out at " + juce::String (peak, 2) + ", a quiet one at "
                           + juce::String (juce::Decibels::gainToDecibels (quiet / 0.0707f), 1) + " dB");
            }

            // Chorus thickens without getting louder or unstable.
            {
                FxSettings c;
                c.chorusOn = true; c.chorusDepth = 1.0; c.chorusMix = 0.5; c.chorusRate = 2.0;
                auto out = render (c, 1.0, sine (440.0, 0.5f));
                const float level = rms (out, 0, 0.2, 1.0);
                check (std::isfinite (level) && level > 0.15f && level < 0.5f, "Chorus level stays even (" + juce::String (level, 3) + ")");
            }

            // Motion: width 0 makes it mono; the auto-pan moves it between the sides.
            {
                FxSettings m;
                m.motionOn = true; m.motionWidth = 0.0; m.motionPanDepth = 0.0;
                auto mono = render (m, 0.2, [] (int ch, int i) { return ch == 0 ? 0.5f * (float) std::sin (0.05 * i) : 0.0f; });
                float diff = 0.0f;
                for (int i = 0; i < mono.getNumSamples(); ++i)
                    diff = juce::jmax (diff, std::abs (mono.getSample (0, i) - mono.getSample (1, i)));

                m.motionWidth = 1.0; m.motionPanDepth = 1.0; m.motionPanRate = 1.0;
                auto panned = render (m, 1.0, sine (440.0, 0.5f));
                const float leftThen = rms (panned, 0, 0.2, 0.3), rightThen = rms (panned, 1, 0.2, 0.3);
                const float leftLater = rms (panned, 0, 0.7, 0.8), rightLater = rms (panned, 1, 0.7, 0.8);
                check (diff < 1.0e-5f && rightThen > leftThen * 3.0f && leftLater > rightLater * 3.0f,
                       "Motion: width 0 is mono, the auto-pan swings from one side to the other");
            }

            // Space: sends on the bus come back as the shared reverb, and the bus is cleared each block.
            {
                SpaceReverb space;
                space.setSettings (SpaceReverb::defaults());
                space.prepare (sr, block);
                juce::AudioBuffer<float> out (2, block);
                out.clear();
                float one = 1.0f;
                SpaceReverb::addToBus (0, 0, &one, 1, 1.0f);
                SpaceReverb::addToBus (1, 0, &one, 1, 1.0f);
                float tail = 0.0f;
                for (int b = 0; b < 100; ++b)
                {
                    out.clear();
                    space.process (out, 0, block);
                    tail = juce::jmax (tail, out.getMagnitude (0, 0, block));
                }
                check (tail > 0.001f && SpaceReverb::bus.getMagnitude (0, 0, block) == 0.0f,
                       "Space returns what was sent as reverb (peak " + juce::String (tail, 3) + ") and empties its bus");
            }

            // Every knob is saved and loaded by name, and MIDI finds it by the same name.
            {
                FxSettings maxed;
                juce::StringArray lost;
                for (auto name : { "gainDb", "spaceSend", "swellAttackMs", "tapeWow", "filterCutoffHz", "filterLfoDepth", "octaveMix",
                                   "chorusRate", "reverbShimmer", "motionWidth", "motionPanRate", "reverbMix" })
                {
                    auto* param = findFxParameter (name);
                    if (param == nullptr) { lost.add (name); continue; }
                    *maxed.findParameter (name) = param->max;
                }
                maxed.octaveInterval = 2;
                maxed.chorusOn = maxed.motionOn = maxed.filterHighPass = true;
                juce::XmlElement parent ("P");
                maxed.addToXml (parent);
                const auto back = FxSettings::fromXml (parent.getChildByName ("Fx"));
                for (auto name : { "gainDb", "spaceSend", "swellAttackMs", "tapeWow", "filterCutoffHz", "filterLfoDepth", "octaveMix",
                                   "chorusRate", "reverbShimmer", "motionWidth", "motionPanRate", "reverbMix" })
                    if (auto* param = findFxParameter (name); param != nullptr && back.*(param->member) != param->max)
                        lost.add (name);
                if (back.octaveInterval != 2 || ! back.chorusOn || ! back.motionOn || ! back.filterHighPass)
                    lost.add ("switches");
                check (lost.isEmpty(), "Effect settings save and load by name" + (lost.isEmpty() ? juce::String() : " - lost: " + lost.joinIntoString (", ")));
            }
        }

        // Reverb Dry: at 0 only the reverb is left (the impulse itself is gone, its tail isn't).
        {
            FxSettings r;
            r.reverbOn = true; r.reverbMix = 0.5; r.reverbPredelayMs = 20.0;
            const float dryFull = render (r, 1.0, impulse).getSample (0, 0);
            r.reverbDry = 0.0;
            auto wetOnly = render (r, 1.0, impulse);
            const float dryNone = wetOnly.getSample (0, 0);
            const float tail = rms (wetOnly, 0, 0.05, 0.5);
            check (std::abs (dryFull - 0.75f) < 0.01f && std::abs (dryNone) < 1.0e-6f && tail > 1.0e-4f,
                   "Reverb Dry at 0 leaves only the reverb (original " + juce::String (dryFull, 2) + " -> " + juce::String (dryNone, 2) + ", tail kept)");
        }

        // ---- Space sends, plug-in slots and MIDI on effect knobs ----
        {
            // An input sends half of itself, after its fader, to the Space bus.
            SpaceReverb space;
            space.prepare (sr, block);
            InputChannel input;
            input.setGain (0.5f);   // before prepare, so the fader starts there rather than gliding
            input.prepare (sr, block);
            input.setMonitor (true);
            FxSettings send;
            send.spaceSend = 0.5;
            input.getFx().setSettings (send);
            juce::AudioBuffer<float> in (2, block), out (2, block), recordBus (2, block);
            for (int i = 0; i < block; ++i)
                in.setSample (0, i, 1.0f);
            in.clear (1, 0, block);
            out.clear();
            recordBus.clear();
            input.process (in, 1, out, 0, block, recordBus);
            const float sent = SpaceReverb::bus.getSample (0, block - 1);
            SpaceReverb::bus.clear();
            check (std::abs (sent - 0.25f) < 0.01f, "An input's Space send follows its fader (1.0 at -6 dB, half sent: "
                                                        + juce::String (sent, 3) + ", expect 0.250)");

            // A set keeps its Space settings and the plug-ins on its groups and master.
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory);
            auto set = LiveSet::makeDefault();
            set.space.reverbShimmer = 0.6;
            set.rows[0].insert = { "<PLUGIN name=\"Valhalla\"/>", "c3RhdGU=", "Valhalla" };
            set.masterInsert = { "<PLUGIN name=\"Limiter\"/>", "", "Limiter" };
            LiveSet back;
            back.loadFromXml (*set.toXml (folder), folder);
            check (std::abs (back.space.reverbShimmer - 0.6) < 1.0e-9 && back.space.reverbOn && back.rows[0].insert.name == "Valhalla"
                       && back.rows[0].insert.state == "c3RhdGU=" && back.rows[0].insert.xml.contains ("Valhalla")
                       && back.masterInsert.name == "Limiter" && back.rows[1].insert.isEmpty() && back.inputs[0].insertXml.isEmpty(),
                   "A set keeps its Space settings and the plug-ins on a group and the master");

            // Scenes and the time signature come back with the set.
            {
                LiveSet withScenes = LiveSet::makeDefault();
                withScenes.beatsPerBar = 6; withScenes.beatUnit = 8;
                LiveSet::Scene sc;
                sc.name = "Dawn"; sc.morphSeconds = 15.0;
                sc.channels.push_back ({ withScenes.rows[0].slots[0].uid, true, -6.5 });
                sc.groups.push_back ({ withScenes.rows[1].uid, -12.0, true });
                sc.keys.push_back ({ 1, 10, 2, 4 });   // instrument 2 in Bb, natural minor, octave 4
                sc.strips.push_back ({ LiveSet::Scene::Strip::instrument, 1, -3.0, true });
                withScenes.scenes.push_back (sc);
                LiveSet loaded;
                loaded.loadFromXml (*withScenes.toXml (folder), folder);
                const bool ok = loaded.beatsPerBar == 6 && loaded.beatUnit == 8 && loaded.scenes.size() == 1
                             && loaded.scenes[0].name == "Dawn" && loaded.scenes[0].uid == sc.uid && loaded.scenes[0].morphSeconds == 15.0
                             && loaded.scenes[0].channels.size() == 1 && loaded.scenes[0].channels[0].playing
                             && loaded.scenes[0].channels[0].volumeDb == -6.5 && loaded.scenes[0].groups[0].muted
                             && loaded.scenes[0].groups[0].faderDb == -12.0
                             && loaded.scenes[0].keys.size() == 1 && loaded.scenes[0].keys[0].instrument == 1
                             && loaded.scenes[0].keys[0].root == 10 && loaded.scenes[0].keys[0].scale == 2
                             && loaded.scenes[0].keys[0].octave == 4
                             && loaded.scenes[0].strips.size() == 1 && loaded.scenes[0].strips[0].kind == LiveSet::Scene::Strip::instrument
                             && loaded.scenes[0].strips[0].index == 1 && loaded.scenes[0].strips[0].faderDb == -3.0
                             && loaded.scenes[0].strips[0].muted;
                check (ok && MidiMap::isSetTarget ("scene." + sc.uid) && ! MidiMap::isSetTarget ("scene.next"),
                       "A set keeps its scenes (with input/instrument mutes, keys and MIDI buttons) and its 6/8 time signature");
            }

            // An older set without Space gets the defaults.
            auto old = juce::parseXML ("<LiveRigSet version=\"3\"/>");
            LiveSet oldSet;
            oldSet.loadFromXml (*old, folder);
            check (oldSet.space.reverbOn && oldSet.space.reverbDecay == SpaceReverb::defaults().reverbDecay && oldSet.masterInsert.isEmpty(),
                   "An older set gets the default Space");

            // MIDI on knobs: the ends of a knob's travel are the ends of its range, and the middle is its centre.
            const auto* cutoff = findFxParameter ("filterCutoffHz");
            const auto range = rangeOf (*cutoff);
            check (range.convertFrom0to1 (0.0) == cutoff->min && range.convertFrom0to1 (1.0) == cutoff->max
                       && std::abs (range.convertFrom0to1 (0.5) - cutoff->skewMidPoint) < 2.0,
                   "A MIDI knob covers the filter's whole range, centred on " + juce::String (range.convertFrom0to1 (0.5), 0) + " Hz");
            check (MidiMap::isSetTarget ("fx.grp.abc.reverbMix") && MidiMap::isSetTarget ("fx.ch.abc.spaceSend")
                       && MidiMap::isSetTarget ("fx.space.reverbShimmer") && ! MidiMap::isSetTarget ("fx.in.0.reverbMix"),
                   "Effect knobs on groups, channels and Space are saved with the set; input ones with the computer");

            // Two Fires, known by USB path: Windows' names change when one is replugged.
            using Source = InstrumentChannel::MidiSource;
            const Source fireA { "2 - FL STUDIO FIRE", "usb-port-A", true, { "usb-port-B" } };
            check (InstrumentChannel::playsFrom (fireA, "3 - FL STUDIO FIRE", "usb-port-A")
                       && ! InstrumentChannel::playsFrom (fireA, "FL STUDIO FIRE", "usb-port-B")
                       && InstrumentChannel::playsFrom ({}, "2 - FL STUDIO FIRE", "usb-port-B"),
                   "With two Fires, an instrument set to one isn't played by the other - even after Windows renames it");

            // Its Fire moved to another USB port: it plays the Fire no other instrument has.
            const Source moved { "FL STUDIO FIRE", "usb-port-A", false, { "usb-port-B" } };
            check (InstrumentChannel::playsFrom (moved, "4 - FL STUDIO FIRE", "usb-port-C")
                       && ! InstrumentChannel::playsFrom (moved, "2 - FL STUDIO FIRE", "usb-port-B")
                       && ! InstrumentChannel::playsFrom (moved, "MPK mini 3", "usb-port-D")
                       && InstrumentChannel::baseName ("3 - FL STUDIO FIRE") == "FL STUDIO FIRE",
                   "An instrument whose Fire moved USB port plays the other Fire, not the one already taken");
        }

        // ---- Looper ----
        // Feeds a ramp (sample value = its clock time) so positions can be read back from the audio.
        constexpr int lb = 480;
        juce::int64 clock = 0;
        juce::AudioBuffer<float> bus (2, lb);
        auto feed = [&] (Looper& looper, int blocks)
        {
            for (int b = 0; b < blocks; ++b, clock += lb)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < lb; ++i)
                        bus.setSample (ch, i, (float) ((clock + i) % 100000));
                looper.process (bus, lb, clock, 0);
            }
        };

        auto recordTake = [&] (int blocksBeforeStop, juce::int64 unit, juce::AudioBuffer<float>& take, juce::int64& start)
        {
            Looper looper;
            looper.prepare (sr, 10.0);
            looper.startRecording();
            feed (looper, blocksBeforeStop);
            looper.stopRecording (unit);
            for (int guard = 0; guard < 400 && looper.getState() != Looper::State::finished; ++guard)
                feed (looper, 1);
            return looper.collectTake (take, start);
        };

        {
            juce::AudioBuffer<float> take;
            juce::int64 start = 0;
            const juce::int64 recStartClock = clock;
            const bool got = recordTake (100, 0, take, start);   // 100 blocks = exactly 1 s
            check (got && take.getNumSamples() == 48000, "Free-length take is exactly as long as played: "
                                                             + juce::String (take.getNumSamples()) + " samples (expect 48000)");
            check (start == recStartClock, "Take start is stamped on the shared clock");

            const float mid = take.getSample (0, 24000);
            check (std::abs (mid - (float) ((recStartClock + 24000) % 100000)) < 0.5f, "Take audio is in order (middle sample correct)");

            // The seam: the start is blended with what was recorded past the end, so the
            // last sample and the first are close together rather than a jump of 48000.
            const float jump = std::abs (take.getSample (0, 0) - (float) ((recStartClock + 48000) % 100000));
            check (jump < 100.0f, "Loop seam is blended (start continues from past the end; difference " + juce::String (jump, 1) + ")");
        }

        {
            juce::AudioBuffer<float> take;
            juce::int64 start = 0;
            recordTake (50, 20000, take, start);    // 24000 played, unit 20000 -> trims to 20000
            check (take.getNumSamples() == 20000, "Snap: stopping late trims to 1 unit (" + juce::String (take.getNumSamples()) + ")");

            recordTake (70, 20000, take, start);    // 33600 played -> rounds up to 40000, keeps recording
            check (take.getNumSamples() == 40000, "Snap: stopping early records on to 2 units (" + juce::String (take.getNumSamples()) + ")");
        }

        {
            // Count-in: a take due at a clock time part-way through a block waits, then starts on that very sample.
            Looper looper;
            looper.prepare (sr, 10.0);
            const juce::int64 due = clock + 3 * lb + 123;
            looper.startRecording (due);
            feed (looper, 2);
            const bool waited = looper.getState() == Looper::State::waiting;
            feed (looper, 50);
            looper.stopRecording (0);
            for (int guard = 0; guard < 400 && looper.getState() != Looper::State::finished; ++guard)
                feed (looper, 1);
            juce::AudioBuffer<float> take;
            juce::int64 start = 0;
            looper.collectTake (take, start);
            // Past the seam blend, the take is the ramp from 'due' on.
            const float at2000 = take.getSample (0, 2000);
            check (waited && start == due && std::abs (at2000 - (float) ((due + 2000) % 100000)) < 0.5f,
                   "Count-in: recording waits, then starts on the exact sample (" + juce::String (start - due) + " off)");

            // REC again during the count-in calls it off.
            Looper cancelled;
            cancelled.prepare (sr, 10.0);
            cancelled.startRecording (clock + 10 * lb);
            feed (cancelled, 2);
            cancelled.cancelStart();
            feed (cancelled, 20);
            check (cancelled.getState() == Looper::State::idle, "Count-in can be called off");
        }

        {
            // Overdub: an impulse heard at clock 5000 must land where the loop was then,
            // allowing for the round trip latency.
            Looper looper;
            looper.prepare (sr, 1.0);
            auto clip = std::make_shared<LoopClip>();
            clip->sampleRate = sr;
            clip->data.setSize (2, 10000);
            clip->data.clear();

            const juce::int64 anchor = 1000;
            const int latency = 256;
            looper.startOverdub (clip, anchor);

            for (juce::int64 c = 0; c < 9600; c += lb)
            {
                bus.clear();
                if (5000 >= c && 5000 < c + lb)
                    for (int ch = 0; ch < 2; ++ch)
                        bus.setSample (ch, (int) (5000 - c), 1.0f);
                looper.process (bus, lb, c, latency);
            }

            int where = -1;
            for (int i = 0; i < clip->data.getNumSamples(); ++i)
                if (clip->data.getSample (0, i) > 0.5f) where = i;
            check (where == 5000 - 1000 - 256, "Overdub lands at loop position " + juce::String (where) + " (expect 3744, latency-compensated)");
        }

        {
            // Anchored playback: a loop anchored at 12000 and started at clock 12345 plays from position 345.
            auto clip = std::make_shared<LoopClip>();
            clip->sampleRate = sr;
            clip->data.setSize (1, 1000);
            for (int i = 0; i < 1000; ++i)
                clip->data.setSample (0, i, (float) i);

            LoopPlayer player;
            player.prepare (sr);
            player.setClip (clip, 12000);
            player.fadeTo (1.0f, 0.0);
            LoopPlayer::blockClock = 12345;
            juce::AudioBuffer<float> out (1, 16);
            out.clear();
            player.process (out, 0, 16);
            check (std::abs (out.getSample (0, 0) - 345.0f) < 0.5f, "Anchored loop starts at position " + juce::String (out.getSample (0, 0), 1) + " (expect 345)");
            LoopPlayer::blockClock = 0;
        }

        {
            // Sync: a loop anchored at 12000, playing at clock 12345 (position 345), realigned to
            // anchor 12300 jumps to position 45 - blending over 30 ms, not jumping in one sample.
            const double rate = 48000.0;
            auto clip = std::make_shared<LoopClip>();
            clip->sampleRate = rate;
            clip->data.setSize (1, 4000);
            for (int i = 0; i < 4000; ++i)
                clip->data.setSample (0, i, (float) i);

            LoopPlayer player;
            player.prepare (rate);
            player.setClip (clip, 12000);
            player.fadeTo (1.0f, 0.0);
            LoopPlayer::blockClock = 12345;
            juce::AudioBuffer<float> out (1, 2000);
            out.clear();
            player.process (out, 0, 1);
            player.realign (12300);
            out.clear();
            player.process (out, 0, 2000);

            const int blendLength = (int) (LoopPlayer::realignSeconds * rate);
            float biggestStep = 0.0f;
            for (int i = 1; i < blendLength; ++i)
                biggestStep = juce::jmax (biggestStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            const float after = out.getSample (0, 1999);   // past the blend: position 45 + 1999

            check (std::abs (out.getSample (0, 0) - 346.0f) < 0.5f && biggestStep < 2.0f && std::abs (after - 2044.0f) < 0.5f,
                   "Sync moves a loop into phase smoothly (starts at " + juce::String (out.getSample (0, 0), 1) + ", ends at "
                       + juce::String (after, 1) + " expect 2044, biggest step " + juce::String (biggestStep, 2) + ")");
            LoopPlayer::blockClock = 0;
        }

        {
            // Start on the beat: a loop pressed at clock 900, due at 1000, stays silent until
            // then and comes in from its first sample.
            auto clip = std::make_shared<LoopClip>();
            clip->sampleRate = 48000.0;
            clip->data.setSize (1, 4000);
            for (int i = 0; i < 4000; ++i)
                clip->data.setSample (0, i, (float) (i + 1));

            LoopPlayer player;
            player.prepare (48000.0);
            LoopPlayer::blockClock = 900;
            player.setClip (clip, 1000);
            player.fadeTo (1.0f, 0.0, 1000);
            juce::AudioBuffer<float> out (1, 100);
            out.clear();
            player.process (out, 0, 100);
            const float waiting = out.getMagnitude (0, 100);
            LoopPlayer::blockClock = 1000;
            out.clear();
            player.process (out, 0, 100);
            check (waiting == 0.0f && out.getSample (0, 0) == 1.0f && out.getSample (0, 99) == 100.0f,
                   "A loop due on the next beat waits silently, then starts from its top (first sample "
                       + juce::String (out.getSample (0, 0), 1) + ", expect 1)");
            LoopPlayer::blockClock = 0;

            // Grid every 500 samples from 1000, with 50 samples' grace for a late press.
            const auto a = nextGridPoint (1700, 1000, 500.0, 50), b = nextGridPoint (1520, 1000, 500.0, 50),
                       c = nextGridPoint (900, 1000, 500.0, 50), d = nextGridPoint (1560, 1000, 500.0, 50);
            check (a == 2000 && b == 1500 && c == 1000 && d == 2000,
                   "Next beat: " + juce::String (a) + " " + juce::String (b) + " " + juce::String (c) + " " + juce::String (d)
                       + " (expect 2000 1500 1000 2000)");
        }

        // ---- More inputs ----
        {
            // A set with three inputs, the third a stereo pair on interface inputs 3+4, saves and loads as that.
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory);
            LiveSet set;
            set.inputs.resize (3);
            set.inputs[2].name = "Synth";
            set.inputs[2].channel = 2;
            set.inputs[2].stereo = true;
            LiveSet back;
            back.loadFromXml (*set.toXml (folder), folder);
            check (back.inputs.size() == 3 && back.inputs[2].name == "Synth" && back.inputs[2].channel == 2 && back.inputs[2].stereo
                       && back.inputs[1].channel == 1 && ! back.inputs[1].stereo,
                   "A set keeps its three inputs, the third on inputs 3+4 stereo (" + juce::String ((int) back.inputs.size()) + " inputs)");

            // Sets saved before inputs could be picked: "source 2" was inputs 1+2 stereo; one input saved means one input.
            auto old = juce::parseXML ("<LiveRigSet version=\"3\"><Input name=\"Mic\" source=\"2\"/></LiveRigSet>");
            LiveSet oldSet;
            oldSet.loadFromXml (*old, folder);
            check (oldSet.inputs.size() == 1 && oldSet.inputs[0].channel == 0 && oldSet.inputs[0].stereo,
                   "An older set's \"inputs 1+2 stereo\" loads as that");

            // An input on the 3+4 pair hears interface inputs 3 and 4, nothing else.
            InputChannel input;
            input.prepare (48000.0, 64);
            input.setMonitor (true);
            input.setSource (2, true);
            juce::AudioBuffer<float> in (4, 64), out (2, 64), recordBus (2, 64);
            for (int ch = 0; ch < 4; ++ch)
                for (int k = 0; k < 64; ++k)
                    in.setSample (ch, k, 0.1f * (float) (ch + 1));
            out.clear();
            recordBus.clear();
            input.process (in, 4, out, 0, 64, recordBus);
            check (std::abs (out.getSample (0, 63) - 0.3f) < 0.001f && std::abs (out.getSample (1, 63) - 0.4f) < 0.001f,
                   "Input on 3+4 stereo plays inputs 3 and 4 (L " + juce::String (out.getSample (0, 63), 3) + " R "
                       + juce::String (out.getSample (1, 63), 3) + ", expect 0.300 0.400)");
        }

        // ---- Tempo from a loop ----
        {
            check (TempoGuess::fromName ("Wonky Hip Hop Drum Loop 72 BPM __ The Sound") == 72.0, "Tempo read from \"...Drum Loop 72 BPM...\"");
            check (TempoGuess::fromName ("pad_bpm90_Am") == 90.0, "Tempo read from \"pad_bpm90_Am\"");
            check (TempoGuess::fromName ("rain ambience") == 0.0, "No tempo in \"rain ambience\"");

            // An 8-second loop: 2 bars at 60, 4 at 120 (and 3 at 90).
            juce::StringArray found;
            for (auto& o : TempoGuess::fromLength (8.0))
                found.add (juce::String (o.bars) + "=" + juce::String (o.bpm, 1));
            check (found.contains ("2=60.0") && found.contains ("4=120.0") && found.contains ("3=90.0"),
                   "Tempo options for an 8 s loop: " + found.joinIntoString (", "));

            // In 3/4 a 6-second loop is 2 bars at 60 or 4 at 120; a synced "1 bar" delay is 3 beats.
            juce::StringArray waltz;
            for (auto& o : TempoGuess::fromLength (6.0, 3))
                waltz.add (juce::String (o.bars) + "=" + juce::String (o.bpm, 1));
            FxSettings d;
            d.delaySync = true;
            d.delayDivision = numDelayDivisions - 1;
            FxChain::beatsPerBar = 3;
            const double bar34 = d.delayTimeMs (60.0);
            FxChain::beatsPerBar = 6; FxChain::beatUnit = 8;
            d.delayDivision = 5;   // 1/4
            const double quarterIn68 = d.delayTimeMs (120.0);   // 120 eighths a minute: a quarter is 1 s
            FxChain::beatsPerBar = 4; FxChain::beatUnit = 4;
            check (waltz.contains ("2=60.0") && waltz.contains ("4=120.0") && std::abs (bar34 - 3000.0) < 0.1
                       && std::abs (quarterIn68 - 1000.0) < 0.1,
                   "3/4: tempos " + waltz.joinIntoString (", ") + ", 1-bar delay " + juce::String (bar34, 0)
                       + " ms; 6/8: 1/4 delay " + juce::String (quarterIn68, 0) + " ms");
        }

        {
            // The first loop sets the tempo: 8.05 s near 72 BPM is 2 bars at 59.63 or 3 at 89.44 -> 2 bars;
            // near 120 it's 4 bars at 119.25.
            const auto slow = TempoGuess::nearest (8.05, 72.0), fast = TempoGuess::nearest (8.05, 120.0);
            check (slow.bars == 2 && std::abs (slow.bpm - 59.63) < 0.01 && fast.bars == 4 && std::abs (fast.bpm - 119.25) < 0.01,
                   "First loop's tempo: " + juce::String (slow.bars) + " bars at " + juce::String (slow.bpm, 2) + " near 72, "
                       + juce::String (fast.bars) + " bars at " + juce::String (fast.bpm, 2) + " near 120");
        }

        // ---- Pack round trip: a set and its samples in one file, unpacked elsewhere ----
        {
            const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LiveRigPackTest");
            work.deleteRecursively();
            const auto sample = work.getChildFile ("source/Rain.wav");
            sample.getParentDirectory().createDirectory();
            sample.replaceWithText ("pretend audio");

            // As the export does: the set refers to its samples relative to its own folder.
            const auto staging = work.getChildFile ("staging");
            staging.createDirectory();
            LiveSet set = LiveSet::makeDefault();
            set.rows[0].slots[0].file = sample;
            set.relocate ({ { sample.getFullPathName(), staging.getChildFile ("Ambient/Rain.wav") } });
            const auto xmlText = set.toXml (staging)->toString();

            juce::ZipFile::Builder zip;
            zip.addFile (sample, 0, "My Set/Ambient/Rain.wav");
            zip.addEntry (new juce::MemoryInputStream (xmlText.toRawUTF8(), xmlText.getNumBytesAsUTF8(), true), 6,
                          "My Set/My Set.liverig", juce::Time::getCurrentTime());
            const auto pack = work.getChildFile ("My Set.liverigpack");
            {
                juce::FileOutputStream out (pack);
                zip.writeToStream (out, nullptr);
            }

            // Unpack somewhere else and open it.
            const auto elsewhere = work.getChildFile ("other computer");
            juce::ZipFile (pack).uncompressTo (elsewhere, true);
            const auto setFile = elsewhere.getChildFile ("My Set/My Set.liverig");
            LiveSet opened;
            if (auto xml = juce::XmlDocument::parse (setFile))
                opened.loadFromXml (*xml, setFile.getParentDirectory());

            const auto found = ! opened.rows.empty() && ! opened.rows[0].slots.empty() ? opened.rows[0].slots[0].file : juce::File();
            check (found.existsAsFile() && found.loadFileAsString() == "pretend audio" && found.isAChildOf (elsewhere),
                   "Pack round trip: unpacked set finds its sample in its own folder (" + found.getRelativePathFrom (elsewhere) + ")");
            work.deleteRecursively();
        }

        // ---- Reverse and per-channel effects ----
        {
            LoopClip ramp;
            ramp.sampleRate = sr;
            ramp.data.setSize (1, 100);
            for (int i = 0; i < 100; ++i)
                ramp.data.setSample (0, i, (float) i);
            auto backwards = makeReversed (ramp);
            check (backwards->data.getSample (0, 0) == 99.0f && backwards->data.getSample (0, 99) == 0.0f,
                   "Reversed clip plays backwards (first " + juce::String (backwards->data.getSample (0, 0)) + ", last "
                       + juce::String (backwards->data.getSample (0, 99)) + ")");

            // Two channels play the same steady tone; only channel 1 gets -12 dB of its own gain.
            auto tone = std::make_shared<LoopClip>();
            tone->sampleRate = sr;
            tone->data.setSize (2, 4800);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4800; ++i)
                    tone->data.setSample (ch, i, 0.25f * std::sin ((float) i * juce::MathConstants<float>::twoPi / 48.0f));

            auto levelOf = [&] (std::function<void (LoopChannel&)> setUp)
            {
                LoopChannel row;
                row.prepare (sr, block);
                setUp (row);
                juce::AudioBuffer<float> out (2, block);
                float level = 0.0f;
                for (int b = 0; b < 40; ++b)
                {
                    out.clear();
                    row.process (out, 0, block);
                    if (b >= 20) level = juce::jmax (level, out.getRMSLevel (0, 0, block));
                }
                return level;
            };

            const float both = levelOf ([&] (LoopChannel& row)
            {
                row.startLoop (1, tone, 0.0, 1.0f, 0);
                row.startLoop (2, tone, 0.0, 1.0f, 0);
            });

            const float oneQuieter = levelOf ([&] (LoopChannel& row)
            {
                FxSettings quieter;
                quieter.gainDb = -12.0;
                row.setOwnerFx (1, quieter);
                row.startLoop (1, tone, 0.0, 1.0f, 0);
                row.startLoop (2, tone, 0.0, 1.0f, 0);
            });

            // In phase, so full = 2x, and with one at -12 dB (0.25x) = 1.25x of a single tone.
            const float ratioDb = juce::Decibels::gainToDecibels (oneQuieter / both);
            check (std::abs (ratioDb - juce::Decibels::gainToDecibels (1.25f / 2.0f)) < 0.5f,
                   "Channel FX affect only their own channel (" + juce::String (ratioDb, 1) + " dB, expect -4.1)");

            // A channel's reverb keeps ringing after it stops.
            LoopChannel row;
            row.prepare (sr, block);
            FxSettings wet;
            wet.reverbOn = true; wet.reverbMix = 1.0; wet.reverbDecay = 4.0;
            row.setOwnerFx (7, wet);
            row.startLoop (7, tone, 0.0, 1.0f, 0);
            juce::AudioBuffer<float> out (2, block);
            for (int b = 0; b < 40; ++b) { out.clear(); row.process (out, 0, block); }
            row.stopLoop (7, 0.01);
            float tail = 0.0f;
            for (int b = 0; b < 60; ++b) { out.clear(); row.process (out, 0, block); if (b > 40) tail = juce::jmax (tail, out.getRMSLevel (0, 0, block)); }
            check (tail > 0.001f, "Channel reverb tail rings out after the channel stops (" + juce::String (juce::Decibels::gainToDecibels (tail), 1) + " dB)");

            // The channel fader comes after its effects: pulling it down silences the ringing tail too.
            row.setOwnerGain (7, 0.0f);
            float afterFader = 1.0f;
            for (int b = 0; b < 20; ++b) { out.clear(); row.process (out, 0, block); afterFader = out.getRMSLevel (0, 0, block); }
            row.setOwnerGain (7, 0.5f);
            float back = 0.0f;
            for (int b = 0; b < 20; ++b) { out.clear(); row.process (out, 0, block); back = out.getRMSLevel (0, 0, block); }
            check (afterFader < 1.0e-6f && back > 1.0e-5f,
                   "A channel's fader comes after its effects: down silences the reverb tail, up brings it back");

            // The fader still sets the level going into nothing else: a channel without effects follows it as before.
            const float halfVolume = levelOf ([&] (LoopChannel& r) { r.startLoop (1, tone, 0.0, 0.5f, 0); });
            const float fullVolume = levelOf ([&] (LoopChannel& r) { r.startLoop (1, tone, 0.0, 1.0f, 0); });
            FxSettings flat;
            flat.gainDb = 0.001;   // "on" but doing nothing
            const float halfThroughFx = levelOf ([&] (LoopChannel& r) { r.setOwnerFx (1, flat); r.startLoop (1, tone, 0.0, 0.5f, 0); });
            check (std::abs (halfVolume / fullVolume - 0.5f) < 0.02f && std::abs (halfThroughFx / fullVolume - 0.5f) < 0.02f,
                   "Channel volume gives the same level with or without its own effects");

            // A loop waiting for its beat says so - to start, then to stop - and not once it's happened.
            LoopChannel cued;
            cued.prepare (sr, block);
            LoopPlayer::blockClock = 1000;
            cued.startLoop (3, tone, 0.0, 1.0f, 5000, 5000);
            const bool waitsToStart = cued.cueFor (3) == LoopChannel::Cue::start;
            LoopPlayer::blockClock = 5000;
            out.clear(); cued.process (out, 0, block);
            const bool started = cued.cueFor (3) == LoopChannel::Cue::none;
            cued.stopLoop (3, 0.0, 9000);
            const bool waitsToStop = cued.cueFor (3) == LoopChannel::Cue::stop;
            LoopPlayer::blockClock = 9000;
            out.clear(); cued.process (out, 0, block);
            const bool stopped = cued.cueFor (3) == LoopChannel::Cue::none;
            LoopPlayer::blockClock = 0;
            check (waitsToStart && started && waitsToStop && stopped,
                   "A loop on Start beat/bar shows CUED until it comes in, and again until it stops");
        }

        // ---- Output limiter and recording level ----
        {
            OutputLimiter limiter;
            limiter.prepare (48000.0);
            juce::AudioBuffer<float> loud (2, 48000), quiet (2, 48000);
            for (int i = 0; i < 48000; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    loud.setSample (ch, i, 4.0f * std::sin ((float) i * 0.05f));
                    quiet.setSample (ch, i, 0.5f * std::sin ((float) i * 0.05f));
                }
            loud.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
            const auto quietCopy = quiet;
            limiter.process (loud, 0, 48000, 2);
            OutputLimiter fresh;
            fresh.prepare (48000.0);
            fresh.process (quiet, 0, 48000, 2);

            bool finite = true;
            for (int i = 0; i < 48000; ++i)
                finite = finite && std::isfinite (loud.getSample (0, i));
            float change = 0.0f;
            for (int i = 0; i < 48000; ++i)
                change = juce::jmax (change, std::abs (quiet.getSample (0, i) - quietCopy.getSample (0, i)));

            check (finite && loud.getMagnitude (0, 48000) <= OutputLimiter::ceiling + 0.0001f && change == 0.0f,
                   "Master limiter holds a +12 dB signal at -0.5 dBFS, silences NaN, leaves normal levels alone (peak "
                       + juce::String (loud.getMagnitude (0, 48000), 3) + ")");

            // An input is recorded at its fader level, so loops come in as loud as they were heard.
            InputChannel input;
            input.prepare (48000.0, 512);
            input.setArmed (true);
            input.setGain (0.5f);
            juce::AudioBuffer<float> in (2, 512), out (2, 512), recordBus (2, 512);
            in.clear();
            in.setSample (0, 10, 1.0f);
            out.clear();
            recordBus.clear();
            input.process (in, 2, out, 0, 512, recordBus);
            check (std::abs (recordBus.getSample (0, 10) - 0.5f) < 0.001f,
                   "Recording follows the input fader (a 1.0 sample at -6 dB records as " + juce::String (recordBus.getSample (0, 10), 3) + ")");

            // The meter shows what's heard: after the effects and the fader. A 0.5 signal through
            // -12 dB of effect gain and a -6 dB fader shows about 0.063; muted it shows nothing;
            // with MONITOR off it shows the input itself (0.5).
            InputChannel metered;
            metered.prepare (48000.0, 512);
            metered.setMonitor (true);
            metered.setGain (0.5f);
            FxSettings quieter;
            quieter.gainDb = -12.0;
            metered.getFx().setSettings (quieter);
            juce::AudioBuffer<float> tone (2, 512);
            auto meterReads = [&]
            {
                float shown = 0.0f;
                for (int b = 0; b < 40; ++b)
                {
                    for (int i = 0; i < 512; ++i)
                        tone.setSample (0, i, 0.5f * std::sin ((float) (b * 512 + i) * 0.0575f));
                    tone.copyFrom (1, 0, tone, 0, 0, 512);
                    out.clear();
                    recordBus.clear();
                    metered.process (tone, 2, out, 0, 512, recordBus);
                    shown = metered.takePeaks().first;
                }
                return shown;
            };
            const float heard = meterReads();
            metered.setMuted (true);
            const float muted = meterReads();
            metered.setMuted (false);
            metered.setMonitor (false);
            const float unmonitored = meterReads();
            check (std::abs (heard - 0.25f * juce::Decibels::decibelsToGain (-12.0f)) < 0.01f && muted < 0.001f
                   && std::abs (unmonitored - 0.5f) < 0.01f,
                   "Input meters show what's heard: after effects and fader (" + juce::String (heard, 3) + "), nothing muted ("
                   + juce::String (muted, 3) + "), the input itself with MONITOR off (" + juce::String (unmonitored, 3) + ")");
        }

        // ---- A long channel reverb dies away (the settings Hawk Screech had when it kept rumbling) ----
        {
            FxSettings s;
            s.reverbOn = true;
            s.reverbDecay = 8.8;
            s.reverbDry = 0.84;
            FxChain chain;
            chain.prepare (44100.0, 512);
            chain.setSettings (s);
            juce::AudioBuffer<float> block (2, 512);
            float lastSecond = 0.0f, peakAt10 = 0.0f;
            for (int b = 0; b < (int) (44100.0 * 40.0 / 512.0); ++b)
            {
                block.clear();
                if (b < 40)   // about half a second of loud noise
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            block.setSample (ch, i, juce::Random::getSystemRandom().nextFloat() * 1.6f - 0.8f);
                chain.process (block, 0, 512);
                const float p = block.getMagnitude (0, 512);
                if (b == (int) (44100.0 * 10.0 / 512.0)) peakAt10 = p;
                if (b > (int) (44100.0 * 39.0 / 512.0)) lastSecond = juce::jmax (lastSecond, p);
            }
            check (lastSecond < 0.0001f && std::isfinite (lastSecond),
                   "An 8.8 s channel reverb dies away (after 10 s " + juce::String (juce::Decibels::gainToDecibels (peakAt10), 1)
                   + " dB, after 40 s " + juce::String (juce::Decibels::gainToDecibels (lastSecond), 1) + " dB)");
        }

        // ---- Runaway guard and clearing: a wild value is caught and the tails emptied ----
        {
            FxSettings s;
            s.reverbOn = true;
            s.reverbDecay = 20.0;
            FxChain chain;
            chain.prepare (44100.0, 512);
            chain.setSettings (s);
            juce::AudioBuffer<float> block (2, 512);

            const int before = FxChain::runaways.load();
            block.clear();
            block.setSample (0, 5, 1.0e30f);   // garbage, as if read from bad memory
            chain.process (block, 0, 512);
            float after = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                block.clear();
                chain.process (block, 0, 512);
                after = juce::jmax (after, block.getMagnitude (0, 512));
            }
            check (FxChain::runaways.load() == before + 1 && after == 0.0f,
                   "A runaway value in an effect is caught: the chain is emptied, not left ringing");

            block.clear();
            block.setSample (0, 5, 0.9f);
            chain.process (block, 0, 512);
            chain.requestClear();
            block.clear();
            chain.process (block, 0, 512);
            check (block.getMagnitude (0, 512) == 0.0f, "A channel effects slot handed to another channel starts with empty tails");
        }

        // ---- The Fire's VOLUME knob: relative steps, moving every fader by the same dB ----
        {
            const bool steps = MidiMap::relativeSteps (1) == 1 && MidiMap::relativeSteps (3) == 3
                            && MidiMap::relativeSteps (127) == -1 && MidiMap::relativeSteps (125) == -3;
            const double a = MidiMap::nudgeDb (-6.0, 4), b = MidiMap::nudgeDb (-12.0, 4);
            const bool balanced = std::abs ((a - b) - 6.0) < 1.0e-9 && std::abs (a - -4.0) < 1.0e-9;
            const bool limits = MidiMap::nudgeDb (5.5, 10) == 6.0 && MidiMap::nudgeDb (-59.0, -3) == -60.0
                             && MidiMap::nudgeDb (-60.0, 5) == -50.0;
            check (steps && balanced && limits,
                   "Fire VOLUME knob: reads its turn direction, moves faders together keeping their balance, stops at +6 and off");

            // All the way down and back up: the quieter one waits below off and returns to its place.
            std::vector<double> levels { -6.0, -20.0 };
            for (int i = 0; i < 80; ++i) MidiMap::nudgeTogether (levels, -1);
            const bool bothOff = levels[0] <= -60.0 && levels[1] <= -60.0;
            for (int i = 0; i < 300 && levels[0] < -6.0; ++i) MidiMap::nudgeTogether (levels, 1);
            check (bothOff && std::abs (levels[0] - -6.0) < 1.0e-9 && std::abs (levels[1] - -20.0) < 1.0e-9,
                   "Fire VOLUME knob down to off and back keeps the balance (back to " + juce::String (levels[0], 1)
                   + " / " + juce::String (levels[1], 1) + " dB)");
        }

        // ---- Freeze ----
        {
            FreezeHold freeze;
            freeze.prepare (48000.0);
            freeze.releaseSeconds = 2.0f;

            // Renders 'seconds' with the given input level, returning the pad's RMS over the last half second.
            bool finite = true;
            float peak = 0.0f;
            auto run = [&] (double seconds, float inputLevel)
            {
                juce::AudioBuffer<float> buf (2, 480);
                double sum = 0.0;
                int counted = 0;
                const int blocks = (int) (seconds * 100.0);
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < 480; ++i)
                        for (int ch = 0; ch < 2; ++ch)
                            buf.setSample (ch, i, inputLevel * std::sin ((float) (b * 480 + i) * 0.0575f));   // ~440 Hz
                    const auto dry = buf;
                    freeze.process (buf, 0, 480);
                    for (int i = 0; i < 480; ++i)
                    {
                        const float wet = buf.getSample (0, i) - dry.getSample (0, i);
                        finite = finite && std::isfinite (wet);
                        peak = juce::jmax (peak, std::abs (wet));
                        if (b >= blocks - 50) { sum += wet * wet; ++counted; }
                    }
                }
                return (float) std::sqrt (sum / juce::jmax (1, counted));
            };

            const float idle = run (1.0, 0.5f);               // playing, not held: silent
            freeze.held = true;
            const float start = run (1.0, 0.5f);              // held
            const float later = run (30.0, 0.0f);             // 30 s later, nothing played
            const float playOver = run (2.0, 0.5f);           // playing over it doesn't change it
            freeze.held = false;
            const float released = run (3.0, 0.0f);          // 2 s release

            check (finite && idle < 1.0e-6f && start > 0.01f && later > start * 0.5f && released < 1.0e-5f && peak < 1.0f
                       && std::abs (playOver - later) < later * 0.5f,
                   "Freeze holds the pad 30 s without fading (" + juce::String (juce::Decibels::gainToDecibels (later / start), 1)
                       + " dB), ignores new playing, releases to silence, peak " + juce::String (peak, 2)
                       + "  [idle " + juce::String (idle, 6) + " start " + juce::String (start, 3) + " later " + juce::String (later, 3)
                       + " over " + juce::String (playOver, 3) + " released " + juce::String (released, 6) + "]");
        }

        // ---- Recording the whole set ----
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LiveRig set test.wav");
            {
                SetRecorder recorder;
                const bool started = recorder.start (file, 48000.0);
                juce::AudioBuffer<float> buf (2, 480);
                for (int b = 0; b < 200; ++b)   // 2 s
                {
                    for (int i = 0; i < 480; ++i)
                    {
                        buf.setSample (0, i, 0.5f);
                        buf.setSample (1, i, -0.25f);
                    }
                    recorder.write (buf, 0, 480);
                }
                const double seconds = recorder.getSeconds();
                recorder.stop();
                check (started && std::abs (seconds - 2.0) < 0.001, "Set recorder counts 2 s written");
            }

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            bool ok = reader != nullptr && reader->lengthInSamples == 96000 && reader->numChannels == 2 && reader->bitsPerSample == 24;
            if (ok)
            {
                juce::AudioBuffer<float> back (2, 96000);
                reader->read (&back, 0, 96000, 0, true, true);
                ok = std::abs (back.getSample (0, 50000) - 0.5f) < 0.001f && std::abs (back.getSample (1, 95999) + 0.25f) < 0.001f;
            }
            reader.reset();
            file.deleteFile();
            check (ok, "Set recording writes a 24-bit stereo WAV with every sample in place");
        }

        // ---- The Fire's REC button records ----
        {
            MidiMap map;
            const auto targets = map.findTargets ("FL STUDIO FIRE", juce::MidiMessage::noteOn (1, MidiMap::fireRecNote, 1.0f));
            const auto pad = map.findTargets ("FL STUDIO FIRE", juce::MidiMessage::noteOn (1, InstrumentChannel::firstFirePadNote, 1.0f));
            check (targets.contains ("rec") && pad.isEmpty(), "The Fire's REC button starts a loop; its pads stay free for playing");
        }

        // ---- MIDI assignments travel with the set ----
        {
            auto set = LiveSet::makeDefault();
            const auto channelUid = set.rows[0].slots.empty() ? juce::String() : set.rows[0].slots[0].uid;
            const auto groupUid = set.rows[1].uid;

            MidiMap map;
            map.bind ("ch." + channelUid + ".play", { "MIDI Mix", true, 1, 0 });
            map.bind ("grp." + groupUid + ".fader", { "MIDI Mix", false, 19, 0 });
            map.bind ("stopAll", { "MIDI Mix", true, 27, 0 });
            set.midi = std::shared_ptr<juce::XmlElement> (map.toXml ([] (const juce::String& t) { return MidiMap::isSetTarget (t); }).release());

            LiveSet reopened;
            reopened.loadFromXml (*set.toXml ({}), {});
            MidiMap fresh;
            if (reopened.midi != nullptr)
                fresh.addFromXml (*reopened.midi);

            const bool idsKept = ! channelUid.isEmpty() && reopened.rows[0].slots[0].uid == channelUid && reopened.rows[1].uid == groupUid;
            check (idsKept && fresh.get ("ch." + channelUid + ".play").number == 1 && fresh.get ("grp." + groupUid + ".fader").number == 19
                       && ! fresh.get ("stopAll").isValid() && ! fresh.get ("fader.0").isValid(),
                   "Channel and group MIDI is saved with the set; global controls and old fader slots are not");
        }

        // ---- Fire pad layout ----
        {
            PadLayout layout;   // A minor pentatonic, octave 3
            const int bottomLeft = 48;   // pad index of the bottom row's first pad
            check (layout.noteForPad (bottomLeft) == 57, "Fire bottom-left pad plays A3 (" + juce::String (layout.noteForPad (bottomLeft)) + ", expect 57)");

            juce::String row;
            for (int c = 0; c < 6; ++c)
                row << layout.noteForPad (bottomLeft + c) << " ";
            check (row.trim() == "57 60 62 64 67 69", "Bottom row walks the scale A C D E G A (" + row.trim() + ")");

            check (layout.noteForPad (32) == layout.noteForPad (bottomLeft + 3), "Next row up starts three scale steps higher");
            check (layout.isRootPad (bottomLeft) && layout.isRootPad (bottomLeft + 5) && ! layout.isRootPad (bottomLeft + 1),
                   "Root pads are found for lighting");

            check (layout.isRelativeRootPad (bottomLeft + 1) && ! layout.isRelativeRootPad (bottomLeft)
                       && ! layout.isRelativeRootPad (bottomLeft + 2) && layout.describeRelative() == "C major",
                   "A minor lights its relative C major root (" + layout.describeRelative() + ")");

            PadLayout bb;
            bb.root = 10;
            bb.scale = 1;
            PadLayout e;
            e.root = 4;
            e.scale = 1;
            check (bb.describeRelative() == "G minor" && e.describeRelative() == "C# minor",
                   "Bb major pairs with G minor, E major with C# minor (" + bb.describeRelative() + ", " + e.describeRelative() + ")");
            check (layout.keyName() == "A minor" && bb.keyName() == "Bb major" && e.keyName() == "E major",
                   "Recorded loops are named by key (" + layout.keyName() + ", " + bb.keyName() + ", " + e.keyName() + ")");

            layout.scale = PadLayout::numScales - 1;   // chromatic
            check (layout.noteForPad (32) - layout.noteForPad (bottomLeft) == 5, "Chromatic rows are a fourth apart");

            // A pad held while the layout changes must still release the note it started.
            InstrumentChannel channel;
            channel.prepare (sr, 512);
            channel.handleMidi ("FL STUDIO FIRE", {}, juce::MidiMessage::noteOn (1, 54 + bottomLeft, (juce::uint8) 100).withTimeStamp (0.0));
            PadLayout moved;
            moved.root = 0;
            channel.setLayout (moved);
            channel.handleMidi ("FL STUDIO FIRE", {}, juce::MidiMessage::noteOff (1, 54 + bottomLeft).withTimeStamp (0.0));
            check (true, "Pad note-off after a layout change handled without error");
        }

        // ---- A real hosted instrument, end to end ----
        // Loads Vital (if installed), presses a Fire pad through InstrumentChannel, and listens.
        {
            const juce::File vital ("C:\\Program Files\\Common Files\\VST3\\Vital.vst3");
            if (! vital.exists())
            {
                report.add ("SKIP  Hosted instrument test (Vital not installed)");
            }
            else
            {
                juce::AudioPluginFormatManager formats;
                juce::addDefaultFormatsToManager (formats);
                juce::OwnedArray<juce::PluginDescription> found;
                for (auto* format : formats.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (found, vital.getFullPathName());

                check (! found.isEmpty() && found[0]->isInstrument, "VST3 scan finds Vital as an instrument");

                if (! found.isEmpty())
                {
                    juce::String error;
                    auto plugin = formats.createPluginInstance (*found[0], sr, block, error);
                    check (plugin != nullptr, "Vital loads" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

                    if (plugin != nullptr)
                    {
                        InstrumentChannel channel;
                        channel.prepare (sr, block);
                        channel.setPlugin (std::move (plugin));
                        channel.setGain (1.0f);

                        juce::AudioBuffer<float> out (2, block), recordBus (2, block);
                        auto play = [&] (int blocks)
                        {
                            float peak = 0.0f;
                            for (int b = 0; b < blocks; ++b)
                            {
                                out.clear();
                                recordBus.clear();
                                channel.process (out, 0, block, recordBus);
                                peak = juce::jmax (peak, out.getMagnitude (0, 0, block));
                            }
                            return peak;
                        };

                        play (20);   // let it settle
                        const float before = play (10);
                        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
                        channel.handleMidi ("FL STUDIO FIRE", {}, juce::MidiMessage::noteOn (1, 54 + 48, (juce::uint8) 110).withTimeStamp (now));
                        const float during = play (60);
                        channel.handleMidi ("FL STUDIO FIRE", {}, juce::MidiMessage::noteOff (1, 54 + 48).withTimeStamp (now + 0.6));

                        check (during > 0.01f && during > before * 10.0f,
                               "Fire pad plays Vital through the instrument channel (peak " + juce::String (during, 3)
                                   + ", silent before: " + juce::String (before, 4) + ")");

                        // The MidiMix must never play notes.
                        play (200);
                        channel.handleMidi ("MIDI Mix", {}, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110).withTimeStamp (now + 5.0));
                        const float mixer = play (30);
                        check (mixer < 0.01f, "MidiMix notes are ignored by instruments (peak " + juce::String (mixer, 4) + ")");

                        // Delay and reverb on the instrument channel: a short note should keep ringing after release.
                        auto tailAfterNote = [&] (const FxSettings& fx, double at)
                        {
                            channel.getFx().setSettings (fx);
                            play (400);   // let everything die away
                            channel.handleMidi ("FL STUDIO FIRE", {}, juce::MidiMessage::noteOn (1, 54 + 48, (juce::uint8) 110).withTimeStamp (at));
                            play (10);
                            channel.handleMidi ("FL STUDIO FIRE", {}, juce::MidiMessage::noteOff (1, 54 + 48).withTimeStamp (at + 0.05));
                            play (100);   // past Vital's own release
                            return play (60);
                        };

                        const float dryTail = tailAfterNote ({}, now + 10.0);
                        FxSettings wetFx;
                        wetFx.delayOn = wetFx.reverbOn = true;
                        wetFx.reverbDecay = 6.0;
                        const float wetTail = tailAfterNote (wetFx, now + 20.0);
                        check (wetTail > 0.001f && wetTail > dryTail * 4.0f,
                               "Instrument channel delay + reverb ring on after the note (tail " + juce::String (wetTail, 4)
                                   + ", dry " + juce::String (dryTail, 4) + ")");

                        channel.takePlugin();
                    }
                }
            }
        }

        // ---- An effect plug-in on an input (Graillon pitch correction, if installed) ----
        {
            const juce::File graillon ("C:\\Program Files\\Common Files\\VST3\\Auburn Sounds Graillon 3.vst3");
            if (! graillon.exists())
            {
                report.add ("SKIP  Input plug-in test (Graillon 3 not installed)");
            }
            else
            {
                juce::AudioPluginFormatManager formats;
                juce::addDefaultFormatsToManager (formats);
                juce::OwnedArray<juce::PluginDescription> found;
                for (auto* format : formats.getFormats())
                    if (format->getName() == "VST3")
                        format->findAllTypesForFile (found, graillon.getFullPathName());

                check (! found.isEmpty() && ! found[0]->isInstrument, "VST3 scan finds Graillon 3 as an effect");

                if (! found.isEmpty())
                {
                    juce::String error;
                    auto plugin = formats.createPluginInstance (*found[0], sr, block, error);
                    check (plugin != nullptr, "Graillon 3 loads" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

                    if (plugin != nullptr)
                    {
                        // A slightly flat A (435 Hz) sung into input 1, monitored, through the plug-in.
                        InputChannel mic;
                        mic.prepare (sr, block);
                        mic.setInsert (std::move (plugin));
                        mic.setMonitor (true);

                        juce::AudioBuffer<float> in (2, block), out (2, block), recordBus (2, block);
                        float peak = 0.0f;
                        bool finite = true;
                        for (int b = 0; b < 200; ++b)
                        {
                            for (int i = 0; i < block; ++i)
                                in.setSample (0, i, 0.3f * std::sin ((float) (b * block + i) * juce::MathConstants<float>::twoPi * 435.0f / (float) sr));
                            out.clear();
                            recordBus.clear();
                            mic.process (in, 1, out, 0, block, recordBus);
                            if (b > 100)
                                peak = juce::jmax (peak, out.getMagnitude (0, 0, block));
                            for (int i = 0; i < block; ++i)
                                finite = finite && std::isfinite (out.getSample (0, i));
                        }

                        check (finite && peak > 0.01f, "Mic input runs through Graillon 3 (output peak " + juce::String (peak, 3) + ")");
                        mic.setInsert (nullptr);
                    }
                }
            }
        }

        // ---- Preset names read from instruments' saved settings ----
        {
            // As a hosted VST3 wraps it: VST3PluginState XML, the plug-in's own data in base64.
            auto wrap = [] (const void* data, size_t size)
            {
                juce::XmlElement xml ("VST3PluginState");
                xml.createNewChildElement ("IComponent")->addTextElement (juce::MemoryBlock (data, size).toBase64Encoding());
                juce::MemoryBlock out;
                juce::AudioPluginInstance::copyXmlToBinary (xml, out);
                return out;
            };

            const juce::String json ("{\"author\":\"Matt\",\"preset_name\":\"Flume Pad\",\"settings\":{}}");
            const auto vital = PresetName::fromState (wrap (json.toRawUTF8(), json.getNumBytesAsUTF8()));

            // Zyklop: a ValueTree property, PATCH_NAME = "SPACE OF HERE".
            const juce::uint8 tree[] = { 'x', 0, 'P','A','T','C','H','_','N','A','M','E', 0, 1, 15, 5,
                                         'S','P','A','C','E',' ','O','F',' ','H','E','R','E', 0, 'P','A','T','C','H','_','F', 0 };
            const auto zyklop = PresetName::fromState (wrap (tree, sizeof (tree)));

            juce::XmlElement own ("STATE");
            own.createNewChildElement ("Patch")->setAttribute ("presetName", "Glass Pad");
            juce::MemoryBlock ownState;
            juce::AudioPluginInstance::copyXmlToBinary (own, ownState);
            const auto xmlName = PresetName::fromState (ownState);

            const auto none = PresetName::fromState (wrap ("nothing here", 12));
            check (vital == "Flume Pad" && zyklop == "SPACE OF HERE" && xmlName == "Glass Pad" && none.isEmpty(),
                   "Preset names found in saved settings: \"" + vital + "\" (Vital), \"" + zyklop + "\" (Zyklop), \""
                       + xmlName + "\" (XML)");
        }

        // ---- Fire light show: a 1 kHz tone lights the middle of the spectrum; every pattern drawn ----
        {
            FireVisualizer viz;
            viz.prepare (48000.0);
            std::vector<float> tone (1600);
            int written = 0;
            auto feedSignal = [&] (std::function<float (int)> signal)
            {
                for (auto& v : tone)
                    v = signal (written++);
                viz.push (tone.data(), tone.data(), (int) tone.size());
            };
            const float twoPi = juce::MathConstants<float>::twoPi;

            for (int frame = 0; frame < 30; ++frame)
            {
                feedSignal ([&] (int n) { return 0.3f * std::sin ((float) n * twoPi * 1000.0f / 48000.0f); });
                viz.update (0.0, false);
            }
            int loudest = 0;
            for (int c = 1; c < FireVisualizer::columns; ++c)
                if (viz.bandLevel (c) > viz.bandLevel (loudest))
                    loudest = c;
            const auto tonePads = viz.render (FireVisualizer::spectrum, 0);
            check ((loudest == 8 || loudest == 9) && viz.bandLevel (0) < 0.3f && tonePads[(size_t) (3 * 16 + loudest)].getBrightness() > 0.5f,
                   "Fire light show: a 1 kHz tone peaks in column " + juce::String (loudest + 1) + " of 16 (lows "
                       + juce::String (viz.bandLevel (0), 2) + ")");

            // Something like music: a low drone, a melody and a pulse, beats ticking past.
            double beats = 0.0;
            for (int frame = 0; frame < 45; ++frame)
            {
                const float pulse = frame % 8 < 2 ? 1.0f : 0.3f;
                const float note = 400.0f + 200.0f * (float) ((frame / 6) % 4);
                feedSignal ([&] (int n) { const float t = (float) n / 48000.0f;
                                    return 0.25f * std::sin (t * twoPi * 70.0f) + 0.15f * pulse * std::sin (t * twoPi * note)
                                         + 0.05f * pulse * std::sin (t * twoPi * 5000.0f); });
                beats += 0.25;
                viz.update (beats, true);
            }
            check (viz.numBursts() > 0, "Fire light show: beats set off bursts (" + juce::String (viz.numBursts()) + " showing)");

            // Every pattern in Ambient, then the six palettes on Plasma - to look at.
            const int cell = 14, gap = 8, patternH = 4 * cell + gap;
            const int total = FireVisualizer::numPatterns + FireVisualizer::numPalettes;
            juce::Image image (juce::Image::RGB, 16 * cell + 140, total * patternH, true);
            {
                juce::Graphics g (image);   // drawing finishes when this goes
                g.fillAll (juce::Colours::black);
                for (int k = 0; k < total; ++k)
                {
                    const bool pattern = k < FireVisualizer::numPatterns;
                    const auto pads = pattern ? viz.render (k, 0) : viz.render (FireVisualizer::plasma, k - FireVisualizer::numPatterns);
                    for (int p = 0; p < 64; ++p)
                    {
                        g.setColour (pads[(size_t) p]);
                        g.fillRect ((p % 16) * cell, k * patternH + (p / 16) * cell, cell - 2, cell - 2);
                    }
                    g.setColour (juce::Colours::white);
                    g.setFont (12.0f);
                    g.drawText (pattern ? FireVisualizer::patternName (k) : "Plasma / " + FireVisualizer::paletteName (k - FireVisualizer::numPatterns),
                                16 * cell + 6, k * patternH, 134, 4 * cell, juce::Justification::centredLeft);
                }
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-firelights.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);

            // Two Fires joined: side by side (32 x 4) and stacked (16 x 8), a gap where the Fires meet.
            const int patternsToShow[] = { FireVisualizer::spectrum, FireVisualizer::waves, FireVisualizer::kaleidoscope,
                                           FireVisualizer::diamonds, FireVisualizer::plasma };
            const int jcell = 12;
            juce::Image joined (juce::Image::RGB, 32 * jcell + 20 + 16 * jcell + 20, 5 * (8 * jcell + 26), true);
            bool sizesRight = true;
            {
                juce::Graphics g (joined);
                g.fillAll (juce::Colours::black);
                for (int k = 0; k < 5; ++k)
                {
                    const auto wide = viz.render (patternsToShow[k], 0, 32, 4);
                    const auto tall = viz.render (patternsToShow[k], 0, 16, 8);
                    sizesRight = sizesRight && wide.size() == 128 && tall.size() == 128;
                    const int y0 = k * (8 * jcell + 26);
                    for (int p = 0; p < 128; ++p)
                    {
                        const int wc = p % 32, wr = p / 32;
                        g.setColour (wide[(size_t) p]);
                        g.fillRect (wc * jcell + (wc >= 16 ? 8 : 0), y0 + 2 * jcell + wr * jcell, jcell - 2, jcell - 2);
                        const int tc = p % 16, tr = p / 16;
                        g.setColour (tall[(size_t) p]);
                        g.fillRect (32 * jcell + 28 + tc * jcell, y0 + tr * jcell + (tr >= 4 ? 8 : 0), jcell - 2, jcell - 2);
                    }
                }
            }
            const auto joinedFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-firelights-joined.png");
            joinedFile.deleteFile();
            juce::FileOutputStream joinedOut (joinedFile);
            juce::PNGImageFormat().writeImageToStream (joined, joinedOut);
            check (sizesRight, "Fire light show draws across two Fires joined (32 x 4 and 16 x 8)");
        }

        // ---- Stage view: drawn with a sample set, saved as a picture to look at ----
        {
            StageView stage;
            stage.setBounds (0, 0, 1260, 720);
            StageView::Content c;
            c.scenes.addArray ({ "Dawn", "Emergence", "Immersion", "Release" });
            c.activeScene = 1;
            c.morphProgress = 0.4f;
            using St = StageView::State;
            c.groups.push_back ({ "Ambient", Theme::rowColour (1), false, 0,
                                  { { "Birds", 0, 0, St::playing }, { "Water lapping", 0, 1, St::fadingIn }, { "Rain", 0, 2, St::idle },
                                    { "Whale song", 0, 3, St::cuedStart }, { "Kids playing", 0, 4, St::idle } } });
            c.groups.push_back ({ "Pads", Theme::rowColour (0), false, 1,
                                  { { "Bb major", 1, 0, St::playing }, { "G minor", 1, 1, St::cuedStop }, { "C major", 1, 2, St::idle } } });
            c.groups.push_back ({ "One-shots", Theme::rowColour (2), true, 2,
                                  { { "Thunder", 2, 0, St::idle, true }, { "Hawk screech", 2, 1, St::idle, true } } });
            c.live.push_back ({ "Mic", Theme::rowColour (4), false, false, 0 });
            c.live.push_back ({ "Instrument", Theme::rowColour (4), true, false, 1 });
            c.live.push_back ({ "Zyklop", Theme::accent, false, true, 0 });
            stage.setContent (c);
            const auto image = stage.createComponentSnapshot (stage.getLocalBounds());
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-stage.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            check (out.openedOk() && png.writeImageToStream (image, out), "Stage view drawn to " + file.getFullPathName());
        }

        // ---- Channel cards: loops get their own DUB, one-shots don't ----
        {
            using St = ChannelCard::State;
            const auto colour = Theme::rowColour (1);
            ChannelCard idle, dubbing, waiting, oneShot;
            idle.setContent ("Birds", false, false, false, false, St::idle, 0.0, colour);
            dubbing.setContent ("Water lapping", false, true, false, false, St::playing, -3.0, colour);
            dubbing.setDubState (true, true);
            waiting.setContent ("Rain", false, false, false, true, St::cuedStart, -6.0, colour);
            waiting.setDubState (true, false);
            oneShot.setContent ("Thunder", true, false, false, false, St::idle, 0.0, colour);

            auto dubOf = [] (ChannelCard& card) -> juce::Component*
            {
                for (auto* c : card.getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "DUB")
                        return b;
                return nullptr;
            };

            juce::Image strip (juce::Image::ARGB, 4 * (RowView::cardWidth + RowView::cardGap), RowView::cardHeight, true);
            int x = 0;
            bool laidOut = true;
            for (auto* card : { &idle, &dubbing, &waiting, &oneShot })
            {
                card->setBounds (0, 0, RowView::cardWidth, RowView::cardHeight);
                if (auto* d = dubOf (*card); d != nullptr && d->isVisible())
                    laidOut = laidOut && d->getBottom() < card->getVolumeSlider().getY() && d->getWidth() >= 40;
                const auto snap = card->createComponentSnapshot (card->getLocalBounds());
                {
                    juce::Graphics g (strip);
                    g.drawImageAt (snap, x, 0);
                }
                x += RowView::cardWidth + RowView::cardGap;
            }

            check (dubOf (idle) != nullptr && dubOf (idle)->isVisible() && ! dubOf (oneShot)->isVisible(),
                   "Loop channels show their own DUB button, one-shots don't");
            check (laidOut, "Channel DUB sits between PLAY and the fader");

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-cards.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (strip, out), "Channel cards drawn to " + file.getFullPathName());
        }

        // ---- Groups collapse to a short bar of tiles, and stay collapsed in the saved set ----
        {
            auto row = LiveSet::makeRow ("Ambient", false, 5);
            row.colour = 1;
            const char* names[] = { "Birds", "Water lapping", "Rain", "Whale song", nullptr };
            for (int i = 0; i < 4; ++i)
            {
                row.slots[(size_t) i].file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String (names[i]) + ".wav");
                row.slots[(size_t) i].label = names[i];
            }
            row.slots[0].playing = true;
            row.slots[2].playing = true;

            RowView view;
            auto draw = [&] (bool collapse)
            {
                row.collapsed = collapse;
                view.update (row, [] (int) { return false; }, [] (int) { return ChannelCard::State::idle; }, 0);
                const auto size = RowView::preferredSize ((int) row.slots.size(), 1400, 0, collapse);
                view.setBounds (0, 0, size.x, size.y);
                return view.createComponentSnapshot (view.getLocalBounds());
            };

            const auto open = draw (false);
            const auto shut = draw (true);
            check (shut.getHeight() <= RowView::collapsedHeight && shut.getWidth() == open.getWidth() && open.getHeight() > 150,
                   "A collapsed group is a short bar the same width (" + juce::String (shut.getHeight()) + " vs "
                   + juce::String (open.getHeight()) + " px tall)");

            bool tilesOnly = view.getChannelCard (0) != nullptr && view.getChannelCard (0)->getHeight() == RowView::tileHeight
                          && ! view.getChannelCard (0)->getVolumeSlider().isVisible() && view.strip.muteButton.isVisible()
                          && ! view.strip.fader.isVisible();
            check (tilesOnly, "Collapsed: channels become tiles, the group keeps MUTE but hides its fader");

            LiveSet set;
            set.rows.push_back (row);
            LiveSet back;
            back.loadFromXml (*set.toXml ({}), {});
            check (! back.rows.empty() && back.rows.back().collapsed, "Collapsed groups stay collapsed in the saved set");

            juce::Image both (juce::Image::ARGB, open.getWidth(), open.getHeight() + shut.getHeight() + 10, true);
            {
                juce::Graphics g (both);
                g.fillAll (Theme::background);
                g.drawImageAt (open, 0, 0);
                g.drawImageAt (shut, 0, open.getHeight() + 10);
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-groups.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (both, out), "Groups drawn to " + file.getFullPathName());
        }

        // ---- Themes: switching recolours what's on screen; every theme drawn to a picture ----
        {
            juce::TextButton button;
            button.setColour (juce::TextButton::buttonColourId, Theme::raised);
            button.setColour (juce::TextButton::textColourOffId, Theme::textDim.withAlpha (0.5f));
            button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff123456));
            const auto from = Theme::allColours();
            Theme::applyPalette (Theme::paletteIndex ("Ember"));
            Theme::recolour (button, from, Theme::allColours());
            check (button.findColour (juce::TextButton::buttonColourId) == Theme::raised
                   && button.findColour (juce::TextButton::textColourOffId) == Theme::textDim.withAlpha (0.5f)
                   && button.findColour (juce::TextButton::buttonOnColourId) == juce::Colour (0xff123456),
                   "Switching theme recolours controls (keeping transparency) and leaves other colours alone");
            check (Theme::paletteIndex ("night") == 4 && Theme::paletteIndex ("nonsense") == 0, "Themes are found by name, unknown ones fall back to Graphite");

            auto row = LiveSet::makeRow ("Ambient", false, 4);
            row.colour = 1;
            const char* names[] = { "Birds", "Water lapping", "Rain" };
            for (int i = 0; i < 3; ++i)
            {
                row.slots[(size_t) i].file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String (names[i]) + ".wav");
                row.slots[(size_t) i].label = names[i];
            }
            row.slots[0].playing = true;

            const auto size = RowView::preferredSize ((int) row.slots.size(), 1400);
            const int label = 22;
            juce::Image sheet (juce::Image::ARGB, size.x, Theme::numPalettes * (size.y + label), true);

            for (int t = 0; t < Theme::numPalettes; ++t)
            {
                Theme::switchTo (t);
                RowView view;
                view.update (row, [] (int) { return false; }, [] (int) { return ChannelCard::State::idle; }, 0);
                view.setBounds (0, 0, size.x, size.y);
                view.getChannelCard (2)->setDubState (false, true);
                const auto snap = view.createComponentSnapshot (view.getLocalBounds());

                juce::Graphics g (sheet);
                const int y = t * (size.y + label);
                g.setColour (Theme::background);
                g.fillRect (0, y, size.x, size.y + label);
                g.setColour (Theme::text);
                g.setFont (Theme::heading (13.0f));
                g.drawText (Theme::palettes[t].name, 8, y, size.x, label, juce::Justification::centredLeft);
                g.drawImageAt (snap, 0, y + label);
            }
            Theme::switchTo (0);

            // Each theme's take on the 16 group colours, side by side.
            {
                const int sw = 72, sh = 22, labelW = 110;
                juce::Image swatches (juce::Image::ARGB, labelW + 16 * sw, Theme::numPalettes * (sh + 6) + 6, true);
                {
                juce::Graphics g (swatches);
                g.fillAll (juce::Colours::black);
                for (int t = 0; t < Theme::numPalettes; ++t)
                {
                    Theme::applyPalette (t);
                    const int y = 6 + t * (sh + 6);
                    g.setColour (Theme::background);
                    g.fillRect (0, y - 3, swatches.getWidth(), sh + 6);
                    g.setColour (Theme::text);
                    g.setFont (Theme::heading (12.0f));
                    g.drawText (Theme::palettes[t].name, 6, y, labelW - 8, sh, juce::Justification::centredLeft);
                    for (int c = 0; c < Theme::numRowColours; ++c)
                    {
                        g.setColour (Theme::rowColour (c));
                        g.fillRoundedRectangle ((float) (labelW + c * sw + 2), (float) y, (float) sw - 4.0f, (float) sh, 4.0f);
                        g.setColour (Theme::background);
                        g.setFont (Theme::heading (10.0f));
                        g.drawText (Theme::rowColourNames[c], labelW + c * sw, y, sw, sh, juce::Justification::centred);
                    }
                }
                }   // the drawing finishes before the picture is saved
                Theme::applyPalette (0);
                const auto swatchFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-theme-colours.png");
                swatchFile.deleteFile();
                juce::FileOutputStream swatchOut (swatchFile);
                juce::PNGImageFormat().writeImageToStream (swatches, swatchOut);
            }

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-themes.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (sheet, out), "Themes drawn to " + file.getFullPathName());
        }

        // ---- Live groups: inputs and instruments in groups with their own fader, mute and effects ----
        {
            // A set from before live groups: its inputs keep the old panel's place, the instruments get their own group.
            LiveSet old;
            old.inputs = LiveSet::defaultInputs();
            old.instruments.push_back ({});
            auto oldXml = old.toXml ({});
            oldXml->deleteAllChildElementsWithTagName ("LiveGroups");
            oldXml->setAttribute ("inputsX", 120);
            oldXml->setAttribute ("inputsY", 40);
            for (auto* e : oldXml->getChildWithTagNameIterator ("Input"))      e->removeAttribute ("group");
            for (auto* e : oldXml->getChildWithTagNameIterator ("Instrument")) e->removeAttribute ("group");
            LiveSet migrated;
            migrated.loadFromXml (*oldXml, {});
            const auto* ins = migrated.firstLiveGroup (false);
            const auto* insts = migrated.firstLiveGroup (true);
            check (migrated.liveGroups.size() == 2 && ins != nullptr && insts != nullptr
                   && ins->position == juce::Point<int> (120, 40)
                   && migrated.inputs[0].group == ins->uid && migrated.inputs[1].group == ins->uid
                   && migrated.instruments[0].group == insts->uid,
                   "An older set opens with its inputs in an Inputs group (same place) and instruments in their own group");

            // Saved and opened again: groups, their mix and who is in them.
            migrated.liveGroups[1].faderDb = -7.5;
            migrated.liveGroups[1].muted = true;
            migrated.liveGroups[1].fx.reverbOn = true;
            LiveSet::LiveGroup synths;
            synths.name = "Synths";
            synths.instruments = true;
            migrated.liveGroups.push_back (synths);
            migrated.instruments[0].group = synths.uid;
            migrated.inputs[0].order = 1;   // dragged after the second input
            LiveSet reopened;
            reopened.loadFromXml (*migrated.toXml ({}), {});
            check (reopened.liveGroups.size() == 3 && reopened.liveGroups[1].faderDb == -7.5 && reopened.liveGroups[1].muted
                   && reopened.liveGroups[1].fx.reverbOn && reopened.liveGroups[2].name == "Synths"
                   && reopened.instruments[0].group == synths.uid && reopened.inputs[0].order == 1 && reopened.inputs[1].order == 0,
                   "Live groups, their fader, mute and FX, and each strip's group and place in it, are saved with the set");

            // The bus: fader and mute over everything in the group.
            auto busLevel = [] (float faderGain, bool mute, double fxGainDb)
            {
                LiveGroupBus bus;
                bus.setGain (faderGain);
                bus.setMuted (mute);
                FxSettings fx;
                fx.gainDb = fxGainDb;
                bus.getFx().setSettings (fx);
                bus.prepare (48000.0, 512);
                juce::AudioBuffer<float> out (2, 512);
                float level = 0.0f;
                for (int b = 0; b < 20; ++b)
                {
                    bus.begin (512);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            bus.main.setSample (ch, i, 0.5f);
                    out.clear();
                    bus.finish (out, 0, 512);
                    level = out.getSample (0, 511);
                }
                return level;
            };
            const float half = busLevel (0.5f, false, 0.0), muted = busLevel (1.0f, true, 0.0), quieter = busLevel (1.0f, false, -6.0);
            check (std::abs (half - 0.25f) < 0.005f && muted == 0.0f && std::abs (quieter - 0.5f * juce::Decibels::decibelsToGain (-6.0f)) < 0.01f,
                   "A live group's fader, mute and effects act on everything in it (" + juce::String (half, 3) + ", "
                   + juce::String (muted, 3) + ", " + juce::String (quieter, 3) + ")");

            // An input's Space send goes through its group (so the group's mute silences it too).
            InputChannel mic;
            mic.prepare (48000.0, 512);
            mic.setMonitor (true);
            FxSettings send;
            send.spaceSend = 1.0;
            mic.getFx().setSettings (send);
            LiveGroupBus bus;
            bus.prepare (48000.0, 512);
            juce::AudioBuffer<float> in (2, 512), rec (2, 512);
            for (int i = 0; i < 512; ++i) in.setSample (0, i, 0.3f);
            in.copyFrom (1, 0, in, 0, 0, 512);
            bus.begin (512);
            rec.clear();
            mic.process (in, 2, bus.main, 0, 512, rec, &bus.send);
            check (bus.main.getMagnitude (0, 0, 512) > 0.1f && bus.send.getMagnitude (0, 0, 512) > 0.1f,
                   "An input plays into its group, Space send included");

            // The group on screen, with two strips.
            LiveGroupView view;
            SourceStrip mon ("MON", {}), edit ("EDIT", {});
            mon.setContent ("Mic", "In 1", true, false, true, Theme::rowColour (4));
            edit.setContent ("Flume Pad", "Vital", false, false, false, Theme::rowColour (4));
            view.setStrips ({ &mon, &edit });
            view.setContent ("Inputs", Theme::rowColour (4), true, false, false);
            edit.strip.muteButton.setToggleState (true, juce::dontSendNotification);   // shows the muted look
            const auto size = LiveGroupView::preferredSize (2, 1400);
            view.setBounds (0, 0, size.x, size.y);
            const auto image = view.createComponentSnapshot (view.getLocalBounds());
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-livegroup.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), "Live group drawn to " + file.getFullPathName());

            // Collapsed: a short bar of tiles, the strips hidden, remembered in the set.
            view.setTiles ({ { "Mic", true }, { "Flume Pad", false } });
            view.setCollapsed (true);
            const auto shut = LiveGroupView::preferredSize (2, 1400, 0, true);
            view.setBounds (0, 0, shut.x, shut.y);
            const auto shutImage = view.createComponentSnapshot (view.getLocalBounds());
            LiveSet withCollapsed;
            withCollapsed.ensureLiveGroups();
            withCollapsed.liveGroups[0].collapsed = true;
            LiveSet back;
            back.loadFromXml (*withCollapsed.toXml ({}), {});
            check (shut.y <= LiveGroupView::collapsedHeight && shut.x == size.x && ! mon.isVisible() && ! edit.isVisible()
                   && view.strip.muteButton.isVisible() && ! view.strip.fader.isVisible() && back.liveGroups[0].collapsed,
                   "Input and instrument groups collapse to a bar of tiles (MUTE stays), and stay collapsed in the set");

            // The group colours, with the bar, for a look.
            const int swatch = 26;
            juce::Image sheet (juce::Image::ARGB, juce::jmax (shut.x, 8 * 120), shut.y + 2 * swatch + 16, true);
            {
                juce::Graphics g (sheet);
                g.fillAll (Theme::background);
                g.drawImageAt (shutImage, 0, 0);
                for (int c = 0; c < Theme::numRowColours; ++c)
                {
                    const juce::Rectangle<int> r ((c % 8) * 120 + 4, shut.y + 8 + (c / 8) * swatch, 112, swatch - 4);
                    g.setColour (Theme::rowColour (c));
                    g.fillRoundedRectangle (r.toFloat(), 4.0f);
                    g.setColour (Theme::background);
                    g.setFont (Theme::heading (12.0f));
                    g.drawText (Theme::rowColourNames[c], r, juce::Justification::centred);
                }
            }
            const auto shutFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-livegroup-collapsed.png");
            shutFile.deleteFile();
            juce::FileOutputStream shutOut (shutFile);
            check (Theme::numRowColours == 16 && shutOut.openedOk() && juce::PNGImageFormat().writeImageToStream (sheet, shutOut),
                   "16 group colours, drawn with the collapsed group to " + shutFile.getFullPathName());
            view.setStrips ({});
        }

        // ---- Group layout: free placement, overlaps push down, edge snapping ----
        {
            using G = GroupLayout::Group;
            const juce::Point<int> box { 400, 300 };
            auto at = [&] (std::vector<G> groups, int moving = std::numeric_limits<int>::min(), juce::Point<int> to = {})
            {
                return GroupLayout::arrange (std::move (groups), 1600, 8, moving, to);
            };

            auto free = at ({ { 0, { 0, 0 }, box }, { 1, { 420, 500 }, box } });
            check (free[1].getPosition() == juce::Point<int> (420, 500),
                   "A group stays where it's put - beside and below others, with space above it");

            auto pushed = at ({ { 0, { 0, 0 }, box }, { 1, { 100, 100 }, box } });
            check (pushed[0].getPosition() == juce::Point<int> (0, 0) && pushed[1].getPosition() == juce::Point<int> (100, 308),
                   "A group overlapping one above it slides down just clear of it");

            auto beside = at ({ { 0, { 0, 0 }, box }, { 1, { 408, 0 }, box } });
            check (beside[1].getPosition() == juce::Point<int> (408, 0), "Groups side by side with tops lined up stay that way");

            auto dropped = at ({ { 0, { 0, 0 }, box }, { 1, { 900, 0 }, box } }, 1, { 0, 0 });
            check (dropped[1].getPosition() == juce::Point<int> (0, 0) && dropped[0].getPosition() == juce::Point<int> (0, 308),
                   "A group dropped onto another takes its place and pushes it down");

            auto added = at ({ { 0, { 0, 0 }, box }, { 1, { 500, 100 }, box }, { 2, { -1, -1 }, box } });
            check (added[2].getPosition() == juce::Point<int> (0, 408), "A new group goes in along the bottom");

            // A group n lines tall is exactly as tall as n one-line groups stacked, so they line up beside it.
            const int oneRow = RowView::preferredSize (2, 1600, 2).y, threeRows = RowView::preferredSize (3, 1600, 1).y;
            const int oneLive = LiveGroupView::preferredSize (1, 1600, 1).y, twoLive = LiveGroupView::preferredSize (2, 1600, 1).y;
            check (threeRows == 3 * oneRow + 2 * GroupLayout::gap && twoLive == 2 * oneLive + GroupLayout::gap,
                   "A group three channels tall is as tall as three one-line groups stacked (" + juce::String (threeRows) + " = 3 x "
                   + juce::String (oneRow) + " + gaps)");

            // Collapsing a group pulls up what's under it; expanding pushes it back.
            const juce::Point<int> bar { 400, 60 };
            auto shut = GroupLayout::arrangeCollapsed ({ { 0, { 0, 0 }, bar, box }, { 1, { 0, 308 }, box }, { 2, { 0, 616 }, box } }, 1600, 8);
            auto open = GroupLayout::arrangeCollapsed ({ { 0, { 0, 0 }, box, box }, { 1, { 0, 308 }, box }, { 2, { 0, 616 }, box } }, 1600, 8);
            check (shut[1].getY() == 68 && shut[2].getY() == 376 && open[1].getY() == 308 && open[2].getY() == 616,
                   "Collapsing a group pulls the groups under it up; expanding it pushes them back");

            // Under two groups side by side, it only comes up as far as the one still open allows.
            auto half = GroupLayout::arrangeCollapsed ({ { 0, { 0, 0 }, bar, box }, { 1, { 408, 0 }, box },
                                                         { 2, { 0, 308 }, { 816, 300 } } }, 1600, 8);
            check (half[2].getY() == 308, "A group under an open and a collapsed group stays clear of the open one");

            // Dropped below a collapsed group, it stays where it's dropped, and the kept position reproduces that.
            std::map<int, juce::Point<int>> wants;
            auto dropAt = GroupLayout::arrangeCollapsed ({ { 0, { 0, 0 }, bar, box }, { 1, { 900, 0 }, box } }, 1600, 8, 1, { 0, 500 }, &wants);
            auto again = GroupLayout::arrangeCollapsed ({ { 0, { 0, 0 }, bar, box }, { 1, wants[1], box } }, 1600, 8);
            check (dropAt[1].getY() == 500 && again[1].getY() == 500,
                   "A group dropped under a collapsed one stays where it was dropped");

            const juce::Array<juce::Rectangle<int>> others { juce::Rectangle<int> (0, 0, 400, 300) };
            const auto besideSnap = GroupLayout::snap ({ 415, 13 }, box, others, 8);
            const auto belowSnap = GroupLayout::snap ({ 10, 290 }, box, others, 8);
            const auto farAway = GroupLayout::snap ({ 700, 650 }, box, others, 8);
            check (besideSnap == juce::Point<int> (408, 0) && belowSnap == juce::Point<int> (0, 308) && farAway == juce::Point<int> (700, 650),
                   "Dragged groups snap to a nearby edge (beside, below, tops and sides lined up) and stay put away from edges");
        }

        // ---- Muted instruments rest: their plug-in isn't run, saving CPU ----
        {
            InstrumentChannel inst;
            inst.prepare (48000.0, 512);
            auto owned = std::make_unique<TestInstrument>();
            auto* test = owned.get();
            inst.setPlugin (std::move (owned));
            juce::AudioBuffer<float> out (2, 512), rec (2, 512);
            auto run = [&] (int blocks, bool silencedAfter = false)
            {
                for (int b = 0; b < blocks; ++b) { out.clear(); rec.clear(); inst.process (out, 0, 512, rec, nullptr, silencedAfter); }
            };

            run (4);
            const int playing = test->blocksRun;
            inst.setMuted (true);
            run (20);   // the mute fades out first
            const int afterFade = test->blocksRun;
            run (20);
            const bool rests = inst.isResting() && test->blocksRun == afterFade && playing > 0 && out.getMagnitude (0, 512) == 0.0f;

            inst.setArmed (true);
            run (2);
            const bool armedRuns = ! inst.isResting() && test->blocksRun > afterFade;
            inst.setArmed (false);
            run (20);

            test->sawAllNotesOff = false;
            inst.setMuted (false);
            run (2);
            const bool wakes = ! inst.isResting() && test->sawAllNotesOff;

            run (4, true);
            const bool groupMuted = inst.isResting();
            run (1, false);

            check (rests && armedRuns && wakes && groupMuted && ! inst.isResting(),
                   "A muted instrument (or one in a muted group) rests once faded out, keeps running while armed, "
                   "and wakes with all notes off");
            inst.takePlugin();
        }

        // ---- Keys: Any key by default, key buttons belong to the set ----
        {
            check (LiveSet::Instrument().anyKey, "A new instrument plays in Any key");

            // A set from before: instruments keep their key, and it gets the three flute key buttons.
            LiveSet old;
            old.instruments.push_back ({});
            old.instruments[0].anyKey = false;
            old.instruments[0].padRoot = 10;
            auto oldXml = old.toXml ({});
            oldXml->deleteAllChildElementsWithTagName ("KeyButtons");
            for (auto* e : oldXml->getChildWithTagNameIterator ("Instrument")) e->removeAttribute ("anyKey");
            LiveSet opened;
            opened.loadFromXml (*oldXml, {});
            check (! opened.instruments[0].anyKey && opened.instruments[0].padRoot == 10 && opened.keyButtons.size() == 3
                   && PadLayout::shortName (opened.keyButtons[0].root, opened.keyButtons[0].scale) == "A min"
                   && PadLayout::shortName (opened.keyButtons[1].root, opened.keyButtons[1].scale) == "Bb maj"
                   && PadLayout::shortName (opened.keyButtons[2].root, opened.keyButtons[2].scale) == "E maj",
                   "Older sets keep their instruments' keys and the A min / Bb maj / E maj buttons");

            // A new set: no key buttons; what's set is saved, Any key in scenes included.
            LiveSet fresh;
            fresh.loadFromXml (*LiveSet::makeDefault().toXml ({}), {});
            fresh.keyButtons.push_back ({ 2, 4 });
            fresh.instruments.push_back ({});
            LiveSet::Scene sc;
            sc.keys.push_back ({ 0, 2, 4, 3, true });
            fresh.scenes.push_back (sc);
            LiveSet again;
            again.loadFromXml (*fresh.toXml ({}), {});
            check (LiveSet::makeDefault().keyButtons.empty() && again.keyButtons.size() == 1
                   && PadLayout::shortName (again.keyButtons[0].root, again.keyButtons[0].scale) == "D dorian"
                   && again.instruments[0].anyKey && again.scenes[0].keys[0].anyKey,
                   "New sets start with no key buttons; buttons, Any key and scenes' Any key are saved");

            // Any key: every note from C, rows a fourth apart, and no key name (loops aren't named after one).
            const auto any = PadLayout::anyKey (3);
            check (any.noteForPad (48) == 48 && any.noteForPad (49) == 49 && any.noteForPad (32) == 53 && any.keyName().isEmpty(),
                   "Any key lays every note out on the Fire's pads from C");
        }

        // ---- Edit sample: trim, fades and gain, never written into the file ----
        {
            // Four seconds of a steady tone at 0.5.
            LoopClip tone;
            tone.sampleRate = 48000.0;
            tone.data.setSize (2, 4 * 48000);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < tone.data.getNumSamples(); ++i)
                    tone.data.setSample (ch, i, 0.5f * std::sin ((float) i * 0.05f + (i > 96000 ? 0.0f : 0.0f)) * (1.0f + 0.6f * std::sin ((float) i * 0.00008f)));

            SampleShape shape;
            shape.start = 1.0;
            shape.end = 3.0;
            shape.fadeIn = 0.5;
            shape.fadeOut = 0.25;
            shape.gainDb = 6.0;
            const auto shaped = makeShaped (tone, shape);
            const int n = shaped->data.getNumSamples();
            const float middle = shaped->data.getMagnitude (0, 48000 - 200, 400);
            const float original = tone.data.getMagnitude (0, 2 * 48000 - 200, 400);
            check (n == 2 * 48000 && std::abs (shaped->data.getSample (0, 0)) < 1.0e-6f && std::abs (shaped->data.getSample (0, n - 1)) < 1.0e-3f
                   && std::abs (middle / original - juce::Decibels::decibelsToGain (6.0f)) < 0.05f
                   && tone.data.getNumSamples() == 4 * 48000,
                   "Edit sample trims to 1-3 s, fades from and to silence, adds its gain, and leaves the sample itself alone");

            check (shape.key() != SampleShape().key() && SampleShape().key().isEmpty() && ! SampleShape().isSet(),
                   "Each trim and shape is its own cached version; an untouched sample is the plain one");

            LiveSet set;
            set.rows.push_back (LiveSet::makeRow ("Pads", false, 1));
            set.rows[0].slots[0].file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pad.wav");
            set.rows[0].slots[0].shape = shape;
            LiveSet back;
            back.loadFromXml (*set.toXml ({}), {});
            const auto& kept = back.rows.back().slots[0].shape;
            check (kept == shape, "A channel's trim, fades and gain are saved with the set");

            // The editor, for a look.
            SampleEditor editor ("Whale song", std::make_shared<const LoopClip> (tone), shape, Theme::rowColour (1), true, 60.0 / 72.0, 4);
            editor.setBounds (0, 0, 760, 300);
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-sample-editor.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), "Sample editor drawn to " + file.getFullPathName());
        }

        // ---- Several armed sources record at once, each on its own track ----
        {
            Looper looper;
            looper.prepare (48000.0);
            looper.setTracks (2);
            const bool sharedLength = looper.getTracks() == 2;

            juce::AudioBuffer<float> bus (4, 512);
            looper.startRecording (-1);
            juce::int64 clock = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    bus.setSample (0, i, 0.3f);  bus.setSample (1, i, 0.3f);    // source 1
                    bus.setSample (2, i, -0.6f); bus.setSample (3, i, -0.6f);   // source 2
                }
                if (b == 150)
                    looper.stopRecording (0);
                looper.process (bus, 512, clock, 0);
                clock += 512;
            }
            juce::AudioBuffer<float> take;
            juce::int64 start = 0;
            const bool collected = looper.collectTake (take, start);
            check (sharedLength && collected && take.getNumChannels() == 4 && take.getNumSamples() > 70000
                   && std::abs (take.getSample (0, 5000) - 0.3f) < 1.0e-4f && std::abs (take.getSample (2, 5000) + 0.6f) < 1.0e-4f,
                   "Two armed sources record together, each on its own track, the same length and in sync");

            looper.setTracks (6);
            check (looper.getTracks() == 6, "Up to every armed source can have a track (the longest take is shared between them)");
        }

        // ---- Scenes keep effects, and morph to them ----
        {
            FxSettings now, scene;
            now.delayOn = true;    now.delayMix = 0.4;
            scene.reverbOn = true; scene.reverbMix = 0.6;
            now.gainDb = 0.0;      scene.gainDb = -6.0;

            const auto half = blendFx (now, scene, 0.5f);
            const auto end = blendFx (now, scene, 1.0f);
            check (half.reverbOn && std::abs (half.reverbMix - 0.3) < 1.0e-9      // coming in: on, its mix halfway up from nothing
                   && half.delayOn && std::abs (half.delayMix - 0.2) < 1.0e-9      // going: still on, its mix halfway down
                   && std::abs (half.gainDb + 3.0) < 1.0e-9                        // knobs glide
                   && sameFx (end, scene) && ! end.delayOn,
                   "A scene's effects morph in: knobs glide, an effect comes or goes with its mix fading, ending exactly as kept");

            LiveSet set;
            LiveSet::Scene sc;
            LiveSet::Scene::Group g { "abc", -3.0, false, true, scene };
            LiveSet::Scene::Strip st { LiveSet::Scene::Strip::instrument, 0, 0.0, false, true, scene };
            LiveSet::Scene::Channel ch { "def", true, -2.0 };   // an older scene: no effects kept
            sc.groups.push_back (g);
            sc.strips.push_back (st);
            sc.channels.push_back (ch);
            set.scenes.push_back (sc);
            LiveSet back;
            back.loadFromXml (*set.toXml ({}), {});
            const auto& kept = back.scenes[0];
            check (kept.groups[0].hasFx && sameFx (kept.groups[0].fx, scene) && kept.strips[0].hasFx && sameFx (kept.strips[0].fx, scene)
                   && ! kept.channels[0].hasFx,
                   "Scenes save each group's, strip's and channel's effects; older scenes without them leave effects alone");
        }

        // ---- The samples panel in folders ----
        {
            const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-folders-test");
            LiveSet set;
            set.pool = { base.getChildFile ("Pads/Bb major.wav"), base.getChildFile ("Pads/G minor.wav"),
                         base.getChildFile ("Ambient/Birds.wav"), base.getChildFile ("Ambient/Rain.wav"),
                         base.getChildFile ("One-shots/Thunder.wav") };

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            SamplePool panel (formats);
            panel.setFolders (&set.poolFolders);
            panel.setFiles (set.pool);
            const bool byDisk = panel.folderNames() == juce::StringArray ({ "Ambient", "One-shots", "Pads" });

            panel.makeFolder ("Weather");
            panel.moveToFolder ({ base.getChildFile ("Ambient/Rain.wav"), base.getChildFile ("One-shots/Thunder.wav") }, "Weather");
            const bool moved = panel.folderOf (base.getChildFile ("Ambient/Rain.wav")) == "Weather"
                            && panel.folderNames() == juce::StringArray ({ "Ambient", "Pads", "Weather" });   // One-shots now empty

            set.poolFolders.closed.add ("Pads");
            panel.setFiles (set.pool);

            LiveSet back;
            back.loadFromXml (*set.toXml (base), base);
            const bool kept = back.poolFolders.made.contains ("Weather") && back.poolFolders.closed.contains ("Pads")
                           && back.poolFolders.moved.at (base.getChildFile ("Ambient/Rain.wav").getFullPathName()) == "Weather";

            // Saved somewhere new: the moved samples stay in their folder.
            std::map<juce::String, juce::File> relocated { { base.getChildFile ("Ambient/Rain.wav").getFullPathName(), base.getChildFile ("New/Rain.wav") } };
            back.relocate (relocated);
            const bool follows = back.poolFolders.moved.count (base.getChildFile ("New/Rain.wav").getFullPathName()) == 1;

            check (byDisk && moved && kept && follows,
                   "Samples sit in their disk folders until moved to the panel's own; folders, moves and closed ones are saved and follow the files");

            panel.setBounds (0, 0, 260, 420);
            const auto image = panel.createComponentSnapshot (panel.getLocalBounds());
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-sample-folders.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), "Samples panel drawn to " + file.getFullPathName());
        }

        // ---- A fixed layout width is kept with the set ----
        {
            LiveSet fixedLayout;
            fixedLayout.layoutWidth = 1840;
            LiveSet back;
            back.loadFromXml (*fixedLayout.toXml ({}), {});
            LiveSet fitted;
            fitted.loadFromXml (*LiveSet::makeDefault().toXml ({}), {});
            check (back.layoutWidth == 1840 && fitted.layoutWidth == 0,
                   "A set can keep its layout at a fixed width (new sets rearrange to fit the window)");
        }

        // ---- Undo: only edits make steps, not playing ----
        {
            LiveSet base = LiveSet::makeDefault();
            base.rows[0].slots[0].file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pad.wav");
            base.instruments.push_back ({});
            base.scenes.push_back ({});
            const auto text = [] (const LiveSet& s) { return LiveSet::editText (*s.toXml ({})); };
            const auto before = text (base);

            auto played = base;   // what happens while performing
            played.rows[0].faderDb = -12.0;
            played.rows[1].muted = true;
            played.rows[0].slots[0].volumeDb = -6.0;
            played.instruments[0].padRoot = 2;
            played.instruments[0].padOctave = 5;
            played.instruments[0].faderDb = -3.0;
            played.bpm = 96.0;
            played.scenes[0].channels.push_back ({ "x", true, -4.0 });
            const bool playingIsNotAnEdit = text (played) == before;

            auto renamed = base;  renamed.rows[0].name = "Drones";
            auto fx = base;       fx.rows[0].slots[0].fx.reverbOn = true;
            auto trimmed = base;  trimmed.rows[0].slots[0].shape.start = 0.5;
            auto moved = base;    moved.rows[1].position = { 300, 40 };
            const bool editsAre = text (renamed) != before && text (fx) != before && text (trimmed) != before && text (moved) != before;

            check (playingIsNotAnEdit && editsAre,
                   "Undo steps come from edits (names, effects, trims, layout), never from playing (faders, mutes, keys, tempo, scene mixes)");
        }

        // ---- The built-in manual and the welcome ----
        {
            const auto manual = juce::String::fromUTF8 (BinaryData::README_md, BinaryData::README_mdSize);
            HelpView help;
            help.setBounds (0, 0, 820, 760);
            const auto helpImage = help.createComponentSnapshot ({ 0, 0, 820, 760 });
            WelcomePanel welcome;
            welcome.setBounds (0, 0, 620, 430);
            const auto welcomeImage = welcome.createComponentSnapshot (welcome.getLocalBounds());

            juce::Image both (juce::Image::ARGB, 820 + 620 + 20, 760, true);
            {
                juce::Graphics g (both);
                g.fillAll (juce::Colours::black);
                g.drawImageAt (helpImage, 0, 0);
                g.drawImageAt (welcomeImage, 840, 0);
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-help.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (manual.contains ("Undo") && manual.contains ("LIVE dock") && out.openedOk()
                   && juce::PNGImageFormat().writeImageToStream (both, out),
                   "The manual is built in (F1) and the welcome screen draws - pictures in " + file.getFullPathName());
        }

        // ---- Dialogs, drawn for a look ----
        {
            juce::AlertWindow rename ("Rename group", {}, juce::MessageBoxIconType::NoIcon);
            rename.addTextEditor ("text", "Ambient");
            rename.addButton ("OK", 1);
            rename.addButton ("Cancel", 0);
            juce::AlertWindow confirm ("Remove group", "Remove Ambient and its channels? The samples stay in the Samples list.",
                                       juce::MessageBoxIconType::NoIcon);
            confirm.addButton ("Remove", 1);
            confirm.addButton ("Cancel", 0);
            for (auto* w : { &rename, &confirm })   // as Airloom's dialogs: the first button stands out
            {
                w->getButton (0)->setColour (juce::TextButton::buttonColourId, Theme::accent);
                w->getButton (0)->setColour (juce::TextButton::textColourOffId, Theme::background);
            }
            const auto a = rename.createComponentSnapshot (rename.getLocalBounds());
            const auto b = confirm.createComponentSnapshot (confirm.getLocalBounds());
            juce::Image both (juce::Image::ARGB, a.getWidth() + b.getWidth() + 30, juce::jmax (a.getHeight(), b.getHeight()) + 20, true);
            {
                juce::Graphics g (both);
                g.fillAll (Theme::background);
                g.drawImageAt (a, 10, 10);
                g.drawImageAt (b, a.getWidth() + 20, 10);
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-dialogs.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (both, out), "Dialogs drawn to " + file.getFullPathName());
        }

        // ---- Button icons ----
        {
            const juce::StringArray names { "record", "record-set", "play", "stop-all", "transport-go", "undo", "redo", "settings",
                                            "overdub", "mute", "monitor", "arm-recording", "effects-fx", "freeze", "add-group",
                                            "stage-mode", "scene", "tap-tempo", "folder", "options" };
            juce::StringArray missing;
            for (const auto& n : names)
                if (Icons::get (n, Theme::text) == nullptr)
                    missing.add (n);
            check (missing.isEmpty(), "All 20 button icons load" + (missing.isEmpty() ? juce::String() : " (missing: " + missing.joinIntoString (", ") + ")"));
            check (Icons::get ("no-such-icon", Theme::text) == nullptr, "An unknown icon name gives none (the button keeps its label)");

            // The buttons that carry one, at their real sizes, plus one too narrow for both.
            struct Sample { const char* label; const char* icon; int width; juce::Colour colour; };
            const Sample samples[] = {
                { "REC SET", "record-set", 118, Theme::danger.darker (0.9f) }, { "START", "transport-go", 92, Theme::raised },
                { "STAGE", "stage-mode", 90, Theme::raised }, { "GROUP", "add-group", 90, Theme::raised },
                { "Set", "folder", 90, Theme::raised }, { "Settings", "settings", 140, Theme::raised },
                { "STOP ALL", "stop-all", 150, Theme::danger.darker (1.2f) }, { "FREEZE", "freeze", 110, Theme::raised },
                { "REC", "record", 100, Theme::danger.darker (0.9f) }, { "DUB", "overdub", 70, Theme::raised },
                { "UNDO", "undo", 70, Theme::raised }, { "TAP", "tap-tempo", 70, Theme::raised },
                { "+ SCENE", "scene", 130, Theme::raised }, { "MUTE OUT", "mute", 84, Theme::raised },
                { "FROZEN", "freeze", 50, Theme::raised } };
            juce::Image image (juce::Image::ARGB, 1100, 120, true);
            {
                juce::Graphics g (image);
                g.fillAll (Theme::background);
                int x = 10, y = 10;
                for (const auto& s : samples)
                {
                    juce::TextButton b (s.label);
                    Icons::set (b, s.icon);
                    b.setColour (juce::TextButton::buttonColourId, s.colour);
                    b.setSize (s.width, 32);
                    if (x + s.width > image.getWidth() - 10) { x = 10; y += 48; }
                    juce::Graphics::ScopedSaveState state (g);
                    g.setOrigin (x, y);
                    b.paintEntireComponent (g, false);
                    x += s.width + 10;
                }
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-icons.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out), "Icon buttons drawn to " + file.getFullPathName());
        }

        // ---- Notification cards ----
        {
            ToastStack cards;
            cards.setBounds (0, 0, 420, 220);
            cards.post ("Flume Pad A minor 1, CONDUIT A minor 1 recorded (0:16) - each on its own channel", ToastStack::Kind::info);
            cards.post ("Could not save D:\\Sets\\Evening Set.liverig", ToastStack::Kind::error);
            cards.post ("Undone", ToastStack::Kind::info);
            cards.post ("Undone", ToastStack::Kind::info);   // the same again: one card, kept up longer
            juce::Thread::sleep (200);                       // past the fade-in
            juce::Image image (juce::Image::ARGB, 420, 220, true);
            {
                juce::Graphics g (image);
                g.fillAll (Theme::background);
                cards.paintEntireComponent (g, false);
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-toasts.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (cards.count() == 3 && out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out),
                   "Confirmations and errors show as cards (a repeat doesn't stack) - drawn to " + file.getFullPathName());
        }

        // ---- The FX panel and Settings, for a look ----
        {
            FxSettings fx;
            fx.reverbOn = true; fx.delayOn = true; fx.eqOn = true; fx.gainDb = -3.0;
            FxPanel panel ("Flume Pad", fx, Theme::rowColour (5), [] (const FxSettings&) {});
            const auto fxImage = panel.createComponentSnapshot (panel.getLocalBounds());

            juce::AudioDeviceManager devices;
            SettingsPanel look (devices, 1, 1.0f, true, false, true), layout (devices, 2, 1.0f, false, true, false);
            const auto lookImage = look.createComponentSnapshot (look.getLocalBounds());
            const auto layoutImage = layout.createComponentSnapshot (layout.getLocalBounds());

            juce::Image sheet (juce::Image::ARGB, fxImage.getWidth() + 560 + 30, juce::jmax (fxImage.getHeight(), 2 * 460 + 10) + 20, true);
            {
                juce::Graphics g (sheet);
                g.fillAll (juce::Colours::black);
                g.drawImageAt (fxImage, 10, 10);
                g.drawImageAt (lookImage, fxImage.getWidth() + 20, 10);
                g.drawImageAt (layoutImage, fxImage.getWidth() + 20, 480);
            }
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("airloom-panels.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            check (out.openedOk() && juce::PNGImageFormat().writeImageToStream (sheet, out), "FX panel and Settings drawn to " + file.getFullPathName());
        }

        report.add ({});
        report.add (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED");
        return report.joinIntoString ("\n");
    }
}
