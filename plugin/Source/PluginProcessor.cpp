#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginUpdater.h"

#include "JazzMidiImport.h"

#include <mutex>
#include <vector>

namespace {

// Order shown in the UI. The engine's own enum values are deliberately looked up
// through a table rather than assumed to match the menu order -- the Android
// build once had these transposed, which silently swapped two of the modes.
constexpr int kQualityModeIds[] = {
    static_cast<int>(dsp::QualityMode::Vocoder),
    static_cast<int>(dsp::QualityMode::SampleRate),
    static_cast<int>(dsp::QualityMode::BitDepth),
};

constexpr int kHarmonyModeIds[] = {
    static_cast<int>(dsp::HarmonyMode::FixedInterval),
    static_cast<int>(dsp::HarmonyMode::Absolute),
    static_cast<int>(dsp::HarmonyMode::ChordVoicing),
};

constexpr int kChordDegrees[] = {1, 3, 5, 7, 9, 11, 13};

constexpr int kFftSizes[] = {256, 512, 1024, 2048};

// Jazz mode's register controls, as the menu order maps onto actual shifts.
constexpr int kJazzOctaves[] = {-2, -1, 0, 1, 2};
constexpr int kJazzInversions[] = {-3, -2, -1, 0, 1, 2, 3};

// How often the played note is re-read, and how long it has to stay put before
// the chord follows it. Five milliseconds is far finer than anyone plays, and
// three of them is quick enough to feel instant while still ignoring the slide
// through a neighbouring note on the way to this one.
constexpr double kJazzDecisionSeconds = 0.005;
constexpr int    kJazzStableTicks = 3;
// Within this much of a tempered note, the reading is taken as that note.
constexpr float  kJazzCentsWindow = 40.0f;

template <typename T, size_t N>
int pick(const T (&table)[N], float normalisedIndex) {
    const int i = juce::jlimit(0, static_cast<int>(N) - 1,
                               static_cast<int>(std::lround(normalisedIndex)));
    return table[i];
}

/** "D3", "Gb5" -- flats, to match the way the chord symbols are spelled. */
juce::String noteLabel(int note) {
    return juce::String(jazz::pitchClassName(note)) + juce::String(note / 12 - 1);
}

}  // namespace

const juce::StringArray HarmonizerAudioProcessor::kFftChoices { "256", "512", "1024", "2048" };

const juce::StringArray HarmonizerAudioProcessor::kJazzStyleNames {
    "Close", "Drop 2", "Drop 3", "Drop 2 & 4", "Rootless",
    "Quartal", "Shell", "Spread", "Cluster"
};

const char* const HarmonizerAudioProcessor::ParamId::jazzStyle[jazz::kStyleCount] = {
    "jazzStyleClose", "jazzStyleDrop2", "jazzStyleDrop3", "jazzStyleDrop24",
    "jazzStyleRootless", "jazzStyleQuartal", "jazzStyleShell", "jazzStyleSpread",
    "jazzStyleCluster"
};

juce::String HarmonizerAudioProcessor::ParamId::jazzCustomOffsetId(bool minor, int degree,
                                                                    int slot) {
    return juce::String(minor ? "jazzCustomMinorOffset" : "jazzCustomMajorOffset") +
           juce::String(degree) + "_" + juce::String(slot);
}

HarmonizerAudioProcessor::HarmonizerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         // Logic hosts a MIDI-controlled effect in an instrument
                         // slot, where the track itself carries no audio and the
                         // signal arrives on the side chain instead. Without this
                         // bus there is nothing for Logic to route into, and the
                         // plugin sits there silent -- dry included.
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "state", createLayout()) {
    // Sweep up any bundle left behind by a previous in-place update. Once per
    // process: a host may create many instances.
    static std::once_flag cleanupOnce;
    std::call_once(cleanupOnce, [] { PluginUpdater::cleanUpPreviousUpdate(); });

    pWetDry_ = apvts.getRawParameterValue(ParamId::wetDry);
    pOutputGain_ = apvts.getRawParameterValue(ParamId::outputGain);
    pHarmonyMode_ = apvts.getRawParameterValue(ParamId::harmonyMode);
    pChordDegree_ = apvts.getRawParameterValue(ParamId::chordDegree);
    pDoubleAnchor_ = apvts.getRawParameterValue(ParamId::doubleAnchor);
    pQualityMode_ = apvts.getRawParameterValue(ParamId::qualityMode);
    pQualityAmount_ = apvts.getRawParameterValue(ParamId::qualityAmount);
    pFormant_ = apvts.getRawParameterValue(ParamId::formant);
    pAdaptLatency_ = apvts.getRawParameterValue(ParamId::adaptLatency);
    pAdaptVoices_ = apvts.getRawParameterValue(ParamId::adaptVoices);
    pFftSize_ = apvts.getRawParameterValue(ParamId::fftSize);
    pBypass_ = apvts.getRawParameterValue(ParamId::bypass);

    pJazzMode_ = apvts.getRawParameterValue(ParamId::jazzMode);
    pJazzNinth_ = apvts.getRawParameterValue(ParamId::jazzNinth);
    pJazzEleventh_ = apvts.getRawParameterValue(ParamId::jazzEleventh);
    pJazzThirteenth_ = apvts.getRawParameterValue(ParamId::jazzThirteenth);
    pJazzOctave_ = apvts.getRawParameterValue(ParamId::jazzOctave);
    pJazzInversion_ = apvts.getRawParameterValue(ParamId::jazzInversion);
    pJazzRangeLow_ = apvts.getRawParameterValue(ParamId::jazzRangeLow);
    pJazzRangeHigh_ = apvts.getRawParameterValue(ParamId::jazzRangeHigh);
    pJazzSmoothness_ = apvts.getRawParameterValue(ParamId::jazzSmoothness);
    pJazzVoices_ = apvts.getRawParameterValue(ParamId::jazzVoices);
    pJazzVoicesAuto_ = apvts.getRawParameterValue(ParamId::jazzVoicesAuto);
    pJazzShuffle_ = apvts.getRawParameterValue(ParamId::jazzShuffle);
    pJazzDouble_ = apvts.getRawParameterValue(ParamId::jazzDouble);
    for (int i = 0; i < jazz::kStyleCount; ++i) {
        pJazzStyle_[i] = apvts.getRawParameterValue(ParamId::jazzStyle[i]);
    }
    pJazzTranspose_ = apvts.getRawParameterValue(ParamId::jazzTranspose);
    pJazzTransposeAudioIn_ = apvts.getRawParameterValue(ParamId::jazzTransposeAudioIn);
    pJazzLatchKeys_ = apvts.getRawParameterValue(ParamId::jazzLatchKeys);
    pJazzGlideMs_ = apvts.getRawParameterValue(ParamId::jazzGlideMs);

    pJazzCustomOn_ = apvts.getRawParameterValue(ParamId::jazzCustomOn);
    pJazzCustomUseMajor_ = apvts.getRawParameterValue(ParamId::jazzCustomUseMajor);
    pJazzCustomUseMinor_ = apvts.getRawParameterValue(ParamId::jazzCustomUseMinor);
    for (int ctx = 0; ctx < 2; ++ctx) {
        for (int degree = 0; degree < 12; ++degree) {
            for (int slot = 0; slot < jazz::kMaxVoicingNotes; ++slot) {
                pJazzCustomOffset_[ctx][degree][slot] = apvts.getRawParameterValue(
                    ParamId::jazzCustomOffsetId(ctx == 1, degree, slot));
            }
        }
    }
    for (auto& n : jvNotes_) n.store(-1);
    for (auto& n : jazzRecordNotes_) n.store(-1);

    // The shuffle should not play the same sequence of voicings every time the
    // plugin is loaded.
    jazzVoicer_.setSeed(static_cast<unsigned>(juce::Time::currentTimeMillis()) | 1u);
}

HarmonizerAudioProcessor::~HarmonizerAudioProcessor() {
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout
HarmonizerAudioProcessor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Step sizes and display text are set here rather than only on the sliders,
    // so the host's own generic parameter panel reads the same way -- and so
    // neither shows a mix of 0.4999999.
    const auto percent = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; });

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::wetDry, 1}, "Wet / Dry",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::outputGain, 1}, "Output Gain",
        NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 2) + " x"; })));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamId::harmonyMode, 1}, "Harmony Mode",
        StringArray{"Fixed interval", "Absolute pitch", "Chord voicing"}, 0));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamId::chordDegree, 1}, "You Are The",
        StringArray{"Root", "3rd", "5th", "7th", "9th", "11th", "13th"}, 0));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::doubleAnchor, 1}, "Double Your Note", false));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamId::qualityMode, 1}, "Quality Mode",
        StringArray{"Vocoder bands", "Sample rate", "Bit depth"}, 0));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::qualityAmount, 1}, "Reduction Amount",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.35f, percent));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::formant, 1}, "Formant Correction", true));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::adaptLatency, 1}, "Adapt To Load", false));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::adaptVoices, 1}, "Adapt To Voices", false));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamId::fftSize, 1}, "Analysis Window", kFftChoices, 2));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::bypass, 1}, "Bypass", false));

    // --- Jazz chord mode ---------------------------------------------------
    // Held keys name a key centre, the played note is read as a degree of it,
    // and the chord for that degree is voiced around the player.
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzMode, 1}, "Jazz Chord Mode", false));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzNinth, 1}, "Add 9ths", false));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzEleventh, 1}, "Add 11ths", false));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzThirteenth, 1}, "Add 13ths", false));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamId::jazzOctave, 1}, "Chord Octave",
        StringArray{"-2", "-1", "0", "+1", "+2"}, 2));

    layout.add(std::make_unique<AudioParameterChoice>(
        ParameterID{ParamId::jazzInversion, 1}, "Chord Inversion",
        StringArray{"-3", "-2", "-1", "0", "+1", "+2", "+3"}, 3));

    // The window every voice has to live in. Everything about how the chords
    // sit -- and how smoothly they lead -- comes back to these two.
    const auto asNote = AudioParameterIntAttributes().withStringFromValueFunction(
        [](int v, int) { return noteLabel(v); });

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{ParamId::jazzRangeLow, 1}, "Chord Range Low", 24, 96, 50, asNote));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{ParamId::jazzRangeHigh, 1}, "Chord Range High", 36, 108, 79, asNote));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::jazzSmoothness, 1}, "Voice Leading",
        NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent));

    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{ParamId::jazzVoices, 1}, "Chord Voices", 2, jazz::kMaxVoicingNotes, 5));

    // Auto ignores the slider above and lets the chord's own note count
    // through unmodified -- a custom voicing's exact tone count, or the
    // built-in chord's (three plus whichever extensions are switched on) --
    // rather than capping it to a number picked ahead of time.
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzVoicesAuto, 1}, "Auto Chord Voices", false));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzShuffle, 1}, "Shuffle Voicings", false));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzDouble, 1}, "Double Your Note (Jazz)", false));

    // None of these switched on means "choose for me", which is a mode in its
    // own right rather than an empty selection.
    for (int i = 0; i < jazz::kStyleCount; ++i) {
        layout.add(std::make_unique<AudioParameterBool>(
            ParameterID{ParamId::jazzStyle[i], 1},
            "Voicing: " + kJazzStyleNames[i], false));
    }

    // Display-only, both of them -- never reaches the engine, never shifts a
    // single Hz of real audio or a single held MIDI note used for the actual
    // chord. Each just renames what one input reads as, for a player who
    // thinks in a transposing instrument's written pitch: held keys get their
    // own control, the live audio input gets a separate one, so a Bb trumpet
    // and a concert-pitch keyboard can each be labelled correctly and still
    // read the same letter for the same underlying pitch. Zero (concert
    // pitch) changes nothing; common transpositions are labelled.
    const auto asTransposition = AudioParameterIntAttributes().withStringFromValueFunction(
        [](int v, int) {
            switch (v) {
                case 0:  return juce::String("Concert (C)");
                case -2: return juce::String("Bb");
                case 3:  return juce::String("Eb");
                case 5:  return juce::String("F");
                default: return (v > 0 ? juce::String("+") : juce::String("")) +
                                juce::String(v) + " st";
            }
        });
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{ParamId::jazzTranspose, 1}, "Keys Transpose", -12, 12, 0, asTransposition));
    layout.add(std::make_unique<AudioParameterInt>(
        ParameterID{ParamId::jazzTransposeAudioIn, 1}, "Audio In Transpose", -12, 12, 0,
        asTransposition));

    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzLatchKeys, 1}, "Latch Key Centre", false));

    // How long a chord change cross-fades between the tones leaving and the
    // tones arriving, instead of the ~15 ms floor that already exists just
    // to keep note starts and stops from clicking. 0 leaves that floor as
    // the whole story.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::jazzGlideMs, 1}, "Glide",
        NormalisableRange<float>(0.0f, 400.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return v < 1.0f ? juce::String("Off") : juce::String(juce::roundToInt(v)) + " ms";
        })));

    // --- Jazz custom chord dictionary ---------------------------------------
    // A user-built alternative to the dictionary above: pick the chord type
    // for each of the twelve scale degrees yourself. It is always rooted on
    // the note you play, which is what keeps the played note a tone of the
    // chord without the editor having to enforce anything. Off by default, so
    // it changes nothing until it is deliberately turned on; turning it back
    // off (or leaving both context toggles off) is how you get back to the
    // dictionary jazz mode always had.
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzCustomOn, 1}, "Use Custom Dictionary", false));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzCustomUseMajor, 1}, "Custom Dictionary For Major", false));
    layout.add(std::make_unique<AudioParameterBool>(
        ParameterID{ParamId::jazzCustomUseMinor, 1}, "Custom Dictionary For Minor", false));

    // Each degree's entry is an explicit voicing rather than a chord type:
    // jazz::kMaxVoicingNotes "slot" parameters, each either 0 (unused) or a
    // semitone offset above the root packed as jazzCustomOffsetToRaw() below
    // describes. Seeded from the built-in dictionary's own chord qualities --
    // third, fifth, seventh, each now rooted on its own degree instead --
    // so turning the custom dictionary on for the first time starts from a
    // voicing you already know rather than a blank keyboard.
    constexpr int kDefaultMajorType[12] = {0, 1, 3, 5, 0, 0, 1, 1, 0, 3, 1, 1};
    constexpr int kDefaultMinorType[12] = {3, 0, 4, 0, 1, 3, 4, 2, 0, 1, 1, 2};

    for (int ctx = 0; ctx < 2; ++ctx) {
        const bool minor = ctx == 1;
        for (int degree = 0; degree < 12; ++degree) {
            int seedOffsets[3] = {};
            const int typeIndex = minor ? kDefaultMinorType[degree] : kDefaultMajorType[degree];
            const int seedCount = jazz::chordTypeTones(
                static_cast<jazz::ChordType>(typeIndex), seedOffsets, 3);

            for (int slot = 0; slot < jazz::kMaxVoicingNotes; ++slot) {
                const int defaultRaw =
                    slot < seedCount ? jazzCustomOffsetToRaw(seedOffsets[slot]) : 0;
                layout.add(std::make_unique<AudioParameterInt>(
                    ParameterID{ParamId::jazzCustomOffsetId(minor, degree, slot), 1},
                    "Custom " + juce::String(minor ? "Minor " : "Major ") + juce::String(degree) +
                        " Slot " + juce::String(slot),
                    0, jazz::kMaxCustomOffset * 2 + 1, defaultRaw));
            }
        }
    }

    return layout;
}

int HarmonizerAudioProcessor::jazzCustomOffsetToRaw(int semitoneOffset) {
    return juce::jlimit(-jazz::kMaxCustomOffset, jazz::kMaxCustomOffset, semitoneOffset) +
           jazz::kMaxCustomOffset + 1;
}

int HarmonizerAudioProcessor::jazzCustomRawToOffset(int raw) {
    return raw - jazz::kMaxCustomOffset - 1;
}

jazz::Settings HarmonizerAudioProcessor::jazzSettings() const {
    jazz::Settings s;
    s.ninth = pJazzNinth_->load() > 0.5f;
    s.eleventh = pJazzEleventh_->load() > 0.5f;
    s.thirteenth = pJazzThirteenth_->load() > 0.5f;
    s.octaveShift = pick(kJazzOctaves, pJazzOctave_->load());
    s.inversionShift = pick(kJazzInversions, pJazzInversion_->load());
    s.rangeLow = static_cast<int>(std::lround(pJazzRangeLow_->load()));
    s.rangeHigh = static_cast<int>(std::lround(pJazzRangeHigh_->load()));
    s.smoothness = pJazzSmoothness_->load();
    s.maxNotes = pJazzVoicesAuto_->load() > 0.5f
                     ? jazz::kMaxVoicingNotes
                     : static_cast<int>(std::lround(pJazzVoices_->load()));
    s.shuffle = pJazzShuffle_->load() > 0.5f;
    s.doubleMelody = pJazzDouble_->load() > 0.5f;
    for (int i = 0; i < jazz::kStyleCount; ++i) {
        s.styles[i] = pJazzStyle_[i]->load() > 0.5f;
    }

    s.useCustomDictionary = pJazzCustomOn_->load() > 0.5f;
    s.customDict.useMajor = pJazzCustomUseMajor_->load() > 0.5f;
    s.customDict.useMinor = pJazzCustomUseMinor_->load() > 0.5f;
    for (int degree = 0; degree < 12; ++degree) {
        s.customDict.major[degree] = jazzCustomEntry(false, degree);
        s.customDict.minor[degree] = jazzCustomEntry(true, degree);
    }
    return s;
}

jazz::CustomEntry HarmonizerAudioProcessor::jazzCustomEntry(bool minor, int degree) const {
    jazz::CustomEntry entry;
    if (degree < 0 || degree > 11) return entry;
    const int ctx = minor ? 1 : 0;
    for (int slot = 0; slot < jazz::kMaxVoicingNotes; ++slot) {
        const int raw = static_cast<int>(std::lround(pJazzCustomOffset_[ctx][degree][slot]->load()));
        if (raw > 0) entry.offsets[entry.count++] = jazzCustomRawToOffset(raw);
    }
    return entry;
}

void HarmonizerAudioProcessor::setJazzCustomVoicingNote(bool minor, int degree,
                                                        int semitoneOffset, bool on) {
    if (degree < 0 || degree > 11) return;
    const int ctx = minor ? 1 : 0;
    int emptySlot = -1;
    for (int slot = 0; slot < jazz::kMaxVoicingNotes; ++slot) {
        const int raw = static_cast<int>(std::lround(pJazzCustomOffset_[ctx][degree][slot]->load()));
        if (raw > 0 && jazzCustomRawToOffset(raw) == semitoneOffset) {
            if (!on) setParamValue(ParamId::jazzCustomOffsetId(minor, degree, slot), 0.0f);
            return;
        }
        if (raw == 0 && emptySlot < 0) emptySlot = slot;
    }
    // Toggling a note already off with nothing to remove is a no-op; toggling
    // one on when every slot is already taken is quietly ignored rather than
    // replacing a note the player did not ask to lose.
    if (on && emptySlot >= 0) {
        setParamValue(ParamId::jazzCustomOffsetId(minor, degree, emptySlot),
                     static_cast<float>(jazzCustomOffsetToRaw(semitoneOffset)));
    }
}

void HarmonizerAudioProcessor::clearJazzCustomVoicing(bool minor, int degree) {
    if (degree < 0 || degree > 11) return;
    for (int slot = 0; slot < jazz::kMaxVoicingNotes; ++slot) {
        setParamValue(ParamId::jazzCustomOffsetId(minor, degree, slot), 0.0f);
    }
}

void HarmonizerAudioProcessor::copyJazzCustomVoicing(bool fromMinor, int fromDegree, bool toMinor,
                                                      int toDegree) {
    if (fromDegree < 0 || fromDegree > 11 || toDegree < 0 || toDegree > 11) return;
    if (fromMinor == toMinor && fromDegree == toDegree) return;

    const auto src = jazzCustomEntry(fromMinor, fromDegree);
    // The semitone distance between the two scale degrees -- copying degree 2
    // (a whole step above the root) onto degree 5 (a fourth) shifts every
    // tone in the voicing up a minor third, so it still resolves the same
    // way relative to whichever note reaches this new degree.
    const int shift = toDegree - fromDegree;

    clearJazzCustomVoicing(toMinor, toDegree);
    for (int i = 0; i < src.count; ++i) {
        const int shifted =
            juce::jlimit(-jazz::kMaxCustomOffset, jazz::kMaxCustomOffset, src.offsets[i] + shift);
        setParamValue(ParamId::jazzCustomOffsetId(toMinor, toDegree, i),
                     static_cast<float>(jazzCustomOffsetToRaw(shifted)));
    }
}

void HarmonizerAudioProcessor::copyJazzCustomTable(bool fromMinor, bool toMinor) {
    if (fromMinor == toMinor) return;
    for (int degree = 0; degree < 12; ++degree) {
        copyJazzCustomVoicing(fromMinor, degree, toMinor, degree);
    }
}

void HarmonizerAudioProcessor::setJazzCustomRecording(bool active) {
    if (active) clearJazzCustomRecordedNotes();
    jazzRecordActive_.store(active);
}

juce::Array<int> HarmonizerAudioProcessor::jazzCustomRecordedNotes() const {
    juce::Array<int> notes;
    for (auto& n : jazzRecordNotes_) {
        const int v = n.load();
        if (v >= 0) notes.add(v);
    }
    return notes;
}

void HarmonizerAudioProcessor::clearJazzCustomRecordedNotes() {
    for (auto& n : jazzRecordNotes_) n.store(-1);
}

HarmonizerAudioProcessor::MidiImportSummary
HarmonizerAudioProcessor::importJazzCustomDictionaryFromMidiFile(const juce::File& file) {
    MidiImportSummary summary;

    juce::FileInputStream stream(file);
    if (!stream.openedOk()) {
        summary.error = "Could not open that file.";
        return summary;
    }

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(stream)) {
        summary.error = "That did not read as a MIDI file.";
        return summary;
    }

    // readFrom() leaves timestamps in ticks -- exactly what the analysis
    // wants, since it works entirely in ticks and has no notion of tempo.
    const int ticksPerQuarterNote = midiFile.getTimeFormat();
    if (ticksPerQuarterNote <= 0) {
        summary.error = "This file uses SMPTE timecode, which import does not understand.";
        return summary;
    }

    juce::MidiMessageSequence merged;
    for (int i = 0; i < midiFile.getNumTracks(); ++i) merged.addSequence(*midiFile.getTrack(i), 0.0);
    merged.updateMatchedPairs();

    std::vector<jazz::ImportNote> notes;
    for (int i = 0; i < merged.getNumEvents(); ++i) {
        const auto* holder = merged.getEventPointer(i);
        if (holder == nullptr || !holder->message.isNoteOn() || holder->noteOffObject == nullptr) {
            continue;
        }
        const double startTicks = holder->message.getTimeStamp();
        const double endTicks = holder->noteOffObject->message.getTimeStamp();
        if (endTicks <= startTicks) continue;

        jazz::ImportNote note;
        note.startTick = static_cast<long long>(std::llround(startTicks));
        note.durationTicks = static_cast<long long>(std::llround(endTicks - startTicks));
        note.pitch = holder->message.getNoteNumber();
        notes.push_back(note);
    }

    if (notes.empty()) {
        summary.error = "No notes found in that file.";
        return summary;
    }

    const auto result = jazz::analyzeForCustomDictionary(
        notes.data(), static_cast<int>(notes.size()), ticksPerQuarterNote);
    if (result.keySegments == 0) {
        summary.error = "Could not find a key centre in that file.";
        return summary;
    }

    // Replaces the whole dictionary -- the same "start fresh from this" a
    // preset load gives, since a partial merge with whatever was there
    // before would leave it unclear which degrees came from which source.
    for (int ctx = 0; ctx < 2; ++ctx) {
        const bool minor = ctx == 1;
        for (int degree = 0; degree < 12; ++degree) {
            const auto& entry = minor ? result.dict.minor[degree] : result.dict.major[degree];
            clearJazzCustomVoicing(minor, degree);
            for (int i = 0; i < entry.count; ++i) {
                setJazzCustomVoicingNote(minor, degree, entry.offsets[i], true);
            }
        }
    }
    setParamValue(ParamId::jazzCustomUseMajor, 1.0f);
    setParamValue(ParamId::jazzCustomUseMinor, 1.0f);
    setParamValue(ParamId::jazzCustomOn, 1.0f);

    summary.ok = true;
    summary.keySegments = result.keySegments;
    summary.chordsAnalyzed = result.chordsAnalyzed;
    summary.degreesFilled = result.degreesFilled;
    return summary;
}

void HarmonizerAudioProcessor::addJazzCustomRecordedNote(int note) {
    if (note < 0 || note > 127) return;
    for (auto& n : jazzRecordNotes_) {
        if (n.load() == note) return;   // already captured
    }
    for (auto& n : jazzRecordNotes_) {
        int expected = -1;
        if (n.compare_exchange_strong(expected, note)) return;
    }
    // Every slot full (kMaxVoicingNotes already) -- further notes are quietly
    // dropped, the same cap the engine itself enforces on a voicing.
}

void HarmonizerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.prepare(sampleRate, samplesPerBlock);
    pushParameters();

    const int capacity = juce::jmax(samplesPerBlock, 2048);
    monoIn_.setSize(1, capacity, false, true, false);
    monoOut_.setSize(1, capacity, false, true, false);
    monoIn_.clear();
    monoOut_.clear();

    jazzVoiceCount_ = 0;
    jazzVoicer_.reset();
    jazzCandidateNote_ = -1;
    jazzCandidateTicks_ = 0;
    jazzInputHash_ = 0;
    jazzDecisionCountdown_ = 0;
    jazzOn_ = pJazzMode_->load() > 0.5f;
    jazzLatchActive_ = false;
    jazzSustainHeld_.store(false);

    reportedLatency_ = engine_.algorithmicLatencySamples();
    setLatencySamples(reportedLatency_);
}

bool HarmonizerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) {
        return false;
    }

    // Mono, stereo or absent is fine for either input. The main bus is empty
    // when a host puts this in an instrument slot and feeds the side chain
    // instead, so refusing a disabled main input rules out Logic's whole
    // MIDI-controlled-effect workflow.
    const auto acceptable = [](const juce::AudioChannelSet& set) {
        return set.isDisabled() || set == juce::AudioChannelSet::mono() ||
               set == juce::AudioChannelSet::stereo();
    };

    if (!acceptable(layouts.getMainInputChannelSet())) return false;
    if (layouts.inputBuses.size() > 1 && !acceptable(layouts.getChannelSet(true, 1))) {
        return false;
    }
    return true;
}

void HarmonizerAudioProcessor::pushParameters() {
    auto& p = engine_.params();
    p.wetDry.store(pWetDry_->load());
    p.outputGain.store(pOutputGain_->load());
    // Jazz mode drives the engine itself: it works out the chord and feeds the
    // notes in as absolute pitches, so it owns the harmony mode while it is on.
    p.harmonyMode.store(pJazzMode_->load() > 0.5f
                            ? static_cast<int>(dsp::HarmonyMode::Absolute)
                            : pick(kHarmonyModeIds, pHarmonyMode_->load()));
    p.chordAnchorDegree.store(pick(kChordDegrees, pChordDegree_->load()));
    p.doubleAnchor.store(pDoubleAnchor_->load() > 0.5f);
    p.qualityMode.store(pick(kQualityModeIds, pQualityMode_->load()));
    p.qualityAmount.store(pQualityAmount_->load());
    p.formantCorrection.store(pFormant_->load() > 0.5f);
    p.adaptiveLatency.store(pAdaptLatency_->load() > 0.5f);
    p.adaptiveVoiceScaling.store(pAdaptVoices_->load() > 0.5f);
    p.fftSize.store(pick(kFftSizes, pFftSize_->load()));
    p.bypass.store(pBypass_->load() > 0.5f);
    // Glide is a jazz mode control -- the ordinary harmony modes keep the
    // engine's plain click-avoidance floor rather than picking up whatever
    // the jazz page's knob happens to be set to.
    p.glideMs.store(pJazzMode_->load() > 0.5f ? pJazzGlideMs_->load() : 0.0f);
}

void HarmonizerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    pushParameters();

    const bool jazzOn = pJazzMode_->load() > 0.5f;

    // Notes arrive from the host rather than from a MIDI device, but the engine
    // consumes the same raw bytes either way -- except in jazz mode, where the
    // keys name a key centre rather than a harmony, and what reaches the engine
    // is the chord this plugin works out instead.
    for (const auto meta : midi) {
        const auto message = meta.getMessage();
        const auto* bytes = message.getRawData();
        const int size = message.getRawDataSize();
        if (size <= 0) continue;

        // Record mode borrows this same MIDI stream to build a custom
        // dictionary voicing by ear -- while it is active, notes go into its
        // capture buffer instead of naming a key centre or sounding through
        // the engine, the same way a click on the keyboard editor would.
        if (jazzRecordActive_.load()) {
            if (message.isNoteOn()) addJazzCustomRecordedNote(message.getNoteNumber());
            midiMessages_.fetch_add(1);
            continue;
        }

        if (message.isNoteOn()) {
            hostKeyDown_[message.getNoteNumber()] = true;
            jazzKeyVelocity_ = juce::jlimit(1, 127, static_cast<int>(message.getVelocity()));

            // Latch (or sustain standing in for it) only ever updates from a
            // fresh key press -- never a release, a few lines below, which is
            // the whole point of it. Recomputed from every key now held, so
            // adding a third note to an already-latched minor pair still
            // updates the lowest note correctly.
            if (jazzOn && (pJazzLatchKeys_->load() > 0.5f || jazzSustainHeld_.load())) {
                int keys[16];
                const int count = collectKeys(keys, 16);
                latchKeysFrom(keys, count);
            }
        } else if (message.isNoteOff()) {
            hostKeyDown_[message.getNoteNumber()] = false;
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            for (bool& key : hostKeyDown_) key = false;
        } else if (message.isController() && message.getControllerNumber() == 64) {
            // Sustain pedal. Tracked regardless of jazz mode, the same as
            // hostKeyDown_ above, so it is already correct the moment jazz
            // mode is switched on rather than only from the next press.
            jazzSustainHeld_.store(message.getControllerValue() >= 64);
        }

        if (!jazzOn) {
            dsp::MidiEvent event;
            event.status = static_cast<uint8_t>(bytes[0]);
            event.data1 = size > 1 ? static_cast<uint8_t>(bytes[1]) : 0;
            event.data2 = size > 2 ? static_cast<uint8_t>(bytes[2]) : 0;
            engine_.midiQueue().push(event);
        }

        midiMessages_.fetch_add(1);
        if (message.isNoteOn()) {
            lastNote_.store(message.getNoteNumber());
            noteOns_.fetch_add(1);
        }
    }

    if (jazzOn != jazzOn_) jazzReconfigure(jazzOn);
    if (jazzOn) jazzUpdate(buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    if (monoIn_.getNumSamples() < numSamples) {
        monoIn_.setSize(1, numSamples, false, true, false);
        monoOut_.setSize(1, numSamples, false, true, false);
    }

    // Take audio from wherever the host is providing it. On an audio track that
    // is the main bus; in Logic's instrument slot the track carries nothing and
    // the signal arrives on the side chain.
    //
    // Each bus is averaged to mono on its own and the results are summed, rather
    // than averaging every channel together. That distinction matters: Logic
    // hands over a silent two-channel main bus alongside the live side chain,
    // and averaging across all four channels quietly attenuated the only real
    // signal by 6 dB.
    float* in = monoIn_.getWritePointer(0);
    juce::FloatVectorOperations::clear(in, numSamples);

    float busPeak[2] = {0.0f, 0.0f};
    int busChannels[2] = {0, 0};

    for (int busIndex = 0; busIndex < juce::jmin(2, getBusCount(true)); ++busIndex) {
        const auto bus = getBusBuffer(buffer, true, busIndex);
        const int channels = bus.getNumChannels();
        busChannels[busIndex] = channels;
        if (channels <= 0) continue;

        const float scale = 1.0f / static_cast<float>(channels);
        for (int ch = 0; ch < channels; ++ch) {
            juce::FloatVectorOperations::addWithMultiply(in, bus.getReadPointer(ch), scale,
                                                         numSamples);
            busPeak[busIndex] = juce::jmax(busPeak[busIndex],
                                           bus.getMagnitude(ch, 0, numSamples));
        }
    }

    mainChannels_.store(busChannels[0]);
    sideChannels_.store(busChannels[1]);
    mainPeak_.store(busPeak[0]);
    sidePeak_.store(busPeak[1]);

    float* out = monoOut_.getWritePointer(0);
    engine_.process(in, out, numSamples);

    auto mainOut = getBusBuffer(buffer, false, 0);
    for (int ch = 0; ch < mainOut.getNumChannels(); ++ch) {
        juce::FloatVectorOperations::copy(mainOut.getWritePointer(ch), out, numSamples);
    }

    // The window size can change while running, which changes the engine's
    // latency. Tell the host, but off the audio thread.
    const int latency = engine_.algorithmicLatencySamples();
    if (latency != reportedLatency_) {
        pendingLatency_.store(latency);
        triggerAsyncUpdate();
    }
}

// ---------------------------------------------------------------------------
// Jazz chord mode. Everything below runs on the audio thread, allocates
// nothing, and talks to the engine through the same MIDI queue a keyboard would.
// ---------------------------------------------------------------------------

int HarmonizerAudioProcessor::keyTransposeSemitones() const {
    return static_cast<int>(std::lround(pJazzTranspose_->load()));
}

int HarmonizerAudioProcessor::melodyTransposeSemitones() const {
    return static_cast<int>(std::lround(pJazzTransposeAudioIn_->load()));
}

int HarmonizerAudioProcessor::collectKeys(int* keys, int maxKeys) const {
    int count = 0;
    for (int note = 0; note < 128 && count < maxKeys; ++note) {
        if (hostKeyDown_[note]) keys[count++] = note;
    }
    return count;
}

void HarmonizerAudioProcessor::latchKeysFrom(const int* keys, int count) {
    if (keys == nullptr || count <= 0) return;
    int lowest = keys[0];
    for (int i = 1; i < count; ++i) lowest = juce::jmin(lowest, keys[i]);
    jazzLatchedKeyPc_ = ((lowest % 12) + 12) % 12;
    jazzLatchedMinor_ = count >= 2;
    jazzLatchActive_ = true;
}

void HarmonizerAudioProcessor::jazzReconfigure(bool enabled) {
    // Whichever direction this is going, the engine is holding notes that mean
    // something different on the other side of the switch.
    engine_.allNotesOff();
    jazzVoiceCount_ = 0;

    jazzVoicer_.reset();
    jazzCandidateNote_ = -1;
    jazzCandidateTicks_ = 0;
    jazzInputHash_ = 0;
    jazzDecisionCountdown_ = 0;
    jazzLatchActive_ = false;

    // Leaving jazz mode with keys still down: hand those keys to the engine so
    // the ordinary modes pick up from where the keyboard actually is, rather
    // than staying silent until the player lifts and presses again.
    if (!enabled) {
        for (int note = 0; note < 128; ++note) {
            if (!hostKeyDown_[note]) continue;
            dsp::MidiEvent event;
            event.status = 0x90;
            event.data1 = static_cast<uint8_t>(note);
            event.data2 = static_cast<uint8_t>(jazzKeyVelocity_);
            engine_.midiQueue().push(event);
        }
    }

    jvSounding_.store(false);
    jvCount_.store(0);
    jvKeyLatched_.store(false);
    jvSustainHeld_.store(false);
    jazzOn_ = enabled;
}

void HarmonizerAudioProcessor::jazzSilence() {
    for (int i = 0; i < jazzVoiceCount_; ++i) {
        const int note = jazzVoiceNotes_[i];
        if (note < 0 || note > 127) continue;
        dsp::MidiEvent event;
        event.status = 0x80;
        event.data1 = static_cast<uint8_t>(note);
        event.data2 = 0;
        engine_.midiQueue().push(event);
    }
    jazzVoiceCount_ = 0;
    jazzVoicer_.reset();
    jazzCandidateNote_ = -1;
    jazzCandidateTicks_ = 0;
    jazzInputHash_ = 0;
    jvSounding_.store(false);
    jvCount_.store(0);
}

void HarmonizerAudioProcessor::jazzApply(const jazz::Voicing& voicing) {
    const int newCount = juce::jlimit(0, jazz::kMaxVoicingNotes, voicing.count);
    const float glideMs = pJazzGlideMs_->load();

    // No glide, or nothing was sounding to glide from (the first chord of a
    // phrase): the ordinary note-off/note-on diff. A tone common to both
    // chords is left alone rather than retriggered, which is what makes a
    // held common tone actually sustain through the change.
    if (glideMs <= 0.0f || jazzVoiceCount_ == 0) {
        bool wanted[128] = {};
        for (int i = 0; i < newCount; ++i) {
            const int note = voicing.notes[i];
            if (note >= 0 && note < 128) wanted[note] = true;
        }
        bool currentlyOn[128] = {};
        for (int i = 0; i < jazzVoiceCount_; ++i) {
            const int note = jazzVoiceNotes_[i];
            if (note >= 0 && note < 128) currentlyOn[note] = true;
        }
        for (int note = 0; note < 128; ++note) {
            if (currentlyOn[note] == wanted[note]) continue;
            dsp::MidiEvent event;
            event.status = wanted[note] ? 0x90 : 0x80;
            event.data1 = static_cast<uint8_t>(note);
            event.data2 = wanted[note] ? static_cast<uint8_t>(jazzKeyVelocity_) : 0;
            engine_.midiQueue().push(event);
        }
    } else {
        // Glide: match each previously-sounding voice to a new one and
        // retarget it in place instead of stopping and restarting -- the
        // engine slews the pitch across the glide time rather than
        // snapping.
        const int oldCount = jazzVoiceCount_;
        int oldNow[jazz::kMaxVoicingNotes];
        bool oldMatched[jazz::kMaxVoicingNotes] = {};
        for (int i = 0; i < oldCount; ++i) oldNow[i] = jazzVoiceNotes_[i];

        // A voice already sitting on a note the new chord wants is left
        // exactly alone, first, regardless of where either falls in its own
        // list -- the same common-tone rule the no-glide path applies,
        // needed here too so an already-correct voice never gets stolen
        // away from its tone just because ascending-index pairing below
        // would otherwise have matched it to something else.
        bool newMatched[jazz::kMaxVoicingNotes] = {};
        for (int i = 0; i < oldCount; ++i) {
            for (int j = 0; j < newCount; ++j) {
                if (newMatched[j]) continue;
                if (oldNow[i] == voicing.notes[j]) {
                    oldMatched[i] = true;
                    newMatched[j] = true;
                    break;
                }
            }
        }

        // What is left on each side, in order, is what actually needs to
        // move. Both are still ascending, so pairing them by index is
        // still pairing each with its nearest remaining neighbour.
        int remOld[jazz::kMaxVoicingNotes]; int remOldCount = 0;
        for (int i = 0; i < oldCount; ++i) if (!oldMatched[i]) remOld[remOldCount++] = oldNow[i];
        int remNew[jazz::kMaxVoicingNotes]; int remNewCount = 0;
        for (int j = 0; j < newCount; ++j) if (!newMatched[j]) remNew[remNewCount++] = voicing.notes[j];

        const int paired = juce::jmin(remOldCount, remNewCount);
        for (int i = 0; i < paired; ++i) {
            if (remOld[i] != remNew[i]) engine_.retargetVoiceNote(remOld[i], remNew[i]);
            remOld[i] = remNew[i];
        }

        if (remOldCount > remNewCount) {
            // Excess voices have no partner of their own -- each glides to
            // whichever remaining tone is nearest, even if that means two
            // converge on the same one, rather than being cut. Both are
            // still genuinely sounding afterwards -- retargeting never
            // releases a voice -- so bookkeeping keeps tracking all oldCount
            // of the original voices (matched ones unchanged, the rest as
            // their now-converged notes), not just newCount: otherwise the
            // next chord change would have no idea the extra, still-active
            // voice exists and would spawn a redundant one on top of it.
            for (int i = paired; i < remOldCount; ++i) {
                // Nearest among every new tone, not just the unmatched ones
                // -- converging onto one that another voice already matched
                // is perfectly fine, and newCount is always at least 1 here.
                int nearest = voicing.notes[0];
                int nearestDist = std::abs(remOld[i] - nearest);
                for (int j = 1; j < newCount; ++j) {
                    const int d = std::abs(remOld[i] - voicing.notes[j]);
                    if (d < nearestDist) { nearest = voicing.notes[j]; nearestDist = d; }
                }
                if (remOld[i] != nearest) engine_.retargetVoiceNote(remOld[i], nearest);
                remOld[i] = nearest;
            }
            jazzVoiceCount_ = oldCount;
            int idx = 0;
            for (int i = 0; i < oldCount; ++i) {
                jazzVoiceNotes_[i] = oldMatched[i] ? oldNow[i] : remOld[idx++];
            }
            return;
        } else if (remNewCount > remOldCount) {
            // Extra tones have no existing voice of their own -- one splits
            // off from whichever voice (matched or not) is nearest.
            for (int i = paired; i < remNewCount; ++i) {
                const int to = remNew[i];
                int source = oldCount > 0 ? oldNow[0] : -1;
                int sourceDist = oldCount > 0 ? std::abs(oldNow[0] - to) : 0;
                for (int j = 1; j < oldCount; ++j) {
                    const int d = std::abs(oldNow[j] - to);
                    if (d < sourceDist) { sourceDist = d; source = oldNow[j]; }
                }
                engine_.spawnVoiceFromNote(source, to, static_cast<float>(jazzKeyVelocity_) / 127.0f);
            }
        }
    }

    jazzVoiceCount_ = newCount;
    for (int i = 0; i < newCount; ++i) jazzVoiceNotes_[i] = voicing.notes[i];
}

void HarmonizerAudioProcessor::jazzPublish(const jazz::Voicing& v, float melodyHz,
                                           int heldKeys) {
    jvKeyCentre_.store(v.keyCentrePc);
    jvMinor_.store(v.minorKey);
    jvDegree_.store(v.scaleDegree);
    jvRoot_.store(v.chordRootPc);
    jvType_.store(static_cast<int>(v.type));
    jvStyle_.store(static_cast<int>(v.style));
    jvCustomVoicing_.store(v.customVoicing);
    jvMelodyNote_.store(v.melodyNote);
    jvMelodyDegree_.store(v.melodyDegree);
    jvMelodyHz_.store(melodyHz);
    jvHeldKeys_.store(heldKeys);
    jvRoman_.store(v.roman);
    jvLimited_.store(v.rangeLimited);
    jvWindowLow_.store(v.windowLow);
    jvWindowHigh_.store(v.windowHigh);
    for (int i = 0; i < jazz::kMaxVoicingNotes; ++i) {
        jvNotes_[i].store(i < v.count ? v.notes[i] : -1);
    }
    jvCount_.store(v.count);
    jvSounding_.store(v.count > 0);
}

void HarmonizerAudioProcessor::jazzUpdate(int frames) {
    jazzDecisionCountdown_ -= frames;
    if (jazzDecisionCountdown_ > 0) return;
    jazzDecisionCountdown_ = juce::jmax(
        1, static_cast<int>(getSampleRate() * kJazzDecisionSeconds));

    if (jazzPanic_.exchange(false)) {
        jazzVoiceCount_ = 0;
        jazzInputHash_ = 0;
        jazzCandidateTicks_ = 0;
    }

    int liveKeys[16];
    const int liveKeyCount = collectKeys(liveKeys, 16);
    jvHeldKeys_.store(liveKeyCount);

    // Latch (the toggle, or the sustain pedal standing in for it while held)
    // freezes the key centre against releases: once something has been
    // captured, only a fresh key press -- handled in processBlock(), never
    // here -- can change it. Turning latch on (or pressing sustain) with
    // keys already down captures them immediately rather than waiting for
    // the next press.
    const bool sustainHeld = jazzSustainHeld_.load();
    const bool latched = pJazzLatchKeys_->load() > 0.5f || sustainHeld;
    jvSustainHeld_.store(sustainHeld);
    if (latched && !jazzLatchActive_ && liveKeyCount > 0) {
        latchKeysFrom(liveKeys, liveKeyCount);
    }

    int keys[2];
    int keyCount = 0;
    if (latched) {
        if (jazzLatchActive_) {
            keys[0] = 60 + jazzLatchedKeyPc_;
            keyCount = 1;
            if (jazzLatchedMinor_) { keys[1] = keys[0] + 7; keyCount = 2; }
        }
    } else {
        for (int i = 0; i < liveKeyCount && i < 2; ++i) keys[i] = liveKeys[i];
        keyCount = juce::jmin(liveKeyCount, 2);
        // More than two keys held only ever changes which is lowest, already
        // reflected by collectKeys() being in ascending note order;
        // Voicer::update() only reads the first entry and the count.
        if (liveKeyCount > 2) keyCount = 2;
    }
    jvKeyLatched_.store(latched && jazzLatchActive_);

    // No key, no key centre: there is nothing to build a chord out of.
    if (keyCount == 0) {
        if (jvSounding_.load() || jazzInputHash_ != 0) jazzSilence();
        return;
    }

    const float hz = engine_.metrics().detectedPitchHz;
    jvMelodyHz_.store(hz);
    if (hz < 30.0f) {
        // Between notes. The chord stays up -- there is no input for the engine
        // to shift, so it is silent anyway, and releasing it here would only
        // make every breath retrigger the harmony.
        jazzCandidateNote_ = -1;
        jazzCandidateTicks_ = 0;
        return;
    }

    const float midiPitch = 69.0f + 12.0f * std::log2(hz / 440.0f);
    const int note = juce::roundToInt(midiPitch);
    if (note < 0 || note > 127) return;

    // Halfway between two notes is a slide, not a note. Waiting it out stops the
    // chord flickering on the way into a phrase.
    if (std::abs(midiPitch - static_cast<float>(note)) * 100.0f > kJazzCentsWindow) return;

    if (note != jazzCandidateNote_) {
        jazzCandidateNote_ = note;
        jazzCandidateTicks_ = 1;
        return;
    }
    if (jazzCandidateTicks_ < kJazzStableTicks) {
        ++jazzCandidateTicks_;
        return;
    }

    const jazz::Settings settings = jazzSettings();

    // Sustain freezes the chord itself, not just the key centre: whatever is
    // already sounding holds out even as the melody note moves on. Pitch
    // stability tracking above keeps running regardless, so the moment the
    // pedal comes back up the chord already matching whatever is playing
    // right now takes effect immediately rather than waiting out another
    // stability window.
    if (sustainHeld) return;

    // Re-voice only when something actually changed. Holding the answer steady
    // is what keeps a shuffled voicing from reshuffling under a held note.
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    for (int i = 0; i < keyCount; ++i) mix(static_cast<uint64_t>(keys[i]) + 1u);
    mix(static_cast<uint64_t>(note) + 1000u);
    mix(static_cast<uint64_t>(settings.ninth) | (static_cast<uint64_t>(settings.eleventh) << 1) |
        (static_cast<uint64_t>(settings.thirteenth) << 2) |
        (static_cast<uint64_t>(settings.shuffle) << 3) |
        (static_cast<uint64_t>(settings.doubleMelody) << 4));
    mix(static_cast<uint64_t>(settings.octaveShift + 8));
    mix(static_cast<uint64_t>(settings.inversionShift + 8));
    mix(static_cast<uint64_t>(settings.rangeLow));
    mix(static_cast<uint64_t>(settings.rangeHigh));
    mix(static_cast<uint64_t>(settings.maxNotes));
    mix(static_cast<uint64_t>(std::lround(settings.smoothness * 100.0f)));
    uint64_t styleBits = 0;
    for (int i = 0; i < jazz::kStyleCount; ++i) {
        if (settings.styles[i]) styleBits |= (1ull << i);
    }
    mix(styleBits);

    // A custom dictionary edited while a note is held has to revoice too, not
    // just the next new note.
    mix(static_cast<uint64_t>(settings.useCustomDictionary) |
        (static_cast<uint64_t>(settings.customDict.useMajor) << 1) |
        (static_cast<uint64_t>(settings.customDict.useMinor) << 2));
    if (settings.useCustomDictionary) {
        const auto mixEntry = [&mix](const jazz::CustomEntry& e) {
            mix(static_cast<uint64_t>(e.count));
            for (int i = 0; i < e.count; ++i) mix(static_cast<uint64_t>(e.offsets[i] + 1000));
        };
        for (int degree = 0; degree < 12; ++degree) {
            mixEntry(settings.customDict.major[degree]);
            mixEntry(settings.customDict.minor[degree]);
        }
    }

    if (hash == jazzInputHash_) return;
    jazzInputHash_ = hash;

    jazz::Voicing voicing;
    if (!jazzVoicer_.update(keys, keyCount, note, settings, voicing)) {
        jazzSilence();
        return;
    }

    jazzApply(voicing);
    jazzPublish(voicing, hz, keyCount);
}

HarmonizerAudioProcessor::JazzView HarmonizerAudioProcessor::jazzView() const {
    JazzView v;
    v.enabled = pJazzMode_->load() > 0.5f;
    v.sounding = jvSounding_.load();
    v.keyCentrePc = jvKeyCentre_.load();
    v.minorKey = jvMinor_.load();
    v.scaleDegree = jvDegree_.load();
    v.chordRootPc = jvRoot_.load();
    v.typeIndex = jvType_.load();
    v.styleIndex = jvStyle_.load();
    v.customVoicing = jvCustomVoicing_.load();
    v.melodyNote = jvMelodyNote_.load();
    v.melodyDegree = jvMelodyDegree_.load();
    v.melodyHz = jvMelodyHz_.load();
    v.heldKeys = jvHeldKeys_.load();
    v.keyLatched = jvKeyLatched_.load();
    v.sustainHeld = jvSustainHeld_.load();
    v.noteCount = juce::jlimit(0, jazz::kMaxVoicingNotes, jvCount_.load());
    for (int i = 0; i < jazz::kMaxVoicingNotes; ++i) v.notes[i] = jvNotes_[i].load();
    v.roman = jvRoman_.load();
    v.rangeLimited = jvLimited_.load();
    v.windowLow = jvWindowLow_.load();
    v.windowHigh = jvWindowHigh_.load();
    v.keyTransposeSemitones = keyTransposeSemitones();
    v.melodyTransposeSemitones = melodyTransposeSemitones();
    return v;
}

// ---------------------------------------------------------------------------

void HarmonizerAudioProcessor::handleAsyncUpdate() {
    const int latency = pendingLatency_.load();
    if (latency >= 0 && latency != reportedLatency_) {
        reportedLatency_ = latency;
        setLatencySamples(latency);
    }
}

// ---------------------------------------------------------------------------
// Custom jazz dictionary presets. The host session already keeps the current
// custom dictionary via ordinary plugin state -- it is APVTS parameters like
// everything else -- so this is a separate, named library of them on disk,
// letting one built for a project be reused in another. Message thread only.
// ---------------------------------------------------------------------------

void HarmonizerAudioProcessor::setParamValue(const juce::String& id, float rawValue) {
    if (auto* p = apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(rawValue));
        p->endChangeGesture();
    }
}

juce::File HarmonizerAudioProcessor::jazzDictionaryPresetDirectory() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Harmonizer")
                   .getChildFile("JazzDictionaryPresets");
    dir.createDirectory();
    return dir;
}

namespace {
juce::File jazzPresetFile(const juce::String& name) {
    return HarmonizerAudioProcessor::jazzDictionaryPresetDirectory().getChildFile(
        juce::File::createLegalFileName(name.trim()) + ".xml");
}
}  // namespace

juce::StringArray HarmonizerAudioProcessor::jazzDictionaryPresetNames() const {
    juce::StringArray names;
    for (const auto& f : jazzDictionaryPresetDirectory().findChildFiles(
             juce::File::findFiles, false, "*.xml")) {
        names.add(f.getFileNameWithoutExtension());
    }
    names.sort(true);
    return names;
}

bool HarmonizerAudioProcessor::saveJazzDictionaryPreset(const juce::String& name) const {
    if (name.trim().isEmpty()) return false;

    juce::XmlElement root("HarmonizerJazzDictionary");
    root.setAttribute("version", 2);
    root.setAttribute("useMajor", pJazzCustomUseMajor_->load() > 0.5f);
    root.setAttribute("useMinor", pJazzCustomUseMinor_->load() > 0.5f);
    for (int ctx = 0; ctx < 2; ++ctx) {
        const bool minor = ctx == 1;
        for (int degree = 0; degree < 12; ++degree) {
            auto* el = root.createNewChildElement(minor ? "Minor" : "Major");
            el->setAttribute("degree", degree);

            juce::StringArray offsets;
            for (int slot = 0; slot < jazz::kMaxVoicingNotes; ++slot) {
                const int raw = static_cast<int>(
                    std::lround(pJazzCustomOffset_[ctx][degree][slot]->load()));
                if (raw > 0) offsets.add(juce::String(jazzCustomRawToOffset(raw)));
            }
            el->setAttribute("offsets", offsets.joinIntoString(","));
        }
    }
    return root.writeTo(jazzPresetFile(name));
}

bool HarmonizerAudioProcessor::loadJazzDictionaryPreset(const juce::String& name) {
    auto xml = juce::XmlDocument::parse(jazzPresetFile(name));
    if (xml == nullptr || !xml->hasTagName("HarmonizerJazzDictionary")) return false;

    setParamValue(ParamId::jazzCustomUseMajor, xml->getBoolAttribute("useMajor", false) ? 1.0f : 0.0f);
    setParamValue(ParamId::jazzCustomUseMinor, xml->getBoolAttribute("useMinor", false) ? 1.0f : 0.0f);

    // Clear every slot first, so a degree the preset leaves out actually ends
    // up blank rather than keeping whatever the live dictionary had there.
    for (int ctx = 0; ctx < 2; ++ctx) {
        for (int degree = 0; degree < 12; ++degree) clearJazzCustomVoicing(ctx == 1, degree);
    }

    for (auto* child : xml->getChildIterator()) {
        const int degree = child->getIntAttribute("degree", -1);
        if (degree < 0 || degree > 11) continue;
        const bool minor = child->hasTagName("Minor");
        if (!minor && !child->hasTagName("Major")) continue;

        if (child->hasAttribute("offsets")) {
            // Current format: an explicit voicing, semitones above the root.
            const auto parts =
                juce::StringArray::fromTokens(child->getStringAttribute("offsets"), ",", "");
            int slot = 0;
            for (const auto& part : parts) {
                if (slot >= jazz::kMaxVoicingNotes) break;
                if (part.trim().isEmpty()) continue;
                const int offset =
                    juce::jlimit(-jazz::kMaxCustomOffset, jazz::kMaxCustomOffset, part.getIntValue());
                setParamValue(ParamId::jazzCustomOffsetId(minor, degree, slot),
                             static_cast<float>(jazzCustomOffsetToRaw(offset)));
                ++slot;
            }
        } else if (child->hasAttribute("type")) {
            // A preset saved by the version of this dictionary that picked
            // one of six fixed chord types per degree rather than an
            // explicit voicing. Converted on load so an old preset keeps
            // working rather than silently coming back empty.
            const int typeIndex = juce::jlimit(0, static_cast<int>(jazz::ChordType::Count) - 1,
                                               child->getIntAttribute("type", 0));
            int offsets[3] = {};
            const int n =
                jazz::chordTypeTones(static_cast<jazz::ChordType>(typeIndex), offsets, 3);
            for (int slot = 0; slot < n; ++slot) {
                setParamValue(ParamId::jazzCustomOffsetId(minor, degree, slot),
                             static_cast<float>(jazzCustomOffsetToRaw(offsets[slot])));
            }
        }
    }

    // Loading a preset means "use this now" -- it would be a strange button to
    // press and have nothing change.
    setParamValue(ParamId::jazzCustomOn, 1.0f);
    return true;
}

bool HarmonizerAudioProcessor::deleteJazzDictionaryPreset(const juce::String& name) const {
    return jazzPresetFile(name).deleteFile();
}

// ---------------------------------------------------------------------------

void HarmonizerAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml()) {
        copyXmlToBinary(*xml, destData);
    }
}

void HarmonizerAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
        }
    }
}

juce::AudioProcessorEditor* HarmonizerAudioProcessor::createEditor() {
    return new HarmonizerAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HarmonizerAudioProcessor();
}
