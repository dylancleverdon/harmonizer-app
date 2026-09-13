#include "Fft.h"

#include <cmath>
#include <cstddef>
#include <utility>

namespace dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

int floorLog2(int n) {
    int levels = 0;
    while ((1 << levels) < n) ++levels;
    return levels;
}
}  // namespace

Fft::Fft(int n) : n_(n) {
    if (n_ < 1) n_ = 1;
    const int levels = floorLog2(n_);

    cos_.resize(static_cast<size_t>(n_ / 2));
    sin_.resize(static_cast<size_t>(n_ / 2));
    for (int i = 0; i < n_ / 2; ++i) {
        cos_[i] = static_cast<float>(std::cos(kTwoPi * i / n_));
        sin_[i] = static_cast<float>(std::sin(kTwoPi * i / n_));
    }

    rev_.resize(static_cast<size_t>(n_));
    for (int i = 0; i < n_; ++i) {
        unsigned x = static_cast<unsigned>(i);
        unsigned r = 0;
        for (int b = 0; b < levels; ++b) {
            r = (r << 1) | (x & 1u);
            x >>= 1;
        }
        rev_[i] = static_cast<int>(r);
    }
}

void Fft::transform(float* re, float* im, bool) const {
    if (n_ == 1) return;

    for (int i = 0; i < n_; ++i) {
        const int j = rev_[i];
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (int size = 2; size <= n_; size *= 2) {
        const int halfsize = size / 2;
        const int tablestep = n_ / size;
        for (int i = 0; i < n_; i += size) {
            for (int j = i, k = 0; j < i + halfsize; ++j, k += tablestep) {
                const int l = j + halfsize;
                const float c = cos_[k];
                const float s = sin_[k];
                const float tre =  re[l] * c + im[l] * s;
                const float tim = -re[l] * s + im[l] * c;
                re[l] = re[j] - tre;
                im[l] = im[j] - tim;
                re[j] += tre;
                im[j] += tim;
            }
        }
    }
}

void Fft::forward(float* re, float* im) const {
    transform(re, im, false);
}

void Fft::inverse(float* re, float* im) const {
    // Conjugate trick: swapping the real and imaginary planes turns the
    // forward butterfly into the inverse one.
    transform(im, re, true);
    const float scale = 1.0f / static_cast<float>(n_);
    for (int i = 0; i < n_; ++i) {
        re[i] *= scale;
        im[i] *= scale;
    }
}

// ---------------------------------------------------------------------------

RealFft::RealFft(int n) : n_(n), half_(n / 2), fft_(n / 2) {
    cosT_.resize(static_cast<size_t>(half_));
    sinT_.resize(static_cast<size_t>(half_));
    for (int k = 0; k < half_; ++k) {
        cosT_[k] = static_cast<float>(std::cos(kTwoPi * k / n_));
        sinT_[k] = static_cast<float>(std::sin(kTwoPi * k / n_));
    }
    zr_.resize(static_cast<size_t>(half_));
    zi_.resize(static_cast<size_t>(half_));
}

void RealFft::forward(const float* x, float* re, float* im) const {
    // Pack the n real samples into n/2 complex values, transform, then undo
    // the packing to recover the true spectrum.
    for (int j = 0; j < half_; ++j) {
        zr_[j] = x[2 * j];
        zi_[j] = x[2 * j + 1];
    }
    fft_.forward(zr_.data(), zi_.data());

    for (int k = 0; k < half_; ++k) {
        const int mk = (half_ - k) % half_;
        const float ar = zr_[k],  ai = zi_[k];
        const float br = zr_[mk], bi = -zi_[mk];   // conj(Z[half-k])

        const float feRe = 0.5f * (ar + br);
        const float feIm = 0.5f * (ai + bi);
        // Fo = -0.5i * (Z[k] - conj(Z[half-k]))
        const float foRe =  0.5f * (ai - bi);
        const float foIm = -0.5f * (ar - br);

        const float c = cosT_[k], s = sinT_[k];    // W = c - i*s
        re[k] = feRe + c * foRe + s * foIm;
        im[k] = feIm + c * foIm - s * foRe;
    }
    // Nyquist bin is real.
    re[half_] = zr_[0] - zi_[0];
    im[half_] = 0.0f;
}

void RealFft::inverse(const float* re, const float* im, float* x) const {
    for (int k = 0; k < half_; ++k) {
        const int mk = half_ - k;                  // uses bin half_ when k == 0
        const float ar = re[k],  ai = im[k];
        const float br = re[mk], bi = -im[mk];     // conj(X[half-k])

        const float feRe = 0.5f * (ar + br);
        const float feIm = 0.5f * (ai + bi);
        const float dRe  = 0.5f * (ar - br);
        const float dIm  = 0.5f * (ai - bi);

        const float c = cosT_[k], s = sinT_[k];    // W^-1 = c + i*s
        const float foRe = dRe * c - dIm * s;
        const float foIm = dRe * s + dIm * c;

        zr_[k] = feRe - foIm;                      // Z[k] = Fe + i*Fo
        zi_[k] = feIm + foRe;
    }

    fft_.inverse(zr_.data(), zi_.data());

    for (int j = 0; j < half_; ++j) {
        x[2 * j]     = zr_[j];
        x[2 * j + 1] = zi_[j];
    }
}

}  // namespace dsp
