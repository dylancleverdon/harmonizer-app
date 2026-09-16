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
        bool  customVoicing = false;   // named by a custom entry, not the built-in dictionary
        int   melodyNote = -1;
        int   melodyDegree = 1;
        float melodyHz = 0.0f;
        int   heldKeys = 0;
        // True when the key centre above came from a latch (the toggle, or
        // the sustain pedal standing in for it) rather than being read live
        // from currently held keys -- heldKeys can be 0 while this is true.
        bool  keyLatched = false;
        bool  sustainHeld = false;   // the sustain pedal (CC64) is down right now
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
        static constexpr const char* jazzVoicesAuto = "jazzVoicesAuto";
        static constexpr const char* jazzShuffle = "jazzShuffle";
        static constexpr const char* jazzDouble = "jazzDouble";
        // One per voicing style, in jazz::Style order.
        static const char* const jazzStyle[jazz::kStyleCount];

        // Semitones added to every held key before it is read as a key
        // centre -- for a transposing instrument (trumpet, clarinet...)
        // whose player thinks in written rather than concert pitch. Never
        // applied to the melody note, which is measured from real sound and
        // is concert pitch already.
        static constexpr const char* jazzTranspose = "jazzTranspose";

        // Freezes the key centre against key releases: once engaged it only
        // changes on a fresh key press, never a release, so lifting one
        // finger of a held minor chord can't be misread as "you meant
        // major" mid-release. See jazzLatchActive_.
        static constexpr const char* jazzLatchKeys = "jazzLatchKeys";

        // Custom chord dictionary: a user-built alternative to the dictionary
        // baked into JazzVoicer.cpp. Plugin only, and off by default -- with
        // it off, or with both context toggles below off, jazz mode is
        // exactly what it always was.
        static constexpr const char* jazzCustomOn = "jazzCustomOn";
        static constexpr const char* jazzCustomUseMajor = "jazzCustomUseMajor";
        static constexpr const char* jazzCustomUseMinor = "jazzCustomUseMinor";
        // Each scale degree's custom entry is an explicit voicing: up to
        // jazz::kMaxVoicingNotes semitone-offset "slots", per context. Each
        // entry is always rooted on the note being played -- that is what
        // guarantees the played note stays a tone of the chord, the way the
        // built-in dictionary always promised, without the editor having to
        // enforce it. IDs are generated rather than hand-written: 2 contexts
        // x 12 degrees x jazz::kMaxVoicingNotes slots is too many to list.
        static juce::String jazzCustomOffsetId(bool minor, int degree, int slot);
    };

    static const juce::StringArray kFftChoices;
    static const juce::StringArray kJazzStyleNames;

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

    /**
     * Custom voicing editing, for the keyboard editor in the Jazz page. All
     * message thread only, like every other editor-to-processor parameter
     * write -- the audio thread only ever reads the settled result through
     * jazzSettings().
     */
    jazz::CustomEntry jazzCustomEntry(bool minor, int degree) const;
    void setJazzCustomVoicingNote(bool minor, int degree, int semitoneOffset, bool on);
    void clearJazzCustomVoicing(bool minor, int degree);

    /** Copies one degree's voicing onto another (or into the other context),
     *  transposed by the semitone distance between the two degrees -- "copy
     *  this chord to a different note and shift it up or down." Overwrites
     *  whatever the destination had. */
    void copyJazzCustomVoicing(bool fromMinor, int fromDegree, bool toMinor, int toDegree);

    /** Copies every degree from one context onto the other, degree for
     *  degree -- no transposition, since major and minor already share the
     *  same twelve scale degrees. The way to reuse a table built for one
     *  context as a starting point for the other. */
    void copyJazzCustomTable(bool fromMinor, bool toMinor);

    /**
     * Record mode: while active, incoming MIDI note-ons are diverted from
     * their usual jobs (naming a key centre, or playing straight through)
     * into a capture buffer instead, exactly like clicking notes on the
     * keyboard editor but played on a real controller. Starting clears
     * whatever was captured before; stopping leaves the buffer alone, so a
     * "Save" button can commit it afterwards. Notes are absolute MIDI note
     * numbers, on the same middle-C-as-root convention the keyboard editor
     * itself uses -- the editor is what turns them into offsets.
     */
    void setJazzCustomRecording(bool active);
    bool jazzCustomRecording() const { return jazzRecordActive_.load(); }
    juce::Array<int> jazzCustomRecordedNotes() const;
    void clearJazzCustomRecordedNotes();

    struct MidiImportSummary {
        bool ok = false;
        juce::String error;
        int keySegments = 0;
        int chordsAnalyzed = 0;
        int degreesFilled = 0;
    };

    /**
     * Finds the key centre(s) and chords in a .mid file and replaces the
     * whole custom dictionary with what it found -- the same result running
     * the keyboard editor by hand would give, for as much of the twelve
     * degrees (each context) as the file gave evidence for. A degree the
     * file never touched is left blank, falling back to the built-in
     * dictionary the same way an unfilled hand-built entry does.
     */
    MidiImportSummary importJazzCustomDictionaryFromMidiFile(const juce::File& file);

    /** How a custom voicing's semitone offsets are packed into an
     *  AudioParameterInt: 0 means "unused", everything else maps onto
     *  -jazz::kMaxCustomOffset..+jazz::kMaxCustomOffset. */
    static int jazzCustomOffsetToRaw(int semitoneOffset);
    static int jazzCustomRawToOffset(int raw);

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
    std::atomic<float>* pJazzVoicesAuto_ = nullptr;
    std::atomic<float>* pJazzShuffle_ = nullptr;
    std::atomic<float>* pJazzDouble_ = nullptr;
    std::atomic<float>* pJazzStyle_[jazz::kStyleCount] = {};
    std::atomic<float>* pJazzTranspose_ = nullptr;
    std::atomic<float>* pJazzLatchKeys_ = nullptr;

    std::atomic<float>* pJazzCustomOn_ = nullptr;
    std::atomic<float>* pJazzCustomUseMajor_ = nullptr;
    std::atomic<float>* pJazzCustomUseMinor_ = nullptr;
    // [context: 0 = major, 1 = minor][degree 0..11][slot 0..kMaxVoicingNotes)
    std::atomic<float>* pJazzCustomOffset_[2][12][jazz::kMaxVoicingNotes] = {};

    // Record mode's capture buffer -- see setJazzCustomRecording(). Written
    // only from the audio thread, read from the message thread by the editor.
    std::atomic<bool> jazzRecordActive_{false};
    std::atomic<int> jazzRecordNotes_[jazz::kMaxVoicingNotes];
    void addJazzCustomRecordedNote(int note);

    /** Sets a parameter by id from the message thread -- used by preset load
     *  and by the custom voicing editor, both of which write parameters
     *  outside of any bound UI control. */
    void setParamValue(const juce::String& id, float rawValue);

    // --- key latch and sustain --------------------------------------------
    // Latch (the toggle, or the sustain pedal standing in for it while held)
    // freezes the key centre against releases. It is only ever written from
    // a fresh key press -- see the note-on handling in processBlock() -- so
    // a release can never change it, which is the whole point: lifting one
    // finger of a held minor chord should never read as "you meant major".
    bool jazzLatchActive_ = false;   // has anything been captured yet
    int  jazzLatchedKeyPc_ = 0;
    bool jazzLatchedMinor_ = false;
    // CC64 (sustain pedal), independent of jazzOn. Atomic only because
    // jazzView() (message thread) reads it for the status panel; every write
    // is from the audio thread.
    std::atomic<bool> jazzSustainHeld_{false};

    /** Currently held keys, each shifted by the transpose parameter -- what
     *  every reading of hostKeyDown_ should use instead of the raw notes, so
     *  latch capture and the live reading in jazzUpdate() never disagree. */
    int collectTransposedKeys(int* keys, int maxKeys) const;
    int transposeSemitones() const;

    /** Captures a fresh latch from a set of (already transposed) keys --
     *  lowest key names the centre, two or more means minor. */
    void latchKeysFrom(const int* keys, int count);

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
    std::atomic<bool>  jvCustomVoicing_{false};
    std::atomic<int>   jvMelodyNote_{-1}, jvMelodyDegree_{1}, jvCount_{0}, jvHeldKeys_{0};
    std::atomic<bool>  jvKeyLatched_{false};
    std::atomic<bool>  jvSustainHeld_{false};
    std::atomic<bool>  jvMinor_{false}, jvLimited_{false};
    std::atomic<int>   jvWindowLow_{0}, jvWindowHigh_{127};
    std::atomic<float> jvMelodyHz_{0.0f};
    std::atomic<int>   jvNotes_[jazz::kMaxVoicingNotes];
    std::atomic<const char*> jvRoman_{""};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonizerAudioProcessor)
};
