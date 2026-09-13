#pragma once
#include <vector>
#include "Analysis.h"
#include "Types.h"

namespace dsp {

// ---------------------------------------------------------------------------
// Additive / "vocoder" synthesis.
//
// Each voice is rebuilt every hop from only the K loudest spectral peaks, then
// rendered by a bank of K interpolating oscillators. Cost is linear in K and in
// the voice count, with no per-voice transform at all, which is what makes the
// K knob a direct and predictable CPU control -- and what makes ten voices
// affordable on a phone.
// ---------------------------------------------------------------------------
class PartialVoice {
public:
    void prepare(int maxPartials);
    void reset();

    // Re-target the oscillator bank from this hop's analysis, shifted by ratio.
    void update(const Analyzer& an, float ratio, bool formantCorrection, float nyquist);
    // Adds gain-scaled output into out[0..n). Ramps frequency and amplitude
    // across the block so re-targeting never clicks.
    void render(float* out, int n, float sampleRate, float gain);

    int activeCount() const { return static_cast<int>(osc_.size()); }

private:
    struct Osc {
        float f0 = 0.0f, f1 = 0.0f;   // frequency at block start / end
        float a0 = 0.0f, a1 = 0.0f;   // amplitude at block start / end
        float phase = 0.0f;           // in turns, [0,1)
    };

    std::vector<Osc>   osc_, next_;
    std::vector<float> tf_, ta_;      // this hop's targets, sorted by frequency
    std::vector<int>   order_;
    std::vector<unsigned char> used_;   // preallocated: no audio-thread allocation
    int maxPartials_ = 0;
};

// Scratch space for the residual resynthesis, shared rather than per-voice.
struct SpectralScratch {
    std::vector<float> mag, freq, best, re, im, time;
    void prepare(int maxFft);
};

}  // namespace dsp
