# Harmonizer

A live MIDI-controlled harmoniser. Play or sing into it, hold chords on a MIDI
controller, and hear up to ten pitch-shifted copies following the notes you play.

Two front ends, **one engine**:

* **Android app** — built for a Galaxy S23 Ultra. Mic in, harmonies out, no computer.
* **Audio Unit / VST3 plugin** — for Logic Pro, FL Studio and any other DAW, on
  macOS and Windows. See [plugin/README.md](plugin/README.md).

They are not two implementations. Both compile the same files from
`app/src/main/cpp/dsp/`, which depends on nothing platform-specific — so a fix to
the pitch shifting fixes both, and the offline test suite covers both.

All processing is local. Nothing is uploaded.

---

## Start here

**https://dylancleverdon.github.io/harmonizer-app/**

A page that works out which computer you are on, asks which music software you
use, hands you the one file you need, and tells you exactly what to click when
your operating system inevitably warns you about an unsigned app. Send that link
to anyone you want to give this to.

If you would rather grab files directly:

| You want | Download | Then |
|---|---|---|
| The phone app | `harmonizer.apk` | Open it on the phone |
| The plugin, Mac | `HarmonizerSetup-macOS.zip` | Right-click the app, Open |
| The plugin, Windows | `HarmonizerSetup.exe` | Run it |
| Plugin files only | `Harmonizer-macOS.zip` / `Harmonizer-Windows.zip` | Install by hand |

Everything is on the
[latest release](https://github.com/dylancleverdon/harmonizer-app/releases/latest).

**Harmonizer Setup** is the guided installer. It installs the plugin wherever
your DAWs look, explains MIDI routing for the DAWs you pick, and copies itself
somewhere permanent so every future update is one button in that same window.

## Getting it onto the phone

Download `harmonizer.apk` on the phone and open it. Android will ask permission
to install from your browser the first time, and Play Protect will warn about an
unrecognised developer — expected for a sideloaded app.

### Updating

Once installed, you never need to do that again. Open **Settings → Updates** in
the app and tap **Check for updates**. If a newer build exists it shows the
version and what changed, downloads it, and hands it to Android's installer. You
confirm once and the app restarts on the new version.

The first time you use it, Android will ask permission for this app to install
apps. Grant it once and it stops asking.

> **One last manual install.** Builds before this one were each signed with a
> different throwaway key, which is why every install needed the old version
> deleted first. From this build onward every build is signed with the same key
> — but crossing over to it still requires one uninstall. Delete the app, install
> `harmonizer.apk` from the link above, and from then on updates happen in place.

Builds are produced by GitHub Actions on every push and attached to the release
tagged `latest`, alongside a `version.json` the app polls to decide whether an
update exists.

To build locally instead: open the project in Android Studio, plug the phone in,
and press Run.

### A note on the signing key

`keystore/release.jks` is committed to this public repository, which makes the
signing key public. That is a deliberate trade for zero-setup builds on a
personal app — and it does mean anyone could build an APK this phone would accept
as an update. `keystore/README.md` explains the exposure and how to rotate to a
key held in repository secrets before sharing the app with anyone.

---

## Using it

**Before anything else, put headphones on.** The built-in mic feeding the phone
speaker is a feedback loop. The app warns you when it detects that routing, but it
cannot prevent it.

1. Grant microphone access.
2. Pick your **audio input** and **audio output** independently under Routing. A
   USB-C audio interface, the built-in mic, wired headphones, a Bluetooth headset
   — whatever is connected shows up in the lists. "Automatic" lets Android choose.
3. Plug a MIDI controller into the USB-C port and tap **Rescan**, then pick it
   under **MIDI controller**. Bluetooth LE controllers appear in the same list
   once the system has paired them.
4. Press **Start**.
5. Sing, and play notes. Use the big knob to balance the harmonies against your
   original voice.

**Panic** silences all voices immediately if a note sticks.

### A note on Bluetooth

Bluetooth audio output adds roughly 150–300 ms of its own latency on top of
everything the app does. No amount of DSP tuning recovers that — it happens after
the audio leaves the app. It is fine for practising against a backing track and
unusable for live monitoring. Wired or USB output is the only real fix. The app
flags it when it sees a Bluetooth output selected.

---

## How the harmony is worked out

There are three harmony modes.

**Fixed interval** (the default) treats every MIDI note as an offset in cents from
middle C, and applies that offset to whatever you are playing:

| You play | Semitones from middle C | Shift applied |
|---|---|---|
| C4 (middle C, note 60) | 0 | unison |
| E4 (note 64) | +4 | **+400 cents** |
| G4 (note 67) | +7 | +700 cents |
| G3 (note 55) | −5 | −500 cents |
| C5 (note 72) | +12 | +1200 cents |

Play a G and hold E4, and that voice sounds 400 cents above your G. The harmony is
parallel and moves with you.

**Absolute pitch** instead makes each voice land on the exact pitch of the note
played, whatever you play — hold C–E–G and you get a C–E–G triad out. This needs a
confident read on your own pitch, so it works best on sustained, clearly voiced
notes, and it is worth raising the analysis window to 2048 for low registers.

**Chord voicing** is the one to reach for with a trumpet. Hold a chord and *you*
supply one of its tones; the engine builds the rest around your pitch. Hold C–E–G
and play a concert A: the A becomes the root, and two voices appear +400 and +700
cents above it. Hold the same shape anywhere on the keyboard and you get the same
chord — the shape is reduced to intervals, so its absolute position is irrelevant.

Because your own note *is* one of the chord tones, nothing has to measure your
pitch — the intervals are simply applied to whatever you play. This mode costs no
more than fixed-interval mode and needs no pitch tracker.

### Which tone are you?

By default you are the root and the chord stacks upward. **You are playing the**
lets you be any chord tone instead — 3rd, 5th, 7th, 9th, 11th or 13th — and the
rest of the chord is placed around you, above *and below*:

| Hold | You are | Play a concert A | Voices you get | Chord that results |
|---|---|---|---|---|
| C–E–G | Root | A | C♯, E above | A major |
| C–E–G | 3rd | A | F below, C above | F major |
| C–E–G | 5th | A | D, F♯ below | D major |
| C–E–G–B♭ | 7th | A | B, D♯, F♯ below | B7 |

The same three keys held down give you four different chords depending only on
which tone you declare yourself to be.

Anchoring on the 5th or 7th is how you put the whole chord *underneath* the
melody, which is usually what you want when the trumpet is carrying the top line.

The degree is matched by pitch class, so it is found wherever it is voiced in the
chord, and it adapts to chord quality: "3rd" finds the minor third in a minor
chord, "5th" finds the flattened fifth in a diminished one. Preference order is
major third before minor, perfect fifth before diminished or augmented, dominant
seventh before major.

**If the chord does not contain the degree you asked for, the root is used
instead** — you still get a usable chord built on your note rather than silence.
The main screen says so when it happens, so it is never a silent surprise.

Two things worth knowing:

* A three-note chord sounds **two** voices, because you are the third one. The
  voice counter reflects this and the screen explains it.
* Holding a single note produces no harmony at all — that note is just you. Hold
  at least two.

**Double your own note** is off by default, since you are already playing that
tone. Turn it on when running fully wet, where you would otherwise lose it.

All three modes are switchable on the main screen. The pitch tracker only runs in
absolute mode, so the other two pay nothing for it.

---

## The three quality-reduction methods

One method is active at a time, chosen in Settings; the **Reduction amount** slider
sets how far it goes. Vocoder is the default.

### Vocoder bands (default)

Each voice is resynthesised from only the K loudest spectral peaks — the same idea
as a vocoder reducing a signal to a handful of bands. K runs from 96 down to 6.

This is the default because the cost is directly proportional to K with no
transform per voice at all, and because voice degrades gracefully as K falls: it
gets progressively more synthetic and less airy rather than gaining artefacts.

Measured, ten voices held: **6.2% → 2.3% of realtime** across the slider's range.

### Sample rate

Runs the wet path at 48, 24 or 12 kHz. The analysis window shrinks in proportion
to the rate, so the transform gets cheaper **while the window duration stays the
same** — which is why latency does not move when you change this. That is
deliberate and verified by a test; adjusting quality mid-performance must not
shift the timing of the harmonies against your voice.

Measured, ten voices held: **6.2% → 2.0% of realtime**.

### Bit depth

Quantises the wet path from 24 bits down to 4.

**This one buys tone, not speed, and the app says so.** Everything downstream is
32-bit float on this SoC, so quantising *adds* a step rather than removing work.
Measured cost at 4-bit is 6.17% of realtime against 6.19% at 24-bit — within noise
of each other. It is included because it was asked for and because it is a real
lo-fi effect, but if the goal is latency, the other two are the ones that pay.

### The two experimental options

**Adapt to measured latency** — the engine times its own work against the callback
deadline every block. When the smoothed load passes 72% it raises the reduction
amount; below 45% it eases back off. Adjustments are rate-limited to ten a second
with hysteresis so it settles rather than oscillating. The Settings screen shows
the effective amount live, so you can see it working.

**Reduce quality as voices are added** — scales the reduction amount up with the
number of notes held, so a ten-note chord costs closer to what one note costs.

Both are off by default and stack with the manual slider.

---

## Latency and dropouts

Two settings control the stream itself, separately from the harmony quality modes.

**Stream rate** is what everything runs at, not just the wet path. Lowering it
means every stage handles proportionally fewer samples per second, while the
callback deadline stays the same length in milliseconds — so it is the most
direct lever against dropouts. Device default is cheapest of all, because nothing
has to be converted.

**Output buffer** is how much slack the output has before a late callback becomes
an audible gap. 1 burst is tightest and least forgiving; raise it if you hear
crackle under a big chord. Each burst costs roughly one burst of latency.

**Analysis window** is the biggest single lever on latency, and the one thing the
quality modes deliberately never touch:

| Window | at 48 kHz | at 24 kHz | at 16 kHz |
|---|---|---|---|
| 256 | 5.3 ms | 10.6 ms | 15.9 ms |
| 512 | 9.3 ms | 18.6 ms | 27.9 ms |
| 1024 | 17.3 ms | 34.6 ms | 51.9 ms |
| 2048 | 33.3 ms | 66.6 ms | 99.9 ms |

The window is a fixed number of *samples*, so at a lower stream rate it lasts
proportionally longer. Halving the rate and halving the window together keeps
latency where it was while halving the work. The Settings screen computes the
real figure from the rate actually running and says so when the two are working
against each other.

Small windows resolve low notes poorly, so 256 suits a trumpet far better than it
suits a bass voice. Use 2048 for absolute-pitch mode on low registers.

These are the engine's own contribution. Add the audio hardware on top: roughly
10–15 ms round trip with a USB interface, 25–40 ms with the built-in mic and
wired headphones. The main screen shows the measured total.

---

## How it works

```
mic ─┬─────────────── dry delay (matched to engine latency) ──────────┐
     │                                                                 │
     └─ decimate ─ STFT analysis ─┬─ peaks + true frequencies ─┐       ├─ mix ─ out
        (quality:   (once per hop, │                            │       │
         sample      shared by ALL │  ─ formant envelope        │       │
         rate)       ten voices)   │  ─ pitch (absolute mode)   │       │
                                   │                            │       │
                                   └─ residual (unvoiced) ──┐   │       │
                                                            │   │       │
                     ten additive oscillator banks ◄────────┼───┘       │
                     + one residual pass ─── overlap-add ───┴ interpolate┘
```

The thing that makes ten voices affordable on a phone is that the **expensive part
is paid once**. A single STFT analysis per hop produces the peaks, formant envelope
and pitch estimate; each of the ten voices is then just an oscillator bank reading
from that shared analysis. There is no transform per voice.

Each voice is sinusoidal: the analysis peaks, shifted by the voice's ratio, drive a
bank of interpolating oscillators with frequency and amplitude ramped across each
hop and phase carried across hops. Partials are matched frame to frame by
frequency so a sustained note stays coherent instead of buzzing.

Alongside them, one **residual** pass resynthesises what the peaks do not explain —
breath, consonants, sibilance — with randomised phase. Without it the harmony
voices lose every unvoiced sound. It is computed once for the whole chord rather
than per voice, since unvoiced sound has no pitch to shift.

**Formant correction** rescales each shifted partial by how the original spectral
envelope differs between its old and new frequency, so the vowel stays put while
the pitch moves. The envelope comes from cepstral liftering of the shared
analysis. Without it, shifted voices sound chipmunk-like going up and ogre-like
going down.

The **dry path is delayed to match the engine exactly**, so wet and dry stay
phase-aligned as the mix knob moves instead of combing against each other.

### Threading

The DSP is C++ driven by Oboe's callback in exclusive low-latency mode. Kotlin
handles UI, device enumeration and MIDI only. Nothing on the audio thread
allocates, locks or calls into the JVM: every buffer is sized once at its maximum
in `prepare()`, parameters are plain atomics, and MIDI arrives through a
lock-free single-producer queue. Changing window size or sample-rate divisor at
runtime recomputes coefficients into already-allocated buffers.

A JVM audio thread would have been far simpler, and a garbage collection pause in
the middle of a 4 ms callback budget would have ruined it.

---

## Layout

```
app/src/main/cpp/
  dsp/            engine — pure standard C++, shared with the plugin
    Fft           real FFT on a half-length complex transform
    Resampler     polyphase integer decimation and interpolation
    Analysis      STFT, peaks, true frequencies, formant envelope, pitch
    Voices        additive oscillator banks with frame-to-frame partial matching
    Harmonizer    buffering, voice allocation, quality modes, adaptive control
  AudioEngine     Oboe full-duplex, device routing, restart on disconnect
  native-lib      JNI bridge
app/src/main/java/com/dylan/harmonizer/
  NativeBridge, MidiController, AudioDevices, Settings, HarmonizerViewModel
  Updater, InstallResultReceiver     in-app update check, download and install
  ui/             Compose screens, rotary knob, meters
tools/dsptest/    offline validation — see tools/dsptest/README.md

plugin/
  CMakeLists.txt  JUCE build for AU, VST3 and standalone
  Source/         processor, editor, in-plugin updater
  Tests/          headless checks on the wrapper (parameters, MIDI, latency)
  Setup/          the guided installer and updater app
  Shared/         install logic shared by the installer and the in-plugin updater
  packaging/      the install scripts shipped inside each archive
site/             the download page, published to GitHub Pages by CI
```

`dsp/` deliberately has no Android dependency, which is what lets the hard part be
compiled and measured on a desktop. `tools/dsptest` feeds it known signals and
checks the output: intervals accurate to within 0.2 cents across ±1 octave, dry
delay exact to the sample, latency unchanged across quality settings, ten-voice
polyphony with voice stealing. CI runs it before it will build an APK.

---

## Known limitations

* **Foreground only.** There is no background service, so the app stops processing
  when you leave it. The screen is kept awake while it is open.
* **Mono.** Input is summed to mono and the wet signal is centred. Voices are not
  spread across the stereo field.
* **Absolute mode needs a clear pitch.** On breathy, very quiet or heavily
  consonantal input the tracker gives up and the last ratio is held rather than
  snapping to unison mid-phrase. Below roughly `rate / (window / 2)` Hz it cannot
  track at all — use a 2048 window for low voices.
* **The residual is not pitch-shifted.** Breath and consonants come through at
  their original pitch, which is correct for unvoiced sound but means a
  fully-wet signal still carries some of the original's texture.
* **The signing key is public**, because it is committed to this public
  repository so that CI needs no setup. See `keystore/README.md` — rotate to a
  secret-held key before distributing the app to anyone else.
* **The updater installs whatever the release page offers.** It verifies nothing
  beyond the version number, so it is exactly as trustworthy as the repository
  and the signing key are.
