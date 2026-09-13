#pragma once
#include <vector>

namespace dsp {

// Integer-factor decimation and interpolation for the sample-rate quality mode.
// Factors are restricted to powers of two ({1,2,4}) so the internal FFT size
// stays a power of two and the window *duration* -- and therefore the latency --
// is unchanged when the rate drops. Coefficient sets for every supported factor
// are built once in prepare(); switching factors at runtime never allocates.

inline constexpr int kResamplerTapsPerPhase = 16;

class Decimator {
public:
    void prepare(int maxFactor);
    void setFactor(int factor);       // keeps the delay line, resets the phase
    void reset();

    // Consumes n high-rate samples, writes floor((n + phase) / factor) low-rate
    // samples to out. out must have room for n / factor + 1.
    int process(const float* in, int n, float* out);

    int factor() const { return factor_; }
    // Group delay contributed by this stage, in high-rate samples.
    float latencySamples() const;

private:
    int factor_ = 1;
    int taps_ = 0;
    int phase_ = 0;
    std::vector<std::vector<float>> coeffs_;   // indexed by log2(factor)
    std::vector<float> hist_;                  // circular, length taps_
    int histPos_ = 0;
};

class Interpolator {
public:
    void prepare(int maxFactor);
    void setFactor(int factor);
    void reset();

    // Consumes n low-rate samples, writes n * factor high-rate samples.
    int process(const float* in, int n, float* out);

    int factor() const { return factor_; }
    float latencySamples() const;              // in high-rate samples

private:
    int factor_ = 1;
    int tapsPerPhase_ = 0;
    std::vector<std::vector<float>> coeffs_;   // indexed by log2(factor)
    std::vector<float> hist_;                  // low-rate history
    int histPos_ = 0;
};

}  // namespace dsp
