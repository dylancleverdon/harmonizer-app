#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginUpdater.h"

#include <mutex>

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
    pJazzShuffle_ = apvts.getRawParameterValue(ParamId::jazzShuffle);
    pJazzDouble_ = apvts.getRawParameterValue(ParamId::jazzDouble);
    for (int i = 0; i < jazz::kStyleCount; ++i) {
        pJazzStyle_[i] = apvts.getRawParameterValue(ParamId::jazzStyle[i]);
    }
    for (auto& n : jvNotes_) n.store(-1);

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

    return layout;
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
    s.maxNotes = static_cast<int>(std::lround(pJazzVoices_->load()));
    s.shuffle = pJazzShuffle_->load() > 0.5f;
    s.doubleMelody = pJazzDouble_->load() > 0.5f;
    for (int i = 0; i < jazz::kStyleCount; ++i) {
        s.styles[i] = pJazzStyle_[i]->load() > 0.5f;
    }
    return s;
}

void HarmonizerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.prepare(sampleRate, samplesPerBlock);
    pushParameters();

    const int capacity = juce::jmax(samplesPerBlock, 2048);
    monoIn_.setSize(1, capacity, false, true, false);
    monoOut_.setSize(1, capacity, false, true, false);
    monoIn_.clear();
    monoOut_.clear();

    for (bool& note : jazzSounding_) note = false;
    jazzVoicer_.reset();
    jazzCandidateNote_ = -1;
    jazzCandidateTicks_ = 0;
    jazzInputHash_ = 0;
    jazzDecisionCountdown_ = 0;
    jazzOn_ = pJazzMode_->load() > 0.5f;

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

        if (message.isNoteOn()) {
            hostKeyDown_[message.getNoteNumber()] = true;
            jazzKeyVelocity_ = juce::jlimit(1, 127, static_cast<int>(message.getVelocity()));
        } else if (message.isNoteOff()) {
            hostKeyDown_[message.getNoteNumber()] = false;
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            for (bool& key : hostKeyDown_) key = false;
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

void HarmonizerAudioProcessor::jazzReconfigure(bool enabled) {
    // Whichever direction this is going, the engine is holding notes that mean
    // something different on the other side of the switch.
    engine_.allNotesOff();
    for (bool& n : jazzSounding_) n = false;

    jazzVoicer_.reset();
    jazzCandidateNote_ = -1;
    jazzCandidateTicks_ = 0;
    jazzInputHash_ = 0;
    jazzDecisionCountdown_ = 0;

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
    jazzOn_ = enabled;
}

void HarmonizerAudioProcessor::jazzSilence() {
    for (int note = 0; note < 128; ++note) {
        if (!jazzSounding_[note]) continue;
        dsp::MidiEvent event;
        event.status = 0x80;
        event.data1 = static_cast<uint8_t>(note);
        event.data2 = 0;
        engine_.midiQueue().push(event);
        jazzSounding_[note] = false;
    }
    jazzVoicer_.reset();
    jazzCandidateNote_ = -1;
    jazzCandidateTicks_ = 0;
    jazzInputHash_ = 0;
    jvSounding_.store(false);
    jvCount_.store(0);
}

void HarmonizerAudioProcessor::jazzApply(const jazz::Voicing& voicing) {
    bool wanted[128] = {};
    for (int i = 0; i < voicing.count; ++i) {
        const int note = voicing.notes[i];
        if (note >= 0 && note < 128) wanted[note] = true;
    }

    // Notes common to both chords are left alone rather than retriggered, which
    // is what makes a held common tone actually sustain through a change.
    for (int note = 0; note < 128; ++note) {
        if (jazzSounding_[note] == wanted[note]) continue;
        dsp::MidiEvent event;
        event.status = wanted[note] ? 0x90 : 0x80;
        event.data1 = static_cast<uint8_t>(note);
        event.data2 = wanted[note] ? static_cast<uint8_t>(jazzKeyVelocity_) : 0;
        engine_.midiQueue().push(event);
        jazzSounding_[note] = wanted[note];
    }
}

void HarmonizerAudioProcessor::jazzPublish(const jazz::Voicing& v, float melodyHz,
                                           int heldKeys) {
    jvKeyCentre_.store(v.keyCentrePc);
    jvMinor_.store(v.minorKey);
    jvDegree_.store(v.scaleDegree);
    jvRoot_.store(v.chordRootPc);
    jvType_.store(static_cast<int>(v.type));
    jvStyle_.store(static_cast<int>(v.style));
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
        for (bool& note : jazzSounding_) note = false;
        jazzInputHash_ = 0;
        jazzCandidateTicks_ = 0;
    }

    int keys[16];
    int keyCount = 0;
    for (int note = 0; note < 128 && keyCount < 16; ++note) {
        if (hostKeyDown_[note]) keys[keyCount++] = note;
    }
    jvHeldKeys_.store(keyCount);

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
    v.melodyNote = jvMelodyNote_.load();
    v.melodyDegree = jvMelodyDegree_.load();
    v.melodyHz = jvMelodyHz_.load();
    v.heldKeys = jvHeldKeys_.load();
    v.noteCount = juce::jlimit(0, jazz::kMaxVoicingNotes, jvCount_.load());
    for (int i = 0; i < jazz::kMaxVoicingNotes; ++i) v.notes[i] = jvNotes_[i].load();
    v.roman = jvRoman_.load();
    v.rangeLimited = jvLimited_.load();
    v.windowLow = jvWindowLow_.load();
    v.windowHigh = jvWindowHigh_.load();
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
