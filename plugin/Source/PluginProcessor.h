#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Harmonizer.h"
#include "JazzVoicer.h"

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

    /**
     * What the host is actually delivering. The two audio buses are reported
     * separately on purpose: in Logic the signal arrives on the side chain, and
     * a single combined meter cannot tell you whether that routing worked.
     */
    struct Traffic {
        int   mainChannels = 0;
        int   sideChannels = 0;
        float mainPeak = 0.0f;
        float sidePeak = 0.0f;
        int   midiMessages = 0;   // running total since load
        int   noteOns = 0;
        int   lastNote = -1;
    };
    Traffic traffic() const {
        return {mainChannels_.load(), sideChannels_.load(), mainPeak_.load(), sidePeak_.load(),
                midiMessages_.load(), noteOns_.load(), lastNote_.load()};
    }
    void allNotesOff() {
        // Panic comes from the UI thread. The engine's own slots are dropped
        // here; jazz mode's bookkeeping is cleared by the audio thread on its
        // next pass, so that it re-voices rather than believing it is still
        // holding a chord nothing is sounding.
        jazzPanic_.store(true);
        engine_.allNotesOff();
    }

    /**
     * What jazz mode is doing right now, for the panel. Written by the audio
     * thread a field at a time; a torn read shows a one-frame-old chord on a
     * label, which is what these are for.
     */
    struct JazzView {
        bool  enabled = false;
        bool  sounding = false;      // a chord is being held up
        int   keyCentrePc = -1;
        bool  minorKey = false;
        int   scaleDegree = 0;
        int   chordRootPc = -1;
        int   typeIndex = 0;
        int   styleIndex = 0;
        int   melodyNote = -1;
        int   melodyDegree = 1;
        float melodyHz = 0.0f;
        int   heldKeys = 0;
        int   noteCount = 0;
        int   notes[jazz::kMaxVoicingNotes] = {};
        // The asked-for range was further from the played note than the engine
        // can shift, so the chord was brought closer to stay in tune.
        bool  rangeLimited = false;
        int   windowLow = 0;
        int   windowHigh = 127;
        const char* roman = "";
    };
    JazzView jazzView() const;

    /** The voicer's settings as the parameters currently read them. */
    jazz::Settings jazzSettings() const;

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

        // Jazz chord mode. Plugin only -- the app does not ship this.
        static constexpr const char* jazzMode = "jazzMode";
        static constexpr const char* jazzNinth = "jazzNinth";
        static constexpr const char* jazzEleventh = "jazzEleventh";
        static constexpr const char* jazzThirteenth = "jazzThirteenth";
        static constexpr const char* jazzOctave = "jazzOctave";
        static constexpr const char* jazzInversion = "jazzInversion";
        static constexpr const char* jazzRangeLow = "jazzRangeLow";
        static constexpr const char* jazzRangeHigh = "jazzRangeHigh";
        static constexpr const char* jazzSmoothness = "jazzSmoothness";
        static constexpr const char* jazzVoices = "jazzVoices";
        static constexpr const char* jazzShuffle = "jazzShuffle";
        static constexpr const char* jazzDouble = "jazzDouble";
        // One per voicing style, in jazz::Style order.
        static const char* const jazzStyle[jazz::kStyleCount];

        // Custom chord dictionary: a user-built alternative to the dictionary
        // baked into JazzVoicer.cpp. Plugin only, and off by default -- with
        // it off, or with both context toggles below off, jazz mode is
        // exactly what it always was.
        static constexpr const char* jazzCustomOn = "jazzCustomOn";
        static constexpr const char* jazzCustomUseMajor = "jazzCustomUseMajor";
        static constexpr const char* jazzCustomUseMinor = "jazzCustomUseMinor";
        // One chord type per scale degree, per context. Each entry is always
        // rooted on the note being played -- that is what guarantees the
        // played note stays a tone of the chord, the way the built-in
        // dictionary always promised, without the editor having to enforce it.
        static const char* const jazzCustomMajorType[12];
        static const char* const jazzCustomMinorType[12];
    };

    static const juce::StringArray kFftChoices;
    static const juce::StringArray kJazzStyleNames;
    static const juce::StringArray kJazzCustomTypeNames;   // "Maj7", "Dom7" ...

    /**
     * Custom chord dictionaries saved as named presets, independent of the
     * host's own session state -- so a dictionary built for one project can
     * be brought into another rather than living only in that project's file.
     * All of these touch the filesystem and parameters, so they are message
     * thread only, called from the editor.
     */
    static juce::File jazzDictionaryPresetDirectory();
    juce::StringArray jazzDictionaryPresetNames() const;
    bool saveJazzDictionaryPreset(const juce::String& name) const;
    bool loadJazzDictionaryPreset(const juce::String& name);
    bool deleteJazzDictionaryPreset(const juce::String& name) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushParameters();
    void handleAsyncUpdate() override;

    // --- jazz mode, all of it on the audio thread --------------------------
    void jazzReconfigure(bool enabled);
    void jazzUpdate(int frames);
    void jazzApply(const jazz::Voicing& voicing);
    void jazzSilence();
    void jazzPublish(const jazz::Voicing& voicing, float melodyHz, int heldKeys);

    template <typename T>
    T* raw(const char* id) { return reinterpret_cast<T*>(apvts.getRawParameterValue(id)); }

    dsp::Harmonizer engine_;

    juce::AudioBuffer<float> monoIn_, monoOut_;

    // Latency must be reported to the host so it can time-align the track, but
    // setLatencySamples() is a message-thread call, so the audio thread only
    // records the change and pokes the async updater.
    std::atomic<int> pendingLatency_{-1};
    std::atomic<int>   mainChannels_{0}, sideChannels_{0};
    std::atomic<float> mainPeak_{0.0f}, sidePeak_{0.0f};
    std::atomic<int> midiMessages_{0};
    std::atomic<int> noteOns_{0};
    std::atomic<int> lastNote_{-1};
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

    std::atomic<float>* pJazzMode_ = nullptr;
    std::atomic<float>* pJazzNinth_ = nullptr;
    std::atomic<float>* pJazzEleventh_ = nullptr;
    std::atomic<float>* pJazzThirteenth_ = nullptr;
    std::atomic<float>* pJazzOctave_ = nullptr;
    std::atomic<float>* pJazzInversion_ = nullptr;
    std::atomic<float>* pJazzRangeLow_ = nullptr;
    std::atomic<float>* pJazzRangeHigh_ = nullptr;
    std::atomic<float>* pJazzSmoothness_ = nullptr;
    std::atomic<float>* pJazzVoices_ = nullptr;
    std::atomic<float>* pJazzShuffle_ = nullptr;
    std::atomic<float>* pJazzDouble_ = nullptr;
    std::atomic<float>* pJazzStyle_[jazz::kStyleCount] = {};

    std::atomic<float>* pJazzCustomOn_ = nullptr;
    std::atomic<float>* pJazzCustomUseMajor_ = nullptr;
    std::atomic<float>* pJazzCustomUseMinor_ = nullptr;
    std::atomic<float>* pJazzCustomMajorType_[12] = {};
    std::atomic<float>* pJazzCustomMinorType_[12] = {};

    /** Sets a parameter by id from the message thread -- used by preset load,
     *  which has to write many parameters at once outside of any UI control. */
    void setParamValue(const char* id, float rawValue);

    jazz::Voicer jazzVoicer_;
    bool jazzOn_ = false;                    // what the last block ran as
    bool hostKeyDown_[128] = {};             // keys the host is holding
    bool jazzSounding_[128] = {};            // notes we are holding up ourselves
    int  jazzKeyVelocity_ = 100;
    int  jazzDecisionCountdown_ = 0;         // samples until the next decision
    int  jazzCandidateNote_ = -1;
    int  jazzCandidateTicks_ = 0;
    uint64_t jazzInputHash_ = 0;
    std::atomic<bool> jazzPanic_{false};

    // Read by the editor; see JazzView.
    std::atomic<bool>  jvSounding_{false};
    std::atomic<int>   jvKeyCentre_{-1}, jvDegree_{0}, jvRoot_{-1}, jvType_{0}, jvStyle_{0};
    std::atomic<int>   jvMelodyNote_{-1}, jvMelodyDegree_{1}, jvCount_{0}, jvHeldKeys_{0};
    std::atomic<bool>  jvMinor_{false}, jvLimited_{false};
    std::atomic<int>   jvWindowLow_{0}, jvWindowHigh_{127};
    std::atomic<float> jvMelodyHz_{0.0f};
    std::atomic<int>   jvNotes_[jazz::kMaxVoicingNotes];
    std::atomic<const char*> jvRoman_{""};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerAudioProcessor)
};
