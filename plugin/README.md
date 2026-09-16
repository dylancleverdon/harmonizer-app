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

### Transpose

Shifts the keys you hold before they name a key centre — never the note you
play, which is measured from real sound and is concert pitch already. If
you play a transposing instrument and think in its written pitch — a Bb
trumpet reading a chart in Bb, say — set this to match and hold keys using
that instrument's key names; the plugin does the conversion to concert
pitch for you. `Concert (C)` (the default), `Bb`, `Eb` and `F` are labelled;
anything else shows as a semitone count.

### Latch and sustain

**Latch key centre** freezes the key centre against releases: once
something is held, only a fresh key press changes it, never a release. This
exists because MIDI keys don't release at the same instant — lifting your
hand off a held minor chord lifts one finger before the other, and without
latch, that momentary one-key state reads as major for the few milliseconds
before the second finger comes up too. With latch on, a minor key centre
stays minor through the whole release, down to no keys held at all; the way
back to major is to release every key and press exactly one, fresh.

The **sustain pedal** (MIDI CC 64) does two things while held, needing
nothing else switched on: it stands in for latch, so the key centre survives
your hand coming off the keys entirely, and it freezes the chord itself —
whatever was last triggered holds out even if you move to a different note
on your horn. To pick up a new chord, release the pedal and press it again;
merely holding it captures nothing new. Pitch tracking keeps running
underneath while the pedal is down, so the moment it comes back up, the
chord already matches whatever you are currently playing rather than
waiting out another stability window.

### Chord tones

Sevenths are always in. **9ths**, **11ths** and **13ths** stack on top, and the
chord symbol follows what is switched on — the spelling is handled for you, so a
dominant takes a `#11` rather than the natural 11 that sits a semitone off its
third, and an altered dominant takes `b9`, `#11` and `b13`. **Harmony voices**
caps how many notes sound; past the cap the fifth goes first and then the root,
the two tones that say least about the chord. **Auto** ignores that cap
entirely and plays exactly as many voices as the chord actually has — a
custom voicing's own note count, or the built-in chord's three plus whichever
extensions are switched on — rather than a number picked ahead of time.

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
* **Glide** — chord changes as pitch portamento instead of a retrigger. Each
  voice in the old chord is matched to one in the new chord and slides to
  it over however many milliseconds this is set to, rather than stopping
  and restarting; a tone common to both is untouched either way, since it
  was already sustaining through the change. When the chord's voice count
  changes, nothing is ever just cut or invented from nothing: if it shrinks,
  the voices that no longer have a tone of their own slide onto whichever
  remaining tone is nearest, so a tone can end up doubled rather than a
  voice dropping out silently; if it grows, one of the existing voices
  splits in two, the new one starting from the same pitch as its source and
  sliding away to the new tone it covers. Off (the default) is an ordinary
  retrigger, exactly as if glide did not exist.

### Voicing style

Close, drop 2, drop 3, drop 2 & 4, rootless, quartal, shell, spread and cluster.
Select as many as you like and the best of them for the moment is used; select
**none** and every style is a candidate, which is the setting to leave it on if
you would rather not think about it. **Shuffle** varies which of the chosen
styles a new chord gets instead of always taking the highest-scoring one — it
only ever picks from what you selected, and never mid-chord.

### Custom chord dictionary

The built-in dictionary above is fixed -- twelve degrees, one chord each, hand
picked for functional harmony. The **Custom chord dictionary** card at the
bottom of the Jazz page is a second dictionary you build yourself, in its
place: not a choice of a few fixed chord types, but the exact notes and
voicing you want, played on a keyboard right there in the card.

For each of the twelve notes of the key you can build an exact voicing --
whichever notes you want, in whichever octave -- by clicking them on the
keyboard, playing them on a MIDI controller with **Record**, or letting a
MIDI file work it out for you. The chord is always rooted on the note you
actually play, so the one rule the built-in dictionary never breaks holds
here too without anything having to enforce it: you are always a tone of the
chord, its root. It is written once in terms of the key centre, the same way
the built-in dictionary is, so building it once already covers all twelve
keys -- naming a key centre just transposes it, and it does not matter which
octave you play the note in either, only which one it is. **Show it over this
key** picks which key the degree buttons and the keyboard are shown relative
to -- purely a display and editing convenience, changing nothing about what
is stored -- so to see exactly what plays for a C5 on your horn over a held
G, select G there and then D as the degree (a fifth above G).

Major and minor are separate, independent toggles, and so is every individual
degree within them: leaving a degree blank falls back to the **built-in**
chord for just that one note, so a table you have only half filled in still
gives you ordinary chords everywhere else. Turning the whole card off (or
leaving both context toggles off) is the way back to how jazz mode has always
worked -- not a one-way door. Range, octave, inversion and voice leading all
still apply on top of a custom voicing, the same as anywhere else in jazz
mode; extensions and voicing style do not, since a voicing you built yourself
already says exactly what it wants to be.

**Record** builds a voicing by ear instead of by clicking: press it, play the
chord on your MIDI controller as if the key you were holding were the one
selected in **Show it over this key** -- the same reference the keyboard
uses -- and every note you play joins it, shown highlighted live. **Reset**
clears what has been captured without leaving
record mode; **Save** writes it to the selected degree.

**Copy this voicing to** moves a finished voicing to another note or context,
shifting every tone by the distance between them so it keeps its shape --
build one chord well and reuse it. **Copy whole major table to minor** (and
back) copies all twelve degrees across contexts untransposed, since major and
minor already share the same twelve scale degrees.

**Generate from MIDI** runs the same idea in reverse: feed it a MIDI file and
it finds the key centre (or centres, if the performance modulates) and, within
each, what chord was actually played over which scale degree, filling in as
much of the table as the file gives evidence for. It is a heuristic --
built-in-dictionary jazz musicians do not label their own chords -- so treat
it as a fast first draft: check the keyboard afterwards and fix anything it
got wrong the same way you would edit a hand-built entry, including reaching
for Record or the note grid directly. It replaces the whole dictionary, so
save a preset first if the existing one is worth keeping.

Custom dictionaries can be saved as named **presets** under the card below it,
independent of any particular DAW project -- a dictionary built for one song
can be loaded into another. They are stored under
`~/Library/Application Support/Harmonizer/JazzDictionaryPresets` on macOS (the
platform-equivalent app-data folder elsewhere) as one small XML file per
preset. Loading a preset switches the custom dictionary on. A preset saved by
an older version of this dictionary, back when it picked one of six fixed
chord types per degree rather than an explicit voicing, still loads -- it is
converted to the equivalent voicing on the way in.

### How it is put together

The dictionary and the voicer are `plugin/Source/JazzVoicer.{h,cpp}`: integer
music theory with no JUCE, no engine and no allocation, so they can be tested on
their own (`plugin/Tests/JazzHarness.cpp` builds with one compiler invocation)
and so none of this reaches the app. `PluginProcessor` reads the played pitch
from the engine's own tracker, asks the voicer for a chord when the pitch has
held still for about 15 ms, and feeds the result to the engine as MIDI in
absolute-pitch mode. Notes common to the old chord and the new one are left
alone rather than retriggered, so a held common tone really does sustain through
the change. The custom dictionary lives in the same `Settings`/`Voicer` pair as
everything else in jazz mode -- it is a per-context override of the lookup,
not a separate code path, and a custom entry is just semitone offsets above
the root rather than a chord type, so the same representation serves manual
editing, a recorded voicing and an imported one without three different data
models. Presets are a small XML file per name, read and written by
`PluginProcessor` on the message thread only.

The keyboard editor itself, `plugin/Source/PianoKeyboard.{h,cpp}`, is a plain
JUCE component that knows nothing about jazz mode -- it shows a range of keys,
highlights whichever are passed to it, and reports clicks. The editor decides
what a click means; record mode reuses the same component to show live
capture instead of a saved entry.

MIDI import's analysis, `plugin/Source/JazzMidiImport.{h,cpp}`, is written the
same way the voicer is -- no JUCE, tested on its own in
`plugin/Tests/JazzHarness.cpp` -- so the actual key-finding and chord-reading
logic has nothing to do with parsing a `.mid` file. It works entirely in MIDI
ticks: a sliding window's pitch-class histogram is correlated against the
standard Krumhansl-Kessler major and minor key profiles to guess a key centre
a couple of bars at a time, short-lived disagreements are folded into their
longer neighbours, and within each resulting stretch of one key, the lowest
note sounding at each beat is read as the chord's root and everything above
it as the voicing, with the most frequently seen voicing for each scale
degree winning a simple vote. `PluginProcessor::importJazzCustomDictionaryFromMidiFile`
is the thin JUCE-facing wrapper: it loads the file with `juce::MidiFile`,
turns its matched note-on/note-off pairs into the tick-based note list the
analysis wants, and writes the result into the same parameters the keyboard
editor does.

Latch and sustain live entirely in `PluginProcessor`'s
`processBlock()`/`jazzUpdate()` -- held keys are read through `collectKeys()`
everywhere rather than straight from the raw MIDI state, so latch capture and
the live reading never disagree. Transpose never reaches this: it is a pure
display transform, applied only in `PluginEditor` when naming the key centre
and the live melody note (`written = concert - transposeSemitones`, matching
the slider's own convention that -2/+3/+5 are Bb/Eb/F). The engine, the
voicer and every value on `JazzView` besides `transposeSemitones` itself stay
real concert pitch -- what the key centre and the melody actually are was
never going to agree with each other under a shift applied to only one of
them, since the melody note comes from live audio and can't be transposed to
match. Glide is the one piece that reaches into the shared
engine, and the only place jazz mode's voice matching lives is
`PluginProcessor::jazzApply()` -- the engine itself has no idea a "chord"
exists, only individual voices.

`Harmonizer` gains two methods alongside the ordinary MIDI note-on/off path:
`retargetVoiceNote()` moves a sounding voice to a new note in place, gain and
velocity untouched, so nothing retriggers; `spawnVoiceFromNote()` starts a
new voice that begins audibly at another one's current pitch and slides away
from there. Both are synchronous, direct calls (never through `midiQueue()`),
safe because `jazzApply()` and `Harmonizer::process()` already run on the
same audio thread in the same `processBlock()`. Each `Slot` gained a
`targetHz` that slews toward its note's frequency every hop in Absolute mode
-- at `dsp::Params::glideMs` (`app/src/main/cpp/dsp/Types.h`), or landing in
one hop when that is 0, which is what an ordinary MIDI note-on always
forces regardless of the setting, and what every caller that never touches
it gets by default. The Android app, and every other harmony mode, never
sets it and so never sees a behaviour change.

`jazzApply()`'s own job is matching: a voice already sitting on a note the
new chord wants is left alone outright (the same common-tone rule the
no-glide path applies, just needed here too so an already-correct voice is
never stolen from its tone by index-based pairing); what is left on each
side pairs off by ascending index; and whichever side has more left over
either converges (shrinking) or splits (growing), as above. Bookkeeping
(`jazzVoiceNotes_`) tracks the true count of physically sounding voices, not
the chord's own note count -- they diverge exactly when a shrink converges
two voices onto one tone without releasing either, and the next chord change
needs to know both are still there or it would spawn a redundant third
rather than reusing them.

`tools/dsptest` exercises `retargetVoiceNote()`/`spawnVoiceFromNote()`
directly, by spectral measurement of where the pitch actually is at each
point in the glide; `PluginHarness.cpp` exercises the matching algorithm
itself -- converging, splitting, and reusing a previously-converged pair --
through the full plugin, using a custom dictionary to pin exact voice
counts.

## Updating

Press **Check for updates** in the plugin window. It checks the releases page,
downloads the build for your platform, verifies it against the published
checksum, and replaces the installed bundle — the AU and the VST3 together, so
they never drift to different versions.

A plugin cannot restart itself while a DAW has it loaded, so the install finishes
on disk and you restart your DAW to pick it up. On Windows the loaded binary
cannot be deleted, but it *can* be renamed, which is what makes replacing it
while it runs possible; the leftover is swept up next time the plugin loads.

### Version history

Every build gets its own permanent release — nothing is ever deleted the way
`latest` is — so if an update causes a problem, the **Version history** panel
next to **Updates** can put a past build back. Press **Load version history**,
pick a version from the list, and **Install this version** runs through the
exact same download/verify/install pipeline an ordinary update does, just
pointed at that build instead of the newest one. It is not a separate,
smaller install: the AU and VST3 are replaced together either way, and you
restart your DAW afterwards the same as any other update. `PluginUpdater`
tracks which of its background jobs is running, so a check, an install, a
history load and a rollback all share one worker and one status line rather
than racing each other.

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
