#include "Voices.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

// A 4096-point sine table with linear interpolation sits around -84 dBFS of
// error, which is well under the noise floor of anything a phone mic captures,
// and costs a fraction of a real sin() call per oscillator per sample.
constexpr int kLutBits = 12;
constexpr int kLutSize = 1 << kLutBits;

struct SineLut {
    float t[kLutSize + 1];
    SineLut() {
        for (int i = 0; i <= kLutSize; ++i) {
            t[i] = static_cast<float>(std::sin(2.0 * kPi * i / kLutSize));
        }
    }
};
const SineLut g_sine;

inline float sineAt(float turns) {
    const float x = turns * static_cast<float>(kLutSize);
    int i = static_cast<int>(x);
    const float frac = x - static_cast<float>(i);
    i &= (kLutSize - 1);
    return g_sine.t[i] + frac * (g_sine.t[i + 1] - g_sine.t[i]);
}

inline float wrap01(float p) {
    p -= std::floor(p);
    return p;
}
}  // namespace

// ---------------------------------------------------------------------------

void PartialVoice::prepare(int maxPartials) {
    maxPartials_ = maxPartials;
    // Room for this hop's targets plus the previous hop's orphans, which get
    // one block to ramp to silence instead of being cut off.
    osc_.reserve(static_cast<size_t>(maxPartials) * 2);
    next_.reserve(static_cast<size_t>(maxPartials) * 2);
    tf_.reserve(static_cast<size_t>(maxPartials));
    ta_.reserve(static_cast<size_t>(maxPartials));
    order_.reserve(static_cast<size_t>(maxPartials));
    used_.assign(static_cast<size_t>(maxPartials) * 2, 0);
    reset();
}

void PartialVoice::reset() {
    osc_.clear();
    next_.clear();
}

void PartialVoice::update(const Analyzer& an, float ratio, bool formantCorrection,
                          float nyquist) {
    const std::vector<Peak>& peaks = an.peaks();

    tf_.clear();
    ta_.clear();
    order_.clear();
    for (size_t i = 0; i < peaks.size() && static_cast<int>(i) < maxPartials_; ++i) {
        order_.push_back(static_cast<int>(i));
    }
    std::sort(order_.begin(), order_.end(), [&peaks](int a, int b) {
        return peaks[static_cast<size_t>(a)].freq < peaks[static_cast<size_t>(b)].freq;
    });

    for (int idx : order_) {
        const Peak& p = peaks[static_cast<size_t>(idx)];
        const float shifted = p.freq * ratio;
        if (shifted <= 20.0f || shifted >= nyquist * 0.95f) continue;

        float amp = p.amp;
        if (formantCorrection && an.hasEnvelope()) {
            // Move the harmonic but leave the formant where it was: rescale by
            // how the original envelope differs between old and new frequency.
            const float srcEnv = an.envelopeAt(p.freq);
            const float dstEnv = an.envelopeAt(shifted);
            if (srcEnv > 1e-9f) amp *= std::min(4.0f, dstEnv / srcEnv);
        }
        tf_.push_back(shifted);
        ta_.push_back(amp);
    }

    // Match this hop's partials to the ones already sounding. Both lists are
    // sorted by frequency, so a single moving pointer finds the nearest
    // surviving oscillator; continuity of phase across hops is what keeps a
    // sustained note from buzzing.
    next_.clear();
    const int oldCount = static_cast<int>(osc_.size());
    std::fill(used_.begin(), used_.begin() + oldCount, 0);
    int i = 0;

    for (size_t j = 0; j < tf_.size(); ++j) {
        const float target = tf_[j];
        while (i + 1 < oldCount &&
               std::fabs(osc_[static_cast<size_t>(i + 1)].f1 - target) <
                   std::fabs(osc_[static_cast<size_t>(i)].f1 - target)) {
            ++i;
        }
        Osc o;
        // Within a minor third counts as the same partial continuing.
        const bool matched = (i < oldCount) && !used_[static_cast<size_t>(i)] &&
                             std::fabs(osc_[static_cast<size_t>(i)].f1 - target) <
                                 target * 0.19f;
        if (matched) {
            const Osc& prev = osc_[static_cast<size_t>(i)];
            o.f0 = prev.f1;
            o.a0 = prev.a1;
            o.phase = prev.phase;
            used_[static_cast<size_t>(i)] = 1;
            ++i;
        } else {
            o.f0 = target;    // new partial: start at pitch, fade up from silence
            o.a0 = 0.0f;
            o.phase = 0.0f;
        }
        o.f1 = target;
        o.a1 = ta_[j];
        next_.push_back(o);
    }

    for (int k = 0; k < oldCount; ++k) {
        if (used_[static_cast<size_t>(k)]) continue;
        const Osc& prev = osc_[static_cast<size_t>(k)];
        if (prev.a1 <= 1e-6f) continue;
        Osc o = prev;
        o.f0 = prev.f1;
        o.a0 = prev.a1;
        o.a1 = 0.0f;          // one block to ramp out rather than a hard stop
        next_.push_back(o);
    }

    osc_.swap(next_);
}

void PartialVoice::render(float* out, int n, float sampleRate, float gain) {
    if (n <= 0 || osc_.empty()) return;
    const float invN = 1.0f / static_cast<float>(n);
    const float invSr = 1.0f / sampleRate;

    for (Osc& o : osc_) {
        if (o.a0 <= 1e-7f && o.a1 <= 1e-7f) { o.f0 = o.f1; o.a0 = o.a1; continue; }

        const float df = (o.f1 - o.f0) * invN;
        const float da = (o.a1 - o.a0) * invN;
        float f = o.f0;
        float a = o.a0 * gain;
        const float dag = da * gain;
        float phase = o.phase;

        for (int s = 0; s < n; ++s) {
            out[s] += a * sineAt(phase);
            phase += f * invSr;
            if (phase >= 1.0f) phase -= 1.0f;
            f += df;
            a += dag;
        }
        o.phase = wrap01(phase);
        o.f0 = o.f1;
        o.a0 = o.a1;
    }

    // Oscillators that finished ramping to silence are done.
    osc_.erase(std::remove_if(osc_.begin(), osc_.end(),
                              [](const Osc& o) { return o.a1 <= 1e-7f; }),
               osc_.end());
}

// ---------------------------------------------------------------------------

void SpectralScratch::prepare(int maxFft) {
    const size_t bins = static_cast<size_t>(maxFft / 2 + 1);
    mag.assign(bins, 0.0f);
    freq.assign(bins, 0.0f);
    best.assign(bins, 0.0f);
    re.assign(bins, 0.0f);
    im.assign(bins, 0.0f);
    time.assign(static_cast<size_t>(maxFft), 0.0f);
}

}  // namespace dsp
