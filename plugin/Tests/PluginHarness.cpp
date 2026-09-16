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

    std::printf("\n=============================================\n");
    if (g_failures == 0) std::printf(" ALL PLUGIN CHECKS PASSED\n");
    else std::printf(" %d CHECK(S) FAILED\n", g_failures);
    std::printf("=============================================\n");
    return g_failures != 0;
}
