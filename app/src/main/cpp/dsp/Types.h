#pragma once
#include <atomic>
#include <cstdint>

namespace dsp {

// Ten-note polyphony, as specced.
inline constexpr int kMaxVoices = 10;

// Largest analysis window we ever allocate for. Everything is sized for this
// up front so that changing window size or sample-rate divisor at runtime
// never allocates on the audio thread.
inline constexpr int kMaxFftSize = 2048;
inline constexpr int kMinFftSize = 256;
inline constexpr int kOverlap    = 4;            // hop = fft / 4
inline constexpr int kMaxPartials = 96;          // per voice, in vocoder mode
inline constexpr int kMaxDecimation = 4;

// The three ways to trade audio quality for processing time.
enum class QualityMode : int {
    // Run the whole wet path at a lower internal sample rate. Halving the rate
    // halves the FFT size for the same window duration, so latency holds while
    // the transform cost drops.
    SampleRate = 0,
    // Quantise the wet path to fewer bits. Included because it was asked for,
    // but see the note in the UI: on this SoC everything downstream is 32-bit
    // float, so this buys tone, not time.
    BitDepth = 1,
    // Resynthesise each voice from only the strongest K spectral peaks, the way
    // a vocoder reduces a signal to a handful of bands. Fewer partials is
    // directly less work, and it degrades gracefully on voice. Default.
    Vocoder = 2,
};

enum class HarmonyMode : int {
    // MIDI note gives a fixed interval from middle C, applied to whatever the
    // singer is doing. E4 -> +400 cents -> that voice tracks 400 cents above.
    FixedInterval = 0,
    // Voice lands on the absolute pitch of the MIDI note, whatever is sung.
    // Needs the pitch tracker, so it only runs in this mode.
    Absolute = 1,
    // The chord held on the keyboard is reduced to a set of intervals above its
    // lowest note, and the *input* supplies that lowest note. Play C-E-G and the
    // input becomes the root while two voices sit +400 and +700 cents above it;
    // play the same shape anywhere on the keyboard and you get the same chord
    // around the same played note. The anchor need not be the root -- see
    // Params::chordAnchorDegree. Costs no more than a fixed interval
    // -- the input's own pitch never has to be measured, only used.
    ChordVoicing = 2,
};

// Snapshot of engine state for the UI. Written by the audio thread, read by
// the UI thread; each field is independently atomic so a torn read is at worst
// a one-frame-stale number on a meter.
struct Metrics {
    float cpuLoad = 0.0f;               // DSP time / wall time available
    float effectiveQuality = 0.0f;      // after adaptive adjustment, 0..1
    int   activeVoices = 0;
    int   partialsPerVoice = 0;
    float internalSampleRate = 0.0f;
    int   bitDepth = 32;
    float algorithmicLatencyMs = 0.0f;  // engine only, excludes the audio HAL
    float detectedPitchHz = 0.0f;       // 0 when unvoiced or not in absolute mode
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    int   rootNote = -1;                // chord-voicing root (lowest note held)
    // The chord tone the input is standing in for. Equal to rootNote when the
    // chosen degree is the root, or when the chord did not contain it.
    int   anchorNote = -1;
};

// Everything the UI can change while audio is running. Plain atomics rather
// than a lock: the audio thread must never block on the UI thread.
struct Params {
    std::atomic<int>   qualityMode{static_cast<int>(QualityMode::Vocoder)};
    std::atomic<float> qualityAmount{0.35f};        // 0 = best, 1 = cheapest

    // Experimental option 1: watch the measured DSP load and spend less time
    // per block when we are close to missing the deadline.
    std::atomic<bool>  adaptiveLatency{false};
    // Experimental option 2: degrade quality as more voices pile on.
    std::atomic<bool>  adaptiveVoiceScaling{false};

    std::atomic<bool>  formantCorrection{true};
    // Which chord tone the input itself is taken to be: 1 (root), 3, 5, 7, 9,
    // 11 or 13. When the held chord does not contain that degree, the root is
    // used instead.
    std::atomic<int>   chordAnchorDegree{1};
    // The anchor tone is the input, so by default no voice is generated for it.
    // Turn this on to resynthesise it as well, which matters when running fully
    // wet.
    std::atomic<bool>  doubleAnchor{false};
    std::atomic<int>   harmonyMode{static_cast<int>(HarmonyMode::FixedInterval)};
    std::atomic<float> wetDry{0.5f};                // 0 = dry only, 1 = wet only
    std::atomic<float> outputGain{1.0f};
    std::atomic<int>   fftSize{1024};               // 256..2048, power of two
    std::atomic<bool>  bypass{false};

    // How long a voice's gain takes to cross-fade when the note it is
    // assigned to changes, on top of the ~15 ms floor that already exists to
    // keep ordinary note starts and stops from clicking. 0 (the default)
    // leaves that floor as the whole story, exactly today's behaviour; a
    // caller that wants a deliberately smoother hand-off between chords (see
    // jazz mode's Glide) raises it instead of fighting the floor.
    std::atomic<float> glideMs{0.0f};
};

struct MidiEvent {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

// Single-producer / single-consumer queue. The MIDI callback pushes, the audio
// callback pops; neither ever waits on the other.
template <int Capacity>
class MidiQueue {
public:
    bool push(const MidiEvent& e) {
        const int w = write_.load(std::memory_order_relaxed);
        const int next = (w + 1) % Capacity;
        if (next == read_.load(std::memory_order_acquire)) return false;  // full
        buf_[w] = e;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(MidiEvent& out) {
        const int r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;    // empty
        out = buf_[r];
        read_.store((r + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    MidiEvent buf_[Capacity];
    std::atomic<int> write_{0};
    std::atomic<int> read_{0};
};

}  // namespace dsp
