#include "Resampler.h"

#include <cmath>
#include <algorithm>

namespace dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

int log2i(int v) {
    int r = 0;
    while ((1 << r) < v) ++r;
    return r;
}

// Windowed sinc, cutoff normalised to the high rate, unity DC gain.
std::vector<float> designLowpass(int taps, double cutoff) {
    std::vector<float> h(static_cast<size_t>(taps));
    const double centre = (taps - 1) / 2.0;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - centre;
        const double sinc = (std::fabs(m) < 1e-9)
                                ? 2.0 * cutoff
                                : std::sin(2.0 * kPi * cutoff * m) / (kPi * m);
        const double t = 2.0 * kPi * i / (taps - 1);
        const double win = 0.35875 - 0.48829 * std::cos(t)
                         + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        const double v = sinc * win;
        h[static_cast<size_t>(i)] = static_cast<float>(v);
        sum += v;
    }
    if (sum != 0.0) {
        for (auto& v : h) v = static_cast<float>(v / sum);
    }
    return h;
}
}  // namespace

// ---------------------------------------------------------------------------

void Decimator::prepare(int maxFactor) {
    const int levels = log2i(maxFactor) + 1;
    coeffs_.assign(static_cast<size_t>(levels), {});
    for (int l = 0; l < levels; ++l) {
        const int f = 1 << l;
        const int taps = kResamplerTapsPerPhase * f;
        // 0.45 of the *low* rate leaves a guard band for the transition.
        coeffs_[static_cast<size_t>(l)] =
            (f == 1) ? std::vector<float>{1.0f} : designLowpass(taps, 0.45 / f);
    }
    taps_ = kResamplerTapsPerPhase * maxFactor;
    hist_.assign(static_cast<size_t>(taps_), 0.0f);
    setFactor(1);
}

void Decimator::setFactor(int factor) {
    factor_ = std::max(1, factor);
    phase_ = 0;
}

void Decimator::reset() {
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    histPos_ = 0;
    phase_ = 0;
}

float Decimator::latencySamples() const {
    if (factor_ == 1) return 0.0f;
    return static_cast<float>(kResamplerTapsPerPhase * factor_ - 1) * 0.5f;
}

int Decimator::process(const float* in, int n, float* out) {
    if (factor_ == 1) {
        std::copy(in, in + n, out);
        return n;
    }

    const std::vector<float>& h = coeffs_[static_cast<size_t>(log2i(factor_))];
    const int taps = static_cast<int>(h.size());
    const int histLen = static_cast<int>(hist_.size());
    int produced = 0;

    for (int i = 0; i < n; ++i) {
        hist_[static_cast<size_t>(histPos_)] = in[i];
        histPos_ = (histPos_ + 1) % histLen;

        if (++phase_ >= factor_) {
            phase_ = 0;
            float acc = 0.0f;
            int p = histPos_ - 1;
            if (p < 0) p += histLen;
            for (int t = 0; t < taps; ++t) {
                acc += h[static_cast<size_t>(t)] * hist_[static_cast<size_t>(p)];
                if (--p < 0) p += histLen;
            }
            out[produced++] = acc;
        }
    }
    return produced;
}

// ---------------------------------------------------------------------------

void Interpolator::prepare(int maxFactor) {
    const int levels = log2i(maxFactor) + 1;
    coeffs_.assign(static_cast<size_t>(levels), {});
    for (int l = 0; l < levels; ++l) {
        const int f = 1 << l;
        const int taps = kResamplerTapsPerPhase * f;
        // Scale by f to make up the energy lost to zero stuffing.
        std::vector<float> h = (f == 1) ? std::vector<float>{1.0f}
                                        : designLowpass(taps, 0.45 / f);
        if (f != 1) {
            for (auto& v : h) v *= static_cast<float>(f);
        }
        coeffs_[static_cast<size_t>(l)] = std::move(h);
    }
    tapsPerPhase_ = kResamplerTapsPerPhase;
    hist_.assign(static_cast<size_t>(tapsPerPhase_ + 1), 0.0f);
    setFactor(1);
}

void Interpolator::setFactor(int factor) {
    factor_ = std::max(1, factor);
}

void Interpolator::reset() {
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    histPos_ = 0;
}

float Interpolator::latencySamples() const {
    if (factor_ == 1) return 0.0f;
    return static_cast<float>(kResamplerTapsPerPhase * factor_ - 1) * 0.5f;
}

int Interpolator::process(const float* in, int n, float* out) {
    if (factor_ == 1) {
        std::copy(in, in + n, out);
        return n;
    }

    const std::vector<float>& h = coeffs_[static_cast<size_t>(log2i(factor_))];
    const int histLen = static_cast<int>(hist_.size());
    int produced = 0;

    for (int i = 0; i < n; ++i) {
        hist_[static_cast<size_t>(histPos_)] = in[i];
        histPos_ = (histPos_ + 1) % histLen;

        // Each output phase p taps every factor_-th coefficient: the polyphase
        // decomposition, so cost per output sample is taps/factor, not taps.
        for (int p = 0; p < factor_; ++p) {
            float acc = 0.0f;
            int hp = histPos_ - 1;
            if (hp < 0) hp += histLen;
            for (int t = p; t < static_cast<int>(h.size()); t += factor_) {
                acc += h[static_cast<size_t>(t)] * hist_[static_cast<size_t>(hp)];
                if (--hp < 0) hp += histLen;
            }
            out[produced++] = acc;
        }
    }
    return produced;
}

}  // namespace dsp
