#pragma once
#include <vector>

namespace dsp {

// Iterative in-place radix-2 complex FFT. Twiddles and the bit-reversal
// permutation are precomputed once, so process() does no allocation and no
// trig -- both matter on the audio thread.
class Fft {
public:
    explicit Fft(int n);

    // In-place on separate real/imag arrays of length n().
    void forward(float* re, float* im) const;
    void inverse(float* re, float* im) const;   // includes the 1/n scaling

    int n() const { return n_; }

private:
    void transform(float* re, float* im, bool inverse) const;

    int n_ = 0;
    std::vector<float> cos_;   // n/2 twiddles
    std::vector<float> sin_;
    std::vector<int>   rev_;   // bit-reversal permutation
};

// Real-input FFT built on a half-length complex FFT. Produces n/2+1 bins.
// Roughly twice as fast as zero-padding the imaginary part, which is worth
// having when we run one analysis plus ten synthesis transforms per hop.
class RealFft {
public:
    explicit RealFft(int n);

    // x: n real samples -> re/im: n/2+1 bins.
    void forward(const float* x, float* re, float* im) const;
    // re/im: n/2+1 bins -> x: n real samples. forward/inverse reconstruct exactly.
    void inverse(const float* re, const float* im, float* x) const;

    int n() const { return n_; }
    int numBins() const { return n_ / 2 + 1; }

private:
    int n_ = 0;
    int half_ = 0;
    Fft fft_;                  // size n/2
    std::vector<float> cosT_;  // e^(-2*pi*i*k/n) for k = 0..half_-1
    std::vector<float> sinT_;
    mutable std::vector<float> zr_, zi_;   // scratch, sized once in the ctor
};

}  // namespace dsp
