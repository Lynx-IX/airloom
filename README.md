# Airloom

An instrument for live, looping, ambient performance on Windows: play loops and samples in
groups, record new loops from a mic or plug-in instruments in time, shape everything with effects,
and move between scenes - from the screen, a MIDI controller (an Akai MidiMix or Fire, or any
other) or both. Free and open source.

## Download

From the [releases page](../../releases):

- **Airloom-Setup-x.y.z.exe** - the installer. It installs just for you (no administrator
  prompt), adds Airloom to the Start menu, can put a shortcut on the desktop, and makes sets
  (`.liverig`, `.liverigpack`) open in Airloom with a double-click. Uninstall it from Windows
  Settings > Apps; your sets, recordings and settings stay.
- **Airloom for Windows.zip** - the portable copy: unzip it anywhere and run `Airloom.exe`.

Windows 10 or 11, 64-bit. Until the builds are signed, Windows may say "Windows protected your
PC": click *More info* > *Run anyway*.

## Build

Requires Visual Studio 2022 or 2026 (Desktop C++ workload), CMake 3.22+, and JUCE 8 at `C:\JUCE`
(override with `-DJUCE_DIR=...`). Keep the build folder on a short path (MSBuild breaks past 260 chars):

```
cmake -S . -B %TEMP%\LiveRig-build -A x64
cmake --build %TEMP%\LiveRig-build --config Release
```

The app is at `%TEMP%\LiveRig-build\LiveRig_artefacts\Release\Airloom.exe`. It has the C++ runtime built in,
so it runs on any Windows 10/11 PC with nothing else to install.

Self-test (renders test signals through the real effects, looper and Fire pad code, draws the
panels to pictures in `%TEMP%`, and plays a note on Vital if it's installed; writes a pass/fail report;
can run while the app is open):

```
"Airloom.exe" --fx-selftest "%TEMP%\liverig-fxtest.txt"
```

## Releasing

`powershell -ExecutionPolicy Bypass -File package.ps1` builds, runs the self-test, and makes
`Release\Airloom-Setup-<version>.exe` (with [Inno Setup 6](https://jrsoftware.org/isinfo.php):
`winget install JRSoftware.InnoSetup`) and `Release\Airloom for Windows.zip`. The version comes
from `project(... VERSION x.y.z)` in `CMakeLists.txt`; the installer is `Packaging\Airloom.iss`.

On GitHub, `.github/workflows/build.yml` does the same on every push (the files are kept with the
run) and, for a tag like `v1.0.0`, makes a draft release with both files attached: bump the version,
commit, `git tag v1.0.0`, `git push origin v1.0.0`, then check the draft and publish it.

**Signing**, so Windows stops warning about an unknown publisher: open-source projects can get a
free certificate through [SignPath Foundation](https://signpath.org) once the repository is public.
After they accept it, add their signing step to the workflow (where it's marked). With a certificate
of your own, `package.ps1 -Sign "signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a"`
signs the program and the installer.

## Licence

Airloom is free software under the [GNU Affero General Public License v3](LICENSE). It's built
on [JUCE](https://juce.com), used under the same licence, and hosts VST3 plug-ins through the
VST3 SDK (MIT). VST is a registered trademark of Steinberg Media Technologies GmbH.

## How it works

```
 SAMPLES   | PADS       [row vol][mute] | [E maj warm  ] [A min soft  ] [C maj airy ] [+]  | MASTER
 (all your |                            | [PLAY ][XF]    [STOP ][XF]    [PLAY ][XF]        |
  loaded   |                            | [--volume--]   [--volume--]   [--volume--]       |
  samples) | AMBIENT    [grp vol][mute] | [Rain] [Stream] [Birds] [+]                        |
           | TONES      [grp vol][mute] | [Alpha 10Hz] [Theta 6Hz] [+]                       |
           | ONE-SHOTS  [grp vol][mute] | [Bowl 1x] [Chime 1x] [Gong 1x] [+]                 |
```

- **Samples panel (left):** drop audio files or whole folders on it, or use Add files / Add folder. Everything you
  load for the set lives here. Each sample shows which groups use it. Samples are shown in **folders** - at first the
  folder each is in on disk (a saved set has one per group). Click a folder to open or close it. Right-click samples >
  **Move to folder** (or drag them onto a folder) to sort them into folders of your own; right-click a folder to
  rename or remove it (its samples go back to their own folders) or make a new one. Files are never moved: the folders
  are saved with the set. A folder with a playing sample in it shows a dot, even closed.
- **Groups** (loop, input and instrument groups) can go anywhere: **drag a group by its name** and drop it - an
  outline shows where it lands, and it stays exactly there. Near another group's edge it snaps to it: beside it or
  below it, tops or sides lined up. Groups never overlap: one dropped onto another (or that grows into the next)
  pushes just the group in the way down. **Set > Rearrange groups to fit the window** (on by default) slides groups
  in and wraps channels when the window gets narrower; turn it off to keep the layout exactly as it is at the
  current size - a smaller window then scrolls sideways instead of moving anything (saved with the set). Collapsing a group pulls the groups under it up to close the space;
  expanding it pushes them back where they were. Drag
  a group's **right edge** to set how many columns its grid has (6 channels as 6x1, 3x2, 2x3...); double-click the
  edge to go back to automatic. The **<** button folds the
  samples list into a thin strip to give the groups more room (click the strip to open it again).
- Each group is a layer, with its own volume fader, meter and mute on the left. A new set starts with a
  **Samples** group and a **Live Loops** group (where loops recorded with no key land). Click a group's name to rename it, recolour it, remove it, or add a channel.
  **GROUP** (in the header) adds one (up to 8).
- **Collapse a group** with the arrow by its name (or *Collapse* in its name menu; *Collapse all groups* /
  *Expand all groups* do every group). A collapsed group is a short bar: its name, MUTE and FX, and a tile per
  channel that lights while it plays - click a tile to play or stop it, right-click for its menu. The group fader
  and MidiMix keep working. The set remembers which groups are collapsed. Input and instrument groups collapse the
  same way: each strip becomes a tile, lit while it's live (unmuted, and for an input, monitored) - click a tile to
  mute or unmute that strip.
- **Channels** each hold one sample with its own volume. Drag samples from the panel (or straight from Explorer) onto
  a channel, or onto a group to fill its empty channels. **+** at the end of a group adds a channel. Click a channel's
  name (or right-click it) to load, rename, switch between Loop and Play once, move, clear or remove it.
- **Drag a channel** (by its name or background) to reorder it within its group or move it to another group; a line in
  the group's colour shows where it will land. A playing loop keeps playing when reordered; moved to another group, it
  fades out and back in there.

## Playing

- **PLAY / STOP** fades a loop in or out over the **Fade time**. Loops in the same group play together.
- **XF** (auto crossfade) fades that channel in while every other loop in the group fades out over the fade time.
- For a **manual crossfade**, move the channel faders yourself. Each channel is a mixer strip with its own vertical
  fader and level meter; the group's fader and meter sit at the left of the group. A channel with its own **FX**
  is metered after them (so a reverb tail shows); the group meter shows the group after its FX and fader.
- **One-shots** (marked `1x`) play once on top of everything; each has its own volume, plus the group's fader.
- **Channel FX:** every channel (loops, live loops, one-shots) has its own **FX** button with the full effects chain,
  applied before the group's FX. Up to 6 channels per group can have their own effects at once; tails ring out after a
  channel stops. The button lights in the group's colour when something's on. As on a mixer, the channel's fader comes
  **after** its effects, so pulling it down turns the reverb and delay tails down too.
- **Safety net:** if an effect ever produces something out of control (an invalid or hugely over-full-scale signal),
  Airloom empties that effect at once instead of letting it ring, shows a message and notes it in the crash log.
- **Edit sample:** a channel's menu > **Edit sample...** shows its waveform. Drag the bars at either edge to **trim** the
  start and end (cuts land where the sound crosses zero, so they don't click; for loops, **Snap to beats** keeps the
  length a whole number of beats at the set's BPM), and the dots at the top to **fade** in and out. **Gain** and
  **Normalise** (loudest moment to just under full scale) set its level before the fader; **Reset** goes back to the
  whole sample. **PLAY** plays the channel itself, and a playing loop crossfades to each change in time. Nothing is
  written into the file - it's saved with the channel - and it works with reverse (the start is where it starts
  playing), smooth loop points and channel FX. Overdubbing a trimmed loop records the trimmed version.
- **Reverse:** a channel's menu > **Play reversed** flips the sample (it crossfades over if playing). The channel
  shows `REV`. Works with smooth loop points and overdubs; recorded files stay forwards on disk.
- **Samples that aren't cut as loops:** click the channel name > **Smooth loop point** and pick a blend time
  (0.5-8 s). The end of the sample is blended into its start so it repeats without a jump; the channel shows `~`.
  Longer blends are smoother but shorten the loop by that much (at most a third of the sample).
- Each group has its own **colour**, one of 16 (change it from the group name menu); a new group gets whichever
  colour is used least.
- **Muted** strips and groups read **MUTED** on a grey key and fade back (a muted group's channels fade with it), so
  what's live stands out. Red is kept for recording, STOP ALL and MUTE OUT. Buttons stay quiet until they're on. A playing channel lights up in its group's
  colour; amber means fading in or loading.
- The window resizes freely. **Ctrl +** / **Ctrl -** zoom the whole interface, **Ctrl 0** resets (also in the Set
  menu); the zoom is remembered.
- **Set > Theme** changes the colours straight away: **Graphite** (the original), **Midnight** (blue), **Ember** (warm),
  **Forest** (green), **Night** (dimmer everything, for a dark room) and **High contrast** (brighter text and edges, for
  bright stage light or daylight). Each theme has its own take on the 16 group colours - cooler in Midnight, warmer in
  Ember, softer in Forest, dimmer in Night, stronger in High contrast - but each keeps its hue, so a group is still
  recognisably its colour. The theme is remembered on this computer.
- **STOP ALL** fades everything out over the fade time. **Esc** is a quick 1.5 s panic fade.
- Samples are loaded into memory in the background as soon as they're on a button, so presses respond instantly.

## Live inputs and effects

- **The LIVE dock:** interface inputs and hosted instruments sit in their own groups in a dock along the bottom, under
  the loop groups and the samples list (the main area is for loops). The **v** button (or a click on its bar) folds it:
  every group becomes a short bar with a named tile for each input and instrument (click one to mute or unmute it),
  so it takes little room but still shows what's there; **^** opens it again - remembered on this computer. Drag a group by its name to move it along the dock; wider than the window, the dock scrolls sideways.
- **Live groups:** interface inputs and hosted instruments sit in their own groups.
  A new set has an **Inputs** group and an **Instruments** group; **GROUP** > *Input group* or *Instrument group*
  adds more (up to 8 live groups), e.g. "Synths" and "Drones". Like a loop group, each has a **fader**, meter,
  **MUTE** and **FX** on its left that act on everything in it (Space sends included), and **+** to add an input or
  instrument to it. Click its name to rename it, recolour it or remove it (its strips move to another group of the
  same kind); drag its name to move it, and its right edge to set how many strips sit on a line. Move a strip to
  another group from its name menu > *Move to group*, or **drag the strip** (by its name or background) to reorder it
  in its group or drop it into another - a line in the group's colour shows where it will land. Live group faders and mutes can be MIDI-learned and are saved
  in scenes. Sets from before live groups open with their inputs in an Inputs group where the old panel was and
  their instruments in an Instruments group.
- **Muted instruments rest:** once a muted instrument (or one in a muted group) has faded out, its plug-in stops
  running until it's unmuted, so a muted group of synths costs no CPU. An armed instrument keeps running (recording
  takes it before the mute). Waking up it gets "all notes off", so nothing played before the mute hangs on.
- The **Inputs** group starts with two input channels, **Mic** (interface input 1) and **Instrument** (input 2).
  Each has its own fader, a meter, mute, **MONITOR** and **FX**. Input and instrument meters show **what you hear**:
  after the effects, the fader and mute.
- **MONITOR** is off by default so a mic can't feed back when the app opens. With monitoring off the meter shows the
  input itself instead, so you can set your interface gain first, then switch MONITOR on.
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
- **Every group has an FX button too**, with the same chain applied to everything playing in that group (before the
  group fader). The FX button lights in the group's colour when any effect is on. Reverb and delay tails ring out after a
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
- For live playing, latency matters: use the smallest buffer size that plays without crackles in Settings > Audio, and
  ideally the interface's ASIO driver (see Low latency below).

## Instruments (VST3)

Hosted instruments sit in instrument groups (up to 8 instruments in a set).

- **+** on an instrument group lists your VST3 instruments by maker. The first time, it opens the plug-in scanner and scans the
  standard VST3 folder (`C:\Program Files\Common Files\VST3`); a plug-in that crashes while scanning is remembered
  and skipped next time. Rescan any time from an instrument's menu > *Scan for plug-ins*.
- Each instrument strip has **EDIT** (the plug-in's own window), **ARM** (record it into the looper, with or
  without its effects), **FX** (the same effects chain as everything else), a fader, meter and mute.
- **Playing it:** every keyboard plays it (e.g. the MPK mini), plus the **Akai Fire's pads**, which become a scale
  keyboard: left to right walks the scale, each row up starts three scale steps higher.
  - **Any key** is where a new instrument starts: the pads play every note (chromatic from C, rows a fourth apart,
    the Cs lit). Keyboards always play every note; keys only shape the Fire's pads.
  - To play in a key, pick it from the instrument's menu > *Fire pads* > **Key** and **Scale** (minor or major
    pentatonic, natural minor, major, Dorian, Hijaz, chromatic), and the **Octave**. The pads light up: root notes in
    teal, the other scale notes dim. *Any key* at the top of the Key menu goes back to every note.
  - **Key buttons:** *Fire pads* > **Add key button** puts the instrument's key on a row of one-click buttons shown on
    every instrument strip (up to 5); the active one lights up. Right-click a button to remove it. The buttons belong to
    the set: a new set has none (and no row), and sets from before keep the flute keys **A min**, **Bb maj**, **E maj**.
  - Scenes remember each instrument's key, scale, octave - and Any key. **GRID <** / **GRID >** on the Fire move the instruments it plays down / up an
  octave (octaves 1-6; with SHIFT held they pick the light-show pattern instead). The Fire's **VOLUME knob** (top left) turns
  the faders of the instruments it plays up or down together, all by the same dB, so the balance you set between
  them on screen stays the same (half a dB a click, faster below -30 dB). Turned all the way down, the quieter ones
  wait at off and come back in at their own levels on the way up. Moving a fader yourself sets a new balance. The Fire's other buttons stay free
  for MIDI-learn (e.g. REC), and a GRID button given a MIDI-learn job does that job instead. The MidiMix never
  plays notes.
- **Preset names:** an instrument's strip is titled with the **preset loaded in it** (e.g. "Flume Pad"), with the
  plug-in's name moving to the line under it. Airloom reads it from the settings the plug-in saves - it works for
  Vital and Zyklop (and plug-ins that save a preset name the same way); Serum 2 and Analog Lab don't say, so they show
  the plug-in's name. It updates when the plug-in's window closes, and every 2 s while it's open.
- **Rename...** (in its menu) gives an instrument your own name instead - shown on its strip, in the stage view and
  everywhere else, saved with the set. *Name it after its preset* (or *Use the plug-in's name*) goes back, and
  changing the instrument clears it.
- *Play from* (in its menu) limits an instrument to one controller, e.g. the Fire for a pad and the MPK for a lead.
  With **two Fires** (Windows lists the second as "2 - FL STUDIO FIRE") set each instrument to its own Fire to split
  them: each Fire then plays only its instrument and lights its pads in that instrument's key. An instrument left on
  *All keyboards and the Fire* is played by both. Airloom knows each Fire by the **USB port** it's plugged into, so
  unplugging and replugging is fine even though Windows renames it ("3 - FL STUDIO FIRE"). Plugged into a different
  port, a Fire is taken to be the one no other instrument is using (the instrument's line shows "unplugged" for its
  old one); pick it again in *Play from* to make that its port.
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
   **Several armed sources** (e.g. two instruments, or an instrument and the mic) record at once, **each onto its own
   channel**, all starting together in time - named after the source and its key: **Flume Pad A minor 1**,
   **CONDUIT Bb major 1** (an instrument by its preset or your name for it and its own key; an input by its name and
   the key being played). A source that stayed silent gets no channel. A single armed instrument is named the same
   way; a single input keeps **Loop Bb major 1**. With several sources the longest take is shared between them (5
   minutes for one, 2.5 each for two, never less than a minute each).
4. **DUB** records on top of the latest loop (or any loop: its menu > *Overdub on this loop*); press again to stop.
   Every loop channel also has its own small **DUB** under PLAY: it overdubs onto that loop (starting it first, and
   waiting for it to come in, if it's stopped) and lights amber while overdubbing. A second press always undoes the
   first: it stops the overdub (UNDO removes what was added), or cancels the wait and stops the loop it started. The
   main DUB button only moves to that loop once an overdub actually starts. Right-click it to map it to a MIDI button.
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

## Starting loops in time

**Start: now / beat / bar** (next to SYNC) sets when a loop you press comes in. On *beat* or *bar* it waits for the
next beat or bar at the BPM, counted from the loops already playing, so it lands in time with them - and in a
crossfade the old loop starts fading at that same moment. A press just after the beat (within 50 ms) still counts as
on it. With nothing playing, a loop starts straight away and the beat grid starts with it. TAP and SYNC also reset
where the grid's beats fall. The setting is saved with the set. It only changes when loops start - instruments still
play the moment you play them.

## Inputs

The Inputs group starts with two (Mic on interface input 1, Instrument on input 2). **+** on an input group adds another, up to
eight - it takes the first interface input no other strip uses, or input 1 again when they're all taken (the same
mic through a second set of effects). Click an input's name to pick its source: any single
interface input (**Mono input**), or two side by side as a **Stereo pair** (e.g. a synth or a stereo mic on 3+4).
**Remove input** is in the same menu; MIDI assigned to later inputs moves down with them. Airloom opens every input
on the interface (up to 16), so there's nothing to switch on in Settings > Audio. Through Windows Audio, an interface
like the MOTU M Series or a Scarlett 2i2 shows Airloom just its first two inputs.

## Scenes

A **scene** is a saved mix: which loops are playing; every channel's level; every group's, input's and instrument's
fader and mute; and each instrument's key (scale and octave) - the key changes the moment the scene is pressed, and
the Fire's pads relight. Mutes fade over the morph rather than cutting, so muting one instrument and unmuting another
in the next scene crossfades from one voice to the other. Scenes also keep **effects** - every channel's, group's,
input's and instrument's - and morph to them: knobs glide over the morph time, and an effect switched on or off comes
or goes with its mix fading, so reverbs and delays arrive and leave smoothly. Scenes made before effects were kept
leave effects alone until they're saved again (with *keep changes* on, leaving the scene does it). The **SCENES**
strip above the groups holds them. Set up a mix and press **+ SCENE** to save it; press a scene to move there.

- Recalling a scene **morphs**: loops it doesn't use fade out, loops it does fade in (on the beat or bar, if **Start**
  says so), and faders glide to their places over the scene's **morph time** (default: the fade time when it was
  saved). A bar along the scene's button shows the morph's progress; groups the scene mutes go quiet at the end.
- A scene **keeps what you change while you're in it**: start a loop or move a fader, go to another scene, and coming
  back finds it as you left it. Pressing the lit scene again puts it back as it was when you came in. To keep scenes
  fixed as saved instead, right-click **+ SCENE** and untick *Scenes keep changes made in them*.
- Right-click a scene to **update** it to the current mix, rename it, set its morph time (instant to 60 s), move it,
  delete it, or assign a MIDI button. Right-click **+ SCENE** for MIDI buttons that step to the **next** and
  **previous** scene - good on a foot switch.
- Scenes don't change effects, monitoring, ARM or one-shots, and channels added after a scene was saved are left
  as they are. Scenes and their MIDI buttons are saved with the set.

## Stage view

**STAGE** (header) or **F11** swaps the mixer for a view made for performing - the same set, drawn big:

- **Scenes** across the top as large buttons (the morph bar fills along the active one), with + SCENE.
- **Every group's loops as big tiles**: press to play or stop, exactly like their PLAY buttons (Start on beat or bar,
  fades and crossfades all apply). Lit in the group's colour while playing, amber while fading in or **CUED**,
  blinking while it waits to stop. One-shots are tiles too (marked 1x). Each group has its **MUTE** beside its name.
- **LIVE** down the right: every input and instrument with a big mute toggle, and **MUTE OUT** at the bottom.

The bottom bar (looper, tempo, SYNC, STOP ALL) and the count-in stay as they are. Press **MIXER** (or F11) to go back;
Airloom remembers which view you were in. Right-click STAGE to put it on a MIDI button.

## Fire light show

A Fire's pads can show a light show driven by what's going to the speakers. On the Fire, **hold SHIFT** and press:

| Button | Does |
| --- | --- |
| **ALT** | Light show on / off (back to the key lights). Remembers the last pattern and colours. |
| **GRID >** / **GRID <** | Next / previous pattern (switches it on) |
| **PATTERN up** / **down** | Next / previous colour palette |
| **BROWSER** | Join the two Fires into one display: steps this Fire through *on its own -> left -> right -> top -> below* |

Patterns: **Spectrum** (bars, lows on the left), **Mirror** (lows in the middle, bars growing up and down),
**Glow** (slow colour waves), **Plasma** (flowing colour field), **Waves** (two lines snaking across - lows and
highs), **Bursts** (rings bursting out on each beat, or each new sound when nothing's in time), **Kaleidoscope**
(spokes turning round the centre), **Diamonds** (diamond rings flowing out, a bright one on the beat), **Stars**
(twinkles where the music is loud). Palettes: Ambient, Aurora, Ember, Ocean, Rainbow, Moon.

Everything moves faster and brighter as the music fills out, and adjusts to the level so quiet passages still show.
Each Fire has its own setting; the pads keep playing their instrument. The status line shows the pattern and palette.

**Two Fires as one display:** press SHIFT + BROWSER on a Fire to say where it sits - left, right, top or below - and
the other Fire takes the opposite place. They then show one picture across both: 32 x 4 side by side, or 16 x 8
stacked (bars twice as tall). Waves, bursts and rings travel from one into the other, and a pattern or palette change
on either changes both. Keep pressing to go back to *on its own*. The positions are remembered for each Fire's USB port.

## Time signature

Click the **beat lights** (bottom bar; the signature shows beside them) to choose 2/4, 3/4, 4/4, 5/4, 6/8 or 7/8.
Everything that counts bars follows it: the beat lights, Start on the bar, count-ins, Snap to bars, the first loop
setting the tempo, synced "1 bar" delays and the Fire's PLAY light. In 6/8 and 7/8 the BPM counts eighth notes
(plug-ins are given the matching quarter-note tempo). It's saved with the set.

## Recording in time

With **Start** on *beat* or *bar*, REC doesn't start straight away: it counts you in to the next bar line at least a
beat away (with nothing playing, one whole bar from the press). The beat lights go red and count the bar, the looper
says how many beats are left, and the Fire's REC button blinks - start playing on the next 1. The count also shows
**big in the middle of the window** (1, 2, 3, 4 - the last one red, "play on the next 1"), then **REC** for the first
beat of the recording. Press REC again during
the count-in to call it off. Recordings then start exactly on the bar, allowing for the interface's delay, so they
sit on the same grid as the loops already playing. With **Start: now**, REC starts the moment you press it, as before.

The **first loop** you record in *Free length* with nothing else playing sets the tempo: the BPM becomes the one that
fits a whole number of bars into it (the nearest to the BPM you had), and the beat grid starts with it. Everything
after - Start on the bar, Snap to bars, synced delays, SYNC - then follows what you actually played.

The Fire's **PLAY** button is a beat light while loops are playing or you're recording: green on beat 1, yellow on
the others.

## Effects

Every input, instrument, group and channel has an **FX** panel. Tick a section (or turn one of its knobs) to switch it
on; double-click a knob to reset it. In order through the chain:

- **Channel** - gain, pan, and **Space**: how much goes to the shared Space reverb (see below).
- **Gate** - silences what's under the threshold (hiss, room noise).
- **Swell** - fades each note in, like a volume pedal, so the breath at the start of a flute note blooms in like a bowed
  string. A gap below the threshold resets it for the next note.
- **Low cut**, **EQ**, **Compressor**.
- **Tape** - soft saturation (Drive), a slow pitch wobble (Wow) and rolled-off highs (Tone): a worn, older sound.
- **Filter** - a resonant low-pass (or high-pass) for building up and pulling back; with a **Sweep rate** it moves by
  itself, **Sweep depth** octaves either side. Best on a MidiMix knob (below).
- **Octave** - adds a copy an octave down or up, a fifth up or down, or two octaves down, for a bigger sound from one flute.
- **Chorus** - gentle detuned doubling that widens and thickens.
- **Motion** - stereo **Width** (0 = mono, 200% = extra wide) and a slow **auto-pan** drifting the sound around.
- **Delay** - synced to the BPM or free, ping-pong.
- **Reverb** - with **Shimmer**: the tail rises an octave as it fades, the classic glowing ambient halo. **Dry** turns
  the original sound down under it: at 0 only the reverb is left (Mix then sets how loud the reverb is), for a washed-out
  sound or a group that's all space.

**Space** (button under the master fader) is one big reverb that everything can share: turn up a channel's **Space**
knob to send some of it there, after its fader. Sharing one room makes the set sound like one place, and is lighter on
the computer than a reverb on every channel. The Space panel sets its decay, size, tone, pre-delay, movement, shimmer and
return level; it's saved with the set.

**Plug-in effects** go on inputs (their menu), on groups (the group's menu: **Plug-in effect...**) and on the master
(**FX** under the master fader) - any VST3 effect, e.g. a Valhalla reverb on a whole loops group. A group's plug-in comes
after its built-in effects. Their settings are saved with the set.

**MIDI on knobs**: right-click any effect knob > Learn MIDI control, then turn a MidiMix knob. Moving it switches its
section on, and an open panel follows along. Knobs on groups, channels and Space are saved with the set; knobs on
inputs and instruments belong to the computer, like their faders.

## Sync

Loops start the moment you press them, so after a while they can sit off each other's beat. **SYNC** (next to TAP)
lines every playing loop back up with the one that's been playing longest - that one carries on untouched, the rest
slide into step with a 30 ms crossfade, and the beat lights follow. While overdubbing, the loop being overdubbed is the
one the others line up with. Right-click SYNC to **restart all loops together from the top** instead, or to assign a
MIDI button. Loops whose lengths fit together (whole bars at one tempo) stay together; loops of unrelated lengths drift
apart again over time, so press SYNC whenever they need it.

## On stage: cues, mute, CPU and the interface

- **CUED:** with **Start** on *beat* or *bar*, a loop you press waits for its moment - its PLAY button blinks amber and
  says **CUED** until it comes in (press again to cancel). Stopping one waits for the beat too: it blinks in its
  colour until it stops. A MidiMix play button with a light blinks the same way, so you can tell without looking at
  the screen.
- **MUTE OUT** (under the master fader) silences the speakers instantly - for emergencies like feedback. Unlike
  STOP ALL and Esc (musical fades) everything keeps running underneath, so pressing it again brings the sound
  straight back. It blinks red (**MUTED**) while on, and set recordings hear the silence too. Right-click to assign a
  MIDI button.
- **CPU** (in the header) shows how hard the audio is working, and how many **dropouts** (clicks or gaps from the
  computer not keeping up) there have been. Amber over 70%, red over 90%; the dropout count flashes red when a new one
  happens. Click it to reset the count. Fix: a bigger buffer in Settings > Audio, or fewer reverbs and plug-ins (use
  Space).
- **Interface unplugged:** if the interface disappears (cable, power) or stops responding, a red banner says so and
  Airloom goes silent instead of switching to the computer's own speakers and mic (which could feed back). Plug it
  back in and it reconnects by itself with the same settings; loops carry on from where they stopped. Choosing a
  device in Settings > Audio instead ends the wait.

## Freeze

**FREEZE** (bottom bar, next to STOP ALL) holds what you just played - the live inputs and instruments, with their
effects, not the loops - as an endless pad to play over. It keeps listening for a quarter of a second after the press,
to catch the note you're on. Press again (**FROZEN**) to let it go: it fades out over the **fade time**. Right-click
to assign a MIDI button.

## Sets

Everything - samples, groups, channels, volumes, mutes, effects, input settings, fade time - is a **set**. The working session is
saved automatically, so the app reopens exactly where you left off.

- **A saved set saves itself:** a moment after any change, its `.liverig` file is rewritten, so opening another set
  (or a crash) never loses work. Before overwriting, Airloom keeps a dated copy in a **Backups** folder beside the set
  (at most one every 10 minutes, the newest 20) - **Set > Show backups**, then open any of them like a set. A set that
  has never been saved is kept in the session; **New set** or **Open set** asks before replacing it.
- **Opening from Explorer:** with Airloom installed, double-click a `.liverig` set or a `.liverigpack` to open it
  (in the Airloom that's already open, if it is).
- **Undo / Redo:** **Ctrl Z** and **Ctrl Y** (or Ctrl Shift Z; also at the top of the Set menu), up to 60 steps.
  Edits are undone - loading or clearing samples, moving and renaming channels and groups, effects, trims, layout,
  scenes added or removed, folders - while what's playing keeps playing in time, faders and mutes stay where they
  are and plug-ins stay loaded. Playing isn't undone: faders, mutes, starting and stopping loops, scene recalls, keys,
  octaves and tempo. Changes in quick succession (turning a knob) are one step. Adding or removing an input or
  instrument isn't undone (their settings are). Opening a set starts its own history.
- **Messages:** confirmations ("Saved", "Undone", "recorded") and errors appear as cards in the top-right corner for a
  few seconds - errors (red edge) stay longer; click a card to dismiss it. The line along the bottom keeps hints, MIDI
  learn prompts and MIDI devices, and at its right end shows what's busy (samples or an instrument loading, a set
  being packed). Dialogs (rename, confirm) highlight the button Enter presses.
- **Finding things:** every channel, group and input or instrument strip has a small **...** button (top right)
  that opens its options - the same menu as clicking its name or right-clicking it. **F1** (or Set > Manual) opens
  this manual inside Airloom; Set > Welcome screen shows the first-run welcome again.
- **Settings** (the header button, Set > Settings, or **Ctrl ,**) has three tabs: **Audio** (the interface, buffer
  size, inputs), **Look** (theme and zoom) and **Layout** (rearranging groups to fit the window, folding the LIVE dock
  and the samples list). The window remembers its size, place and whether it was maximised.
- **Shortcuts:** Ctrl S save (Ctrl Shift S save as a new folder), Ctrl O open, Ctrl N new set, Ctrl Z / Ctrl Y undo and
  redo, Ctrl + / Ctrl - / Ctrl 0 zoom, Ctrl , settings, F1 the manual, F11 stage view, Esc stop all (or cancel MIDI learn).
- **Set > Rename set...** renames the set's file (its folder stays as it is); **Set > Open recent** lists the last 8.

- **Set > Save set as new folder...** creates a folder and gathers every sample into it - one subfolder per group,
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
- master fader, inputs and instruments (faders, mutes, ARM), REC / DUB / UNDO, STOP ALL, MUTE OUT, TAP, SYNC, FREEZE, REC SET

Channel and group assignments are **saved with the set** (and travel in an exported pack), and follow the channel or
group wherever it's moved. The rest belong to the computer. Out of the box only the MidiMix **master fader**, its
first two **REC ARM** buttons (arm inputs 1-2) and the **Fire's REC button** (records a loop, like REC) are assigned.
MidiMix buttons light up: a group's mute while the group is on, a channel's play button while it plays, REC ARM while
armed. The Fire's REC button glows dim red when ready, bright red while recording and amber while overdubbing.

Faders and knobs use **soft takeover**: a hardware control only grabs the on-screen one once it passes its position,
so nothing jumps. On group and master faders an amber tick shows where the hardware is until then.

Right-click any learnable control to clear its assignment, or to clear all MIDI assignments.

## If Airloom closes unexpectedly

Airloom keeps a crash log in `%APPDATA%\LiveRig`: a crash writes `crash-log.txt` (what failed, and where) and a
minidump in `Crashes\`. If it closed any other way without quitting, the next start says so in the status line. Keep
those files to find the cause - the dump is read against `Airloom.pdb`, which the build writes beside Airloom.exe
(not packaged). `Airloom.exe --crash-test` checks the log by crashing on purpose.

## Low latency (ASIO)

The build uses WASAPI/DirectSound. For the lowest latency with an interface's ASIO driver (needed for live
playing in Stages 3-4), JUCE needs the Steinberg ASIO SDK: download it, then add `JUCE_ASIO=1` to
`target_compile_definitions` and its `common` folder to `target_include_directories` in `CMakeLists.txt`.
