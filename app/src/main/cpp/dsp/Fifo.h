#pragma once
#include <algorithm>
#include <vector>

namespace dsp {

// Plain circular FIFO of floats. Single producer and consumer are both the
// audio thread here, so no atomics are needed -- this exists to keep the
// rate-conversion bookkeeping readable, not for cross-thread handoff.
class Fifo {
public:
    void prepare(int capacity) {
        buf_.assign(static_cast<size_t>(capacity), 0.0f);
        clear();
    }

    void clear() { r_ = 0; w_ = 0; count_ = 0; }

    int available() const { return count_; }
    int capacity() const { return static_cast<int>(buf_.size()); }

    void write(const float* src, int n) {
        for (int i = 0; i < n; ++i) {
            buf_[static_cast<size_t>(w_)] = src[i];
            w_ = (w_ + 1) % static_cast<int>(buf_.size());
            if (count_ < static_cast<int>(buf_.size())) ++count_;
            else r_ = (r_ + 1) % static_cast<int>(buf_.size());   // overwrite oldest
        }
    }

    void writeZeros(int n) {
        for (int i = 0; i < n; ++i) {
            buf_[static_cast<size_t>(w_)] = 0.0f;
            w_ = (w_ + 1) % static_cast<int>(buf_.size());
            if (count_ < static_cast<int>(buf_.size())) ++count_;
            else r_ = (r_ + 1) % static_cast<int>(buf_.size());
        }
    }

    // Reads n samples, zero-filling anything not yet available.
    void read(float* dst, int n) {
        for (int i = 0; i < n; ++i) {
            if (count_ > 0) {
                dst[i] = buf_[static_cast<size_t>(r_)];
                r_ = (r_ + 1) % static_cast<int>(buf_.size());
                --count_;
            } else {
                dst[i] = 0.0f;
            }
        }
    }

private:
    std::vector<float> buf_;
    int r_ = 0, w_ = 0, count_ = 0;
};

// Fixed-length delay line, used to time-align the dry path with the engine's
// algorithmic latency. Without this the bypass signal arrives early and combs
// against the wet signal as the mix knob is turned.
class DelayLine {
public:
    void prepare(int maxDelay) {
        buf_.assign(static_cast<size_t>(maxDelay + 1), 0.0f);
        pos_ = 0;
        delay_ = 0;
    }
    void setDelay(int d) { delay_ = std::max(0, std::min(d, static_cast<int>(buf_.size()) - 1)); }
    int delay() const { return delay_; }
    void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; }

    float process(float x) {
        const int len = static_cast<int>(buf_.size());
        buf_[static_cast<size_t>(pos_)] = x;
        int rp = pos_ - delay_;
        if (rp < 0) rp += len;
        pos_ = (pos_ + 1) % len;
        return buf_[static_cast<size_t>(rp)];
    }

private:
    std::vector<float> buf_;
    int pos_ = 0;
    int delay_ = 0;
};

}  // namespace dsp
