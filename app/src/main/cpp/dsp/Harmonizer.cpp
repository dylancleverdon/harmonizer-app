#include "Harmonizer.h"

#include <chrono>
#include <cmath>

namespace dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kInHistLen = 8192;          // power of two, >= maxFft + one block

int log2i(int v) { int r = 0; while ((1 << r) < v) ++r; return r; }

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Cubic soft clip: unity slope through zero, flat at +/-1.5. Ten voices summed
// can easily exceed full scale on a loud note, and hard clipping there sounds
// far worse than a little saturation.
inline float softClip(float x) {
    if (x >= 1.5f) return 1.0f;
    if (x <= -1.5f) return -1.0f;
    return x - x * x * x * (1.0f / 6.75f);
}

inline float quantise(float x, int bits) {
    const float levels = static_cast<float>(1 << (bits - 1));
    return std::round(clampf(x, -1.0f, 1.0f) * levels) / levels;
}
}  // namespace

void Harmonizer::prepare(double hostSampleRate, int maxBlockFrames) {
    hostSampleRate_ = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
    maxBlock_ = std::min(2048, std::max(64, maxBlockFrames));

    analyzer_.prepare(kMaxFftSize);
    scratch_.prepare(kMaxFftSize);
    for (auto& v : voices_) v.prepare(kMaxPartials);

    decimator_.prepare(kMaxDecimation);
    interpolator_.prepare(kMaxDecimation);

    const int maxLevel = log2i(kMaxFftSize);
    ffts_.clear();
    ffts_.resize(static_cast<size_t>(maxLevel) + 1);
    for (int l = log2i(kMinFftSize); l <= maxLevel; ++l) {
        ffts_[static_cast<size_t>(l)] = std::make_unique<RealFft>(1 << l);
    }

    inHist_.assign(kInHistLen, 0.0f);
    inHistMask_ = kInHistLen - 1;

    synthWindow_.assign(static_cast<size_t>(kMaxFftSize), 0.0f);
    frameBuf_.assign(static_cast<size_t>(kMaxFftSize), 0.0f);
    ola_.assign(static_cast<size_t>(kMaxFftSize), 0.0f);
    hopBuf_.assign(static_cast<size_t>(kMaxFftSize), 0.0f);
    emitBuf_.assign(static_cast<size_t>(kMaxFftSize), 0.0f);

    const int intCap = maxBlock_ + kMaxFftSize;
    decBuf_.assign(static_cast<size_t>(maxBlock_ + 8), 0.0f);
    intBuf_.assign(static_cast<size_t>(intCap), 0.0f);
    interpOut_.assign(static_cast<size_t>(intCap) * kMaxDecimation, 0.0f);
    wetBlock_.assign(static_cast<size_t>(maxBlock_), 0.0f);

    wetIntFifo_.prepare(intCap * 2);
    wetHostFifo_.prepare(kMaxFftSize * 2 + maxBlock_ * 4);
    dryDelay_.prepare(kMaxFftSize);

    reconfigure(params_.fftSize.load(std::memory_order_relaxed), 1);
    reset();
}

void Harmonizer::reset() {
    std::fill(inHist_.begin(), inHist_.end(), 0.0f);
    std::fill(ola_.begin(), ola_.end(), 0.0f);
    inWritten_ = 0;
    nextAnalysisAt_ = fft_;
    olaPos_ = 0;

    decimator_.reset();
    interpolator_.reset();
    analyzer_.reset();
    for (auto& v : voices_) v.reset();

    wetIntFifo_.clear();
    wetHostFifo_.clear();
    wetHostFifo_.writeZeros(primeSamples_);
    dryDelay_.clear();
    dryDelay_.setDelay(primeSamples_);

    for (auto& s : slots_) s = Slot{};
    cpuLoadEma_ = 0.0f;
    adaptiveBoost_ = 0.0f;
    wetFade_ = 0.0f;
}

void Harmonizer::allNotesOff() {
    for (auto& s : slots_) { s.held = false; s.gainTarget = 0.0f; }
}

// Window size and sample-rate divisor both change the internal transform size.
// Everything was allocated at the maximum in prepare(), so this only recomputes
// coefficients and restarts the pipeline -- it never allocates.
void Harmonizer::reconfigure(int baseFft, int decimation) {
    int base = baseFft;
    if (base < kMinFftSize) base = kMinFftSize;
    if (base > kMaxFftSize) base = kMaxFftSize;
    base = 1 << log2i(base);

    // Never let the internal transform fall below the minimum useful size.
    const int maxD = std::max(1, base / kMinFftSize);
    int d = decimation;
    if (d > maxD) d = maxD;
    if (d != 1 && d != 2 && d != 4) d = (d >= 4) ? 4 : ((d >= 2) ? 2 : 1);

    baseFft_ = base;
    decimation_ = d;
    fft_ = base / d;
    hop_ = fft_ / kOverlap;
    internalRate_ = static_cast<float>(hostSampleRate_ / d);

    for (int n = 0; n < fft_; ++n) {
        synthWindow_[static_cast<size_t>(n)] =
            0.5f - 0.5f * static_cast<float>(std::cos(2.0 * kPi * n / fft_));
    }

    analyzer_.configure(fft_, internalRate_);
    decimator_.setFactor(d);
    decimator_.reset();
    interpolator_.setFactor(d);
    interpolator_.reset();

    std::fill(ola_.begin(), ola_.end(), 0.0f);
    olaPos_ = 0;
    inWritten_ = 0;
    nextAnalysisAt_ = fft_;
    std::fill(inHist_.begin(), inHist_.end(), 0.0f);

    for (auto& v : voices_) v.reset();

    // (fft - hop) * decimation is 3/4 of the base size whatever the divisor is,
    // and the resampler term is held at its worst case, so total latency stays
    // fixed as quality changes. That is deliberate: the sample-rate mode buys
    // CPU without moving the wet signal relative to the dry.
    primeSamples_ = (3 * baseFft_) / 4 + (kResamplerTapsPerPhase * kMaxDecimation - 1);

    wetIntFifo_.clear();
    wetHostFifo_.clear();
    wetHostFifo_.writeZeros(primeSamples_);
    dryDelay_.setDelay(primeSamples_);

    wetFade_ = 0.0f;                 // fade the wet path back in, no click
    reconfigCooldown_ = static_cast<int>(hostSampleRate_ * 0.25);
    mInternalRate_.store(internalRate_, std::memory_order_relaxed);
}

void Harmonizer::drainMidi() {
    MidiEvent e;
    while (midi_.pop(e)) {
        const uint8_t cmd = e.status & 0xF0u;
        if (cmd == 0x90u && e.data2 > 0) {
            // Note on: reuse the slot already holding this note, else a free
            // slot, else steal the oldest -- standard last-note-priority.
            int slot = -1;
            for (int i = 0; i < kMaxVoices; ++i) {
                if (slots_[static_cast<size_t>(i)].held &&
                    slots_[static_cast<size_t>(i)].note == e.data1) { slot = i; break; }
            }
            if (slot < 0) {
                for (int i = 0; i < kMaxVoices; ++i) {
                    if (!slots_[static_cast<size_t>(i)].held) { slot = i; break; }
                }
            }
            if (slot < 0) {
                uint64_t oldest = UINT64_MAX;
                for (int i = 0; i < kMaxVoices; ++i) {
                    if (slots_[static_cast<size_t>(i)].order < oldest) {
                        oldest = slots_[static_cast<size_t>(i)].order;
                        slot = i;
                    }
                }
            }
            Slot& s = slots_[static_cast<size_t>(slot)];
            s.held = true;
            s.note = e.data1;
            s.velocity = static_cast<float>(e.data2) / 127.0f;
            s.gainTarget = s.velocity;
            s.order = ++noteCounter_;
        } else if (cmd == 0x80u || (cmd == 0x90u && e.data2 == 0)) {
            for (auto& s : slots_) {
                if (s.held && s.note == e.data1) { s.held = false; s.gainTarget = 0.0f; }
            }
        } else if (cmd == 0xB0u && (e.data1 == 123 || e.data1 == 120)) {
            allNotesOff();
        }
    }
}

void Harmonizer::updateVoiceRatios() {
    const HarmonyMode mode =
        static_cast<HarmonyMode>(params_.harmonyMode.load(std::memory_order_relaxed));
    const float f0 = analyzer_.pitchHz();

    // ~15 ms gain slew: fast enough to feel immediate, slow enough that note
    // starts and stops do not click.
    const float coef = 1.0f - std::exp(-static_cast<float>(hop_) /
                                       (0.015f * internalRate_));
    int active = 0;

    for (auto& s : slots_) {
        if (!s.held && s.gain < 1e-4f) { s.gain = 0.0f; s.note = -1; continue; }
        if (s.note >= 0) {
            if (mode == HarmonyMode::FixedInterval) {
                // Everything is relative to middle C, exactly as specced:
                // E above middle C is 4 semitones = 400 cents = 2^(400/1200).
                s.ratio = std::exp2(static_cast<float>(s.note - 60) / 12.0f);
            } else if (f0 > 20.0f) {
                const float target = 440.0f * std::exp2(static_cast<float>(s.note - 69) / 12.0f);
                s.ratio = clampf(target / f0, 0.25f, 4.0f);
            }
            // In absolute mode with no confident pitch, the last ratio is held
            // rather than snapping to unison mid-phrase.
        }
        s.gain += (s.gainTarget - s.gain) * coef;
        if (s.gain > 1e-4f) ++active;
    }
    mVoices_.store(active, std::memory_order_relaxed);
    mPitch_.store(f0, std::memory_order_relaxed);
}


namespace {
// A table of random unit vectors beats calling sin/cos for every bin of every
// hop, and the residual only needs its phase to be incoherent, not unique.
constexpr int kPhaseTableSize = 1024;
struct PhaseTable {
    float c[kPhaseTableSize], s[kPhaseTableSize];
    PhaseTable() {
        uint32_t x = 22222u;
        for (int i = 0; i < kPhaseTableSize; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            const double a = 2.0 * kPi * (x / 4294967296.0);
            c[i] = static_cast<float>(std::cos(a));
            s[i] = static_cast<float>(std::sin(a));
        }
    }
};
const PhaseTable g_phase;
}  // namespace

void Harmonizer::synthesiseResidual(float gain) {
    if (gain <= 1e-5f) return;
    const int bins = analyzer_.numBins();
    const float* res = analyzer_.residual();
    const RealFft& fft = *ffts_[static_cast<size_t>(log2i(fft_))];

    for (int k = 0; k < bins; ++k) {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        const int idx = static_cast<int>(rng_ & (kPhaseTableSize - 1));
        const float m = res[k] * gain;
        scratch_.re[static_cast<size_t>(k)] = m * g_phase.c[idx];
        scratch_.im[static_cast<size_t>(k)] = m * g_phase.s[idx];
    }
    scratch_.im[0] = 0.0f;
    scratch_.im[static_cast<size_t>(bins - 1)] = 0.0f;

    fft.inverse(scratch_.re.data(), scratch_.im.data(), scratch_.time.data());

    // Successive frames of randomised phase add incoherently, so the overlap
    // gain is sqrt(1.5) rather than the 1.5 that coherent overlap-add gets.
    const float scale = 1.0f / std::sqrt(1.5f);
    int p = olaPos_;
    for (int n = 0; n < fft_; ++n) {
        ola_[static_cast<size_t>(p)] +=
            scratch_.time[static_cast<size_t>(n)] * synthWindow_[static_cast<size_t>(n)] * scale;
        if (++p >= fft_) p = 0;
    }
}

void Harmonizer::runHop() {
    // Copy the newest fft_ samples out of the circular history, oldest first.
    const int64_t start = nextAnalysisAt_ - fft_;
    for (int n = 0; n < fft_; ++n) {
        frameBuf_[static_cast<size_t>(n)] =
            inHist_[static_cast<size_t>((start + n) & inHistMask_)];
    }

    const bool formant = params_.formantCorrection.load(std::memory_order_relaxed);
    const bool absolute =
        static_cast<HarmonyMode>(params_.harmonyMode.load(std::memory_order_relaxed)) ==
        HarmonyMode::Absolute;
    // Below about eight partials the residual costs more than it contributes,
    // and the point of that setting is to be cheap.
    const bool wantResidual = partialsPerVoice_ >= 8;

    analyzer_.analyze(frameBuf_.data(), formant, absolute, partialsPerVoice_, wantResidual);
    updateVoiceRatios();

    int activeVoices = 0;
    float gainSqSum = 0.0f;
    for (const Slot& s : slots_) {
        if (s.gain > 1e-4f) { ++activeVoices; gainSqSum += s.gain * s.gain; }
    }
    // Summing correlated voices grows faster than incoherent sources would;
    // 1/sqrt(N) is the usual compromise between headroom and level drop.
    const float voiceScale = 1.0f / std::sqrt(static_cast<float>(std::max(1, activeVoices)));

    std::fill(hopBuf_.begin(), hopBuf_.begin() + hop_, 0.0f);
    const float nyquist = internalRate_ * 0.5f;
    for (int v = 0; v < kMaxVoices; ++v) {
        Slot& s = slots_[static_cast<size_t>(v)];
        if (s.gain <= 1e-4f || s.note < 0) continue;
        voices_[static_cast<size_t>(v)].update(analyzer_, s.ratio, formant, nyquist);
        voices_[static_cast<size_t>(v)].render(hopBuf_.data(), hop_, internalRate_,
                                               s.gain * voiceScale);
    }
    int p0 = olaPos_;
    for (int n = 0; n < hop_; ++n) {
        ola_[static_cast<size_t>(p0)] += hopBuf_[static_cast<size_t>(n)];
        if (++p0 >= fft_) p0 = 0;
    }

    // One residual pass for the whole chord, not one per voice: unvoiced sound
    // has no pitch to shift, so a single copy at the harmony's level is both
    // correct and constant-cost as voices are added.
    if (wantResidual && activeVoices > 0) {
        synthesiseResidual(std::sqrt(gainSqSum) * voiceScale);
    }

    // After adding the window that starts here, the oldest hop of the
    // accumulator has received every contribution it will ever get.
    int emit = olaPos_;
    for (int n = 0; n < hop_; ++n) {
        emitBuf_[static_cast<size_t>(n)] = ola_[static_cast<size_t>(emit)];
        ola_[static_cast<size_t>(emit)] = 0.0f;
        if (++emit >= fft_) emit = 0;
    }
    olaPos_ = emit;
    wetIntFifo_.write(emitBuf_.data(), hop_);
    nextAnalysisAt_ += hop_;
}

void Harmonizer::applyQualitySettings() {
    float amount = clampf(params_.qualityAmount.load(std::memory_order_relaxed), 0.0f, 1.0f);

    // Experimental: lean harder on the quality reduction as voices pile up, so
    // a ten-note chord costs closer to what one note costs.
    if (params_.adaptiveVoiceScaling.load(std::memory_order_relaxed)) {
        const int v = mVoices_.load(std::memory_order_relaxed);
        amount += 0.6f * static_cast<float>(std::max(0, v - 1)) /
                  static_cast<float>(kMaxVoices - 1);
    }
    amount = clampf(amount + adaptiveBoost_, 0.0f, 1.0f);
    mQuality_.store(amount, std::memory_order_relaxed);

    const QualityMode qm =
        static_cast<QualityMode>(params_.qualityMode.load(std::memory_order_relaxed));

    int wantD = 1;
    int wantBits = 32;
    int wantK = kMaxPartials;

    switch (qm) {
        case QualityMode::SampleRate:
            // Three steps: full rate, half, quarter. The internal transform
            // shrinks with the rate, so cost drops roughly in proportion.
            wantD = (amount < 0.34f) ? 1 : ((amount < 0.67f) ? 2 : 4);
            break;
        case QualityMode::BitDepth:
            wantBits = static_cast<int>(std::lround(24.0f - amount * 20.0f));
            wantBits = std::max(4, std::min(24, wantBits));
            break;
        case QualityMode::Vocoder:
            // Geometric from 96 partials down to 6: equal slider travel gives
            // equal proportional change, which is how it sounds.
            wantK = static_cast<int>(std::lround(
                static_cast<double>(kMaxPartials) *
                std::pow(6.0 / static_cast<double>(kMaxPartials), static_cast<double>(amount))));
            wantK = std::max(4, std::min(kMaxPartials, wantK));
            break;
    }

    partialsPerVoice_ = wantK;
    bitDepth_ = wantBits;
    mPartials_.store(wantK, std::memory_order_relaxed);
    mBits_.store(wantBits, std::memory_order_relaxed);

    const int wantBase = params_.fftSize.load(std::memory_order_relaxed);
    if (reconfigCooldown_ <= 0 && (wantBase != baseFft_ || wantD != decimation_)) {
        reconfigure(wantBase, wantD);
    }
}

void Harmonizer::updateAdaptive(float load) {
    cpuLoadEma_ += (load - cpuLoadEma_) * 0.05f;
    mCpu_.store(cpuLoadEma_, std::memory_order_relaxed);

    // Experimental: watch how close the DSP is running to the callback deadline
    // and trade quality for headroom before an underrun actually happens.
    if (adaptFrames_ < static_cast<int>(hostSampleRate_ * 0.1)) return;
    adaptFrames_ = 0;

    if (params_.adaptiveLatency.load(std::memory_order_relaxed)) {
        if (cpuLoadEma_ > 0.72f)      adaptiveBoost_ = std::min(1.0f, adaptiveBoost_ + 0.06f);
        else if (cpuLoadEma_ < 0.45f) adaptiveBoost_ = std::max(0.0f, adaptiveBoost_ - 0.03f);
    } else {
        adaptiveBoost_ = std::max(0.0f, adaptiveBoost_ - 0.1f);
    }
}

void Harmonizer::process(const float* in, float* out, int frames) {
    int done = 0;
    while (done < frames) {
        const int n = std::min(maxBlock_, frames - done);
        processChunk(in + done, out + done, n);
        done += n;
    }
}

void Harmonizer::processChunk(const float* in, float* out, int frames) {
    const auto t0 = std::chrono::steady_clock::now();

    drainMidi();
    applyQualitySettings();

    if (reconfigCooldown_ > 0) reconfigCooldown_ -= frames;
    adaptFrames_ += frames;

    float inPeak = 0.0f;
    for (int i = 0; i < frames; ++i) inPeak = std::max(inPeak, std::fabs(in[i]));
    mInPeak_.store(inPeak, std::memory_order_relaxed);

    if (params_.bypass.load(std::memory_order_relaxed)) {
        // Still run the dry delay so the latency does not jump when bypass is
        // toggled while monitoring.
        for (int i = 0; i < frames; ++i) out[i] = dryDelay_.process(in[i]);
        mOutPeak_.store(inPeak, std::memory_order_relaxed);
        return;
    }

    // 1. Host rate -> internal rate.
    const int m = decimator_.process(in, frames, decBuf_.data());
    for (int i = 0; i < m; ++i) {
        inHist_[static_cast<size_t>(inWritten_ & inHistMask_)] = decBuf_[static_cast<size_t>(i)];
        ++inWritten_;
    }

    // 2. Run every analysis hop the new input has made available.
    while (inWritten_ >= nextAnalysisAt_) runHop();

    // 3. Internal rate -> host rate.
    int avail = std::min(wetIntFifo_.available(), static_cast<int>(intBuf_.size()));
    if (avail > 0) {
        wetIntFifo_.read(intBuf_.data(), avail);
        const int produced = interpolator_.process(intBuf_.data(), avail, interpOut_.data());
        wetHostFifo_.write(interpOut_.data(), produced);
    }

    // 4. Mix against the delay-compensated dry signal.
    wetHostFifo_.read(wetBlock_.data(), frames);

    const float mix = clampf(params_.wetDry.load(std::memory_order_relaxed), 0.0f, 1.0f);
    // Equal power, so the perceived level holds steady across the knob's travel.
    const float dryGain = std::cos(mix * static_cast<float>(kPi) * 0.5f);
    const float wetGain = std::sin(mix * static_cast<float>(kPi) * 0.5f);
    const float outGain = params_.outputGain.load(std::memory_order_relaxed);
    const int bits = bitDepth_;
    const float fadeStep = 1.0f / (0.01f * static_cast<float>(hostSampleRate_));

    float outPeak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        float wet = wetBlock_[static_cast<size_t>(i)];
        if (bits < 32) wet = quantise(wet, bits);

        if (wetFade_ < 1.0f) {
            wetFade_ = std::min(1.0f, wetFade_ + fadeStep);
            wet *= wetFade_;
        }

        const float dry = dryDelay_.process(in[i]);
        const float y = softClip((dry * dryGain + wet * wetGain) * outGain);
        out[i] = y;
        outPeak = std::max(outPeak, std::fabs(y));
    }
    mOutPeak_.store(outPeak, std::memory_order_relaxed);

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const double budget = static_cast<double>(frames) / hostSampleRate_;
    updateAdaptive(budget > 0.0 ? static_cast<float>(elapsed / budget) : 0.0f);
}

float Harmonizer::algorithmicLatencyMs() const {
    return 1000.0f * static_cast<float>(primeSamples_) / static_cast<float>(hostSampleRate_);
}

Metrics Harmonizer::metrics() const {
    Metrics m;
    m.cpuLoad = mCpu_.load(std::memory_order_relaxed);
    m.effectiveQuality = mQuality_.load(std::memory_order_relaxed);
    m.activeVoices = mVoices_.load(std::memory_order_relaxed);
    m.partialsPerVoice = mPartials_.load(std::memory_order_relaxed);
    m.internalSampleRate = mInternalRate_.load(std::memory_order_relaxed);
    m.bitDepth = mBits_.load(std::memory_order_relaxed);
    m.algorithmicLatencyMs = algorithmicLatencyMs();
    m.detectedPitchHz = mPitch_.load(std::memory_order_relaxed);
    m.inputPeak = mInPeak_.load(std::memory_order_relaxed);
    m.outputPeak = mOutPeak_.load(std::memory_order_relaxed);
    return m;
}

}  // namespace dsp
