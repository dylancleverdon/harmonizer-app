#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Harmonizer.h"

/**
 * Plugin wrapper around the shared harmoniser engine.
 *
 * The engine itself is the same code the Android app runs -- the DSP directory
 * has no platform dependencies precisely so it can be reused here without a
 * fork. This class does three things: hand the host's MIDI to the engine, sum
 * the track to mono and process it, and expose the controls as host-automatable
 * parameters.
 */
class HarmonizerAudioProcessor final : public juce::AudioProcessor,
                                       private juce::AsyncUpdater {
public:
    HarmonizerAudioProcessor();
    ~HarmonizerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Harmonizer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    dsp::Metrics metrics() const { return engine_.metrics(); }
    void allNotesOff() { engine_.allNotesOff(); }

    // Parameter identifiers, shared with the editor.
    struct ParamId {
        static constexpr const char* wetDry = "wetDry";
        static constexpr const char* outputGain = "outputGain";
        static constexpr const char* harmonyMode = "harmonyMode";
        static constexpr const char* chordDegree = "chordDegree";
        static constexpr const char* doubleAnchor = "doubleAnchor";
        static constexpr const char* qualityMode = "qualityMode";
        static constexpr const char* qualityAmount = "qualityAmount";
        static constexpr const char* formant = "formant";
        static constexpr const char* adaptLatency = "adaptLatency";
        static constexpr const char* adaptVoices = "adaptVoices";
        static constexpr const char* fftSize = "fftSize";
        static constexpr const char* bypass = "bypass";
    };

    static const juce::StringArray kFftChoices;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushParameters();
    void handleAsyncUpdate() override;

    template <typename T>
    T* raw(const char* id) { return reinterpret_cast<T*>(apvts.getRawParameterValue(id)); }

    dsp::Harmonizer engine_;

    juce::AudioBuffer<float> monoIn_, monoOut_;

    // Latency must be reported to the host so it can time-align the track, but
    // setLatencySamples() is a message-thread call, so the audio thread only
    // records the change and pokes the async updater.
    std::atomic<int> pendingLatency_{-1};
    int reportedLatency_ = -1;

    std::atomic<float>* pWetDry_ = nullptr;
    std::atomic<float>* pOutputGain_ = nullptr;
    std::atomic<float>* pHarmonyMode_ = nullptr;
    std::atomic<float>* pChordDegree_ = nullptr;
    std::atomic<float>* pDoubleAnchor_ = nullptr;
    std::atomic<float>* pQualityMode_ = nullptr;
    std::atomic<float>* pQualityAmount_ = nullptr;
    std::atomic<float>* pFormant_ = nullptr;
    std::atomic<float>* pAdaptLatency_ = nullptr;
    std::atomic<float>* pAdaptVoices_ = nullptr;
    std::atomic<float>* pFftSize_ = nullptr;
    std::atomic<float>* pBypass_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerAudioProcessor)
};
