#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <oboe/Oboe.h>

#include "dsp/Harmonizer.h"

// Oboe full-duplex wrapper. The output stream drives: its data callback pulls
// whatever the input stream has ready, runs the harmoniser, and writes the
// result. Everything here runs on the audio thread, so nothing in this file may
// allocate, lock, or call back into the JVM.
class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    // Device ids come from Android's AudioDeviceInfo; kUnspecified lets the
    // platform choose. inputPreset maps to oboe::InputPreset.
    bool start(int32_t inputDeviceId, int32_t outputDeviceId, int32_t inputPreset);
    void stop();
    bool isRunning() const { return running_.load(); }

    dsp::Harmonizer& harmonizer() { return harmonizer_; }

    // Round-trip latency in milliseconds: hardware in + engine + hardware out.
    float totalLatencyMs() const;
    float inputLatencyMs() const  { return inputLatencyMs_.load(); }
    float outputLatencyMs() const { return outputLatencyMs_.load(); }
    int   xRuns() const;
    int32_t actualSampleRate() const { return sampleRate_.load(); }
    int32_t burstFrames() const { return burst_.load(); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

    static constexpr int32_t kUnspecified = -1;

private:
    bool openStreams(int32_t inputDeviceId, int32_t outputDeviceId, int32_t inputPreset);
    void closeStreams();

    std::shared_ptr<oboe::AudioStream> inputStream_;
    std::shared_ptr<oboe::AudioStream> outputStream_;
    std::mutex streamLock_;

    dsp::Harmonizer harmonizer_;

    std::vector<float> inMono_;
    std::vector<float> outMono_;
    std::vector<float> readScratch_;

    int32_t inputChannels_ = 1;
    int32_t outputChannels_ = 2;

    std::atomic<bool>  running_{false};
    std::atomic<float> inputLatencyMs_{0.0f};
    std::atomic<float> outputLatencyMs_{0.0f};
    std::atomic<int32_t> sampleRate_{48000};
    std::atomic<int32_t> burst_{192};

    int32_t restartInputDevice_ = kUnspecified;
    int32_t restartOutputDevice_ = kUnspecified;
    int32_t restartPreset_ = 0;
};
