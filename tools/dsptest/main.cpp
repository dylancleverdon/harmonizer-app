// Offline validation for the harmoniser DSP core.
//
// The engine is deliberately free of Android and Oboe dependencies so it can be
// exercised here: feed it known signals, measure what comes out, and check the
// numbers rather than trusting that it "sounds about right" on a phone.

#include "Harmonizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <vector>

using namespace dsp;
static constexpr double kPi = 3.14159265358979323846;
static int g_failures = 0;

static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// A voice-like test signal: harmonic stack with 1/n amplitudes, so the
// fundamental is unambiguously the strongest component.
static void makeVoice(std::vector<float>& x, double f0, double sr, int harmonics = 24) {
    for (size_t n = 0; n < x.size(); ++n) {
        double s = 0.0;
        for (int h = 1; h <= harmonics; ++h) {
            const double f = f0 * h;
            if (f > sr * 0.45) break;
            s += std::sin(2.0 * kPi * f * n / sr) / h;
        }
        x[n] = static_cast<float>(s * 0.25);
    }
}

// Strongest spectral peak, refined parabolically on the log magnitude.
static double dominantFreq(const float* x, int n, double sr) {
    int N = 1;
    while (N * 2 <= n) N *= 2;
    std::vector<float> buf(N), re(N / 2 + 1), im(N / 2 + 1);
    for (int i = 0; i < N; ++i) {
        buf[i] = x[n - N + i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / N));
    }
    RealFft fft(N);
    fft.forward(buf.data(), re.data(), im.data());

    int best = 1;
    double bestMag = 0.0;
    for (int k = 2; k < N / 2 - 1; ++k) {
        const double m = std::hypot(re[k], im[k]);
        if (m > bestMag) { bestMag = m; best = k; }
    }
    if (bestMag <= 0.0) return 0.0;

    const double y0 = std::log(std::hypot(re[best - 1], im[best - 1]) + 1e-20);
    const double y1 = std::log(bestMag + 1e-20);
    const double y2 = std::log(std::hypot(re[best + 1], im[best + 1]) + 1e-20);
    const double denom = 2.0 * (2.0 * y1 - y0 - y2);
    const double delta = (std::fabs(denom) > 1e-12) ? (y2 - y0) / denom : 0.0;
    return (best + delta) * sr / N;
}


// --- multi-peak measurement, for checking a whole chord at once -------------

struct Spectrum {
    int N = 0;
    double sr = 0.0;
    std::vector<double> mag;
    double maxMag = 0.0;
};

static Spectrum analyse(const float* x, int n, double sr) {
    Spectrum s;
    s.sr = sr;
    s.N = 1;
    while (s.N * 2 <= n) s.N *= 2;
    std::vector<float> buf(s.N), re(s.N / 2 + 1), im(s.N / 2 + 1);
    for (int i = 0; i < s.N; ++i) {
        buf[i] = x[n - s.N + i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / s.N));
    }
    RealFft(s.N).forward(buf.data(), re.data(), im.data());
    s.mag.resize(static_cast<size_t>(s.N / 2 + 1));
    for (int k = 0; k <= s.N / 2; ++k) {
        s.mag[k] = std::hypot(re[k], im[k]);
        if (k > 1) s.maxMag = std::max(s.maxMag, s.mag[k]);
    }
    return s;
}

// Loudest bin within tolerance of hz, as a fraction of the spectrum's peak.
static double relLevelNear(const Spectrum& s, double hz, double tolCents = 60.0) {
    const double lo = hz * std::pow(2.0, -tolCents / 1200.0);
    const double hi = hz * std::pow(2.0, tolCents / 1200.0);
    const int kLo = std::max(1, static_cast<int>(std::floor(lo * s.N / s.sr)));
    const int kHi = std::min(s.N / 2 - 1, static_cast<int>(std::ceil(hi * s.N / s.sr)));
    double best = 0.0;
    for (int k = kLo; k <= kHi; ++k) best = std::max(best, s.mag[k]);
    return s.maxMag > 0.0 ? best / s.maxMag : 0.0;
}

// Refined frequency of the strongest component near hz, or 0 if nothing is there.
static double peakFreqNear(const Spectrum& s, double hz, double tolCents = 60.0) {
    const double lo = hz * std::pow(2.0, -tolCents / 1200.0);
    const double hi = hz * std::pow(2.0, tolCents / 1200.0);
    const int kLo = std::max(2, static_cast<int>(std::floor(lo * s.N / s.sr)));
    const int kHi = std::min(s.N / 2 - 2, static_cast<int>(std::ceil(hi * s.N / s.sr)));
    int best = -1;
    double bestMag = 0.0;
    for (int k = kLo; k <= kHi; ++k) {
        if (s.mag[k] > bestMag) { bestMag = s.mag[k]; best = k; }
    }
    if (best < 0 || bestMag < s.maxMag * 0.02) return 0.0;
    const double y0 = std::log(s.mag[best - 1] + 1e-20);
    const double y1 = std::log(bestMag + 1e-20);
    const double y2 = std::log(s.mag[best + 1] + 1e-20);
    const double denom = 2.0 * (2.0 * y1 - y0 - y2);
    const double delta = (std::fabs(denom) > 1e-12) ? (y2 - y0) / denom : 0.0;
    return (best + delta) * s.sr / s.N;
}

static double centsErr(double measured, double expected) {
    if (measured <= 0.0 || expected <= 0.0) return 1e9;
    return 1200.0 * std::log2(measured / expected);
}

static void noteOn(Harmonizer& h, int note, int vel = 100) {
    h.midiQueue().push(MidiEvent{0x90, static_cast<uint8_t>(note), static_cast<uint8_t>(vel)});
}

// Runs `seconds` of audio through the engine in realistic burst-sized chunks.
static std::vector<float> run(Harmonizer& h, const std::vector<float>& in, int burst = 192) {
    std::vector<float> out(in.size(), 0.0f);
    for (size_t i = 0; i < in.size(); i += static_cast<size_t>(burst)) {
        const int n = static_cast<int>(std::min<size_t>(burst, in.size() - i));
        h.process(in.data() + i, out.data() + i, n);
    }
    return out;
}

static bool finite(const std::vector<float>& v) {
    for (float s : v) if (!std::isfinite(s)) return false;
    return true;
}

// ---------------------------------------------------------------------------

static void testIntervals(QualityMode mode, const char* modeName, float amount) {
    printf("\n-- Interval accuracy: %s (quality amount %.2f) --\n", modeName, amount);
    const double sr = 48000.0;
    const double f0 = 220.0;

    // note, semitones above middle C, cents
    const int notes[] = {60, 64, 67, 72, 55, 48};
    for (int note : notes) {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().qualityMode.store(static_cast<int>(mode));
        h.params().qualityAmount.store(amount);
        h.params().formantCorrection.store(false);   // isolate pitch from shaping
        h.params().wetDry.store(1.0f);               // wet only
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::FixedInterval));

        noteOn(h, note);
        std::vector<float> in(static_cast<size_t>(sr * 1.0));
        makeVoice(in, f0, sr);
        std::vector<float> out = run(h, in);

        const double expected = f0 * std::pow(2.0, (note - 60) / 12.0);
        const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
        const double err = centsErr(got, expected);
        const int cents = (note - 60) * 100;

        char msg[220];
        snprintf(msg, sizeof(msg),
                 "MIDI %d = %+5d cents -> want %7.2f Hz, got %7.2f Hz (%+6.1f cents)",
                 note, cents, expected, got, err);
        check(std::fabs(err) < 12.0 && finite(out), msg);
    }
}

static void testLatencyAlignment() {
    printf("\n-- Dry-path delay compensation --\n");
    const double sr = 48000.0;
    for (int fftSize : {256, 512, 1024, 2048}) {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().fftSize.store(fftSize);
        h.params().wetDry.store(0.0f);          // dry only
        h.prepare(sr, 192);                      // re-prepare so fftSize takes effect

        std::vector<float> in(20000, 0.0f);
        in[1000] = 1.0f;
        std::vector<float> out = run(h, in);

        int peak = 0;
        float best = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) {
            if (std::fabs(out[i]) > best) { best = std::fabs(out[i]); peak = static_cast<int>(i); }
        }
        const int expected = 1000 + h.algorithmicLatencySamples();
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "fft %4d: dry impulse at %5d, expected %5d (engine latency %.2f ms)",
                 fftSize, peak, expected, h.algorithmicLatencyMs());
        check(peak == expected, msg);
    }
}

static void testWetKeepsTime() {
    printf("\n-- Wet path stays time-aligned across quality modes --\n");
    const double sr = 48000.0;
    const QualityMode modes[] = {QualityMode::Vocoder, QualityMode::SampleRate, QualityMode::BitDepth};
    const char* names[] = {"vocoder", "sample-rate", "bit-depth"};

    for (int mi = 0; mi < 3; ++mi) {
        for (float amount : {0.0f, 0.5f, 1.0f}) {
            Harmonizer h;
            h.prepare(sr, 192);
            h.params().qualityMode.store(static_cast<int>(modes[mi]));
            h.params().qualityAmount.store(amount);
            h.params().wetDry.store(1.0f);
            noteOn(h, 60);   // unison: wet should line up with dry

            std::vector<float> in(static_cast<size_t>(sr * 0.5), 0.0f);
            makeVoice(in, 220.0, sr);
            std::vector<float> out = run(h, in);

            // Latency must not depend on the quality setting -- that is the
            // whole point of holding the window duration constant.
            const int lat = h.algorithmicLatencySamples();
            char msg[200];
            snprintf(msg, sizeof(msg), "%-12s amount %.1f: latency %d samples (%.2f ms), output finite",
                     names[mi], amount, lat, h.algorithmicLatencyMs());
            check(lat == 831 && finite(out), msg);
        }
    }
}

static void testAbsoluteMode() {
    printf("\n-- Absolute (chord) mode lands on the played note --\n");
    const double sr = 48000.0;
    const struct { double sung; int note; } cases[] = {
        {180.0, 69},   // A4 = 440
        {220.0, 60},   // middle C = 261.63
        {147.0, 64},   // E4 = 329.63
    };

    for (const auto& c : cases) {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
        h.params().qualityAmount.store(0.0f);
        h.params().formantCorrection.store(false);
        h.params().wetDry.store(1.0f);
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::Absolute));
        h.params().fftSize.store(2048);          // recommended for absolute mode
        h.prepare(sr, 192);
        h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
        h.params().qualityAmount.store(0.0f);
        h.params().formantCorrection.store(false);
        h.params().wetDry.store(1.0f);
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::Absolute));

        noteOn(h, c.note);
        std::vector<float> in(static_cast<size_t>(sr * 1.2));
        makeVoice(in, c.sung, sr);
        std::vector<float> out = run(h, in);

        const double expected = 440.0 * std::pow(2.0, (c.note - 69) / 12.0);
        const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
        const double err = centsErr(got, expected);
        char msg[220];
        snprintf(msg, sizeof(msg),
                 "sung %6.1f Hz + MIDI %d -> want %7.2f Hz, got %7.2f Hz (%+6.1f cents)",
                 c.sung, c.note, expected, got, err);
        check(std::fabs(err) < 30.0 && finite(out), msg);
    }
}

static void testPolyphony() {
    printf("\n-- Ten-voice polyphony --\n");
    const double sr = 48000.0;
    Harmonizer h;
    h.prepare(sr, 192);
    h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
    h.params().qualityAmount.store(0.3f);
    h.params().wetDry.store(1.0f);

    const int chord[] = {48, 52, 55, 59, 60, 64, 67, 71, 72, 76};
    for (int n : chord) noteOn(h, n);

    std::vector<float> in(static_cast<size_t>(sr * 1.0));
    makeVoice(in, 220.0, sr);
    std::vector<float> out = run(h, in);

    const Metrics m = h.metrics();
    char msg[200];
    snprintf(msg, sizeof(msg), "10 notes held -> engine reports %d active voices", m.activeVoices);
    check(m.activeVoices == 10, msg);

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::fabs(s));
    snprintf(msg, sizeof(msg), "output finite and within range (peak %.3f)", peak);
    check(finite(out) && peak <= 1.001f && peak > 0.05f, msg);

    // An 11th note must steal rather than overflow.
    noteOn(h, 79);
    std::vector<float> out2 = run(h, in);
    snprintf(msg, sizeof(msg), "11th note steals a voice, still %d active", h.metrics().activeVoices);
    check(h.metrics().activeVoices == 10 && finite(out2), msg);
}



// Regression: the audio engine calls prepare() once when it is constructed and
// again once the device reports its real sample rate and burst size. That second
// call must leave a fully working engine. It did not -- prepare() re-zeroed the
// analysis window, and configure() skipped rebuilding it because the size had
// not changed, so every windowed frame came out zero and the wet path went
// silent while the dry path carried on as normal.
static void testRepeatedPrepare() {
    printf("\n-- Re-preparing the engine (what the audio callback setup does) --\n");
    const double sr = 48000.0;

    for (int passes = 1; passes <= 3; ++passes) {
        Harmonizer h;
        for (int i = 0; i < passes; ++i) h.prepare(sr, 192);   // as AudioEngine does

        h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
        h.params().qualityAmount.store(0.35f);                  // shipped defaults
        h.params().wetDry.store(1.0f);
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::FixedInterval));
        noteOn(h, 64);                                          // E above middle C

        std::vector<float> in(static_cast<size_t>(sr * 0.6));
        makeVoice(in, 220.0, sr);
        std::vector<float> out = run(h, in);

        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::fabs(v));
        const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
        const double want = 220.0 * std::pow(2.0, 4.0 / 12.0);

        char msg[240];
        snprintf(msg, sizeof(msg),
                 "prepare() x%d -> wet peak %.4f, dominant %7.2f Hz (want %7.2f)",
                 passes, peak, got, want);
        check(peak > 0.02f && std::fabs(centsErr(got, want)) < 12.0, msg);
    }

    // The same thing one level up: a device whose rate differs from the default.
    {
        Harmonizer h;
        h.prepare(48000.0, 192);
        h.prepare(44100.0, 96);          // second call with different rate and burst
        h.params().wetDry.store(1.0f);
        noteOn(h, 67);

        std::vector<float> in(static_cast<size_t>(44100.0 * 0.6));
        makeVoice(in, 220.0, 44100.0);
        std::vector<float> out = run(h, in, 96);

        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::fabs(v));
        const double got = dominantFreq(out.data(), static_cast<int>(out.size()), 44100.0);
        const double want = 220.0 * std::pow(2.0, 7.0 / 12.0);
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "re-prepared at 44100/96 -> wet peak %.4f, dominant %7.2f Hz (want %7.2f)",
                 peak, got, want);
        check(peak > 0.02f && std::fabs(centsErr(got, want)) < 12.0, msg);
    }
}


// The stream rate is user-selectable, so the engine has to hold up at all of
// them -- including the extreme corner where a low stream rate is combined with
// maximum sample-rate reduction, which is where the internal transform gets
// smallest and the Nyquist limit starts clipping partials.
static void testLowStreamRates() {
    printf("\n-- Engine across selectable stream rates --\n");
    const double f0 = 220.0;
    const double want = f0 * std::pow(2.0, 4.0 / 12.0);   // +400 cents

    for (double sr : {16000.0, 24000.0, 32000.0, 44100.0, 48000.0}) {
        Harmonizer h;
        h.prepare(sr, 96);
        h.prepare(sr, 96);                                 // as the audio engine does
        h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
        h.params().qualityAmount.store(0.35f);
        h.params().formantCorrection.store(false);
        h.params().wetDry.store(1.0f);
        noteOn(h, 64);

        std::vector<float> in(static_cast<size_t>(sr * 1.0));
        makeVoice(in, f0, sr);
        std::vector<float> out = run(h, in, 96);

        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::fabs(v));
        const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "%6.0f Hz stream: peak %.3f, +400 cents at %7.2f Hz (%+5.1f cents), "
                 "engine latency %.1f ms",
                 sr, peak, got, centsErr(got, want), h.algorithmicLatencyMs());
        check(peak > 0.02f && std::fabs(centsErr(got, want)) < 15.0 && finite(out), msg);
    }

    // Worst case: lowest stream rate and the deepest internal rate reduction on
    // top of it, so the internal transform bottoms out.
    {
        const double sr = 16000.0;
        Harmonizer h;
        h.prepare(sr, 96);
        h.params().qualityMode.store(static_cast<int>(QualityMode::SampleRate));
        h.params().qualityAmount.store(1.0f);              // maximum reduction
        h.params().wetDry.store(1.0f);
        for (int n : {60, 64, 67, 72}) noteOn(h, n);

        std::vector<float> in(static_cast<size_t>(sr * 0.8));
        makeVoice(in, f0, sr);
        std::vector<float> out = run(h, in, 96);

        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::fabs(v));
        const Metrics m = h.metrics();
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "16 kHz + max rate reduction: internal %.0f Hz, %d voices, peak %.3f, finite",
                 m.internalSampleRate, m.activeVoices, peak);
        check(finite(out) && peak <= 1.001f, msg);
    }
}


static void testSmallestWindow() {
    printf("\n-- Smallest analysis window (lowest latency setting) --\n");
    const double sr = 48000.0;
    const double f0 = 330.0;                 // a trumpet-ish register
    const double want = f0 * std::pow(2.0, 7.0 / 12.0);

    Harmonizer h;
    h.prepare(sr, 96);
    h.params().fftSize.store(256);
    h.prepare(sr, 96);
    h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
    h.params().qualityAmount.store(0.35f);
    h.params().formantCorrection.store(false);
    h.params().wetDry.store(1.0f);
    noteOn(h, 67);

    std::vector<float> in(static_cast<size_t>(sr * 0.8));
    makeVoice(in, f0, sr);
    std::vector<float> out = run(h, in, 96);

    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    const double got = dominantFreq(out.data(), static_cast<int>(out.size()), sr);
    char msg[240];
    snprintf(msg, sizeof(msg),
             "window 256 at 48 kHz: %.1f ms engine latency, +700 cents at %7.2f Hz (%+5.1f cents)",
             h.algorithmicLatencyMs(), got, centsErr(got, want));
    check(peak > 0.02f && std::fabs(centsErr(got, want)) < 20.0 && finite(out), msg);
}

static void testChordVoicing() {
    printf("\n-- Chord voicing: the input is the root, the chord goes around it --\n");
    const double sr = 48000.0;

    auto runChord = [&](const std::vector<int>& notes, bool doubleAnchor,
                        double inputHz) -> std::vector<float> {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
        h.params().qualityAmount.store(0.0f);
        h.params().formantCorrection.store(false);
        h.params().wetDry.store(1.0f);            // wet only: judge the harmony alone
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::ChordVoicing));
        h.params().doubleAnchor.store(doubleAnchor);
        for (int n : notes) noteOn(h, n);

        std::vector<float> in(static_cast<size_t>(sr * 1.0));
        makeVoice(in, inputHz, sr);
        return run(h, in);
    };

    // A major triad on the keyboard puts +400 and +700 cents around the input.
    {
        const double f0 = 220.0;
        std::vector<float> out = runChord({60, 64, 67}, false, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);

        const double third = f0 * std::pow(2.0, 4.0 / 12.0);
        const double fifth = f0 * std::pow(2.0, 7.0 / 12.0);
        const double gotThird = peakFreqNear(s, third);
        const double gotFifth = peakFreqNear(s, fifth);
        const double atRoot = relLevelNear(s, f0);

        char msg[240];
        snprintf(msg, sizeof(msg),
                 "C-E-G over %.0f Hz -> +400 at %7.2f Hz (%+5.1f cents), "
                 "+700 at %7.2f Hz (%+5.1f cents)",
                 f0, gotThird, centsErr(gotThird, third), gotFifth, centsErr(gotFifth, fifth));
        check(std::fabs(centsErr(gotThird, third)) < 12.0 &&
              std::fabs(centsErr(gotFifth, fifth)) < 12.0, msg);

        snprintf(msg, sizeof(msg),
                 "root is not doubled: level at %.0f Hz is %.1f dB below the chord",
                 f0, 20.0 * std::log10(atRoot + 1e-12));
        check(atRoot < 0.05, msg);
    }

    // The same shape higher up the keyboard must give the same chord. This is
    // the whole point of measuring intervals from the lowest note held.
    {
        const double f0 = 220.0;
        std::vector<float> a = runChord({60, 64, 67}, false, f0);
        std::vector<float> b = runChord({65, 69, 72}, false, f0);   // F-A-C, same shape
        Spectrum sa = analyse(a.data(), static_cast<int>(a.size()), sr);
        Spectrum sb = analyse(b.data(), static_cast<int>(b.size()), sr);

        const double third = f0 * std::pow(2.0, 4.0 / 12.0);
        const double fifth = f0 * std::pow(2.0, 7.0 / 12.0);
        const double da = centsErr(peakFreqNear(sb, third), peakFreqNear(sa, third));
        const double db = centsErr(peakFreqNear(sb, fifth), peakFreqNear(sa, fifth));

        char msg[240];
        snprintf(msg, sizeof(msg),
                 "transposition invariant: C-E-G vs F-A-C differ by %+.2f and %+.2f cents",
                 da, db);
        check(std::fabs(da) < 2.0 && std::fabs(db) < 2.0, msg);
    }

    // Chord quality carries through: a minor shape gives a minor third.
    {
        const double f0 = 220.0;
        std::vector<float> out = runChord({60, 63, 67}, false, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        const double minor3 = f0 * std::pow(2.0, 3.0 / 12.0);
        const double got = peakFreqNear(s, minor3);
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "C-Eb-G over %.0f Hz -> +300 at %7.2f Hz (want %7.2f, %+5.1f cents)",
                 f0, got, minor3, centsErr(got, minor3));
        check(std::fabs(centsErr(got, minor3)) < 12.0, msg);
    }

    // Same chord, different input note: the harmony follows the player.
    {
        const double f0 = 147.0;
        std::vector<float> out = runChord({60, 64, 67}, false, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        const double fifth = f0 * std::pow(2.0, 7.0 / 12.0);
        const double got = peakFreqNear(s, fifth);
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "same chord over %.0f Hz instead -> +700 at %7.2f Hz (want %7.2f, %+5.1f cents)",
                 f0, got, fifth, centsErr(got, fifth));
        check(std::fabs(centsErr(got, fifth)) < 12.0, msg);
    }

    // Opting in to doubling the root brings the unison voice back.
    {
        const double f0 = 220.0;
        std::vector<float> out = runChord({60, 64, 67}, true, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        const double atRoot = relLevelNear(s, f0);
        char msg[240];
        snprintf(msg, sizeof(msg),
                 "double-root on: level at %.0f Hz is %.1f dB relative to the chord",
                 f0, 20.0 * std::log10(atRoot + 1e-12));
        check(atRoot > 0.3, msg);
    }

    // Voice accounting: a three-note chord sounds two voices, not three.
    {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().wetDry.store(1.0f);
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::ChordVoicing));
        for (int n : {60, 64, 67}) noteOn(h, n);
        std::vector<float> in(static_cast<size_t>(sr * 0.4));
        makeVoice(in, 220.0, sr);
        run(h, in);
        char msg[200];
        snprintf(msg, sizeof(msg), "3-note chord sounds %d voices (the root is the input)",
                 h.metrics().activeVoices);
        check(h.metrics().activeVoices == 2, msg);

        snprintf(msg, sizeof(msg), "engine reports root as MIDI %d", h.metrics().rootNote);
        check(h.metrics().rootNote == 60, msg);
    }

    // One note held is only a root, so there is nothing to place around it.
    {
        const double f0 = 220.0;
        std::vector<float> out = runChord({60}, false, f0);
        float peak = 0.0f;
        for (float v : out) peak = std::max(peak, std::fabs(v));
        char msg[200];
        snprintf(msg, sizeof(msg), "a single held note produces no harmony (peak %.5f)", peak);
        check(peak < 0.01f, msg);
    }
}


static void testAnchorDegree() {
    printf("\n-- Chord voicing: choosing which chord tone the input is --\n");
    const double sr = 48000.0;

    auto runChord = [&](const std::vector<int>& notes, int degree,
                        double inputHz) -> std::vector<float> {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().qualityMode.store(static_cast<int>(QualityMode::Vocoder));
        h.params().qualityAmount.store(0.0f);
        h.params().formantCorrection.store(false);
        h.params().wetDry.store(1.0f);
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::ChordVoicing));
        h.params().chordAnchorDegree.store(degree);
        for (int n : notes) noteOn(h, n);

        std::vector<float> in(static_cast<size_t>(sr * 1.0));
        makeVoice(in, inputHz, sr);
        return run(h, in);
    };

    // Expect a component `semis` semitones from the input, and confirm the
    // input's own pitch is absent because that is the tone the player supplies.
    auto expectTone = [&](const Spectrum& s, double inputHz, int semis, const char* what) {
        const double want = inputHz * std::pow(2.0, semis / 12.0);
        const double got = peakFreqNear(s, want);
        char msg[240];
        snprintf(msg, sizeof(msg), "    %-26s %+3d semis -> %7.2f Hz (want %7.2f, %+5.1f cents)",
                 what, semis, got, want, centsErr(got, want));
        check(std::fabs(centsErr(got, want)) < 12.0, msg);
    };

    const double f0 = 220.0;

    // Anchored on the 5th: the player is the top of the triad, so the root and
    // third are placed below them.
    {
        printf("  C-E-G, input is the 5th (G):\n");
        std::vector<float> out = runChord({60, 64, 67}, 5, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, -7, "root C below");
        expectTone(s, f0, -3, "third E below");
        char msg[200];
        const double atSelf = relLevelNear(s, f0);
        snprintf(msg, sizeof(msg), "    input's own pitch not doubled (%.1f dB down)",
                 20.0 * std::log10(atSelf + 1e-12));
        check(atSelf < 0.05, msg);
    }

    // Anchored on the 3rd: one voice below, one above.
    {
        printf("  C-E-G, input is the 3rd (E):\n");
        std::vector<float> out = runChord({60, 64, 67}, 3, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, -4, "root C below");
        expectTone(s, f0, +3, "fifth G above");
    }

    // A seventh chord anchored on its seventh.
    {
        printf("  C-E-G-Bb, input is the 7th (Bb):\n");
        std::vector<float> out = runChord({60, 64, 67, 70}, 7, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, -10, "root C below");
        expectTone(s, f0,  -6, "third E below");
        expectTone(s, f0,  -3, "fifth G below");
    }

    // Minor chords: "the 3rd" must find the minor third, not give up.
    {
        printf("  C-Eb-G, input is the 3rd (Eb):\n");
        std::vector<float> out = runChord({60, 63, 67}, 3, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, -3, "root C below");
        expectTone(s, f0, +4, "fifth G above");
    }

    // Diminished: "the 5th" must still find the flattened one.
    {
        printf("  C-Eb-Gb, input is the 5th (Gb):\n");
        std::vector<float> out = runChord({60, 63, 66}, 5, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, -6, "root C below");
        expectTone(s, f0, -3, "third Eb below");
    }

    // The requested degree is missing, so it falls back to the root.
    {
        printf("  C-E-G with no 7th, input asks to be the 7th:\n");
        std::vector<float> out = runChord({60, 64, 67}, 7, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, +4, "falls back: third above");
        expectTone(s, f0, +7, "falls back: fifth above");

        Harmonizer h;
        h.prepare(sr, 192);
        h.params().wetDry.store(1.0f);
        h.params().harmonyMode.store(static_cast<int>(HarmonyMode::ChordVoicing));
        h.params().chordAnchorDegree.store(7);
        for (int n : {60, 64, 67}) noteOn(h, n);
        std::vector<float> in(static_cast<size_t>(sr * 0.4));
        makeVoice(in, f0, sr);
        run(h, in);
        const Metrics m = h.metrics();
        char msg[220];
        snprintf(msg, sizeof(msg),
                 "    engine reports root %d, anchor %d (equal = fell back, as asked)",
                 m.rootNote, m.anchorNote);
        check(m.rootNote == 60 && m.anchorNote == 60, msg);
    }

    // A degree voiced high still anchors, and the octave doubling above it is
    // placed an octave up rather than at unison.
    {
        printf("  C3-G3-E5, input is the 3rd (E5, two octaves up):\n");
        std::vector<float> out = runChord({48, 55, 76}, 3, f0);
        Spectrum s = analyse(out.data(), static_cast<int>(out.size()), sr);
        expectTone(s, f0, -28, "root C3 below");
        expectTone(s, f0, -21, "fifth G3 below");
    }
}

static void benchmark() {
    printf("\n-- Cost of each quality mode (10 voices, x86 desktop) --\n");
    printf("   Relative numbers are what matter; absolute figures will differ on the phone.\n\n");
    const double sr = 48000.0;
    const double seconds = 4.0;

    struct Case { QualityMode mode; float amount; const char* label; };
    const Case cases[] = {
        {QualityMode::Vocoder,    0.00f, "vocoder   K=96 (most transparent)"},
        {QualityMode::Vocoder,    0.35f, "vocoder   K=~35 (default setting)"},
        {QualityMode::Vocoder,    0.70f, "vocoder   K=~13"},
        {QualityMode::Vocoder,    1.00f, "vocoder   K=6  (cheapest)"},
        {QualityMode::SampleRate, 0.00f, "sr        48 kHz (full)"},
        {QualityMode::SampleRate, 0.50f, "sr        24 kHz"},
        {QualityMode::SampleRate, 1.00f, "sr        12 kHz"},
        {QualityMode::BitDepth,   0.00f, "bits      24-bit"},
        {QualityMode::BitDepth,   1.00f, "bits      4-bit"},
    };

    for (const Case& c : cases) {
        Harmonizer h;
        h.prepare(sr, 192);
        h.params().qualityMode.store(static_cast<int>(c.mode));
        h.params().qualityAmount.store(c.amount);
        h.params().wetDry.store(1.0f);
        const int chord[] = {48, 52, 55, 59, 60, 64, 67, 71, 72, 76};
        for (int n : chord) noteOn(h, n);

        std::vector<float> in(static_cast<size_t>(sr * seconds));
        makeVoice(in, 220.0, sr);
        std::vector<float> out(in.size());

        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < in.size(); i += 192) {
            const int n = static_cast<int>(std::min<size_t>(192, in.size() - i));
            h.process(in.data() + i, out.data() + i, n);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double el = std::chrono::duration<double>(t1 - t0).count();
        printf("   %-34s  %6.2f%% of realtime   (%.1fx faster than realtime)\n",
               c.label, 100.0 * el / seconds, seconds / el);
    }
}

int main() {
    printf("=============================================\n");
    printf(" Harmoniser DSP validation\n");
    printf("=============================================\n");

    testIntervals(QualityMode::Vocoder, "vocoder mode", 0.0f);
    testIntervals(QualityMode::Vocoder, "vocoder mode", 0.7f);
    testIntervals(QualityMode::SampleRate, "sample-rate mode", 0.5f);
    testIntervals(QualityMode::BitDepth, "bit-depth mode", 0.5f);
    testLatencyAlignment();
    testWetKeepsTime();
    testAbsoluteMode();
    testPolyphony();
    testRepeatedPrepare();
    testLowStreamRates();
    testSmallestWindow();
    testChordVoicing();
    testAnchorDegree();
    benchmark();

    printf("\n=============================================\n");
    if (g_failures == 0) printf(" ALL CHECKS PASSED\n");
    else                 printf(" %d CHECK(S) FAILED\n", g_failures);
    printf("=============================================\n");
    return g_failures != 0;
}
