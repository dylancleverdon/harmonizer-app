#include "Analysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float  kEps = 1e-12f;

int log2i(int v) { int r = 0; while ((1 << r) < v) ++r; return r; }

// Wrap to (-pi, pi].
inline float princarg(float x) {
    const float twoPi = static_cast<float>(2.0 * kPi);
    x = std::fmod(x + static_cast<float>(kPi), twoPi);
    if (x < 0.0f) x += twoPi;
    return x - static_cast<float>(kPi);
}
}  // namespace

void Analyzer::prepare(int maxFftSize) {
    const int maxBins = maxFftSize / 2 + 1;

    ffts_.clear();
    const int maxLevel = log2i(maxFftSize);
    ffts_.resize(static_cast<size_t>(maxLevel) + 1);
    for (int l = log2i(kMinFftSize); l <= maxLevel; ++l) {
        ffts_[static_cast<size_t>(l)] = std::make_unique<RealFft>(1 << l);
    }

    // Everything is allocated once at the maximum size. configure() only ever
    // changes which prefix of these buffers is in use, so switching window size
    // or sample-rate divisor mid-stream cannot allocate on the audio thread.
    window_.assign(static_cast<size_t>(maxFftSize), 0.0f);
    winAutocorr_.assign(static_cast<size_t>(maxFftSize), 0.0f);
    buf_.assign(static_cast<size_t>(maxFftSize), 0.0f);
    cep_.assign(static_cast<size_t>(maxFftSize), 0.0f);
    ac_.assign(static_cast<size_t>(maxFftSize), 0.0f);
    cmndf_.assign(static_cast<size_t>(maxFftSize), 0.0f);

    re_.assign(static_cast<size_t>(maxBins), 0.0f);
    im_.assign(static_cast<size_t>(maxBins), 0.0f);
    mag_.assign(static_cast<size_t>(maxBins), 0.0f);
    freq_.assign(static_cast<size_t>(maxBins), 0.0f);
    phase_.assign(static_cast<size_t>(maxBins), 0.0f);
    lastPhase_.assign(static_cast<size_t>(maxBins), 0.0f);
    envLog_.assign(static_cast<size_t>(maxBins), 0.0f);
    cepRe_.assign(static_cast<size_t>(maxBins), 0.0f);
    cepIm_.assign(static_cast<size_t>(maxBins), 0.0f);
    env_.assign(static_cast<size_t>(maxBins), 1.0f);
    residual_.assign(static_cast<size_t>(maxBins), 0.0f);
    acRe_.assign(static_cast<size_t>(maxBins), 0.0f);
    acIm_.assign(static_cast<size_t>(maxBins), 0.0f);

    peakBins_.reserve(static_cast<size_t>(maxBins));
    peaks_.reserve(static_cast<size_t>(kMaxPartials));

    // Everything above was just zeroed, so whatever configure() is asked for
    // next must rebuild the window even if the size happens to match.
    needsWindowRebuild_ = true;
    configure(1024, 48000.0f);
}

void Analyzer::configure(int fftSize, float sampleRate) {
    const bool rebuild = (fftSize != fftSize_) || needsWindowRebuild_;
    fftSize_ = fftSize;
    sampleRate_ = sampleRate;
    fft_ = ffts_[static_cast<size_t>(log2i(fftSize))].get();

    if (rebuild) {
        for (int n = 0; n < fftSize_; ++n) {
            window_[static_cast<size_t>(n)] =
                0.5f - 0.5f * static_cast<float>(std::cos(2.0 * kPi * n / fftSize_));
        }
        // Autocorrelation of the analysis window, used to unbias the pitch
        // autocorrelation below. Computed via the transform rather than an
        // O(n^2) loop so configure() stays cheap enough to call on a restart.
        std::copy(window_.begin(), window_.begin() + fftSize_, buf_.begin());
        fft_->forward(buf_.data(), re_.data(), im_.data());
        for (int k = 0; k < numBins(); ++k) {
            re_[static_cast<size_t>(k)] = re_[static_cast<size_t>(k)] * re_[static_cast<size_t>(k)]
                                        + im_[static_cast<size_t>(k)] * im_[static_cast<size_t>(k)];
            im_[static_cast<size_t>(k)] = 0.0f;
        }
        fft_->inverse(re_.data(), im_.data(), winAutocorr_.data());
        needsWindowRebuild_ = false;
    }
    reset();
}

void Analyzer::reset() {
    std::fill(lastPhase_.begin(), lastPhase_.end(), 0.0f);
    std::fill(env_.begin(), env_.end(), 1.0f);
    peaks_.clear();
    pitchHz_ = 0.0f;
    pitchConf_ = 0.0f;
    rms_ = 0.0f;
    firstFrame_ = true;
    haveEnvelope_ = false;
    haveResidual_ = false;
}

void Analyzer::analyze(const float* frame, bool wantEnvelope, bool wantPitch, int maxPeaks,
                       bool wantResidual) {
    const int N = fftSize_;
    const int bins = numBins();
    const int hop = hopSize();

    double energy = 0.0;
    for (int n = 0; n < N; ++n) {
        const float x = frame[n];
        energy += static_cast<double>(x) * x;
        buf_[static_cast<size_t>(n)] = x * window_[static_cast<size_t>(n)];
    }
    rms_ = static_cast<float>(std::sqrt(energy / N));

    fft_->forward(buf_.data(), re_.data(), im_.data());

    // Magnitude, and the phase-vocoder estimate of each bin's true frequency:
    // how far the phase actually advanced over the hop versus how far the bin
    // centre frequency says it should have.
    const float expectedPerBin = static_cast<float>(2.0 * kPi * hop / N);
    const float binToHz = sampleRate_ / static_cast<float>(N);
    const float devScale = 1.0f / expectedPerBin;

    for (int k = 0; k < bins; ++k) {
        const float r = re_[static_cast<size_t>(k)];
        const float i = im_[static_cast<size_t>(k)];
        mag_[static_cast<size_t>(k)] = std::sqrt(r * r + i * i);

        const float ph = std::atan2(i, r);
        const float delta = ph - lastPhase_[static_cast<size_t>(k)];
        lastPhase_[static_cast<size_t>(k)] = ph;

        if (firstFrame_) {
            freq_[static_cast<size_t>(k)] = static_cast<float>(k) * binToHz;
        } else {
            const float dev = princarg(delta - expectedPerBin * static_cast<float>(k));
            freq_[static_cast<size_t>(k)] = (static_cast<float>(k) + dev * devScale) * binToHz;
        }
    }
    firstFrame_ = false;

    haveEnvelope_ = wantEnvelope;
    if (wantEnvelope) buildEnvelope();
    if (wantPitch) detectPitch(); else { pitchHz_ = 0.0f; pitchConf_ = 0.0f; }
    findPeaks(maxPeaks);

    haveResidual_ = wantResidual;
    if (wantResidual) {
        // Everything the peaks do not explain. Hann spreads a sinusoid over
        // about four bins, so clearing +/-2 around each peak removes it cleanly.
        std::copy(mag_.begin(), mag_.begin() + bins, residual_.begin());
        for (int k : peakBins_) {
            const int lo = std::max(0, k - 2);
            const int hi = std::min(bins - 1, k + 2);
            for (int j = lo; j <= hi; ++j) residual_[static_cast<size_t>(j)] = 0.0f;
        }
    }
}

// Cepstral envelope: low-quefrency liftering separates the slowly varying
// formant structure from the harmonic comb, so a shifted voice can keep the
// original's formants instead of sounding like a chipmunk.
void Analyzer::buildEnvelope() {
    const int N = fftSize_;
    const int bins = numBins();

    for (int k = 0; k < bins; ++k) {
        envLog_[static_cast<size_t>(k)] =
            std::log(mag_[static_cast<size_t>(k)] + 1e-7f);
        cepIm_[static_cast<size_t>(k)] = 0.0f;
    }

    fft_->inverse(envLog_.data(), cepIm_.data(), cep_.data());

    const int quefrencyCutoff = std::max(8, N / 32);
    for (int n = quefrencyCutoff + 1; n < N - quefrencyCutoff; ++n) {
        cep_[static_cast<size_t>(n)] = 0.0f;
    }

    fft_->forward(cep_.data(), cepRe_.data(), cepIm_.data());
    for (int k = 0; k < bins; ++k) {
        env_[static_cast<size_t>(k)] = std::exp(cepRe_[static_cast<size_t>(k)]);
    }
}

float Analyzer::envelopeAt(float hz) const {
    if (!haveEnvelope_) return 1.0f;
    const int bins = numBins();
    const float b = hz * static_cast<float>(fftSize_) / sampleRate_;
    if (b <= 0.0f) return env_[0];
    if (b >= static_cast<float>(bins - 1)) return env_[static_cast<size_t>(bins - 1)];
    const int i = static_cast<int>(b);
    const float f = b - static_cast<float>(i);
    return env_[static_cast<size_t>(i)] * (1.0f - f) + env_[static_cast<size_t>(i + 1)] * f;
}

// YIN-style pitch detection, but with the autocorrelation taken from the
// transform we already computed rather than a fresh O(n*lag) loop.
void Analyzer::detectPitch() {
    const int N = fftSize_;
    const int bins = numBins();

    for (int k = 0; k < bins; ++k) {
        const float m = mag_[static_cast<size_t>(k)];
        acRe_[static_cast<size_t>(k)] = m * m;
        acIm_[static_cast<size_t>(k)] = 0.0f;
    }
    fft_->inverse(acRe_.data(), acIm_.data(), ac_.data());

    // The window tapers the frame, which biases long lags downward; dividing by
    // the window's own autocorrelation takes that bias back out.
    const float w0 = winAutocorr_[0] + kEps;
    const float r0 = ac_[0] / w0;
    if (r0 <= kEps) { pitchHz_ = 0.0f; pitchConf_ = 0.0f; return; }

    const int minLag = std::max(2, static_cast<int>(sampleRate_ / 1000.0f));
    const int maxLag = std::min(N / 2 - 1, static_cast<int>(sampleRate_ / 55.0f));
    if (maxLag <= minLag) { pitchHz_ = 0.0f; pitchConf_ = 0.0f; return; }

    constexpr float kThreshold = 0.15f;
    double running = 0.0;
    int bestLag = -1;

    for (int tau = 1; tau <= maxLag; ++tau) {
        const float wn = winAutocorr_[static_cast<size_t>(tau)];
        const float rt = (std::fabs(wn) > 1e-4f * w0) ? ac_[static_cast<size_t>(tau)] / wn : 0.0f;
        const float d = 2.0f * (r0 - rt);
        running += static_cast<double>(d);
        cmndf_[static_cast<size_t>(tau)] =
            (running > 0.0) ? static_cast<float>(d * tau / running) : 1.0f;
    }

    // Take the lowest point of the *first* dip that breaks the threshold, not
    // the first sample that happens to be under it. The descent into a dip is
    // not perfectly monotonic, so stopping at the first non-increasing sample
    // lands high on the shoulder -- worth about 50 cents sharp, which is an
    // audibly wrong harmony.
    for (int tau = minLag; tau <= maxLag; ++tau) {
        if (cmndf_[static_cast<size_t>(tau)] >= kThreshold) continue;
        int t = tau;
        bestLag = tau;
        while (t <= maxLag && cmndf_[static_cast<size_t>(t)] < kThreshold) {
            if (cmndf_[static_cast<size_t>(t)] < cmndf_[static_cast<size_t>(bestLag)]) bestLag = t;
            ++t;
        }
        break;
    }

    if (bestLag < 0) {
        float best = 1e30f;
        for (int tau = minLag; tau <= maxLag; ++tau) {
            if (cmndf_[static_cast<size_t>(tau)] < best) { best = cmndf_[static_cast<size_t>(tau)]; bestLag = tau; }
        }
    }
    if (bestLag <= 0) { pitchHz_ = 0.0f; pitchConf_ = 0.0f; return; }

    // Parabolic refinement on the dip gives sub-sample lag resolution, which
    // matters: one sample of lag error at 200 Hz is already ~15 cents.
    float refined = static_cast<float>(bestLag);
    if (bestLag > 1 && bestLag < maxLag) {
        const float y0 = cmndf_[static_cast<size_t>(bestLag - 1)];
        const float y1 = cmndf_[static_cast<size_t>(bestLag)];
        const float y2 = cmndf_[static_cast<size_t>(bestLag + 1)];
        const float denom = 2.0f * (2.0f * y1 - y0 - y2);
        if (std::fabs(denom) > 1e-9f) refined += (y2 - y0) / denom;
    }

    pitchConf_ = 1.0f - cmndf_[static_cast<size_t>(bestLag)];
    pitchHz_ = (pitchConf_ > 0.45f && refined > 0.0f) ? sampleRate_ / refined : 0.0f;
}

void Analyzer::findPeaks(int maxPeaks) {
    peaks_.clear();
    peakBins_.clear();
    if (maxPeaks <= 0) return;

    const int bins = numBins();
    const int N = fftSize_;

    float maxMag = 0.0f;
    for (int k = 1; k < bins - 1; ++k) maxMag = std::max(maxMag, mag_[static_cast<size_t>(k)]);
    if (maxMag <= kEps) return;
    const float floorMag = maxMag * 1e-4f;   // -80 dB relative to the loudest peak

    for (int k = 2; k < bins - 2; ++k) {
        const float m = mag_[static_cast<size_t>(k)];
        if (m < floorMag) continue;
        if (m > mag_[static_cast<size_t>(k - 1)] && m >= mag_[static_cast<size_t>(k + 1)] &&
            m > mag_[static_cast<size_t>(k - 2)] && m >= mag_[static_cast<size_t>(k + 2)]) {
            peakBins_.push_back(k);
        }
    }
    if (peakBins_.empty()) return;

    const int take = std::min<int>(maxPeaks, static_cast<int>(peakBins_.size()));
    // Only the K loudest peaks survive -- this is exactly the knob the vocoder
    // quality mode turns.
    std::partial_sort(peakBins_.begin(), peakBins_.begin() + take, peakBins_.end(),
                      [this](int a, int b) {
                          return mag_[static_cast<size_t>(a)] > mag_[static_cast<size_t>(b)];
                      });
    peakBins_.resize(static_cast<size_t>(take));

    for (int k : peakBins_) {
        // Hann spreads a sinusoid over three bins with a near-constant sum, so
        // the three-bin sum recovers amplitude without needing to know the
        // fractional bin offset.
        const float sum3 = mag_[static_cast<size_t>(k - 1)] + mag_[static_cast<size_t>(k)]
                         + mag_[static_cast<size_t>(k + 1)];
        Peak p;
        p.amp = 2.0f * sum3 / static_cast<float>(N);
        p.freq = freq_[static_cast<size_t>(k)];
        if (p.freq > 0.0f && p.freq < sampleRate_ * 0.5f) peaks_.push_back(p);
    }
}

}  // namespace dsp
