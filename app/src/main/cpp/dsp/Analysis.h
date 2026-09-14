#pragma once
#include <memory>
#include <vector>
#include "Fft.h"
#include "Types.h"

namespace dsp {

struct Peak {
    float freq = 0.0f;   // Hz, refined by the phase-vocoder frequency estimate
    float amp  = 0.0f;   // linear amplitude
};

// One STFT analysis of the input, shared by every voice. This is the reason ten
// voices are affordable: the expensive part -- transform, magnitudes, true
// frequencies, formant envelope, pitch -- is paid once per hop no matter how
// many notes are held.
class Analyzer {
public:
    void prepare(int maxFftSize);
    // Safe to call between hops; allocates nothing.
    void configure(int fftSize, float sampleRate);
    void reset();

    // frame: fftSize time-ordered samples, newest last. Not modified.
    void analyze(const float* frame, bool wantEnvelope, bool wantPitch, int maxPeaks,
                 bool wantResidual);

    int   numBins()  const { return fftSize_ / 2 + 1; }
    int   fftSize()  const { return fftSize_; }
    int   hopSize()  const { return fftSize_ / kOverlapLocal; }
    float sampleRate() const { return sampleRate_; }

    const float* magnitude() const { return mag_.data(); }
    const float* trueFreq()  const { return freq_.data(); }
    const std::vector<Peak>& peaks() const { return peaks_; }

    // Smoothed spectral envelope, linear magnitude, at an arbitrary frequency.
    // Returns 1.0 when envelope extraction is off.
    float envelopeAt(float hz) const;
    bool  hasEnvelope() const { return haveEnvelope_; }

    float pitchHz() const { return pitchHz_; }       // 0 when unvoiced
    float pitchConfidence() const { return pitchConf_; }
    float rms() const { return rms_; }

    // Spectrum left over after the peaks are removed: the unvoiced part of the
    // signal. Resynthesised once per hop with randomised phase so that breath
    // and consonants survive into the harmony voices.
    const float* residual() const { return residual_.data(); }
    bool hasResidual() const { return haveResidual_; }

private:
    static constexpr int kOverlapLocal = 4;

    void buildEnvelope();
    void detectPitch();
    void findPeaks(int maxPeaks);

    int   fftSize_ = 0;
    float sampleRate_ = 48000.0f;
    bool  haveEnvelope_ = false;
    // prepare() reallocates (and therefore zeroes) the window and its
    // autocorrelation, but configure() used to rebuild them only when the FFT
    // size changed. Re-preparing at the same size then left an all-zero window,
    // which silences the whole wet path while leaving dry untouched. This flag
    // makes the dependency explicit instead of implied by the size comparison.
    bool  needsWindowRebuild_ = true;
    bool  haveResidual_ = false;

    std::vector<std::unique_ptr<RealFft>> ffts_;   // indexed by log2(size)
    RealFft* fft_ = nullptr;

    std::vector<float> window_;      // Hann, rebuilt on configure()
    std::vector<float> winAutocorr_; // for unbiasing the pitch autocorrelation
    std::vector<float> buf_, re_, im_, mag_, freq_, phase_, lastPhase_;
    std::vector<float> envLog_, cep_, cepRe_, cepIm_, env_, residual_;
    std::vector<float> acRe_, acIm_, ac_, cmndf_;
    std::vector<int>   peakBins_;
    std::vector<Peak>  peaks_;

    float pitchHz_ = 0.0f;
    float pitchConf_ = 0.0f;
    float rms_ = 0.0f;
    bool  firstFrame_ = true;
};

}  // namespace dsp
