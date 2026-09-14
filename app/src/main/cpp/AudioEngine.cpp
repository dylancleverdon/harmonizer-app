#include "AudioEngine.h"

#include <algorithm>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Harmonizer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Harmonizer", __VA_ARGS__)

namespace {
constexpr int32_t kMaxCallbackFrames = 2048;
}

AudioEngine::AudioEngine() {
    inMono_.assign(kMaxCallbackFrames, 0.0f);
    outMono_.assign(kMaxCallbackFrames, 0.0f);
    readScratch_.assign(static_cast<size_t>(kMaxCallbackFrames) * 2, 0.0f);
    harmonizer_.prepare(48000.0, 192);
}

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start(int32_t inputDeviceId, int32_t outputDeviceId, int32_t inputPreset,
                        int32_t requestedSampleRate, int32_t bufferBursts) {
    std::lock_guard<std::mutex> lock(streamLock_);
    restartInputDevice_ = inputDeviceId;
    restartOutputDevice_ = outputDeviceId;
    restartPreset_ = inputPreset;
    restartSampleRate_ = requestedSampleRate;
    restartBursts_ = bufferBursts;
    closeStreams();
    if (!openStreams(inputDeviceId, outputDeviceId, inputPreset,
                     requestedSampleRate, bufferBursts)) {
        closeStreams();
        return false;
    }
    running_.store(true);
    return true;
}

bool AudioEngine::openStreams(int32_t inputDeviceId, int32_t outputDeviceId,
                              int32_t inputPreset, int32_t requestedSampleRate,
                              int32_t bufferBursts) {
    // Input first: the output stream's callback reads from it, so it has to
    // exist before the callback can fire.
    oboe::AudioStreamBuilder inBuilder;
    inBuilder.setDirection(oboe::Direction::Input)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Mono)
            ->setInputPreset(static_cast<oboe::InputPreset>(inputPreset));
    if (inputDeviceId != kUnspecified) inBuilder.setDeviceId(inputDeviceId);
    if (requestedSampleRate > 0) {
        // Only ask for conversion when a rate is actually requested; left alone,
        // the stream opens at the device's native rate with nothing in the way.
        inBuilder.setSampleRate(requestedSampleRate)
                ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
    }

    oboe::Result r = inBuilder.openStream(inputStream_);
    if (r != oboe::Result::OK) {
        LOGE("input stream open failed: %s", oboe::convertToText(r));
        return false;
    }
    inputChannels_ = inputStream_->getChannelCount();

    oboe::AudioStreamBuilder outBuilder;
    outBuilder.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Stereo)
            // Match whatever the input actually opened at, so no rate conversion
            // is needed between the two halves of the duplex pair.
            ->setSampleRate(inputStream_->getSampleRate())
            ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
            ->setUsage(oboe::Usage::Media)
            ->setDataCallback(this)
            ->setErrorCallback(this);
    if (outputDeviceId != kUnspecified) outBuilder.setDeviceId(outputDeviceId);

    r = outBuilder.openStream(outputStream_);
    if (r != oboe::Result::OK) {
        LOGE("output stream open failed: %s", oboe::convertToText(r));
        return false;
    }
    outputChannels_ = outputStream_->getChannelCount();

    const int32_t sr = outputStream_->getSampleRate();
    const int32_t burst = outputStream_->getFramesPerBurst();
    sampleRate_.store(sr);
    burst_.store(burst);

    // One burst is the lowest latency and the least tolerant of a late callback;
    // more bursts trade milliseconds for robustness. Two is the usual sweet spot,
    // but under a heavy chord on a busy phone, three or four is what stops the
    // crackle.
    const int32_t bursts = std::max(1, std::min(8, bufferBursts));
    outputStream_->setBufferSizeInFrames(burst * bursts);
    bufferFrames_.store(outputStream_->getBufferSizeInFrames());

    harmonizer_.prepare(static_cast<double>(sr), std::min(kMaxCallbackFrames, burst * 4));

    r = inputStream_->requestStart();
    if (r != oboe::Result::OK) {
        LOGE("input start failed: %s", oboe::convertToText(r));
        return false;
    }
    r = outputStream_->requestStart();
    if (r != oboe::Result::OK) {
        LOGE("output start failed: %s", oboe::convertToText(r));
        return false;
    }

    LOGI("streams open: %d Hz (requested %d), burst %d x %d, in ch %d, out ch %d",
         sr, requestedSampleRate, burst, bursts, inputChannels_, outputChannels_);
    return true;
}

void AudioEngine::closeStreams() {
    if (outputStream_) {
        outputStream_->stop();
        outputStream_->close();
        outputStream_.reset();
    }
    if (inputStream_) {
        inputStream_->stop();
        inputStream_->close();
        inputStream_.reset();
    }
    running_.store(false);
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(streamLock_);
    harmonizer_.allNotesOff();
    closeStreams();
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream,
                                                    void* audioData, int32_t numFrames) {
    float* out = static_cast<float*>(audioData);
    const int32_t frames = std::min(numFrames, kMaxCallbackFrames);

    // Drain the input stream and keep only the newest block. If the input has
    // built up a backlog -- which happens at startup and after an underrun --
    // consuming it all here is what stops that backlog turning into permanent
    // added latency.
    int32_t got = 0;
    while (true) {
        auto result = inputStream_->read(readScratch_.data(), frames, 0);
        if (!result || result.value() <= 0) break;
        got = result.value();
        if (inputChannels_ == 1) {
            std::copy(readScratch_.begin(), readScratch_.begin() + got, inMono_.begin());
        } else {
            for (int32_t i = 0; i < got; ++i) {
                float sum = 0.0f;
                for (int32_t c = 0; c < inputChannels_; ++c) {
                    sum += readScratch_[static_cast<size_t>(i * inputChannels_ + c)];
                }
                inMono_[static_cast<size_t>(i)] = sum / static_cast<float>(inputChannels_);
            }
        }
        if (got < frames) break;
    }
    if (got < frames) {
        std::fill(inMono_.begin() + got, inMono_.begin() + frames, 0.0f);
    }

    harmonizer_.process(inMono_.data(), outMono_.data(), frames);

    if (outputChannels_ == 1) {
        std::copy(outMono_.begin(), outMono_.begin() + frames, out);
    } else {
        for (int32_t i = 0; i < frames; ++i) {
            const float v = outMono_[static_cast<size_t>(i)];
            for (int32_t c = 0; c < outputChannels_; ++c) {
                out[i * outputChannels_ + c] = v;
            }
        }
    }

    // Latency numbers are cheap to read here and the UI polls them.
    auto outLat = stream->calculateLatencyMillis();
    if (outLat) outputLatencyMs_.store(static_cast<float>(outLat.value()));
    if (inputStream_) {
        auto inLat = inputStream_->calculateLatencyMillis();
        if (inLat) inputLatencyMs_.store(static_cast<float>(inLat.value()));
    }

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    // Disconnects happen whenever the user unplugs an interface or headphones.
    LOGI("stream error after close (%s); reopening", oboe::convertToText(error));
    std::lock_guard<std::mutex> lock(streamLock_);
    closeStreams();
    if (openStreams(restartInputDevice_, restartOutputDevice_, restartPreset_,
                    restartSampleRate_, restartBursts_)) {
        running_.store(true);
    }
}

float AudioEngine::totalLatencyMs() const {
    return inputLatencyMs_.load() + outputLatencyMs_.load() +
           harmonizer_.algorithmicLatencyMs();
}

int AudioEngine::xRuns() const {
    if (!outputStream_) return 0;
    auto x = outputStream_->getXRunCount();
    return x ? x.value() : 0;
}
