#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "Analysis.h"
#include "Fifo.h"
#include "Resampler.h"
#include "Types.h"
#include "Voices.h"

namespace dsp {

// The whole wet path. Pure standard C++ with no Android or Oboe dependency, so
// it builds and runs on a desktop for testing; the platform layer only has to
// hand it blocks of samples.
class Harmonizer {
public:
    void prepare(double hostSampleRate, int maxBlockFrames);
    void reset();

    Params& params() { return params_; }
    MidiQueue<512>& midiQueue() { return midi_; }

    // Mono in, mono out. May be called with any block size up to the one passed
    // to prepare(); larger blocks are processed in chunks.
    void process(const float* in, float* out, int frames);

    Metrics metrics() const;

    // Engine-only latency: what the harmoniser adds on top of the audio HAL.
    float algorithmicLatencyMs() const;
    int   algorithmicLatencySamples() const { return primeSamples_; }

    void allNotesOff();

private:
    struct Slot {
        bool     held = false;
        int      note = -1;
        float    velocity = 0.0f;
        float    ratio = 1.0f;
        float    gain = 0.0f;
        float    gainTarget = 0.0f;
        uint64_t order = 0;
    };

    void processChunk(const float* in, float* out, int frames);
    void drainMidi();
    void runHop();
    void synthesiseResidual(float gain);
    void updateVoiceRatios();
    int  findAnchorNote(int rootNote, int degree) const;
    void applyQualitySettings();
    void reconfigure(int baseFft, int decimation);
    void updateAdaptive(float load);

    // --- configuration ------------------------------------------------------
    double hostSampleRate_ = 48000.0;
    int    maxBlock_ = 256;
    int    baseFft_ = 1024;          // at host rate
    int    decimation_ = 1;
    int    fft_ = 1024;              // internal, = baseFft_ / decimation_
    int    hop_ = 256;
    float  internalRate_ = 48000.0f;
    int    partialsPerVoice_ = 24;
    int    bitDepth_ = 32;
    int    primeSamples_ = 0;        // total engine latency, host samples

    // --- state --------------------------------------------------------------
    Params  params_;
    MidiQueue<512> midi_;
    std::array<Slot, kMaxVoices> slots_{};
    uint64_t noteCounter_ = 0;

    Analyzer analyzer_;
    SpectralScratch scratch_;
    std::array<PartialVoice, kMaxVoices> voices_;
    uint32_t rng_ = 0x9E3779B9u;      // residual phase randomiser

    Decimator    decimator_;
    Interpolator interpolator_;

    std::vector<std::unique_ptr<RealFft>> ffts_;   // indexed by log2(size)
    std::vector<float> synthWindow_;

    // Sliding history of internal-rate input. A FIFO would consume samples, but
    // successive analysis windows overlap by 75%, so this stays a circular
    // history addressed by an absolute sample count instead.
    std::vector<float> inHist_;
    int      inHistMask_ = 0;
    int64_t  inWritten_ = 0;
    int64_t  nextAnalysisAt_ = 0;

    Fifo wetIntFifo_;    // internal-rate wet awaiting interpolation
    Fifo wetHostFifo_;   // host-rate wet awaiting mixing
    DelayLine dryDelay_;

    std::vector<float> frameBuf_;    // one analysis window
    std::vector<float> ola_;         // circular overlap-add accumulator, length fft_
    int   olaPos_ = 0;
    std::vector<float> hopBuf_;      // additive voices render here
    std::vector<float> decBuf_, intBuf_, interpOut_, emitBuf_, wetBlock_;

    // --- adaptive controllers ----------------------------------------------
    float cpuLoadEma_ = 0.0f;
    float adaptiveBoost_ = 0.0f;
    float wetFade_ = 1.0f;           // ramps back up after a reconfigure
    int   adaptFrames_ = 0;
    int   reconfigCooldown_ = 0;

    // --- metrics (audio thread writes, UI thread reads) ---------------------
    std::atomic<float> mCpu_{0.0f}, mQuality_{0.0f}, mPitch_{0.0f};
    std::atomic<float> mInPeak_{0.0f}, mOutPeak_{0.0f};
    std::atomic<int>   mVoices_{0}, mPartials_{0}, mBits_{32}, mRoot_{-1}, mAnchor_{-1};
    std::atomic<float> mInternalRate_{48000.0f};
};

}  // namespace dsp
