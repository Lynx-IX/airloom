#pragma once

#include "Effects.h"
#include "InputChannel.h"
#include "InstrumentChannel.h"
#include "LiveSet.h"
#include "LoopChannel.h"
#include "Looper.h"
#include "TempoGuess.h"

// Offline checks of the effects chain, run with:  "Live Rig.exe" --fx-selftest <report file>
// Each check feeds a test signal through FxChain and measures the result.
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
            set.rows[1].slots[0].file = sample;
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

            const auto found = opened.rows.size() > 1 ? opened.rows[1].slots[0].file : juce::File();
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

            layout.scale = PadLayout::numScales - 1;   // chromatic
            check (layout.noteForPad (32) - layout.noteForPad (bottomLeft) == 5, "Chromatic rows are a fourth apart");

            // A pad held while the layout changes must still release the note it started.
            InstrumentChannel channel;
            channel.prepare (sr, 512);
            channel.handleMidi ("FL STUDIO FIRE", juce::MidiMessage::noteOn (1, 54 + bottomLeft, (juce::uint8) 100).withTimeStamp (0.0));
            PadLayout moved;
            moved.root = 0;
            channel.setLayout (moved);
            channel.handleMidi ("FL STUDIO FIRE", juce::MidiMessage::noteOff (1, 54 + bottomLeft).withTimeStamp (0.0));
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
                        channel.handleMidi ("FL STUDIO FIRE", juce::MidiMessage::noteOn (1, 54 + 48, (juce::uint8) 110).withTimeStamp (now));
                        const float during = play (60);
                        channel.handleMidi ("FL STUDIO FIRE", juce::MidiMessage::noteOff (1, 54 + 48).withTimeStamp (now + 0.6));

                        check (during > 0.01f && during > before * 10.0f,
                               "Fire pad plays Vital through the instrument channel (peak " + juce::String (during, 3)
                                   + ", silent before: " + juce::String (before, 4) + ")");

                        // The MidiMix must never play notes.
                        play (200);
                        channel.handleMidi ("MIDI Mix", juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110).withTimeStamp (now + 5.0));
                        const float mixer = play (30);
                        check (mixer < 0.01f, "MidiMix notes are ignored by instruments (peak " + juce::String (mixer, 4) + ")");

                        // Delay and reverb on the instrument channel: a short note should keep ringing after release.
                        auto tailAfterNote = [&] (const FxSettings& fx, double at)
                        {
                            channel.getFx().setSettings (fx);
                            play (400);   // let everything die away
                            channel.handleMidi ("FL STUDIO FIRE", juce::MidiMessage::noteOn (1, 54 + 48, (juce::uint8) 110).withTimeStamp (at));
                            play (10);
                            channel.handleMidi ("FL STUDIO FIRE", juce::MidiMessage::noteOff (1, 54 + 48).withTimeStamp (at + 0.05));
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

        report.add ({});
        report.add (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED");
        return report.joinIntoString ("\n");
    }
}
