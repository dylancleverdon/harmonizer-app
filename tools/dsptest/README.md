# Offline DSP validation

The harmoniser's signal processing lives in `app/src/main/cpp/dsp/` and depends
on nothing but the C++ standard library. That is deliberate: it means the part
that is actually hard to get right can be compiled and measured on a desktop,
without a phone, an emulator, or the Android SDK.

Build and run:

    clang++ -std=c++17 -O2 -Iapp/src/main/cpp/dsp \
        tools/dsptest/main.cpp app/src/main/cpp/dsp/*.cpp -o dsptest
    ./dsptest

What it checks:

* **Interval accuracy** — a known harmonic signal in, a MIDI note held, and the
  output's dominant frequency measured against what the cents offset from middle
  C says it should be. Checked across +/- one octave in all three quality modes.
* **Dry-path delay compensation** — an impulse through the dry path must come out
  exactly `algorithmicLatencySamples()` later, or the wet and dry signals comb
  against each other as the mix knob moves.
* **Latency stability** — engine latency must not change when the quality
  settings change, otherwise adjusting quality mid-performance shifts timing.
* **Absolute mode** — sing one pitch, play another note, confirm the output lands
  on the note played rather than an interval from it.
* **Chord voicing** — the whole chord is measured at once: the right tones appear
  around the input, the input's own pitch is absent from the wet signal (it is the
  player's to supply), the same shape transposed gives an identical result, minor
  and diminished chords find their altered degrees, anchoring on the 5th or 7th
  places voices below rather than above, and a degree the chord does not contain
  falls back to the root.
* **Polyphony** — ten notes held, an eleventh steals a voice, output stays finite
  and inside full scale.
* **Cost** — each quality mode timed against realtime, so the settings can be
  compared rather than guessed at.

The CI workflow runs this before it will build an APK.
