# Live Rig

A standalone JUCE app for live ambient/healing performance, replacing FL Studio on stage.

Rig: any audio interface (Scarlett 2i2 on the laptop, MOTU at home), Akai MidiMix, Akai Fire, Windows.

## Roadmap

| Stage | Scope | Status |
|---|---|---|
| 1 | Window, audio device, one seamless WAV loop, transport + level meter | done |
| 2 | Loop layers (Pad 1, Pad 2, Nature, Tones), per-layer volume, cue + GO crossfades, MidiMix | **done - test on the rig** |
| 3 | Mic channel (reverb + delay) and instrument channel from the interface | next |
| 4 | VST3 instrument hosting, Akai Fire note mode into the instrument | |
| 5 | Loop recording from live inputs, overdub | |

## Build

Requires Visual Studio 2026 (Desktop C++ workload), CMake 3.22+, and JUCE at `C:\JUCE`
(override with `-DJUCE_DIR=...`). Keep the build folder on a short path (MSBuild breaks past 260 chars):

```
cmake -S . -B %TEMP%\LiveRig-build -G "Visual Studio 18 2026" -A x64
cmake --build %TEMP%\LiveRig-build --config Release
```

The app is at `%TEMP%\LiveRig-build\LiveRig_artefacts\Release\Live Rig.exe`.

## Loop library

Choose it once with **Library > Choose library folder**. Missing subfolders are created for you:

```
<Library>/
  Keys/
    C/          1 Warm.wav, 2 Airy.wav   <- first two files by name go to Pad 1 and Pad 2
    D/
    F#m/
  Nature/       Rain.wav, Stream.wav ...
  Tones/        Alpha 10Hz.wav ...
```

Prefix file names with numbers to control which pad they land on. **Library > Rescan** picks up new files.

## Performing

- **Cue, then GO.** Click a key (or PREV/NEXT) to cue it - it turns amber and shows as NEXT. Click a channel's
  file name to cue a different nature loop, tone, pad file, or silence. Press **GO** (or Space) and every cued
  change crossfades together over the **Crossfade** time. Loops are loaded in the background as soon as they're
  cued, so GO is instant.
- **START / STOP** fades all layers in or out over the crossfade time. **Esc** is a panic fade (1.5 s).
- Each layer has its own fader and mute. The current key is shown large on the left; teal while playing.
- Everything (library, key, files, faders, mutes, crossfade time, audio device, MIDI map) is restored on launch,
  loaded but silent until START.

Keyboard: Space = GO, Left/Right = cue previous/next key, Esc = panic fade / cancel MIDI learn.

## MidiMix

Works out of the box with the MidiMix factory layout:

| MidiMix | Live Rig |
|---|---|
| Faders 1-4 | Pad 1, Pad 2, Nature, Tones |
| Master fader | Master |
| Mute buttons 1-4 | Layer mutes (lit = layer on) |
| Bank Left / Bank Right | Cue previous / next key |
| Solo | GO |

Faders use **soft takeover**: a hardware fader only grabs the layer once it passes the on-screen position, so
nothing jumps. Until then an amber tick shows where the hardware fader is.

**Right-click** any fader, mute, GO, START or PREV/NEXT to learn a different MIDI control, clear it, or reset to
the MidiMix defaults. Any MIDI controller works this way.

## Low latency (ASIO)

The build uses WASAPI/DirectSound. For the lowest latency with an interface's ASIO driver (needed for live
playing in Stages 3-4), JUCE needs the Steinberg ASIO SDK: download it, then add `JUCE_ASIO=1` to
`target_compile_definitions` and its `common` folder to `target_include_directories` in `CMakeLists.txt`.
