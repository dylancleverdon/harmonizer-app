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
    for (int fftSize : {512, 1024, 2048}) {
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
    benchmark();

    printf("\n=============================================\n");
    if (g_failures == 0) printf(" ALL CHECKS PASSED\n");
    else                 printf(" %d CHECK(S) FAILED\n", g_failures);
    printf("=============================================\n");
    return g_failures != 0;
}
