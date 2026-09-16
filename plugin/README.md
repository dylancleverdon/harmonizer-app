# Harmonizer plugin

The same harmoniser as the Android app, as an Audio Unit and VST3.

The engine is not a port — `plugin/` and `app/` compile the *same* files from
`app/src/main/cpp/dsp/`. That directory has no platform dependencies, which is
what makes one implementation serve both. A fix to the pitch shifting fixes the
phone and the plugin together.

## Formats

| Platform | Formats | Notes |
|---|---|---|
| macOS | Audio Unit, VST3 | Universal: Apple Silicon and Intel |
| Windows | VST3 | 64-bit |

**Logic Pro does not load VST3 at all** — it is Audio Units only, which is why
the AU exists. FL Studio uses the VST3 on either platform.

## Installing

Download the archive for your system from the
[latest release](https://github.com/dylancleverdon/harmonizer-app/releases/latest),
extract it, and run the installer inside:

* **macOS** — `install-macos.command`. If macOS refuses to open it, right-click
  it and choose **Open**. It copies the AU and VST3 into your user plug-in
  folders and clears the quarantine flag that browsers attach to downloads —
  which is the flag that otherwise stops the plugin loading at all.
* **Windows** — `install-windows.bat`. It installs machine-wide if it can, and
  into your personal plug-in folder if it cannot, telling you the path to add to
  your DAW in that case.

Then quit your DAW completely and reopen it so it rescans.

## Getting MIDI into it

This is the step people get stuck on. The plugin is an **audio effect that
accepts notes**, not an instrument: the audio is your voice or horn, and MIDI
chooses the harmonies. It declares itself an AU *music effect* (`aumf`)
specifically so a DAW will let you send it notes — a plain effect cannot receive
them.

* **Logic Pro** — make a **Software Instrument** track and pick Harmonizer in its
  Instrument slot, under *AU MIDI-controlled Effects*. The track itself carries no
  audio, so set **Side Chain** at the top of the plugin window to the track your
  voice or horn is on. Play that instrument track's keyboard to drive the
  harmonies.
* **FL Studio** — add Harmonizer to the mixer insert carrying your audio. Open
  its wrapper settings, enable **Receive notes**, and route a pattern or your
  keyboard to it from the channel rack.

The Signal card at the top of the plugin window answers this directly. It meters
the track input and the side chain **separately**, shows whether any MIDI has
arrived, counts the sounding voices, and names the likely problem in words. If
the side chain reads *not connected* in Logic, that is the routing to fix.

## Jazz chord mode

Plugin only — the Android app does not ship it. It is on its own **Jazz** page in
the plugin window, and while it is on it takes over the harmony: the three modes
on the main page stand down.

The idea is that your left hand names a key and your horn does the rest.

* **Hold one key** and that note is a **major** key centre. **Hold two or more**
  and it is a **minor** key, on the **lowest** key held.
* Whatever you play into the audio input is measured, read as a **scale degree**
  of that key centre, and looked up in a dictionary of jazz chords — one chord
  for each of the twelve degrees, chromatic notes included. Every chord in the
  dictionary contains the note you played, so you are always a chord tone rather
  than something the harmony has to work around.
* The note you are playing is left out of the chord, because you are already
  sounding it. **Double your own note** puts it back, which is worth it when you
  are running fully wet.

In C major, a D makes it `iim7` and you are the root; a B makes it `V7` and you
are its third; an Eb makes it a passing `bIIIdim7`. In a minor key the same line
gives `im7`, `iim7b5`, `bIIImaj7`, `V7b9` and the dorian `IV7`. The panel names
the chord as it is written on a lead sheet and tells you which tone of it you
are.

### Chord tones

Sevenths are always in. **9ths**, **11ths** and **13ths** stack on top, and the
chord symbol follows what is switched on — the spelling is handled for you, so a
dominant takes a `#11` rather than the natural 11 that sits a semitone off its
third, and an altered dominant takes `b9`, `#11` and `b13`. **Harmony voices**
caps how many notes sound; past the cap the fifth goes first and then the root,
the two tones that say least about the chord.

### Where the chord sits

* **Range** — a low and a high note. Nothing sounds outside it, with the one
  exception at the end of this bullet. Widening it
  lets each chord find its own best register; tightening it forces successive
  chords to share registers, which is the bluntest way there is to smooth the
  voice leading. If you park it more than two octaves from what you are actually
  playing, it cannot be used as written — the engine will not shift a voice that
  far — so the chord is held closer to you instead, in tune, and the panel says
  it has done so.
* **Octave** — moves the register the voicer aims for by a whole octave, inside
  whatever the range allows.
* **Inversion** — the finer control between those steps. It rotates the voicing
  rather than transposing it: down an inversion takes the top voice an octave
  lower, so the chord sits lower and **your own note ends up higher inside the
  harmony**. Up an inversion buries you in it.
* **Voice leading** — at 0 % every chord is voiced in its own best register,
  wherever that leaves the last one. At 100 % the voicing that moves least from
  the chord before it wins, even where that means an odd register.

### Voicing style

Close, drop 2, drop 3, drop 2 & 4, rootless, quartal, shell, spread and cluster.
Select as many as you like and the best of them for the moment is used; select
**none** and every style is a candidate, which is the setting to leave it on if
you would rather not think about it. **Shuffle** varies which of the chosen
styles a new chord gets instead of always taking the highest-scoring one — it
only ever picks from what you selected, and never mid-chord.

### How it is put together

The dictionary and the voicer are `plugin/Source/JazzVoicer.{h,cpp}`: integer
music theory with no JUCE, no engine and no allocation, so they can be tested on
their own (`plugin/Tests/JazzHarness.cpp` builds with one compiler invocation)
and so none of this reaches the app. `PluginProcessor` reads the played pitch
from the engine's own tracker, asks the voicer for a chord when the pitch has
held still for about 15 ms, and feeds the result to the engine as MIDI in
absolute-pitch mode. Notes common to the old chord and the new one are left
alone rather than retriggered, so a held common tone really does sustain through
the change.

## Updating

Press **Check for updates** in the plugin window. It checks the releases page,
downloads the build for your platform, verifies it against the published
checksum, and replaces the installed bundle — the AU and the VST3 together, so
they never drift to different versions.

A plugin cannot restart itself while a DAW has it loaded, so the install finishes
on disk and you restart your DAW to pick it up. On Windows the loaded binary
cannot be deleted, but it *can* be renamed, which is what makes replacing it
while it runs possible; the leftover is swept up next time the plugin loads.

## Building it yourself

    cmake -B build -S plugin -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target HarmonizerPlugin_VST3 --parallel

JUCE is fetched automatically. Point `-DHARMONIZER_JUCE_PATH=/path/to/JUCE` at an
existing checkout to skip that.

`HarmonizerPluginTest` is a headless harness covering the wrapper specifically —
parameter mapping, host MIDI, channel summing, reported latency. The DSP has its
own suite in `tools/dsptest`. CI runs both before it will publish anything.

## Licensing, worth knowing

This uses JUCE, which is available under the GPL or a paid commercial licence.
The build sets `JUCE_DISPLAY_SPLASH_SCREEN=0`, which the GPL terms permit only
while the project itself stays open source under a compatible licence. This
repository is public, so that holds today — but if you ever want to sell this or
close the source, you need a JUCE licence first, and the splash setting has to go
back. Adding an explicit `LICENSE` file naming the GPL would make the position
unambiguous.
