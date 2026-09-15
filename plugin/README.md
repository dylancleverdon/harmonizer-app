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

* **Logic Pro** — put Harmonizer on your audio track's **Audio FX**. It appears
  under the MIDI-controlled effects. Create a software instrument track, and in
  its instrument slot pick Harmonizer's *side-chain* MIDI destination; playing
  that track's keyboard then drives the harmonies.
* **FL Studio** — add Harmonizer to the mixer insert carrying your audio. Open
  its wrapper settings, enable **Receive notes**, and route a pattern or your
  keyboard to it from the channel rack.

If you hear your dry signal but no harmonies, MIDI is not reaching the plugin.
The voice counter in the plugin window tells you directly: if it stays at 0 while
you play, the notes are not arriving.

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
