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

template <typename T, size_t N>
int pick(const T (&table)[N], float normalisedIndex) {
    const int i = juce::jlimit(0, static_cast<int>(N) - 1,
                               static_cast<int>(std::lround(normalisedIndex)));
    return table[i];
}

}  // namespace

const juce::StringArray HarmonizerAudioProcessor::kFftChoices { "256", "512", "1024", "2048" };

HarmonizerAudioProcessor::HarmonizerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
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
}

HarmonizerAudioProcessor::~HarmonizerAudioProcessor() {
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout
HarmonizerAudioProcessor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::wetDry, 1}, "Wet / Dry",
        NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{ParamId::outputGain, 1}, "Output Gain",
        NormalisableRange<float>(0.0f, 2.0f), 1.0f));

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
        NormalisableRange<float>(0.0f, 1.0f), 0.35f));

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

    return layout;
}

void HarmonizerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.prepare(sampleRate, samplesPerBlock);
    pushParameters();

    const int capacity = juce::jmax(samplesPerBlock, 2048);
    monoIn_.setSize(1, capacity, false, true, false);
    monoOut_.setSize(1, capacity, false, true, false);
    monoIn_.clear();
    monoOut_.clear();

    reportedLatency_ = engine_.algorithmicLatencySamples();
    setLatencySamples(reportedLatency_);
}

bool HarmonizerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled()) return false;
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void HarmonizerAudioProcessor::pushParameters() {
    auto& p = engine_.params();
    p.wetDry.store(pWetDry_->load());
    p.outputGain.store(pOutputGain_->load());
    p.harmonyMode.store(pick(kHarmonyModeIds, pHarmonyMode_->load()));
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

    // Notes arrive from the host rather than from a MIDI device, but the engine
    // consumes the same raw bytes either way.
    for (const auto meta : midi) {
        const auto message = meta.getMessage();
        const auto* bytes = message.getRawData();
        const int size = message.getRawDataSize();
        if (size <= 0) continue;

        dsp::MidiEvent event;
        event.status = static_cast<uint8_t>(bytes[0]);
        event.data1 = size > 1 ? static_cast<uint8_t>(bytes[1]) : 0;
        event.data2 = size > 2 ? static_cast<uint8_t>(bytes[2]) : 0;
        engine_.midiQueue().push(event);
    }

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples <= 0 || numIn <= 0 || numOut <= 0) return;

    if (monoIn_.getNumSamples() < numSamples) {
        monoIn_.setSize(1, numSamples, false, true, false);
        monoOut_.setSize(1, numSamples, false, true, false);
    }

    // Sum the track to mono: the engine analyses a single voice, and a stereo
    // source here is nearly always the same signal twice.
    float* in = monoIn_.getWritePointer(0);
    juce::FloatVectorOperations::copy(in, buffer.getReadPointer(0), numSamples);
    for (int ch = 1; ch < numIn; ++ch) {
        juce::FloatVectorOperations::add(in, buffer.getReadPointer(ch), numSamples);
    }
    if (numIn > 1) {
        juce::FloatVectorOperations::multiply(in, 1.0f / static_cast<float>(numIn), numSamples);
    }

    float* out = monoOut_.getWritePointer(0);
    engine_.process(in, out, numSamples);

    for (int ch = 0; ch < numOut; ++ch) {
        juce::FloatVectorOperations::copy(buffer.getWritePointer(ch), out, numSamples);
    }

    // The window size can change while running, which changes the engine's
    // latency. Tell the host, but off the audio thread.
    const int latency = engine_.algorithmicLatencySamples();
    if (latency != reportedLatency_) {
        pendingLatency_.store(latency);
        triggerAsyncUpdate();
    }
}

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
