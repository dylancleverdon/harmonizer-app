#include "PluginEditor.h"

namespace {
const juce::Colour kBackground{0xff0e1113};
const juce::Colour kSurface{0xff171b1e};
const juce::Colour kAccent{0xff4dd0c0};
const juce::Colour kText{0xffe3e6e8};
const juce::Colour kMuted{0xffa8b4b8};
const juce::Colour kWarn{0xffe8a33d};

void captionise(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, kMuted);
    label.setFont(juce::FontOptions(12.0f));
    label.setJustificationType(juce::Justification::centredLeft);
}
}  // namespace

HarmonizerAudioProcessorEditor::HarmonizerAudioProcessorEditor(HarmonizerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    auto& apvts = processor_.apvts;

    titleLabel_.setText("Harmonizer", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(titleLabel_);

    versionLabel_.setText(PluginUpdater::currentVersionName(), juce::dontSendNotification);
    versionLabel_.setFont(juce::FontOptions(12.0f));
    versionLabel_.setColour(juce::Label::textColourId, kMuted);
    versionLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel_);

    styleRotary(wetDry_);
    styleRotary(outputGain_);
    addAndMakeVisible(wetDry_);
    addAndMakeVisible(outputGain_);
    captionise(wetDryCaption_, "WET / DRY");
    captionise(outputGainCaption_, "OUTPUT");
    wetDryCaption_.setJustificationType(juce::Justification::centred);
    outputGainCaption_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(wetDryCaption_);
    addAndMakeVisible(outputGainCaption_);

    harmonyMode_.addItemList({"Fixed interval", "Absolute pitch", "Chord voicing"}, 1);
    chordDegree_.addItemList({"Root", "3rd", "5th", "7th", "9th", "11th", "13th"}, 1);
    qualityMode_.addItemList({"Vocoder bands", "Sample rate", "Bit depth"}, 1);
    fftSize_.addItemList(HarmonizerAudioProcessor::kFftChoices, 1);
    for (auto* box : {&harmonyMode_, &chordDegree_, &qualityMode_, &fftSize_}) {
        addAndMakeVisible(*box);
    }

    qualityAmount_.setSliderStyle(juce::Slider::LinearHorizontal);
    qualityAmount_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
    addAndMakeVisible(qualityAmount_);

    doubleAnchor_.setButtonText("Double your note");
    formant_.setButtonText("Formant correction");
    adaptLatency_.setButtonText("Adapt to load");
    adaptVoices_.setButtonText("Adapt to voices");
    bypass_.setButtonText("Bypass");
    for (auto* b : {&doubleAnchor_, &formant_, &adaptLatency_, &adaptVoices_, &bypass_}) {
        b->setColour(juce::ToggleButton::textColourId, kText);
        b->setColour(juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible(*b);
    }

    captionise(harmonyCaption_, "HARMONY MODE");
    captionise(degreeCaption_, "YOU ARE THE");
    captionise(qualityCaption_, "QUALITY REDUCTION");
    captionise(amountCaption_, "AMOUNT");
    captionise(windowCaption_, "ANALYSIS WINDOW");
    for (auto* l : {&harmonyCaption_, &degreeCaption_, &qualityCaption_, &amountCaption_,
                    &windowCaption_}) {
        addAndMakeVisible(*l);
    }

    chordHint_.setFont(juce::FontOptions(11.0f));
    chordHint_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(chordHint_);

    statusLabel_.setFont(juce::FontOptions(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, kMuted);
    addAndMakeVisible(statusLabel_);

    panicButton_.onClick = [this] { processor_.allNotesOff(); };
    addAndMakeVisible(panicButton_);

    updateStatus_.setFont(juce::FontOptions(12.0f));
    updateStatus_.setColour(juce::Label::textColourId, kMuted);
    updateStatus_.setText("Updates", juce::dontSendNotification);
    addAndMakeVisible(updateStatus_);

    checkButton_.onClick = [this] { updater_.checkForUpdates(); };
    installButton_.onClick = [this] { updater_.downloadAndInstall(); };
    addAndMakeVisible(checkButton_);
    addChildComponent(installButton_);
    addChildComponent(updateBar_);

    using SA = APVTS::SliderAttachment;
    using CA = APVTS::ComboBoxAttachment;
    using BA = APVTS::ButtonAttachment;
    using P = HarmonizerAudioProcessor::ParamId;

    aWetDry_ = std::make_unique<SA>(apvts, P::wetDry, wetDry_);
    aOutputGain_ = std::make_unique<SA>(apvts, P::outputGain, outputGain_);
    aQualityAmount_ = std::make_unique<SA>(apvts, P::qualityAmount, qualityAmount_);
    aHarmonyMode_ = std::make_unique<CA>(apvts, P::harmonyMode, harmonyMode_);
    aChordDegree_ = std::make_unique<CA>(apvts, P::chordDegree, chordDegree_);
    aQualityMode_ = std::make_unique<CA>(apvts, P::qualityMode, qualityMode_);
    aFftSize_ = std::make_unique<CA>(apvts, P::fftSize, fftSize_);
    aDoubleAnchor_ = std::make_unique<BA>(apvts, P::doubleAnchor, doubleAnchor_);
    aFormant_ = std::make_unique<BA>(apvts, P::formant, formant_);
    aAdaptLatency_ = std::make_unique<BA>(apvts, P::adaptLatency, adaptLatency_);
    aAdaptVoices_ = std::make_unique<BA>(apvts, P::adaptVoices, adaptVoices_);
    aBypass_ = std::make_unique<BA>(apvts, P::bypass, bypass_);

    setSize(620, 560);
    startTimerHz(10);
}

HarmonizerAudioProcessorEditor::~HarmonizerAudioProcessorEditor() {
    stopTimer();
}

void HarmonizerAudioProcessorEditor::styleRotary(juce::Slider& s) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    s.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour{0xff232a2e});
    s.setColour(juce::Slider::thumbColourId, kAccent);
    s.setColour(juce::Slider::textBoxTextColourId, kText);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void HarmonizerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kBackground);

    auto panel = [&g](juce::Rectangle<int> r) {
        g.setColour(kSurface);
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
    };
    panel({12, 52, 236, 300});
    panel({258, 52, 350, 300});
    panel({12, 362, 596, 84});
    panel({12, 456, 596, 92});
}

void HarmonizerAudioProcessorEditor::resized() {
    titleLabel_.setBounds(16, 12, 300, 32);
    versionLabel_.setBounds(getWidth() - 200, 18, 184, 20);

    // Knobs
    wetDryCaption_.setBounds(24, 64, 212, 16);
    wetDry_.setBounds(56, 82, 148, 148);
    outputGainCaption_.setBounds(24, 238, 212, 16);
    outputGain_.setBounds(76, 254, 108, 92);

    // Controls
    int y = 64;
    harmonyCaption_.setBounds(272, y, 322, 16); y += 18;
    harmonyMode_.setBounds(272, y, 322, 26); y += 32;
    degreeCaption_.setBounds(272, y, 150, 16);
    chordDegree_.setBounds(272, y + 18, 150, 26);
    doubleAnchor_.setBounds(432, y + 18, 164, 26);
    y += 52;
    chordHint_.setBounds(272, y, 322, 16); y += 22;

    qualityCaption_.setBounds(272, y, 322, 16); y += 18;
    qualityMode_.setBounds(272, y, 322, 26); y += 32;
    amountCaption_.setBounds(272, y, 80, 16);
    qualityAmount_.setBounds(352, y - 2, 242, 22); y += 28;

    windowCaption_.setBounds(272, y, 150, 16);
    fftSize_.setBounds(272, y + 18, 110, 26);
    formant_.setBounds(392, y + 18, 202, 26);

    adaptLatency_.setBounds(272, 300, 160, 24);
    adaptVoices_.setBounds(432, 300, 164, 24);
    bypass_.setBounds(272, 324, 120, 24);

    statusLabel_.setBounds(24, 374, 470, 60);
    panicButton_.setBounds(508, 392, 88, 28);

    updateStatus_.setBounds(24, 466, 572, 34);
    checkButton_.setBounds(24, 506, 160, 30);
    installButton_.setBounds(192, 506, 190, 30);
    updateBar_.setBounds(192, 510, 390, 22);
}

void HarmonizerAudioProcessorEditor::layoutChordControls() {
    const bool chordMode = harmonyMode_.getSelectedItemIndex() == 2;
    chordDegree_.setEnabled(chordMode);
    doubleAnchor_.setEnabled(chordMode);
    degreeCaption_.setEnabled(chordMode);

    const auto m = processor_.metrics();
    if (!chordMode) {
        chordHint_.setText(harmonyMode_.getSelectedItemIndex() == 1
                               ? "Tracks your pitch; use a 2048 window for low registers."
                               : "Intervals in cents from middle C, applied to what you play.",
                           juce::dontSendNotification);
        chordHint_.setColour(juce::Label::textColourId, kMuted);
        return;
    }

    if (m.rootNote < 0) {
        chordHint_.setText("Hold a chord: you supply one of its notes.", juce::dontSendNotification);
        chordHint_.setColour(juce::Label::textColourId, kMuted);
    } else if (m.anchorNote == m.rootNote && chordDegree_.getSelectedItemIndex() > 0) {
        chordHint_.setText("No " + chordDegree_.getText().toLowerCase() +
                               " in this chord, so you are the root.",
                           juce::dontSendNotification);
        chordHint_.setColour(juce::Label::textColourId, kWarn);
    } else {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#",
                                      "G", "G#", "A", "A#", "B"};
        const juce::String note = juce::String(names[m.anchorNote % 12]) +
                                  juce::String(m.anchorNote / 12 - 1);
        chordHint_.setText("You are " + note + "; the rest is built around it.",
                           juce::dontSendNotification);
        chordHint_.setColour(juce::Label::textColourId, kMuted);
    }
}

void HarmonizerAudioProcessorEditor::timerCallback() {
    const auto m = processor_.metrics();

    juce::String text;
    text << "Latency " << juce::String(m.algorithmicLatencyMs, 1) << " ms"
         << "   |   DSP load " << juce::String(juce::roundToInt(m.cpuLoad * 100.0f)) << " %"
         << "   |   Voices " << m.activeVoices << " / 10\n";

    switch (static_cast<int>(std::lround(*processor_.apvts.getRawParameterValue(
        HarmonizerAudioProcessor::ParamId::qualityMode)))) {
        case 0: text << "Running " << m.partialsPerVoice << " partials per voice"; break;
        case 1: text << "Internal rate " << juce::String(m.internalSampleRate, 0) << " Hz"; break;
        default: text << m.bitDepth << "-bit wet path"; break;
    }
    if (m.detectedPitchHz > 0.0f) {
        text << "   |   Input pitch " << juce::String(m.detectedPitchHz, 1) << " Hz";
    }
    statusLabel_.setText(text, juce::dontSendNotification);

    layoutChordControls();
    refreshUpdatePanel();
}

void HarmonizerAudioProcessorEditor::refreshUpdatePanel() {
    const auto s = updater_.status();
    updateProgress_ = s.progress;

    const bool downloading = s.stage == PluginUpdater::Stage::Downloading ||
                             s.stage == PluginUpdater::Stage::Installing;
    const bool available = s.stage == PluginUpdater::Stage::Available;

    installButton_.setVisible(available);
    updateBar_.setVisible(downloading);
    checkButton_.setEnabled(!downloading && s.stage != PluginUpdater::Stage::Checking);

    juce::String text;
    switch (s.stage) {
        case PluginUpdater::Stage::Idle:
            text = "Version " + PluginUpdater::currentVersionName() +
                   ". Check the releases page for a newer build.";
            break;
        case PluginUpdater::Stage::Available:
            text = s.message;
            if (s.notes.isNotEmpty()) text << "\n" << s.notes;
            break;
        case PluginUpdater::Stage::NeedsRestart:
        case PluginUpdater::Stage::Failed:
        default:
            text = s.message;
            break;
    }
    updateStatus_.setText(text, juce::dontSendNotification);
    updateStatus_.setColour(juce::Label::textColourId,
                            s.stage == PluginUpdater::Stage::Failed ? kWarn
                            : s.stage == PluginUpdater::Stage::NeedsRestart ? kAccent
                                                                            : kMuted);
}
