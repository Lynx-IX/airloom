# Airloom

*Audio performance rig* (formerly Live Rig).

A standalone JUCE app for live ambient/healing performance, replacing FL Studio on stage.

Rig: any audio interface (Scarlett 2i2 on the laptop, MOTU at home), Akai MidiMix, Akai Fire, Windows.

## Roadmap

| Stage | Scope | Status |
|---|---|---|
| 1 | Window, audio device, one seamless WAV loop, transport + level meter | done |
| 2 | Rows of channels (loops, XF auto crossfade, one-shots), per-channel and per-row volume, MidiMix, sets | **done - test on the rig** |
| 3 | Mic and instrument input channels; low cut, delay and reverb on inputs and on every row | **done - test on the rig** |
| 4 | VST3 instrument hosting; Akai Fire pads as a scale keyboard with lit scale | **done - test on the rig** |
| 5 | Loop recording from live inputs (with or without effects), overdub, undo, length snapping | **done - test on the rig** |

## Build

Requires Visual Studio 2026 (Desktop C++ workload), CMake 3.22+, and JUCE at `C:\JUCE`
(override with `-DJUCE_DIR=...`). Keep the build folder on a short path (MSBuild breaks past 260 chars):

```
cmake -S . -B %TEMP%\LiveRig-build -G "Visual Studio 18 2026" -A x64
cmake --build %TEMP%\LiveRig-build --config Release
```

The app is at `%TEMP%\LiveRig-build\LiveRig_artefacts\Release\Airloom.exe`.

**For another computer:** run `powershell -ExecutionPolicy Bypass -File package.ps1`. It builds and puts a ready copy
in `Release\Airloom\` (the program plus *How to install.txt*) and `Release\Airloom for Windows.zip`. The program has
the C++ runtime built in, so it runs on any Windows 10/11 PC with nothing to install - just copy and double-click.

Self-test (renders test signals through the real effects, looper and Fire pad code, and plays a note on Vital if
it's installed; writes a pass/fail report; can run while the app is open):

```
"Airloom.exe" --fx-selftest "%TEMP%\liverig-fxtest.txt"
```

## How it works

```
 SAMPLES   | PADS       [row vol][mute] | [E maj warm  ] [A min soft  ] [C maj airy ] [+]  | MASTER
 (all your |                            | [PLAY ][XF]    [STOP ][XF]    [PLAY ][XF]        |
  loaded   |                            | [--volume--]   [--volume--]   [--volume--]       |
  samples) | AMBIENT    [row vol][mute] | [Rain] [Stream] [Birds] [+]                        |
           | TONES      [row vol][mute] | [Alpha 10Hz] [Theta 6Hz] [+]                       |
           | ONE-SHOTS  [row vol][mute] | [Bowl 1x] [Chime 1x] [Gong 1x] [+]                 |
```

- **Samples panel (left):** drop audio files or whole folders on it, or use Add files / Add folder. Everything you
  load for the set lives here. Each sample shows which rows use it.
- **Groups** (the Inputs among them) can go anywhere: **drag a group by its name** and drop it - an outline
  shows where it lands, and groups settle up under whatever is above them, so gaps beside a tall group fill in. Drag
  a group's **right edge** to set how many columns its grid has (6 channels as 6x1, 3x2, 2x3...); double-click the
  edge to go back to automatic. The **<** button folds the
  samples list into a thin strip to give the groups more room (click the strip to open it again).
- Each group is a layer, with its own volume fader, meter and mute on the left. A new set starts with Pads,
  Ambient, Tones and One-shots. Click a group's name to rename it, recolour it, remove it, or add a channel.
  **+ ROW** adds one (up to 8).
- **Channels** each hold one sample with its own volume. Drag samples from the panel (or straight from Explorer) onto
  a channel, or onto a row to fill its empty channels. **+** at the end of a row adds a channel. Click a channel's
  name (or right-click it) to load, rename, switch between Loop and Play once, move, clear or remove it.
- **Drag a channel** (by its name or background) to reorder it within its row or move it to another row; a line in
  the row's colour shows where it will land. A playing loop keeps playing when reordered; moved to another row, it
  fades out and back in there.

## Playing

- **PLAY / STOP** fades a loop in or out over the **Fade time**. Loops in the same row play together.
- **XF** (auto crossfade) fades that channel in while every other loop in the row fades out over the fade time.
- For a **manual crossfade**, move the channel faders yourself. Each channel is a mixer strip with its own vertical
  fader and level meter; the row's fader and meter sit at the left of the row.
- **One-shots** (marked `1x`) play once on top of everything; each has its own volume, plus the row's fader.
- **Channel FX:** every channel (loops, live loops, one-shots) has its own **FX** button with the full effects chain,
  applied before the row's FX. Up to 6 channels per row can have their own effects at once; tails ring out after a
  channel stops. The button lights in the row's colour when something's on.
- **Reverse:** a channel's menu > **Play reversed** flips the sample (it crossfades over if playing). The channel
  shows `REV`. Works with smooth loop points and overdubs; recorded files stay forwards on disk.
- **Samples that aren't cut as loops:** click the channel name > **Smooth loop point** and pick a blend time
  (0.5-8 s). The end of the sample is blended into its start so it repeats without a jump; the channel shows `~`.
  Longer blends are smoother but shorten the loop by that much (at most a third of the sample).
- Each row has its own **colour** (change it from the row name menu). A playing channel lights up in its row's
  colour; amber means fading in or loading.
- The window resizes freely. **Ctrl +** / **Ctrl -** zoom the whole interface, **Ctrl 0** resets (also in the Set
  menu); the zoom is remembered.
- **STOP ALL** fades everything out over the fade time. **Esc** is a quick 1.5 s panic fade.
- Samples are loaded into memory in the background as soon as they're on a button, so presses respond instantly.

## Live inputs and effects

- The **INPUTS** section at the top has two input channels, **Mic** (interface input 1) and **Instrument** (input 2).
  Each has its own fader, a meter showing the incoming level, mute, **MONITOR** and **FX**.
- **MONITOR** is off by default so a mic can't feed back when the app opens. The meter still shows the input level
  with monitoring off, so you can set your interface gain first, then switch MONITOR on.
- Click an input's name to rename it or choose its source: input 1, input 2, or inputs 1 + 2 as one stereo channel.
- **FX** opens the effects chain, in signal order:
  - **Channel** - gain (-24 to +24 dB) and pan
  - **Gate** - silences hiss and room noise below a threshold, between phrases
  - **Low cut** - removes rumble and handling noise
  - **EQ** - low shelf, mid (with frequency) and high shelf, +/-15 dB
  - **Compressor** - threshold, ratio, release, makeup; evens out breath and dynamics
  - **Delay** - **Sync to BPM** with note lengths (1/16 to 1 bar, dotted and triplet) or free time in ms; feedback,
    **tone** (repeats get darker as they fade), mix, and **ping-pong**
  - **Reverb** - a lush ambient reverb: **decay** up to 20 s, size, tone (bright/warm/dark), pre-delay,
    **movement** (slow modulation for a shimmering tail) and mix
  Each has its own on/off switch; double-click any knob to reset it. The Mic starts with low cut and reverb on.
- **Plug-in effect on an input** (auto-tune, vocoder, anything VST3): click the input's name > *Plug-in effect* and
  pick one - e.g. **Graillon 3** for pitch correction, from gentle to hard robotic auto-tune. It runs first, before
  the built-in chain. *Open ...* in the same menu shows its window (set its key there to match the flute: A minor,
  Bb or E). It's saved with the set, included in recordings made with effects and left out of dry ones. The input
  shows the plug-in's name under its own.
- **Every row has an FX button too**, with the same chain applied to everything playing in that row (before the
  row fader). The FX button lights in the row's colour when any effect is on. Reverb and delay tails ring out after a
  channel stops.
- **Beat lights:** four lights next to BPM (and in the looper) pulse in time, one per beat of the bar, beat 1
  outlined brighter. TAP sets where beat 1 falls. While recording, the looper's lights turn red and count from the
  moment you pressed REC, and the status shows "bar N - beat N" - handy for stopping on a bar line with Snap to bars.
- **Set BPM from a loop:** click a loop channel's name > *Set BPM from this loop*. It offers the tempo written in the
  file name (e.g. "Drum Loop 72 BPM") and the tempos that fit the loop's length as a whole number of bars
  ("2 bars = 88.42 BPM", "4 bars = 176.84 BPM"...) - pick the one that sounds right. If the loop is playing, beat 1
  lines up with its start. BPM is kept to hundredths, so recorded loops snapped to bars stay locked to it.
- **BPM and TAP** sit in the bottom bar. Drag or type the tempo, or tap it in (TAP is MIDI-learnable). Synced delays
  follow it live. Sample loops don't need it - they repeat at their own length - but Stage 5's loop recording will be
  able to snap to bars.
- Right-click an input fader or mute to learn a MIDI control for it.
- For live playing, latency matters: use the smallest buffer size that plays without crackles in Audio Settings, and
  ideally the interface's ASIO driver (see Low latency below).

## Instruments (VST3)

Hosted instruments sit in the INPUTS section next to Mic and Instrument (up to 4).

- **+ INSTRUMENT** lists your VST3 instruments by maker. The first time, it opens the plug-in scanner and scans the
  standard VST3 folder (`C:\Program Files\Common Files\VST3`); a plug-in that crashes while scanning is remembered
  and skipped next time. Rescan any time from an instrument's menu > *Scan for plug-ins*.
- Each instrument strip has **EDIT** (the plug-in's own window), **ARM** (record it into the looper, with or
  without its effects), **FX** (the same effects chain as everything else), a fader, meter and mute.
- **Playing it:** every keyboard plays it (e.g. the MPK mini), plus the **Akai Fire's pads**, which become a scale
  keyboard: left to right walks the scale, each row up starts three scale steps higher. **One-click keys** for the
  flutes sit on each instrument strip: **A min** (A minor pentatonic), **Bb maj** and **E maj** (major pentatonic) -
  the active one lights up (to change the three, edit `fluteKeys` in `MainComponent.cpp`). Or set any **key**, **scale**
  (minor pentatonic - the default, suits Native American flute keys - major pentatonic, natural minor, major,
  Dorian, Hijaz, chromatic) and **octave** from the instrument's menu > *Fire pads*. The pads light up: root notes
  in teal, the other scale notes dim. The Fire's buttons stay free for MIDI-learn (e.g. REC). The MidiMix never
  plays notes.
- *Play from* (in its menu) limits an instrument to one controller, e.g. the Fire for a pad and the MPK for a lead.
- The plug-in and all its settings are saved with the set and reload when you open it. Instruments also receive the
  set's BPM, so their own synced delays and arpeggiators follow TAP.

## Recording loops

The looper controls (REC, DUB, UNDO, loop length and status) sit in the middle of the bottom bar.

1. **ARM** the input(s) you want to record (the MidiMix's first two REC ARM buttons do the same and light up).
2. Press **REC** and play. The status shows the time.
3. Press **STOP** (the same button). The loop starts playing immediately, in time, as a new channel with its own
   fader, XF, effects and so on. Loops are grouped and named by the key the Fire is set to: the first loop in Bb
   major is **Loop Bb major 1** in a **Loops Bb major** group (created automatically, at the bottom); with no
   instrument key they go into **Live Loops**. If all 8 groups are taken, new keys share an existing loops group.
4. **DUB** records on top of the latest loop (or any loop: its menu > *Overdub on this loop*); press again to stop.
   The new layer lines up with what you heard - the interface's latency is compensated. **UNDO** removes the last
   overdub.

- **With or without effects:** loops are recorded with the input's effects by default (exactly what you hear).
  Click an input's name > *Record loops dry* to record it clean, so the loop's effects can be changed afterwards.
  The input shows "records dry" when set.
- **Length** (the menu next to UNDO): **Free length** (exactly as long as you played), **Snap to bars** (rounds to
  whole bars at the BPM - stop a little late and it trims, stop early and it records on to the bar line), or
  **Match first loop** (every new loop is a whole multiple of the first, so they never drift apart).
- The loop seam is blended using a moment of audio recorded just past the end, so loops repeat without a click
  and keep their exact length.
- Every take is saved as a WAV straight away (in `%APPDATA%\LiveRig\Recordings`), so it survives a restart; saving
  the set gathers takes into the set folder like any other sample. Overdubs are written after each pass.
- REC, DUB, UNDO and the ARM buttons are all MIDI-learnable (right-click), e.g. for a foot switch.

## Set timer

The **SET** clock in the header shows how long the set has been going: **START** begins it, **STOP** pauses it
(START carries on), right-click resets it. Teal while running, amber while paused.

## Recording the set

**REC SET** (next to the set clock) records everything going to the speakers - after the master fader and limiter -
to a WAV file, until pressed again. The button shows how long it has been recording. Files go to
`Recordings/Sets/Set <date> <time>.wav` (right-click REC SET > **Show set recordings**). They're 24-bit stereo,
about 1 GB an hour at 48 kHz.

## Freeze

**FREEZE** (bottom bar, next to STOP ALL) holds what you just played - the live inputs and instruments, with their
effects, not the loops - as an endless pad to play over. It keeps listening for a quarter of a second after the press,
to catch the note you're on. Press again (**FROZEN**) to let it go: it fades out over the **fade time**. Right-click
to assign a MIDI button.

## Sets

Everything - samples, rows, channels, volumes, mutes, effects, input settings, fade time - is a **set**. The working session is
saved automatically, so the app reopens exactly where you left off.

- **Set > Save set as new folder...** creates a folder and gathers every sample into it - one subfolder per row,
  plus `Samples/` for anything not on a channel. You choose to **copy** (originals stay put) or **move** the files.
  Paths inside are relative, so the whole folder can be copied to another computer.
- **Set > Save set** updates it; **Set > Open set...** opens a `.liverig`.
- **Moving to another computer:** **Set > Export set as one file...** packs the set and every sample it uses (live-loop
  recordings included) into a single `.liverigpack` - copy it on a USB stick or cloud drive. On the other computer,
  **Set > Open set...** and pick the pack: it unpacks into `Music\Airloom Sets\<name>` and opens, ready to play.
  Plug-ins (Vital, Serum, Graillon...) can't travel in the pack - they need installing on each computer, often with a
  licence - so the pack includes **Plug-ins needed.txt** listing what to install; their settings are in the set and
  come back once they're installed.

## MIDI (MidiMix or any controller)

Nothing on a set is assigned automatically: **right-click a control and choose Learn MIDI control**, then move or
press the hardware control. Learnable controls:

- each channel's **PLAY/STOP**, **XF** and **volume** (a MIDI tag appears on the channel once it has any)
- each group's **fader** and **mute**
- master fader, inputs and instruments (faders, mutes, ARM), REC / DUB / UNDO, STOP ALL, TAP, FREEZE, REC SET

Channel and group assignments are **saved with the set** (and travel in an exported pack), and follow the channel or
group wherever it's moved. The rest belong to the computer. Out of the box only the MidiMix **master fader**, its
first two **REC ARM** buttons (arm inputs 1-2) and the **Fire's REC button** (records a loop, like REC) are assigned.
MidiMix buttons light up: a group's mute while the group is on, a channel's play button while it plays, REC ARM while
armed. The Fire's REC button glows dim red when ready, bright red while recording and amber while overdubbing.

Faders and knobs use **soft takeover**: a hardware control only grabs the on-screen one once it passes its position,
so nothing jumps. On group and master faders an amber tick shows where the hardware is until then.

Right-click any learnable control to clear its assignment, or to clear all MIDI assignments.

## Low latency (ASIO)

The build uses WASAPI/DirectSound. For the lowest latency with an interface's ASIO driver (needed for live
playing in Stages 3-4), JUCE needs the Steinberg ASIO SDK: download it, then add `JUCE_ASIO=1` to
`target_compile_definitions` and its `common` folder to `target_include_directories` in `CMakeLists.txt`.
