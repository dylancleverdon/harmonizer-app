// Headless checks on the plugin wrapper itself.
//
// The DSP is already covered by tools/dsptest. What this exercises is the glue
// that only exists in the plugin: host parameters mapping onto engine settings,
// MIDI arriving as raw bytes from a MidiBuffer, channel summing, and the latency
// the host is told to compensate for. A wrong index in a parameter table
// compiles perfectly and silently selects the wrong mode, so it gets asserted
// rather than eyeballed.

#include "PluginProcessor.h"
#include "Fft.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;
constexpr double kPi = 3.14159265358979323846;

void check(bool ok, const juce::String& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (!ok) ++g_failures;
}

void makeVoice(std::vector<float>& x, double f0, double sr) {
    for (size_t n = 0; n < x.size(); ++n) {
        double s = 0.0;
        for (int h = 1; h <= 24; ++h) {
            const double f = f0 * h;
            if (f > sr * 0.45) break;
            s += std::sin(2.0 * kPi * f * static_cast<double>(n) / sr) / h;
        }
        x[n] = static_cast<float>(s * 0.25);
    }
}

double dominantFreq(const float* x, int n, double sr) {
    int N = 1;
    while (N * 2 <= n) N *= 2;
    std::vector<float> buf(static_cast<size_t>(N)), re(static_cast<size_t>(N / 2 + 1)),
        im(static_cast<size_t>(N / 2 + 1));
    for (int i = 0; i < N; ++i) {
        buf[static_cast<size_t>(i)] =
            x[n - N + i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / N));
    }
    dsp::RealFft(N).forward(buf.data(), re.data(), im.data());
    int best = 1;
    double bestMag = 0.0;
    for (int k = 2; k < N / 2 - 1; ++k) {
        const double m = std::hypot(re[static_cast<size_t>(k)], im[static_cast<size_t>(k)]);
        if (m > bestMag) { bestMag = m; best = k; }
    }
    return best * sr / N;
}

double centsErr(double got, double want) {
    if (got <= 0.0 || want <= 0.0) return 1e9;
    return 1200.0 * std::log2(got / want);
}

void setChoice(HarmonizerAudioProcessor& p, const char* id, int index) {
    if (auto* param = p.apvts.getParameter(id)) {
        param->setValueNotifyingHost(
            param->convertTo0to1(static_cast<float>(index)));
    }
}

void setValue(HarmonizerAudioProcessor& p, const char* id, float value) {
    if (auto* param = p.apvts.getParameter(id)) {
        param->setValueNotifyingHost(param->convertTo0to1(value));
    }
}

// Runs `seconds` of a harmonic tone through the plugin with one note held.
std::vector<float> render(HarmonizerAudioProcessor& p, double sr, int blockSize,
                          double f0, int midiNote, int numChannels, double seconds) {
    p.setPlayConfigDetails(numChannels, numChannels, sr, blockSize);
    p.prepareToPlay(sr, blockSize);

    const int total = static_cast<int>(sr * seconds);
    std::vector<float> source(static_cast<size_t>(total));
    makeVoice(source, f0, sr);

    std::vector<float> out(static_cast<size_t>(total), 0.0f);
    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    bool noteSent = false;

    for (int pos = 0; pos < total; pos += blockSize) {
        const int n = juce::jmin(blockSize, total - pos);
        buffer.setSize(numChannels, n, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch) {
            juce::FloatVectorOperations::copy(buffer.getWritePointer(ch),
                                              source.data() + pos, n);
        }

        juce::MidiBuffer midi;
        if (!noteSent && midiNote >= 0) {
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.8f), 0);
            noteSent = true;
        }

        p.processBlock(buffer, midi);
        juce::FloatVectorOperations::copy(out.data() + pos, buffer.getReadPointer(0), n);
    }
    return out;
}

}  // namespace


// The bug this covers: with no side-chain bus declared, Logic had nowhere to
// route audio when the plugin sits in an instrument slot, and the plugin sat
// silent -- dry included, which is what made it look completely broken.
static void testSidechainInput() {
    std::printf("\n-- Audio arriving only on the side chain --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;

    HarmonizerAudioProcessor p;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::disabled());   // as in an instrument slot
    layout.inputBuses.add(juce::AudioChannelSet::mono());       // side chain carries the audio
    layout.outputBuses.add(juce::AudioChannelSet::stereo());

    const bool accepted = p.setBusesLayout(layout);
    check(accepted, "host may disable the main input and feed the side chain instead");
    if (!accepted) return;

    setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
    setChoice(p, HarmonizerAudioProcessor::ParamId::harmonyMode, 0);
    setValue(p, HarmonizerAudioProcessor::ParamId::formant, 0.0f);
    p.prepareToPlay(sr, 256);

    const int total = static_cast<int>(sr * 1.0);
    std::vector<float> source(static_cast<size_t>(total));
    makeVoice(source, f0, sr);
    std::vector<float> out(static_cast<size_t>(total), 0.0f);

    const int channels = juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer(channels, 256);
    bool sent = false;

    for (int pos = 0; pos < total; pos += 256) {
        const int n = juce::jmin(256, total - pos);
        buffer.setSize(channels, n, false, false, true);
        buffer.clear();

        // Write into whichever channel the side-chain bus actually occupies.
        auto side = p.getBusBuffer(buffer, true, 1);
        for (int ch = 0; ch < side.getNumChannels(); ++ch) {
            juce::FloatVectorOperations::copy(side.getWritePointer(ch), source.data() + pos, n);
        }

        juce::MidiBuffer midi;
        if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0); sent = true; }
        p.processBlock(buffer, midi);
        juce::FloatVectorOperations::copy(out.data() + pos, buffer.getReadPointer(0), n);
    }

    const auto t = p.traffic();
    char msg[220];
    std::snprintf(msg, sizeof(msg),
                  "side chain reported as %d channel(s), peak %.3f; main reported %d",
                  t.sideChannels, t.sidePeak, t.mainChannels);
    check(t.sideChannels > 0 && t.sidePeak > 0.01f && t.mainChannels == 0, msg);

    const double want = f0 * std::pow(2.0, 4.0 / 12.0);
    const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    std::snprintf(msg, sizeof(msg),
                  "harmony still produced: peak %.3f, %7.2f Hz (want %7.2f, %+5.1f cents)",
                  peak, got, want, centsErr(got, want));
    check(peak > 0.02f && std::fabs(centsErr(got, want)) < 15.0, msg);

    // Regression: a silent main bus must not attenuate the side chain. Averaging
    // every input channel together rather than each bus separately cost 6 dB in
    // exactly the configuration Logic produces.
    {
        HarmonizerAudioProcessor mainOnly;
        juce::AudioProcessor::BusesLayout direct;
        direct.inputBuses.add(juce::AudioChannelSet::mono());
        direct.inputBuses.add(juce::AudioChannelSet::disabled());
        direct.outputBuses.add(juce::AudioChannelSet::stereo());
        if (mainOnly.setBusesLayout(direct)) {
            setValue(mainOnly, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
            setChoice(mainOnly, HarmonizerAudioProcessor::ParamId::harmonyMode, 0);
            setValue(mainOnly, HarmonizerAudioProcessor::ParamId::formant, 0.0f);
            auto reference = render(mainOnly, sr, 256, f0, 64, 1, 1.0);

            float refPeak = 0.0f;
            for (float v : reference) refPeak = std::max(refPeak, std::fabs(v));
            float sidePeak = 0.0f;
            for (float v : out) sidePeak = std::max(sidePeak, std::fabs(v));

            const double ratioDb = 20.0 * std::log10((sidePeak + 1e-9f) / (refPeak + 1e-9f));
            char m2[220];
            std::snprintf(m2, sizeof(m2),
                          "side chain is as loud as the main bus: %.3f vs %.3f (%+.1f dB)",
                          sidePeak, refPeak, ratioDb);
            check(std::fabs(ratioDb) < 1.5, m2);
        }
    }
}



// The configuration Logic actually produces: a main input bus that exists and is
// enabled but carries silence, alongside a live side chain. Averaging across all
// four channels rather than per bus cost 6 dB here -- and a test that disables
// the main bus entirely would not have noticed.
static void testSilentMainBusDoesNotAttenuate() {
    std::printf("\n-- A silent main bus must not quieten the side chain --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;

    auto runWith = [&](bool useSidechain) -> float {
        HarmonizerAudioProcessor p;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(juce::AudioChannelSet::stereo());   // present either way
        layout.inputBuses.add(useSidechain ? juce::AudioChannelSet::stereo()
                                           : juce::AudioChannelSet::disabled());
        layout.outputBuses.add(juce::AudioChannelSet::stereo());
        if (!p.setBusesLayout(layout)) return -1.0f;

        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setChoice(p, HarmonizerAudioProcessor::ParamId::harmonyMode, 0);
        setValue(p, HarmonizerAudioProcessor::ParamId::formant, 0.0f);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.0);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);

        const int channels = juce::jmax(p.getTotalNumInputChannels(),
                                        p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer(channels, 256);
        bool sent = false;
        float peak = 0.0f;

        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(channels, n, false, false, true);
            buffer.clear();

            // Feed the signal to the side chain when testing that path, leaving
            // the main bus enabled but silent -- as Logic does in an instrument
            // slot. Otherwise feed the main bus, for the reference level.
            auto target = p.getBusBuffer(buffer, true, useSidechain ? 1 : 0);
            for (int ch = 0; ch < target.getNumChannels(); ++ch) {
                juce::FloatVectorOperations::copy(target.getWritePointer(ch),
                                                  source.data() + pos, n);
            }

            juce::MidiBuffer midi;
            if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0); sent = true; }
            p.processBlock(buffer, midi);

            if (pos > total / 3) {
                for (int i = 0; i < n; ++i) {
                    peak = std::max(peak, std::fabs(buffer.getSample(0, i)));
                }
            }
        }
        return peak;
    };

    const float viaMain = runWith(false);
    const float viaSide = runWith(true);
    const double ratioDb = 20.0 * std::log10((viaSide + 1e-9f) / (viaMain + 1e-9f));

    char msg[240];
    std::snprintf(msg, sizeof(msg),
                  "main-bus %.3f vs side-chain-with-silent-main %.3f (%+.1f dB)",
                  viaMain, viaSide, ratioDb);
    check(viaMain > 0.02f && viaSide > 0.02f && std::fabs(ratioDb) < 1.5, msg);
}

// Jazz chord mode is the plugin's own layer: the held key names a key centre,
// the pitch tracker says what is being played, and the chord for that degree is
// fed to the engine as absolute pitches. The dictionary and the voicing are
// covered on their own in Tests/JazzHarness.cpp; what matters here is that the
// two halves are actually connected -- pitch in, chord out, and the host's keys
// no longer taken as a harmony.
static void testJazzChordMode() {
    std::printf("\n-- Jazz chord mode --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;        // A3, MIDI 57

    struct Result {
        dsp::Metrics metrics;
        HarmonizerAudioProcessor::JazzView view;
    };

    // Holds one key for the whole render, and plays a steady tone into it.
    const auto run = [&](bool jazzOn, bool ninths, int rangeLow, int rangeHigh,
                         int keyNote, double seconds) {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, jazzOn ? 1.0f : 0.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzNinth, ninths ? 1.0f : 0.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzRangeLow,
                 static_cast<float>(rangeLow));
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzRangeHigh,
                 static_cast<float>(rangeHigh));

        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * seconds);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);

        juce::AudioBuffer<float> buffer(1, 256);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);

            juce::MidiBuffer midi;
            if (!sent) {
                midi.addEvent(juce::MidiMessage::noteOn(1, keyNote, 0.8f), 0);
                sent = true;
            }
            p.processBlock(buffer, midi);
        }
        return Result{p.metrics(), p.jazzView()};
    };

    // One key: C major. Playing A makes that the sixth degree, which this
    // dictionary harmonises as vim7 -- an A minor seventh.
    {
        const auto r = run(true, false, 48, 84, 60, 1.5);
        const auto& v = r.view;
        char msg[260];
        std::snprintf(msg, sizeof(msg),
                      "C held, A3 played -> key centre %s, chord root %s%s, you are the %s",
                      v.keyCentrePc >= 0 ? jazz::pitchClassName(v.keyCentrePc) : "?",
                      v.chordRootPc >= 0 ? jazz::pitchClassName(v.chordRootPc) : "?",
                      v.roman, jazz::degreeName(v.melodyDegree));
        check(v.enabled && v.sounding && v.keyCentrePc == 0 && !v.minorKey &&
                  v.chordRootPc == 9 && v.melodyNote == 57,
              msg);

        std::snprintf(msg, sizeof(msg), "the engine is sounding the chord: %d voices for %d notes",
                      r.metrics.activeVoices, v.noteCount);
        check(v.noteCount >= 2 && r.metrics.activeVoices >= 2, msg);
    }

    // The same single key, without jazz mode, is one harmony note. That is the
    // difference the mode makes.
    {
        const auto plain = run(false, false, 48, 84, 60, 1.0);
        check(!plain.view.enabled && plain.view.noteCount == 0 &&
                  plain.metrics.activeVoices == 1,
              juce::String("with jazz mode off one key is one voice (") +
                  juce::String(plain.metrics.activeVoices) + ")");
    }

    // Two keys name a minor key. A over an A minor centre is the tonic, im7.
    {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.5);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(1, 256);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent) {
                for (int note : {69, 76}) {         // A and the E above it
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
                }
                sent = true;
            }
            p.processBlock(buffer, midi);
        }
        const auto v = p.jazzView();
        check(v.sounding && v.minorKey && v.keyCentrePc == 9 && v.chordRootPc == 9 &&
                  v.melodyDegree == 1,
              juce::String("A and E held, A3 played -> A minor, chord ") + v.roman);
    }

    // The range is the plugin's promise about where the harmony sits.
    {
        const auto r = run(true, true, 60, 72, 60, 1.5);
        bool inside = r.view.noteCount > 0;
        for (int i = 0; i < r.view.noteCount; ++i) {
            inside &= r.view.notes[i] >= 60 && r.view.notes[i] <= 72;
        }
        juce::String notes;
        for (int i = 0; i < r.view.noteCount; ++i) notes += juce::String(r.view.notes[i]) + " ";
        check(inside, juce::String("a C4-C5 range keeps every voice inside it: ") + notes);
    }

    // A range parked far from what the player is playing cannot be honoured:
    // the engine will not shift a voice more than two octaves, so a chord voiced
    // out there would sound at that limit instead. The chord is brought within
    // reach and the panel reports it, rather than naming notes nobody hears.
    {
        const auto r = run(true, true, 24, 36, 60, 1.5);   // C1-C2, under an A3
        const auto& v = r.view;
        bool reachable = v.noteCount > 0;
        int worst = 0;
        for (int i = 0; i < v.noteCount; ++i) {
            const int d = std::abs(v.notes[i] - v.melodyNote);
            worst = juce::jmax(worst, d);
            reachable &= d <= jazz::kEngineReachSemitones;
        }
        check(reachable && v.rangeLimited,
              juce::String("a range two octaves below the played note is pulled into reach "
                           "(worst voice ") + juce::String(worst) + " semitones away, reported as " +
                  (v.rangeLimited ? "limited)" : "NOT limited)"));
    }

    // Switching the mode off mid-session hands the held key back to the engine
    // rather than leaving it silent until the player lifts and presses again.
    {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        setChoice(p, HarmonizerAudioProcessor::ParamId::harmonyMode, 0);  // fixed interval
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.2);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(1, 256);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); sent = true; }
            if (pos > total / 2) {
                setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 0.0f);
            }
            p.processBlock(buffer, midi);
        }
        const auto m = p.metrics();
        check(m.activeVoices == 1 && !p.jazzView().sounding,
              juce::String("leaving jazz mode with the key still down leaves one voice (") +
                  juce::String(m.activeVoices) + ")");
    }
}

// Keys Transpose and Audio In Transpose do genuinely different things.
// Keys Transpose is real: added to every held key before it names a key
// centre, so holding a familiar key while reading a transposing
// instrument's chart actually changes what key the chord is built in --
// this test confirms holding concert C with Keys Transpose set to Bb (-2)
// really does put the engine in the key of concert Bb, answering "can I
// hold C to play in concert Bb?" with yes. Audio In Transpose can't work
// that way -- the melody note is measured from real sound, so it stays
// real and untouched regardless of the setting; only the editor's display
// formula (written = concert - melodyTransposeSemitones) ever uses it.
static void testJazzTranspose() {
    std::printf("\n-- Jazz transpose --\n");
    const double sr = 48000.0;
    const double f0 = 233.082;   // Bb3, concert

    HarmonizerAudioProcessor p;
    setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzTranspose, -2.0f);          // keys: Bb instrument
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzTransposeAudioIn, -2.0f);   // audio: Bb instrument
    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);

    const int total = static_cast<int>(sr * 1.5);
    std::vector<float> source(static_cast<size_t>(total));
    makeVoice(source, f0, sr);
    juce::AudioBuffer<float> buffer(1, 256);
    bool sent = false;
    for (int pos = 0; pos < total; pos += 256) {
        const int n = juce::jmin(256, total - pos);
        buffer.setSize(1, n, false, false, true);
        juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
        juce::MidiBuffer midi;
        // Concert C on the keys -- with Keys Transpose at Bb, this should
        // really put the key centre in concert Bb, not just label it that.
        if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); sent = true; }
        p.processBlock(buffer, midi);
    }
    const auto v = p.jazzView();
    check(v.sounding && v.keyCentrePc == 10,
          juce::String("holding concert C with Keys Transpose set to Bb (-2) names the real key "
                       "centre ") +
              (v.keyCentrePc >= 0 ? jazz::pitchClassName(v.keyCentrePc) : "?") +
              " -- Keys Transpose must actually change what key the engine builds in, not just "
              "how it's labelled");
    check(v.melodyNote % 12 == 10,
          juce::String("Audio In Transpose must never shift the real, measured melody pitch -- "
                       "got pitch class ") + juce::String(v.melodyNote % 12) + ", expected 10 (Bb)");
    check(v.melodyTransposeSemitones == -2,
          "the published view echoes Audio In Transpose for the editor to display with");

    const int writtenMelodyPc = ((v.melodyNote - v.melodyTransposeSemitones) % 12 + 12) % 12;
    check(writtenMelodyPc == 0,
          juce::String("real melody Bb displayed under Audio In Transpose -2 (Bb) reads as ") +
              jazz::pitchClassName(writtenMelodyPc) + ", expected C");
}

// Latch freezes the key centre against key releases: it only ever updates
// from a fresh press, so lifting one finger of a held minor chord can't be
// misread as "you meant major" mid-release.
static void testJazzLatch() {
    std::printf("\n-- Jazz key latch --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;

    const auto run = [&](bool latch) {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzLatchKeys, latch ? 1.0f : 0.0f);
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.5);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(1, 256);

        int step = 0;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (step == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);        // bottom key
            else if (step == 5) midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), 0);   // + upper key -> minor
            else if (step == 10) midi.addEvent(juce::MidiMessage::noteOff(1, 55), 0);       // release upper
            ++step;
            p.processBlock(buffer, midi);
        }
        return p.jazzView();
    };

    const auto latched = run(true);
    check(latched.sounding && latched.minorKey && latched.keyCentrePc == 0 && latched.keyLatched &&
              latched.heldKeys == 1,
          juce::String("with latch on, releasing the upper key of a minor pair stays minor "
                       "(minor=") +
              (latched.minorKey ? "yes" : "no") + ", latched=" + (latched.keyLatched ? "yes" : "no") +
              ", physically held=" + juce::String(latched.heldKeys) + ")");

    const auto live = run(false);
    check(live.sounding && !live.minorKey && !live.keyLatched,
          juce::String("without latch, the same release reverts to major -- the bug latch exists "
                       "to fix (minor=") +
              (live.minorKey ? "yes" : "no") + ")");
}

// Auto keeps the original one-key-major/two-key-minor rule; Major and Minor
// override it so either quality can come from a single held key.
static void testJazzKeyQuality() {
    std::printf("\n-- Jazz key quality switch --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;

    const auto run = [&](int qualityIndex, bool holdSecondKey) {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        setChoice(p, HarmonizerAudioProcessor::ParamId::jazzKeyQuality, qualityIndex);
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.5);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(1, 256);

        int step = 0;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (step == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);        // C3
            else if (step == 5 && holdSecondKey) {
                midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), 0);                   // + G3
            }
            ++step;
            p.processBlock(buffer, midi);
        }
        return p.jazzView();
    };

    // Auto, one key: today's original rule, unchanged.
    const auto autoOne = run(0, false);
    check(autoOne.sounding && !autoOne.minorKey,
          "Auto with one key held still reads major, exactly as before the switch existed");

    // Auto, two keys: also unchanged.
    const auto autoTwo = run(0, true);
    check(autoTwo.sounding && autoTwo.minorKey,
          "Auto with two keys held still reads minor, exactly as before the switch existed");

    // Minor, one key: the whole point of the switch -- no second finger needed.
    const auto minorOne = run(2, false);
    check(minorOne.sounding && minorOne.minorKey && minorOne.heldKeys == 1,
          juce::String("Minor forces minor off a single held key (minor=") +
              (minorOne.minorKey ? "yes" : "no") +
              ", held=" + juce::String(minorOne.heldKeys) + ")");

    // Major, two keys: forced major even though Auto would have read this as minor.
    const auto majorTwo = run(1, true);
    check(majorTwo.sounding && !majorTwo.minorKey && majorTwo.heldKeys == 2,
          juce::String("Major forces major even with a second key down (minor=") +
              (majorTwo.minorKey ? "yes" : "no") +
              ", held=" + juce::String(majorTwo.heldKeys) + ")");
}

// The sustain pedal (CC64) freezes the currently sounding chord -- even as
// the melody note moves on -- and stands in for latch while held, so the
// key centre survives every keyboard key being released too.
static void testJazzSustain() {
    std::printf("\n-- Jazz sustain pedal --\n");
    const double sr = 48000.0;

    HarmonizerAudioProcessor p;
    setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);

    juce::AudioBuffer<float> buffer(1, 256);
    const auto renderTone = [&](double f0, double seconds, const juce::MidiMessage* firstEvent) {
        const int total = static_cast<int>(sr * seconds);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent && firstEvent != nullptr) { midi.addEvent(*firstEvent, 0); sent = true; }
            p.processBlock(buffer, midi);
        }
    };

    auto keyOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
    renderTone(220.0, 1.5, &keyOn);   // hold C, play A3 -- let a chord settle
    const auto before = p.jazzView();
    check(before.sounding, "a chord settles before the pedal is touched");

    auto sustainDown = juce::MidiMessage::controllerEvent(1, 64, 127);
    renderTone(220.0, 0.1, &sustainDown);
    renderTone(330.0, 1.5, nullptr);   // switch to E4 -- a different degree entirely
    const auto frozen = p.jazzView();
    check(frozen.sustainHeld && frozen.chordRootPc == before.chordRootPc &&
              frozen.melodyDegree == before.melodyDegree,
          "the chord holds through a melody change while the pedal is down");

    auto sustainUp = juce::MidiMessage::controllerEvent(1, 64, 0);
    renderTone(330.0, 0.1, &sustainUp);
    renderTone(330.0, 1.5, nullptr);
    const auto released = p.jazzView();
    check(!released.sustainHeld && released.melodyNote == 64,
          juce::String("releasing the pedal lets the chord follow the new melody note again "
                       "(got melody note ") +
              juce::String(released.melodyNote) + ")");

    auto sustainDown2 = juce::MidiMessage::controllerEvent(1, 64, 127);
    renderTone(330.0, 0.1, &sustainDown2);
    auto keyOff = juce::MidiMessage::noteOff(1, 60);
    renderTone(330.0, 0.1, &keyOff);   // release the keyboard key while the pedal is down
    renderTone(330.0, 1.5, nullptr);
    const auto stillNamed = p.jazzView();
    check(stillNamed.sounding && stillNamed.heldKeys == 0 && stillNamed.keyLatched,
          "the pedal keeps the key centre alive even after every keyboard key is released");
}

// Glide's voice matching (retarget in place, converge when the chord
// shrinks, split when it grows) lives entirely in jazzApply() -- dsptest
// covers the engine primitives it calls (retargetVoiceNote,
// spawnVoiceFromNote) directly; this covers the matching algorithm end to
// end through the full plugin, using a custom dictionary to pin the exact
// voice count of each chord.
static void testJazzGlide() {
    std::printf("\n-- Jazz glide --\n");
    const double sr = 48000.0;

    HarmonizerAudioProcessor p;
    setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzGlideMs, 300.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzVoicesAuto, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzCustomOn, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzCustomUseMajor, 1.0f);
    // Degree 0 (the root) gets a four-note voicing; degree 4 (the third)
    // gets a two-note voicing -- switching between them changes the voice
    // count, not just the pitches. Cleared first -- every degree starts out
    // seeded with a built-in-equivalent voicing, not blank.
    p.clearJazzCustomVoicing(false, 0);
    p.clearJazzCustomVoicing(false, 4);
    for (int offset : {4, 7, 11, 14}) p.setJazzCustomVoicingNote(false, 0, offset, true);
    for (int offset : {3, 7}) p.setJazzCustomVoicingNote(false, 4, offset, true);

    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);

    juce::AudioBuffer<float> buffer(1, 256);
    const auto renderTone = [&](double f0, double seconds, const juce::MidiMessage* firstEvent) {
        const int total = static_cast<int>(sr * seconds);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent && firstEvent != nullptr) { midi.addEvent(*firstEvent, 0); sent = true; }
            p.processBlock(buffer, midi);
        }
    };

    auto keyOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
    renderTone(261.63, 1.5, &keyOn);   // C held, sing C4 -- degree 0, the four-note chord
    check(p.metrics().activeVoices == 4,
          juce::String("the four-note custom voicing settles to 4 active voices (got ") +
              juce::String(p.metrics().activeVoices) + ")");

    // Switch to E4 -- degree 4, the two-note voicing. Glide is on, so the
    // two excess voices converge onto the two remaining tones instead of
    // being cut: nothing is released, so the voice count does not drop.
    renderTone(329.63, 1.5, nullptr);
    check(p.metrics().activeVoices == 4,
          juce::String("with glide, shrinking to 2 voices converges rather than cutting -- "
                       "still 4 active (got ") +
              juce::String(p.metrics().activeVoices) + ")");

    // Back to C4 -- degree 0, the four-note voicing again. The two
    // converged pairs are reused as the retarget targets, so this still
    // does not need to spawn anything new.
    renderTone(261.63, 1.5, nullptr);
    check(p.metrics().activeVoices == 4,
          juce::String("back to 4 voices reuses the converged pair rather than doubling up "
                       "further (got ") +
              juce::String(p.metrics().activeVoices) + ")");

    // With glide off, the same shrink is an ordinary note-off/note-on diff:
    // the excess voices are actually released.
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzGlideMs, 0.0f);
    renderTone(329.63, 1.5, nullptr);
    check(p.metrics().activeVoices == 2,
          juce::String("without glide, shrinking to 2 voices actually releases the excess "
                       "(got ") +
              juce::String(p.metrics().activeVoices) + ")");

    // And growing back from there needs a genuine split, since there is no
    // spare converged voice left over to reuse this time.
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzGlideMs, 300.0f);
    renderTone(261.63, 1.5, nullptr);
    check(p.metrics().activeVoices == 4,
          juce::String("growing from 2 voices to 4 splits to cover the new ones (got ") +
              juce::String(p.metrics().activeVoices) + ")");
}

// Vibrato that wobbles right across the tempered boundary between two notes
// should never flicker the chord: once jazzUpdate() locks a note in, it takes
// a wider swing to be read as having left than it took to arrive, so a wobble
// that stays inside that dead zone keeps reading as the same note. This
// synthesises exactly that -- a tone centred on the boundary between two
// notes, with vibrato depth comfortably inside the dead zone but well past
// the plain rounding line -- and checks the published chord never changes
// once it has first settled.
static void testJazzChordStability() {
    std::printf("\n-- Jazz chord stability (vibrato) --\n");
    const double sr = 48000.0;

    HarmonizerAudioProcessor p;
    setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);

    // First half a second dead on A#3 (58, 233.08 Hz) -- plain enough to lock
    // in well within the default hold time. The second second adds vibrato
    // deep enough that the raw pitch actually crosses the 50-cent rounding
    // line into A3's territory and back (what would flicker the chord
    // without hysteresis), while never moving more than 55 cents from the
    // note that already locked in -- comfortably inside the wider dead zone
    // hysteresis gives it.
    const double centreMidi = 58.0;
    const double settleSeconds = 0.5;
    const double depthCents = 55.0;
    const double rateHz = 6.0;
    const int total = static_cast<int>(sr * 1.5);
    const int settleSamples = static_cast<int>(sr * settleSeconds);
    std::vector<float> source(static_cast<size_t>(total));
    double phase = 0.0;
    for (int n = 0; n < total; ++n) {
        const double vibrato =
            n < settleSamples ? 0.0 : depthCents * std::sin(2.0 * kPi * rateHz * (n - settleSamples) / sr);
        const double midi = centreMidi + vibrato / 100.0;
        const double f0 = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        double s = 0.0;
        for (int h = 1; h <= 24; ++h) {
            if (f0 * h > sr * 0.45) break;
            s += std::sin(phase * h) / h;
        }
        source[static_cast<size_t>(n)] = static_cast<float>(s * 0.25);
        phase += 2.0 * kPi * f0 / sr;
    }

    juce::AudioBuffer<float> buffer(1, 256);
    auto keyOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
    bool sent = false;
    int firstRoot = -1, firstMelodyNote = -1;
    bool anySounding = false, stable = true;
    for (int pos = 0; pos < total; pos += 256) {
        const int n = juce::jmin(256, total - pos);
        buffer.setSize(1, n, false, false, true);
        juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
        juce::MidiBuffer midi;
        if (!sent) { midi.addEvent(keyOn, 0); sent = true; }
        p.processBlock(buffer, midi);

        const auto v = p.jazzView();
        if (!v.sounding) continue;
        anySounding = true;
        if (firstRoot < 0) {
            firstRoot = v.chordRootPc;
            firstMelodyNote = v.melodyNote;
        } else if (v.chordRootPc != firstRoot || v.melodyNote != firstMelodyNote) {
            stable = false;
        }
    }

    check(anySounding, "the chord settles at some point during the vibrato");
    check(stable, "a semitone-straddling vibrato never flickers the chord once it settles");
}

// jazzChordHoldMs is the user-facing knob on how long a reading has to hold
// before the chord follows it. This checks it is actually wired up: a much
// longer hold measurably delays when a clean, unwavering tone first settles
// into a chord, relative to a much shorter one.
static void testJazzChordHoldTiming() {
    std::printf("\n-- Jazz chord hold timing --\n");
    const double sr = 48000.0;

    const auto settleBlocks = [&](float holdMs) -> int {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzChordHoldMs, holdMs);
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.5);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, 261.63, sr);   // C4, steady -- no vibrato in this one

        juce::AudioBuffer<float> buffer(1, 256);
        auto keyOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
        bool sent = false;
        int blocks = 0;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent) { midi.addEvent(keyOn, 0); sent = true; }
            p.processBlock(buffer, midi);
            ++blocks;
            if (p.jazzView().sounding) return blocks;
        }
        return -1;   // never settled
    };

    const int shortHold = settleBlocks(5.0f);
    const int longHold = settleBlocks(250.0f);
    check(shortHold > 0 && longHold > 0, "the chord settles under both a short and a long hold time");
    check(longHold > shortHold,
          juce::String("a longer Chord Hold measurably delays when the chord settles (short=") +
              juce::String(shortHold) + " blocks, long=" + juce::String(longHold) + " blocks)");
    // The two hold times differ by 245 ms; a generous margin below that
    // (rather than pinning the exact figure) is enough to prove the
    // parameter drives the delay without coupling the test to the detector's
    // own warm-up time.
    const double blockMs = 256.0 / sr * 1000.0;
    check((longHold - shortHold) * blockMs > 150.0,
          "the extra delay roughly tracks the 245 ms difference in hold time");
}

// The sustain pedal freezes the *chord decision* (see testJazzSustain above),
// but every voice is really a retuned copy of the live input's spectrum, so
// that alone would not stop the actual audio from fading out the instant the
// player goes quiet. This checks the engine-level freeze in
// Harmonizer::runHop() that keeps resynthesising the last real analysis
// instead of a near-silent one while the pedal is held.
static void testJazzSustainFreeze() {
    std::printf("\n-- Jazz sustain pedal holds the audio out --\n");
    const double sr = 48000.0;

    // Plays a tone (or silence, if f0 <= 0) for `seconds`, feeding `event` on
    // the first block if given, and returns the peak of the last 100 ms of
    // output -- long enough after any transient to show what is actually
    // ringing on, not a decay tail from what just stopped.
    const auto run = [&](bool sustainHeld) {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        juce::AudioBuffer<float> buffer(1, 256);
        float lastWindowPeak = 0.0f;

        const auto renderSeconds = [&](double f0, double seconds, const juce::MidiMessage* first) {
            const int total = static_cast<int>(sr * seconds);
            std::vector<float> source(static_cast<size_t>(total));
            if (f0 > 0.0) makeVoice(source, f0, sr);   // else stays zeroed: silence
            bool sent = false;
            const int windowStart = total - static_cast<int>(sr * 0.1);
            for (int pos = 0; pos < total; pos += 256) {
                const int n = juce::jmin(256, total - pos);
                buffer.setSize(1, n, false, false, true);
                juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
                juce::MidiBuffer midi;
                if (!sent && first != nullptr) { midi.addEvent(*first, 0); sent = true; }
                p.processBlock(buffer, midi);
                if (pos + n > windowStart) {
                    for (int i = juce::jmax(0, windowStart - pos); i < n; ++i) {
                        lastWindowPeak = juce::jmax(lastWindowPeak, std::fabs(buffer.getSample(0, i)));
                    }
                }
            }
        };

        auto keyOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
        renderSeconds(261.63, 0.6, &keyOn);   // hold C, play C4 -- let the chord settle
        if (!p.jazzView().sounding) return -1.0f;   // setup failed; let the check below say so

        if (sustainHeld) {
            auto sustainDown = juce::MidiMessage::controllerEvent(1, 64, 127);
            renderSeconds(261.63, 0.05, &sustainDown);
        }
        lastWindowPeak = 0.0f;   // only the final silent stretch counts from here
        renderSeconds(-1.0, 1.0, nullptr);   // go quiet
        return lastWindowPeak;
    };

    const float held = run(true);
    const float notHeld = run(false);
    check(held >= 0.0f && notHeld >= 0.0f, "the chord settles before either run goes quiet");
    check(notHeld < 0.01f,
          juce::String("without the pedal, the chord fades out with the input (peak ") +
              juce::String(notHeld, 4) + ")");
    check(held > notHeld * 3.0f && held > 0.01f,
          juce::String("with the pedal down, the chord is still audibly ringing a second into "
                       "silence (held peak ") +
              juce::String(held, 4) + " vs not held " + juce::String(notHeld, 4) + ")");
}

// These three exercise jazz::Voicer directly rather than through the full
// plugin -- there is no audio or MIDI timing involved in any of them, just
// integer chord math, so going straight at the voicer is both more precise
// and much faster than rendering audio to get the same answer.

static void testJazzBassNote() {
    std::printf("\n-- Bass note --\n");

    jazz::Settings s;
    s.rangeLow = 48;
    s.rangeHigh = 72;   // C3-C5

    jazz::Voicer voicer;
    const int keys[1] = {60};   // C major key centre
    const int melodyNote = 60;  // played the root -- Imaj7

    jazz::Voicing without;
    check(voicer.update(keys, 1, melodyNote, s, without), "the chord voices without a bass note");

    s.addBassNote = true;
    voicer.reset();
    jazz::Voicing with;
    check(voicer.update(keys, 1, melodyNote, s, with), "the chord voices with a bass note added");

    check(with.count == without.count + 1,
          juce::String("adding a bass note adds exactly one voice (") + juce::String(without.count) +
              " -> " + juce::String(with.count) + ")");

    const int bass = with.notes[0];
    bool restMatches = with.count - 1 == without.count;
    for (int i = 1; i < with.count && restMatches; ++i) restMatches &= with.notes[i] == without.notes[i - 1];
    check(restMatches, "the bass note is the new lowest voice; the rest of the chord is unchanged");

    check(bass % 12 == ((without.chordRootPc % 12) + 12) % 12,
          "the bass note is the chord's root, same pitch class as the chord root");
    check(bass <= without.notes[0] - 12,
          juce::String("the bass note sits a clear octave under the rest of the chord (") +
              juce::String(bass) + " vs " + juce::String(without.notes[0]) + ")");
}

static void testJazzMudAvoidance() {
    std::printf("\n-- Mud avoidance --\n");

    // A chord with an 11th, folded into a narrow, low range: the 11th
    // naturally wants to land a step away from a tone below it once
    // everything is squeezed into one tight, low register.
    jazz::Settings s;
    s.eleventh = true;
    s.rangeLow = 36;
    s.rangeHigh = 48;
    s.mudCeiling = 48;   // the whole window counts, for this check

    const auto minGapBelowCeiling = [](const jazz::Voicing& v, int ceiling) {
        int best = 128;
        for (int i = 1; i < v.count; ++i) {
            if (v.notes[i] >= ceiling) continue;
            best = juce::jmin(best, v.notes[i] - v.notes[i - 1]);
        }
        return best;
    };

    const int keys[1] = {60};
    jazz::Voicer voicer;
    jazz::Voicing off;
    check(voicer.update(keys, 1, 60, s, off), "the chord voices with mud avoidance off");
    const int gapOff = minGapBelowCeiling(off, s.mudCeiling);

    s.avoidMud = true;
    voicer.reset();
    jazz::Voicing on;
    check(voicer.update(keys, 1, 60, s, on), "the chord voices with mud avoidance on");
    const int gapOn = minGapBelowCeiling(on, s.mudCeiling);

    check(gapOff <= 1,
          juce::String("without it, this chord actually does pack two tones a step apart (gap ") +
              juce::String(gapOff) + ")");
    check(gapOn > gapOff,
          juce::String("with it on, the tightest gap widens instead (off ") + juce::String(gapOff) +
              " -> on " + juce::String(gapOn) + ")");
}

static void testJazzLockedCustomRegister() {
    std::printf("\n-- Locked custom voicing register --\n");

    // A custom root-degree voicing spanning a couple of octaves, the same
    // shape the README's own example uses: a bass note under the root, plus
    // a third and a fifth above it.
    jazz::CustomEntry entry;
    entry.offsets[0] = -24;
    entry.offsets[1] = 0;
    entry.offsets[2] = 4;
    entry.offsets[3] = 7;
    entry.count = 4;

    jazz::Settings s;
    s.rangeLow = 48;
    s.rangeHigh = 72;   // target register: the octave around C3-C5's middle, 60
    s.smoothness = 1.0f;   // lean as hard as possible into voice leading
    s.useCustomDictionary = true;
    s.customDict.useMajor = true;
    s.customDict.major[0] = entry;

    const int keys[1] = {60};

    // Voice a chord far up in a different register first, purely to give
    // voice leading something distant to pull the next chord toward.
    const auto seedFarAway = [&](jazz::Voicer& voicer) {
        jazz::Voicing distant;
        voicer.update(keys, 1, 91, s, distant);   // G6 -- the seventh, two octaves up
    };

    jazz::Voicer voicer;
    seedFarAway(voicer);
    jazz::Voicing unlocked;
    check(voicer.update(keys, 1, 60, s, unlocked),
          "the custom root voicing settles, unlocked, after a distant previous chord");

    s.customVoicingFixedRegister = true;
    jazz::Voicer voicer2;
    seedFarAway(voicer2);
    jazz::Voicing locked;
    check(voicer2.update(keys, 1, 60, s, locked),
          "the same voicing settles, locked, after the same distant previous chord");

    // Locked always lands at the one octave nearest the range's own middle,
    // regardless of what came before -- so its root sits close to 60
    // (rangeLow/rangeHigh's midpoint) every time.
    const int lockedRoot = locked.notes[1];   // offset 0 is the second-lowest slot (bass is offset -24)
    check(std::abs(lockedRoot - 60) <= 6,
          juce::String("locked, the root lands near the range's own middle regardless of lead-in "
                       "(root ") +
              juce::String(lockedRoot) + ")");

    const int unlockedRoot = unlocked.notes[1];
    check(unlockedRoot != lockedRoot,
          juce::String("unlocked, a strongly weighted previous chord actually pulls the register "
                       "somewhere else (unlocked root ") +
              juce::String(unlockedRoot) + " vs locked " + juce::String(lockedRoot) + ")");
}

// The chord library's "preview" button has nothing to harmonise unless the
// plugin feeds the engine something itself -- checks previewJazzVoicing()'s
// synthetic tone actually produces audible output, on its own, with no host
// MIDI or audio input at all, and that it stops again on its own.
static void testJazzLibraryPreview() {
    std::printf("\n-- Chord library preview --\n");
    const double sr = 48000.0;

    HarmonizerAudioProcessor p;
    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);
    // Jazz mode deliberately left off, and wetDry left at its default --
    // previewJazzVoicing() is documented to depend on neither.

    juce::Array<int> notes{64, 67, 71};   // a triad, arbitrary
    p.previewJazzVoicing(notes, 60);

    juce::AudioBuffer<float> buffer(1, 256);
    juce::MidiBuffer noMidi;
    float duringPeak = 0.0f;
    const int duringBlocks = static_cast<int>(sr * 0.3) / 256;   // well inside the ~0.6s preview
    for (int i = 0; i < duringBlocks; ++i) {
        buffer.clear();
        p.processBlock(buffer, noMidi);
        duringPeak = juce::jmax(duringPeak, buffer.getMagnitude(0, 0, buffer.getNumSamples()));
    }
    check(duringPeak > 0.01f,
          juce::String("the preview is audible with no host input at all (peak ") +
              juce::String(duringPeak, 4) + ")");

    // Render well past the end of the ~0.6s preview (0.3s already elapsed
    // above), then only look at the last 100 ms -- the stretch in between is
    // legitimately still the tail end of the same preview sounding.
    float afterPeak = 0.0f;
    const int afterBlocks = static_cast<int>(sr * 1.0) / 256;
    const int lastWindowBlocks = static_cast<int>(sr * 0.1) / 256;
    for (int i = 0; i < afterBlocks; ++i) {
        buffer.clear();
        p.processBlock(buffer, noMidi);
        if (i >= afterBlocks - lastWindowBlocks) {
            afterPeak = juce::jmax(afterPeak, buffer.getMagnitude(0, 0, buffer.getNumSamples()));
        }
    }
    check(afterPeak < 0.01f,
          juce::String("and stops again on its own once the preview window ends (peak ") +
              juce::String(afterPeak, 4) + ")");
}

// Builds a tiny on-disk MIDI file -- the same eight-bar Cmaj7/Dm7 then
// Gmaj7/Am7 performance JazzHarness.cpp's own MIDI import check uses, known
// to produce two key segments and a clean maj7 tonic -- for the staged
// import and MIDI-candidate tests below, which need a real file to read.
static juce::File writeTestMidiFile() {
    constexpr int tpq = 480;
    const double bar = tpq * 4.0;
    juce::MidiMessageSequence seq;
    const auto addChord = [&](int startBar, std::initializer_list<int> pitches) {
        for (int p : pitches) {
            seq.addEvent(juce::MidiMessage::noteOn(1, p, 0.8f), startBar * bar);
            seq.addEvent(juce::MidiMessage::noteOff(1, p), startBar * bar + bar);
        }
    };
    addChord(0, {60, 64, 67, 71});   // Cmaj7
    addChord(1, {62, 65, 69, 72});   // Dm7
    addChord(2, {60, 64, 67, 71});
    addChord(3, {62, 65, 69, 72});
    addChord(4, {67, 71, 74, 78});   // Gmaj7
    addChord(5, {69, 72, 76, 79});   // Am7
    addChord(6, {67, 71, 74, 78});
    addChord(7, {69, 72, 76, 79});
    seq.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(tpq);
    midiFile.addTrack(seq);

    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("harmonizer_test_import.mid");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    midiFile.writeTo(stream);
    return file;
}

// Analysis has to be inert on its own -- it's only "the last analyzed
// file" until useJazzPendingMidiImport() or saveJazzPendingMidiImportAsPreset()
// explicitly says what to do with it.
static void testJazzMidiImportStaging() {
    std::printf("\n-- Staged MIDI import --\n");
    const auto file = writeTestMidiFile();

    HarmonizerAudioProcessor p;
    p.setPlayConfigDetails(1, 1, 48000.0, 256);
    p.prepareToPlay(48000.0, 256);

    // A known voicing in the live dictionary, to prove analysis alone can't
    // touch it. Degree 2 starts pre-seeded (every degree does, with a
    // plausible built-in-equivalent voicing) rather than blank, so clear it
    // first to get a known starting point.
    p.clearJazzCustomVoicing(false, 2);
    p.setJazzCustomVoicingNote(false, 2, 3, true);
    const auto before = p.jazzCustomEntry(false, 2);
    check(before.count == 1, "seeded a note in the live dictionary before importing");

    const auto summary = p.importJazzCustomDictionaryFromMidiFile(file);
    check(summary.ok && summary.keySegments == 2, "the analysis itself succeeds");

    const auto stillBefore = p.jazzCustomEntry(false, 2);
    check(stillBefore.count == before.count && stillBefore.offsets[0] == before.offsets[0],
          "analysis alone never touches the live dictionary");
    check(p.jazzPendingMidiImportCandidateCount() > 0, "the analysis surfaces candidates");

    check(p.useJazzPendingMidiImport(), "useJazzPendingMidiImport() applies the staged result");
    const auto tonic = p.jazzCustomEntry(false, 0);
    bool has3 = false, has7 = false;
    for (int i = 0; i < tonic.count; ++i) {
        if (tonic.offsets[i] == 4) has3 = true;
        if (tonic.offsets[i] == 11) has7 = true;
    }
    check(tonic.count == 3 && has3 && has7, "applying it writes the analyzed tonic chord in");

    // Saving as a preset from a second processor -- proves the preset path
    // is equally inert on the live dictionary. Every degree starts
    // pre-seeded rather than blank, so this compares against a snapshot
    // taken first rather than assuming an empty entry.
    HarmonizerAudioProcessor p2;
    const auto beforeSave = p2.jazzCustomEntry(false, 0);
    check(p2.importJazzCustomDictionaryFromMidiFile(file).ok,
          "a second, fresh processor can analyse the same file");
    check(p2.saveJazzPendingMidiImportAsPreset("__test_midi_preset__"),
          "the staged result saves as a preset");
    const auto afterSave = p2.jazzCustomEntry(false, 0);
    bool degreeUnchanged = afterSave.count == beforeSave.count;
    for (int i = 0; degreeUnchanged && i < afterSave.count; ++i) {
        degreeUnchanged = afterSave.offsets[i] == beforeSave.offsets[i];
    }
    check(degreeUnchanged, "saving as a preset still never touches the live dictionary");
    check(p2.loadJazzDictionaryPreset("__test_midi_preset__"), "the saved preset loads back");
    check(p2.jazzCustomEntry(false, 0).count == 3, "the loaded preset has the analyzed tonic chord");
    p2.deleteJazzDictionaryPreset("__test_midi_preset__");

    HarmonizerAudioProcessor fresh;
    check(!fresh.useJazzPendingMidiImport(),
          "a fresh processor with no analysis yet has nothing to apply");
    check(!fresh.saveJazzPendingMidiImportAsPreset("__should_not_exist__"),
          "and nothing to save as a preset either");
}

// The personal library round-trips through disk the same way a dictionary
// preset does, and enforces the one rule the UI is supposed to enforce too:
// no entry without an artist.
static void testJazzUserLibrary() {
    std::printf("\n-- Your library --\n");
    HarmonizerAudioProcessor p;

    HarmonizerAudioProcessor::UserLibraryEntry entry;
    entry.name = "__test_entry__";
    entry.description = "A test chord.";
    entry.artist = "";   // blank on purpose
    entry.song = "Some Song";
    entry.theme = jazz::LibraryTheme::Gospel;
    entry.quality = jazz::LibraryQuality::Maj7;
    entry.count = 3;
    entry.offsets[0] = 0;
    entry.offsets[1] = 4;
    entry.offsets[2] = 7;
    check(!p.saveJazzUserLibraryEntry(entry), "saving without an artist is refused");

    entry.artist = "Test Artist";
    check(p.saveJazzUserLibraryEntry(entry), "saving with an artist and a real voicing succeeds");

    const auto entries = p.jazzUserLibraryEntries();
    bool found = false;
    for (const auto& e : entries) {
        if (e.name != entry.name) continue;
        found = e.artist == entry.artist && e.song == entry.song && e.count == 3 &&
                e.offsets[0] == 0 && e.offsets[1] == 4 && e.offsets[2] == 7 &&
                e.theme == jazz::LibraryTheme::Gospel && e.quality == jazz::LibraryQuality::Maj7;
        break;
    }
    check(found, "the saved entry shows up with everything intact");

    check(p.deleteJazzUserLibraryEntry(entry.name), "the entry deletes");
    bool stillThere = false;
    for (const auto& e : p.jazzUserLibraryEntries()) stillThere |= (e.name == entry.name);
    check(!stillThere, "and is gone from the list");
}

// A MIDI candidate previews and saves to the library the same way a chord
// library entry does -- same preview mechanism, same required-attribution
// rule on the save.
static void testJazzMidiCandidatePreviewAndSave() {
    std::printf("\n-- MIDI candidate preview and save --\n");
    const double sr = 48000.0;
    const auto file = writeTestMidiFile();

    HarmonizerAudioProcessor p;
    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);
    check(p.importJazzCustomDictionaryFromMidiFile(file).ok, "the file analyses");
    const int candidateCount = p.jazzPendingMidiImportCandidateCount();
    check(candidateCount > 0, "there is at least one candidate to preview and save");
    if (candidateCount == 0) return;

    p.previewJazzMidiCandidate(0);
    juce::AudioBuffer<float> buffer(1, 256);
    juce::MidiBuffer noMidi;
    float peak = 0.0f;
    const int blocks = static_cast<int>(sr * 0.3) / 256;
    for (int i = 0; i < blocks; ++i) {
        buffer.clear();
        p.processBlock(buffer, noMidi);
        peak = juce::jmax(peak, buffer.getMagnitude(0, 0, buffer.getNumSamples()));
    }
    check(peak > 0.01f, juce::String("previewing a candidate is audible (peak ") +
                            juce::String(peak, 4) + ")");

    check(!p.saveJazzMidiCandidateToLibrary(0, "Test Candidate", "", "", jazz::LibraryTheme::Gospel,
                                            jazz::LibraryQuality::Maj7),
          "saving a candidate without an artist is refused");
    check(!p.saveJazzMidiCandidateToLibrary(-1, "Test Candidate", "Test Artist", "", jazz::LibraryTheme::Gospel,
                                            jazz::LibraryQuality::Maj7),
          "saving an out-of-range candidate index is refused");
    check(p.saveJazzMidiCandidateToLibrary(0, "Test Candidate", "Test Artist", "Test Song",
                                           jazz::LibraryTheme::Gospel, jazz::LibraryQuality::Maj7),
          "saving a real candidate with an artist succeeds");

    bool found = false;
    for (const auto& e : p.jazzUserLibraryEntries()) found |= (e.name == "Test Candidate");
    check(found, "the saved candidate shows up in Your library");
    p.deleteJazzUserLibraryEntry("Test Candidate");
}

// Factory presets are compiled in rather than saved on disk, but loading one
// has to have exactly the same effect a saved preset load does: replace the
// live custom dictionary and switch it on.
static void testJazzFactoryPresets() {
    std::printf("\n-- Factory chord dictionaries --\n");
    HarmonizerAudioProcessor p;

    const int count = p.jazzFactoryDictionaryPresetCount();
    check(count == 14, juce::String("there are fourteen factory presets (got ") + juce::String(count) + ")");
    for (int i = 0; i < count; ++i) {
        check(p.jazzFactoryDictionaryPresetName(i).isNotEmpty() &&
                  p.jazzFactoryDictionaryPresetDescription(i).isNotEmpty(),
              juce::String("preset ") + juce::String(i) + " has a name and a description");
    }

    check(!p.loadJazzFactoryDictionaryPreset(-1), "loading an out-of-range index fails");
    check(!p.loadJazzFactoryDictionaryPreset(count), "loading past the end fails");

    // Barry Harris: degree 0 (I) is a plain major 6th chord, root-3-5-6.
    check(p.loadJazzFactoryDictionaryPreset(0), "Barry Harris loads");
    check(*p.apvts.getRawParameterValue(HarmonizerAudioProcessor::ParamId::jazzCustomOn) > 0.5f,
          "loading a factory preset switches the custom dictionary on");
    {
        const auto entry = p.jazzCustomEntry(false, 0);
        const bool matches = entry.count == 4 && entry.offsets[0] == 0 && entry.offsets[1] == 4 &&
                             entry.offsets[2] == 7 && entry.offsets[3] == 9;
        check(matches, "Barry Harris' major I chord is a root-3-5-6 (major 6th) voicing");
    }

    // Loading a second preset has to fully replace the first, not merge with
    // it -- Glasper's major I is a lydian maj9#11, nothing like a 6th chord.
    check(p.loadJazzFactoryDictionaryPreset(4), "Robert Glasper (neo-soul) loads");
    {
        const auto major0 = p.jazzCustomEntry(false, 0);
        const bool matchesMajor = major0.count == 5 && major0.offsets[0] == 0 &&
                                  major0.offsets[1] == 4 && major0.offsets[2] == 11 &&
                                  major0.offsets[3] == 14 && major0.offsets[4] == 18;
        check(matchesMajor,
              "loading a second preset replaces the first -- Glasper's major I is a maj9#11");

        const auto minor7 = p.jazzCustomEntry(true, 7);
        const bool matchesMinor = minor7.count == 4 && minor7.offsets[0] == 0 &&
                                  minor7.offsets[1] == 4 && minor7.offsets[2] == 10 &&
                                  minor7.offsets[3] == 13;
        check(matchesMinor, "Glasper's minor v is a cadential 7b9");
    }
}

// Auto harmony voices ignores the Chord Voices slider entirely and lets
// through exactly as many notes as the chord naturally has -- extensions
// included -- rather than the number picked ahead of time.
static void testJazzAutoVoices() {
    std::printf("\n-- Auto harmony voices --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;   // A3, MIDI 57 -- degree 9 (the sixth) above a held C

    const auto run = [&](bool autoVoices) {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzNinth, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzEleventh, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzThirteenth, 1.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzVoices, 2.0f);   // a tight cap...
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzVoicesAuto, autoVoices ? 1.0f : 0.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzRangeLow, 24.0f);
        setValue(p, HarmonizerAudioProcessor::ParamId::jazzRangeHigh, 108.0f);

        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 1.5);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(1, 256);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); sent = true; }
            p.processBlock(buffer, midi);
        }
        return p.jazzView();
    };

    const auto capped = run(false);
    check(capped.sounding && capped.noteCount <= 2,
          juce::String("without auto, a tight cap holds (got ") + juce::String(capped.noteCount) +
              " voices)");

    const auto autoResult = run(true);
    check(autoResult.sounding && autoResult.noteCount > 2,
          juce::String("with auto on, the same tight cap is ignored and the whole extended chord "
                       "plays (got ") +
              juce::String(autoResult.noteCount) + " voices)");
}

// The custom chord dictionary is a plugin-only layer over jazz mode's own
// chords: off by default (jazz mode is unchanged), and switched on it hands
// the chosen chord type straight through the engine the same way the
// built-in dictionary always has. Presets are a small file-backed library on
// top of that, independent of host session state.
static void testJazzCustomDictionary() {
    std::printf("\n-- Jazz custom chord dictionary --\n");
    const double sr = 48000.0;
    const double f0 = 220.0;   // A3, MIDI 57

    // Holds C (60) for the whole render and plays A3 (57) into the input --
    // scale degree 9 above C, the sixth.
    HarmonizerAudioProcessor p;
    setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzCustomOn, 1.0f);
    setValue(p, HarmonizerAudioProcessor::ParamId::jazzCustomUseMajor, 1.0f);
    // Degree 9 (the sixth) gets a Min7b5-quality voicing: minor third, tritone,
    // minor seventh above the root. Cleared first -- every degree starts out
    // seeded with a built-in-equivalent voicing, not blank.
    p.clearJazzCustomVoicing(false, 9);
    p.setJazzCustomVoicingNote(false, 9, 3, true);
    p.setJazzCustomVoicingNote(false, 9, 6, true);
    p.setJazzCustomVoicingNote(false, 9, 10, true);

    p.setPlayConfigDetails(1, 1, sr, 256);
    p.prepareToPlay(sr, 256);

    const int total = static_cast<int>(sr * 1.5);
    std::vector<float> source(static_cast<size_t>(total));
    makeVoice(source, f0, sr);
    juce::AudioBuffer<float> buffer(1, 256);
    bool sent = false;
    for (int pos = 0; pos < total; pos += 256) {
        const int n = juce::jmin(256, total - pos);
        buffer.setSize(1, n, false, false, true);
        juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
        juce::MidiBuffer midi;
        if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); sent = true; }
        p.processBlock(buffer, midi);
    }
    const auto v = p.jazzView();
    bool haveMinorThird = false, haveTritone = false, haveMinorSeventh = false;
    for (int i = 0; i < v.noteCount; ++i) {
        switch (((v.notes[i] - v.chordRootPc) % 12 + 12) % 12) {
            case 3:  haveMinorThird = true; break;
            case 6:  haveTritone = true; break;
            case 10: haveMinorSeventh = true; break;
            default: break;
        }
    }
    check(v.sounding && v.chordRootPc == 9 && v.customVoicing && v.melodyNote == 57 &&
              v.melodyDegree == 1 && haveMinorThird && haveTritone && haveMinorSeventh,
          juce::String("C held, A3 played, custom A degree set to a Min7b5-quality voicing -> root ") +
              (v.chordRootPc >= 0 ? jazz::pitchClassName(v.chordRootPc) : "?") + " " + v.roman +
              ", you are the " + jazz::degreeName(v.melodyDegree));

    // Copying a voicing to another degree shifts it by the distance between
    // them, so it keeps the same shape relative to whichever note reaches
    // that new degree; copying to another context copies it untransposed.
    {
        p.copyJazzCustomVoicing(false, 9, false, 2);   // major 9 (b3,b5,b7) -> major 2
        const auto copied = p.jazzCustomEntry(false, 2);
        bool haveShiftedThird = false, haveShiftedFifth = false, haveShiftedSeventh = false;
        for (int i = 0; i < copied.count; ++i) {
            switch (copied.offsets[i]) {
                // Degree 9 -> 2 is a shift of -7 semitones: 3 -> -4, 6 -> -1, 10 -> 3.
                case -4: haveShiftedThird = true; break;
                case -1: haveShiftedFifth = true; break;
                case 3:  haveShiftedSeventh = true; break;
                default: break;
            }
        }
        check(copied.count == 3 && haveShiftedThird && haveShiftedFifth && haveShiftedSeventh,
              juce::String("copying to another degree shifts every tone by the same amount (got ") +
                  juce::String(copied.count) + " notes)");

        p.copyJazzCustomVoicing(false, 9, true, 9);   // major 9 -> minor 9, no shift
        const auto crossContext = p.jazzCustomEntry(true, 9);
        bool haveThirdNoShift = false, haveTritoneNoShift = false, haveSeventhNoShift = false;
        for (int i = 0; i < crossContext.count; ++i) {
            switch (crossContext.offsets[i]) {
                case 3:  haveThirdNoShift = true; break;
                case 6:  haveTritoneNoShift = true; break;
                case 10: haveSeventhNoShift = true; break;
                default: break;
            }
        }
        check(crossContext.count == 3 && haveThirdNoShift && haveTritoneNoShift && haveSeventhNoShift,
              "copying to the other context copies the voicing untransposed");
    }

    // Minor was never turned on for the custom dictionary, so two keys still
    // fall back to the ordinary built-in minor dictionary rather than reusing
    // the major table or going silent.
    {
        HarmonizerAudioProcessor minorP;
        setValue(minorP, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setValue(minorP, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        setValue(minorP, HarmonizerAudioProcessor::ParamId::jazzCustomOn, 1.0f);
        setValue(minorP, HarmonizerAudioProcessor::ParamId::jazzCustomUseMajor, 1.0f);
        minorP.setPlayConfigDetails(1, 1, sr, 256);
        minorP.prepareToPlay(sr, 256);

        std::vector<float> src2(static_cast<size_t>(total));
        makeVoice(src2, f0, sr);
        juce::AudioBuffer<float> buf2(1, 256);
        bool sent2 = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buf2.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buf2.getWritePointer(0), src2.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent2) {
                for (int note : {69, 76}) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
                sent2 = true;
            }
            minorP.processBlock(buf2, midi);
        }
        const auto mv = minorP.jazzView();
        check(mv.sounding && mv.minorKey && mv.typeIndex == static_cast<int>(jazz::ChordType::Min7),
              juce::String("A/E held (minor), custom major on but minor off -> built-in im7 (got ") +
                  mv.roman + ")");
    }

    // Record mode: MIDI note-ons captured while it is active build a voicing
    // by ear, on the same middle-C-as-root convention the keyboard editor
    // uses, instead of naming a key centre or sounding through the engine.
    {
        HarmonizerAudioProcessor recP;
        setValue(recP, HarmonizerAudioProcessor::ParamId::jazzMode, 1.0f);
        recP.setPlayConfigDetails(1, 1, sr, 256);
        recP.prepareToPlay(sr, 256);

        check(!recP.jazzCustomRecording(), "record mode starts off");
        recP.setJazzCustomRecording(true);
        check(recP.jazzCustomRecording(), "record mode switches on");

        juce::AudioBuffer<float> silent(1, 256);
        silent.clear();
        juce::MidiBuffer midi;
        // A C major triad an octave above middle C: 72, 76, 79.
        for (int note : {72, 76, 79}) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
        recP.processBlock(silent, midi);

        const auto captured = recP.jazzCustomRecordedNotes();
        check(captured.size() == 3 && captured.contains(72) && captured.contains(76) &&
                  captured.contains(79),
              juce::String("captured the notes played while recording (got ") +
                  juce::String(captured.size()) + ")");

        // Notes captured while recording do not name a key centre -- jazz
        // mode has nothing held, so nothing sounds.
        check(!recP.jazzView().sounding,
              "notes captured while recording do not also drive jazz mode's own key centre");

        // Reset clears the buffer without leaving record mode.
        recP.clearJazzCustomRecordedNotes();
        check(recP.jazzCustomRecordedNotes().isEmpty() && recP.jazzCustomRecording(),
              "reset clears the capture buffer but stays in record mode");

        // Recapture and commit to a degree, the way the editor's Save button
        // does -- offsets relative to middle C (60).
        juce::MidiBuffer midi2;
        for (int note : {72, 76, 79}) midi2.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
        recP.processBlock(silent, midi2);
        const auto toCommit = recP.jazzCustomRecordedNotes();
        recP.clearJazzCustomVoicing(false, 4);
        for (int note : toCommit) recP.setJazzCustomVoicingNote(false, 4, note - 60, true);
        recP.setJazzCustomRecording(false);

        const auto committed = recP.jazzCustomEntry(false, 4);
        check(!recP.jazzCustomRecording(), "saving stops record mode");
        check(committed.count == 3, "the recorded voicing committed to the chosen degree");
    }

    // Presets: a save/load/delete round trip through the small file-backed
    // library, independent of host session state.
    {
        const juce::String name = "__harmonizer_test_preset__";
        p.deleteJazzDictionaryPreset(name);   // clean slate if a previous run left one

        check(p.saveJazzDictionaryPreset(name), "a named preset saves");
        check(p.jazzDictionaryPresetNames().contains(name), "it shows up in the preset list");

        // Change the live dictionary, then load the preset back over it.
        p.clearJazzCustomVoicing(false, 9);
        p.setJazzCustomVoicingNote(false, 9, 4, true);   // Maj7-quality, for now
        check(p.loadJazzDictionaryPreset(name), "the preset loads");
        const auto loadedBack = p.jazzCustomEntry(false, 9);
        bool loadedMinorThird = false, loadedTritone = false, loadedMinorSeventh = false;
        for (int i = 0; i < loadedBack.count; ++i) {
            switch (loadedBack.offsets[i]) {
                case 3:  loadedMinorThird = true; break;
                case 6:  loadedTritone = true; break;
                case 10: loadedMinorSeventh = true; break;
                default: break;
            }
        }
        check(loadedBack.count == 3 && loadedMinorThird && loadedTritone && loadedMinorSeventh,
              juce::String("loading the preset restores the saved voicing (got ") +
                  juce::String(loadedBack.count) + " notes)");

        check(p.deleteJazzDictionaryPreset(name), "the preset deletes");
        check(!p.jazzDictionaryPresetNames().contains(name), "and is gone from the list");
    }
}

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("=============================================\n");
    std::printf(" Harmoniser plugin wrapper checks\n");
    std::printf("=============================================\n");

    const double sr = 48000.0;
    const double f0 = 220.0;

    // --- MIDI from the host reaches the engine, and shifts by the right amount.
    std::printf("\n-- Host MIDI drives the harmony --\n");
    {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setChoice(p, HarmonizerAudioProcessor::ParamId::harmonyMode, 0);   // fixed interval
        setValue(p, HarmonizerAudioProcessor::ParamId::formant, 0.0f);

        auto out = render(p, sr, 256, f0, 64, 1, 1.0);   // E above middle C
        const double want = f0 * std::pow(2.0, 4.0 / 12.0);
        const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
        check(std::fabs(centsErr(got, want)) < 15.0,
              juce::String("note 64 -> ") + juce::String(got, 2) + " Hz (want " +
                  juce::String(want, 2) + ", " + juce::String(centsErr(got, want), 1) + " cents)");
    }

    // --- Quality mode indices must map onto the right engine enum.
    std::printf("\n-- Parameter choices select the mode they name --\n");
    {
        const char* names[] = {"Vocoder bands", "Sample rate", "Bit depth"};
        for (int index = 0; index < 3; ++index) {
            HarmonizerAudioProcessor p;
            setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
            setChoice(p, HarmonizerAudioProcessor::ParamId::qualityMode, index);
            setValue(p, HarmonizerAudioProcessor::ParamId::qualityAmount, 1.0f);
            render(p, sr, 256, f0, 60, 1, 0.5);

            const auto m = p.metrics();
            bool ok = false;
            juce::String detail;
            if (index == 0) {            // vocoder: partial count collapses
                ok = m.partialsPerVoice <= 8 && m.internalSampleRate > 40000.0f && m.bitDepth == 32;
                detail = juce::String(m.partialsPerVoice) + " partials";
            } else if (index == 1) {     // sample rate: internal rate drops
                ok = m.internalSampleRate < 20000.0f && m.bitDepth == 32;
                detail = juce::String(m.internalSampleRate, 0) + " Hz internal";
            } else {                     // bit depth: quantisation kicks in
                ok = m.bitDepth <= 8 && m.internalSampleRate > 40000.0f;
                detail = juce::String(m.bitDepth) + "-bit";
            }
            check(ok, juce::String(names[index]) + " at full reduction -> " + detail);
        }
    }

    // --- Chord voicing counts the player as one of the chord tones.
    std::printf("\n-- Chord voicing through the plugin --\n");
    {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setChoice(p, HarmonizerAudioProcessor::ParamId::harmonyMode, 2);   // chord voicing
        setChoice(p, HarmonizerAudioProcessor::ParamId::chordDegree, 0);   // root

        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 0.8);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(1, 256);
        bool sent = false;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(1, n, false, false, true);
            juce::FloatVectorOperations::copy(buffer.getWritePointer(0), source.data() + pos, n);
            juce::MidiBuffer midi;
            if (!sent) {
                for (int note : {60, 64, 67}) {
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
                }
                sent = true;
            }
            p.processBlock(buffer, midi);
        }
        const auto m = p.metrics();
        check(m.activeVoices == 2 && m.rootNote == 60,
              juce::String("C-E-G held -> ") + juce::String(m.activeVoices) +
                  " voices sounding, root " + juce::String(m.rootNote));
    }

    // --- The host must be told what to compensate for.
    std::printf("\n-- Latency reported to the host --\n");
    {
        HarmonizerAudioProcessor p;
        p.setPlayConfigDetails(1, 1, sr, 256);
        p.prepareToPlay(sr, 256);
        const int reported = p.getLatencySamples();
        const float ms = p.metrics().algorithmicLatencyMs;
        check(reported == 831,
              juce::String("reports ") + juce::String(reported) + " samples (" +
                  juce::String(ms, 2) + " ms)");
    }

    // --- Stereo tracks are summed in and fanned back out.
    std::printf("\n-- Stereo handling --\n");
    {
        HarmonizerAudioProcessor p;
        setValue(p, HarmonizerAudioProcessor::ParamId::wetDry, 1.0f);
        setChoice(p, HarmonizerAudioProcessor::ParamId::harmonyMode, 0);
        p.setPlayConfigDetails(2, 2, sr, 256);
        p.prepareToPlay(sr, 256);

        const int total = static_cast<int>(sr * 0.6);
        std::vector<float> source(static_cast<size_t>(total));
        makeVoice(source, f0, sr);
        juce::AudioBuffer<float> buffer(2, 256);
        bool sent = false;
        float maxDiff = 0.0f, peak = 0.0f;
        for (int pos = 0; pos < total; pos += 256) {
            const int n = juce::jmin(256, total - pos);
            buffer.setSize(2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch) {
                juce::FloatVectorOperations::copy(buffer.getWritePointer(ch), source.data() + pos, n);
            }
            juce::MidiBuffer midi;
            if (!sent) { midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.8f), 0); sent = true; }
            p.processBlock(buffer, midi);
            for (int i = 0; i < n; ++i) {
                maxDiff = juce::jmax(maxDiff, std::fabs(buffer.getSample(0, i) - buffer.getSample(1, i)));
                peak = juce::jmax(peak, std::fabs(buffer.getSample(0, i)));
            }
        }
        check(peak > 0.02f && maxDiff < 1.0e-6f,
              juce::String("both channels identical (peak ") + juce::String(peak, 3) +
                  ", max L/R difference " + juce::String(maxDiff, 9) + ")");
    }

    testSidechainInput();
    testSilentMainBusDoesNotAttenuate();
    testJazzChordMode();
    testJazzTranspose();
    testJazzLatch();
    testJazzKeyQuality();
    testJazzSustain();
    testJazzGlide();
    testJazzChordStability();
    testJazzChordHoldTiming();
    testJazzSustainFreeze();
    testJazzBassNote();
    testJazzMudAvoidance();
    testJazzLockedCustomRegister();
    testJazzLibraryPreview();
    testJazzMidiImportStaging();
    testJazzUserLibrary();
    testJazzMidiCandidatePreviewAndSave();
    testJazzFactoryPresets();
    testJazzAutoVoices();
    testJazzCustomDictionary();

    std::printf("\n=============================================\n");
    if (g_failures == 0) std::printf(" ALL PLUGIN CHECKS PASSED\n");
    else std::printf(" %d CHECK(S) FAILED\n", g_failures);
    std::printf("=============================================\n");
    return g_failures != 0;
}
