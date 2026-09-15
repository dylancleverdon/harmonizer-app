#include "PluginEditor.h"

using namespace harmonizer;

namespace {

juce::String noteName(int note) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#",
                                  "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

void styleToggle(juce::ToggleButton& b, const juce::String& label) {
    b.setButtonText(label);
    b.setColour(juce::ToggleButton::textColourId, look::text);
    b.setColour(juce::ToggleButton::tickColourId, look::accent);
    b.setColour(juce::ToggleButton::tickDisabledColourId, look::surfaceVariant);
}

void styleSlider(juce::Slider& s) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
    s.setColour(juce::Slider::trackColourId, look::accent);
    s.setColour(juce::Slider::backgroundColourId, look::surfaceVariant);
    s.setColour(juce::Slider::thumbColourId, look::accent);
    s.setColour(juce::Slider::textBoxTextColourId, look::text);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

}  // namespace

// ---------------------------------------------------------------------------

look::Card& Page::addCard(const juce::String& title) {
    auto* card = cards_.add(new look::Card(title));
    addAndMakeVisible(*card);
    return *card;
}

int Page::preferredHeight() const {
    int h = look::cardGap;
    for (auto* c : cards_) h += c->preferredHeight() + look::cardGap;
    return h;
}

void Page::resized() {
    int y = 0;
    for (auto* c : cards_) {
        const int h = c->preferredHeight();
        c->setBounds(0, y, getWidth(), h);
        y += h + look::cardGap;
    }
}

// ---------------------------------------------------------------------------

ChipGroup::ChipGroup(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                     const juce::StringArray& labels)
    : parameter_(*state.getParameter(parameterId)) {
    for (int i = 0; i < labels.size(); ++i) {
        auto* chip = chips_.add(new look::Chip(labels[i]));
        chip->onClick = [this, i] { applyIndex(i); };
        addAndMakeVisible(*chip);
    }

    attachment_ = std::make_unique<juce::ParameterAttachment>(
        parameter_, [this](float value) {
            const int index = juce::jlimit(0, chips_.size() - 1,
                                           static_cast<int>(std::lround(value)));
            for (int i = 0; i < chips_.size(); ++i) {
                chips_[i]->setToggleState(i == index, juce::dontSendNotification);
            }
        });
    attachment_->sendInitialUpdate();
}

void ChipGroup::applyIndex(int index) {
    attachment_->setValueAsCompleteGesture(static_cast<float>(index));
}

int ChipGroup::selectedIndex() const {
    for (int i = 0; i < chips_.size(); ++i) {
        if (chips_[i]->getToggleState()) return i;
    }
    return 0;
}

void ChipGroup::resized() {
    if (chips_.isEmpty()) return;
    const int gap = 6;
    const int total = getWidth() - gap * (chips_.size() - 1);
    const int each = total / chips_.size();
    int x = 0;
    for (int i = 0; i < chips_.size(); ++i) {
        const int w = (i == chips_.size() - 1) ? getWidth() - x : each;
        chips_[i]->setBounds(x, 0, w, getHeight());
        x += w + gap;
    }
}

// ---------------------------------------------------------------------------
// Main page: what is arriving, the mix knob, and the harmony controls.
// ---------------------------------------------------------------------------

class MainPage final : public Page {
public:
    MainPage(HarmonizerAudioProcessor& p, look::KnobLookAndFeel& knobLook) : processor_(p) {
        auto& apvts = processor_.apvts;

        // --- Signal: the answer to "why can I not hear anything". The two audio
        // buses are metered separately because in Logic the audio arrives on the
        // side chain, and a combined meter cannot tell you if that worked.
        auto& signal = addCard("Signal");
        trackRow_ = std::make_unique<look::StatRow>("Track input");
        sideRow_ = std::make_unique<look::StatRow>("Side chain");
        midiRow_ = std::make_unique<look::StatRow>("MIDI");
        voiceRow_ = std::make_unique<look::StatRow>("Voices sounding");
        signal.addRow(*trackRow_, 18);
        signal.addRow(trackMeter_, 8);
        signal.addRow(*sideRow_, 18);
        signal.addRow(sideMeter_, 8);
        signal.addRow(*midiRow_, 18);
        signal.addRow(*voiceRow_, 18);
        signal.addRow(voiceDots_, 10);
        signal.addRow(diagnosis_, 32);

        // --- Mix
        auto& mix = addCard("Mix");
        wetDry_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        wetDry_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        wetDry_.setLookAndFeel(&knobLook);
        wetDry_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        mix.addRow(knobHolder_, 150);
        knobHolder_.addAndMakeVisible(wetDry_);
        knobHolder_.onResize = [this] {
            const int side = juce::jmin(knobHolder_.getHeight(), 150);
            wetDry_.setBounds((knobHolder_.getWidth() - side) / 2, 0, side, side);
        };
        mixRow_ = std::make_unique<look::StatRow>("Wet / dry", true);
        mix.addRow(*mixRow_, 20);
        mixNote_.setText("The dry path is delayed to match the engine exactly, so the two stay "
                         "aligned as the knob moves.");
        mix.addRow(mixNote_, 30);

        // --- Harmony
        auto& harmony = addCard("Harmony");
        harmonyChips_ = std::make_unique<ChipGroup>(
            apvts, HarmonizerAudioProcessor::ParamId::harmonyMode,
            juce::StringArray{"Fixed interval", "Absolute pitch", "Chord voicing"});
        harmony.addRow(*harmonyChips_, 30);
        harmonyNote_.setText("");
        harmony.addRow(harmonyNote_, 44);

        degreeLabel_.setText("YOU ARE PLAYING THE", look::muted);
        harmony.addRow(degreeLabel_, 14);
        degreeChips_ = std::make_unique<ChipGroup>(
            apvts, HarmonizerAudioProcessor::ParamId::chordDegree,
            juce::StringArray{"Root", "3rd", "5th", "7th", "9th", "11th", "13th"});
        harmony.addRow(*degreeChips_, 28);

        styleToggle(doubleAnchor_, "Double your own note");
        harmony.addRow(doubleAnchor_, 24);
        chordHint_.setText("");
        harmony.addRow(chordHint_, 30);

        // --- Status
        auto& status = addCard("Status");
        latencyRow_ = std::make_unique<look::StatRow>("Engine latency", true);
        loadRow_ = std::make_unique<look::StatRow>("DSP load");
        hostRow_ = std::make_unique<look::StatRow>("Host");
        status.addRow(*latencyRow_, 20);
        status.addRow(*loadRow_, 18);
        status.addRow(loadMeter_, 8);
        status.addRow(*hostRow_, 18);

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        aWetDry_ = std::make_unique<SA>(apvts, HarmonizerAudioProcessor::ParamId::wetDry, wetDry_);
        aDoubleAnchor_ = std::make_unique<BA>(
            apvts, HarmonizerAudioProcessor::ParamId::doubleAnchor, doubleAnchor_);
    }

    ~MainPage() override { wetDry_.setLookAndFeel(nullptr); }

    void refresh(const dsp::Metrics& m, const HarmonizerAudioProcessor::Traffic& t,
                 double hostRate, int hostBlock) {
        const auto now = juce::Time::getMillisecondCounter();
        if (t.mainPeak > 0.0008f) lastMainMs_ = now;
        if (t.sidePeak > 0.0008f) lastSideMs_ = now;
        if (t.midiMessages != lastMidiCount_) { lastMidiCount_ = t.midiMessages; lastMidiMs_ = now; }

        const bool mainLive = lastMainMs_ != 0 && now - lastMainMs_ < 1500;
        const bool sideLive = lastSideMs_ != 0 && now - lastSideMs_ < 1500;

        trackRow_->setValue(t.mainChannels == 0 ? "not connected"
                            : mainLive ? juce::String(juce::Decibels::gainToDecibels(t.mainPeak), 1) + " dB"
                                       : "silent");
        sideRow_->setValue(t.sideChannels == 0 ? "not connected"
                           : sideLive ? juce::String(juce::Decibels::gainToDecibels(t.sidePeak), 1) + " dB"
                                      : "silent");
        trackMeter_.setLevel(std::sqrt(t.mainPeak));
        sideMeter_.setLevel(std::sqrt(t.sidePeak));

        const bool midiEver = t.midiMessages > 0;
        juce::String midiText = midiEver
            ? juce::String(t.noteOns) + " notes" +
                  (t.lastNote >= 0 ? "  (" + noteName(t.lastNote) + ")" : juce::String())
            : juce::String("none received");
        midiRow_->setValue(midiText);

        voiceRow_->setValue(juce::String(m.activeVoices) + " / 10");
        voiceDots_.setActive(m.activeVoices);

        const int mode = harmonyChips_->selectedIndex();
        const auto d = diagnose(m, t, mainLive, sideLive, midiEver, mode);
        diagnosis_.setText(d.first, d.second);

        mixRow_->setValue(juce::String(juce::roundToInt(
            *processor_.apvts.getRawParameterValue(HarmonizerAudioProcessor::ParamId::wetDry)
            * 100.0f)) + " % wet");

        latencyRow_->setValue(juce::String(m.algorithmicLatencyMs, 1) + " ms");
        loadRow_->setValue(juce::String(juce::roundToInt(m.cpuLoad * 100.0f)) + " %");
        loadMeter_.setLevel(m.cpuLoad);
        hostRow_->setValue(juce::String(juce::roundToInt(hostRate)) + " Hz / " +
                           juce::String(hostBlock) + " frames");

        harmonyNote_.setText(harmonyDescription(mode));

        const bool chord = mode == 2;
        degreeLabel_.setVisible(chord);
        degreeChips_->setVisible(chord);
        doubleAnchor_.setVisible(chord);
        chordHint_.setVisible(chord);
        if (chord) chordHint_.setText(chordText(m), chordColour(m));
    }

private:
    /** Forwards a resize to a lambda, so a card row can centre the knob. */
    class Holder final : public juce::Component {
    public:
        std::function<void()> onResize;
        void resized() override { if (onResize) onResize(); }
    };

    static juce::String harmonyDescription(int mode) {
        switch (mode) {
            case 0: return "Each note is a fixed interval in cents from middle C, applied to "
                           "whatever you play. E above middle C is +400 cents.";
            case 1: return "Each voice lands on the exact pitch of the note played. Needs a "
                           "confident read on your pitch, so it suits sustained notes.";
            default: return "Hold a chord and you supply one of its tones yourself; the rest is "
                            "built around your pitch. No pitch tracking needed.";
        }
    }

    juce::String chordText(const dsp::Metrics& m) const {
        if (m.rootNote < 0) return "Hold a chord to hear it.";
        if (m.anchorNote == m.rootNote && degreeChips_->selectedIndex() > 0) {
            return "That degree is not in this chord, so you are the root (" +
                   noteName(m.rootNote) + ").";
        }
        return "You are " + noteName(m.anchorNote) + "; the rest is built around it.";
    }

    juce::Colour chordColour(const dsp::Metrics& m) const {
        return (m.rootNote >= 0 && m.anchorNote == m.rootNote && degreeChips_->selectedIndex() > 0)
                   ? look::warn : look::muted;
    }

    std::pair<juce::String, juce::Colour> diagnose(
        const dsp::Metrics& m, const HarmonizerAudioProcessor::Traffic& t, bool mainLive,
        bool sideLive, bool midiEver, int mode) const {
        const bool anyAudio = mainLive || sideLive;
        if (t.mainChannels == 0 && t.sideChannels == 0) {
            return {"The host is not sending this plugin any audio at all.", look::warn};
        }
        if (!anyAudio && !midiEver) {
            return {"Nothing arriving yet - no audio and no MIDI.", look::warn};
        }
        if (!anyAudio) {
            return {t.sideChannels > 0
                        ? "MIDI is arriving but no audio. Set the Side Chain to your source track."
                        : "MIDI is arriving but no audio is reaching the plugin.",
                    look::warn};
        }
        if (!midiEver) {
            return {"Audio is coming through, but no MIDI notes have arrived yet.", look::warn};
        }
        if (m.activeVoices == 0) {
            if (mode == 2 && m.rootNote >= 0) {
                return {"That is only your own note - hold at least two.", look::warn};
            }
            return {"Ready. Hold some notes.", look::muted};
        }
        return {"Running - " + juce::String(m.activeVoices) + " harmony voice" +
                    (m.activeVoices == 1 ? "." : "s."), look::accent};
    }

    HarmonizerAudioProcessor& processor_;

    std::unique_ptr<look::StatRow> trackRow_, sideRow_, midiRow_, voiceRow_, mixRow_,
        latencyRow_, loadRow_, hostRow_;
    look::Meter trackMeter_, sideMeter_, loadMeter_;
    look::VoiceDots voiceDots_;
    look::Note diagnosis_, mixNote_, harmonyNote_, chordHint_, degreeLabel_;

    Holder knobHolder_;
    juce::Slider wetDry_;
    juce::ToggleButton doubleAnchor_;
    std::unique_ptr<ChipGroup> harmonyChips_, degreeChips_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aWetDry_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aDoubleAnchor_;

    int lastMidiCount_ = 0;
    juce::uint32 lastMainMs_ = 0, lastSideMs_ = 0, lastMidiMs_ = 0;
};

// ---------------------------------------------------------------------------
// Settings page: everything the app's Settings screen offers that still makes
// sense inside a host. Stream rate, buffer size and input device belong to the
// DAW here, so those are reported rather than controlled.
// ---------------------------------------------------------------------------

class SettingsPage final : public Page {
public:
    SettingsPage(HarmonizerAudioProcessor& p, PluginUpdater& updater)
        : processor_(p), updater_(updater) {
        auto& apvts = processor_.apvts;
        using P = HarmonizerAudioProcessor::ParamId;

        // --- Quality reduction method
        auto& quality = addCard("Quality reduction method");
        qualityIntro_.setText("How the wet path gives up quality to save processing time. "
                              "One method at a time; the amount below sets how far it goes.");
        quality.addRow(qualityIntro_, 30);
        qualityChips_ = std::make_unique<ChipGroup>(
            apvts, P::qualityMode,
            juce::StringArray{"Vocoder bands", "Sample rate", "Bit depth"});
        quality.addRow(*qualityChips_, 30);
        qualityNote_.setText("");
        quality.addRow(qualityNote_, 58);
        runningRow_ = std::make_unique<look::StatRow>("Currently running", true);
        quality.addRow(*runningRow_, 20);

        // --- Reduction amount
        auto& amount = addCard("Reduction amount");
        styleSlider(amountSlider_);
        amount.addRow(amountSlider_, 24);
        amountRow_ = std::make_unique<look::StatRow>("Setting");
        amount.addRow(*amountRow_, 18);
        adaptiveNote_.setText("");
        amount.addRow(adaptiveNote_, 16);

        // --- Experimental
        auto& experimental = addCard("Experimental");
        styleToggle(adaptLatency_, "Adapt to measured load");
        experimental.addRow(adaptLatency_, 24);
        adaptLatencyNote_.setText("Watches how close the DSP runs to the deadline and increases "
                                  "the reduction before a dropout happens, then eases back.");
        experimental.addRow(adaptLatencyNote_, 30);
        styleToggle(adaptVoices_, "Reduce quality as voices are added");
        experimental.addRow(adaptVoices_, 24);
        adaptVoicesNote_.setText("Scales the reduction up with the number of notes held, so a "
                                 "ten-note chord costs closer to what one note costs.");
        experimental.addRow(adaptVoicesNote_, 30);
        effectiveRow_ = std::make_unique<look::StatRow>("Effective amount now");
        experimental.addRow(*effectiveRow_, 18);

        // --- Engine
        auto& engine = addCard("Engine");
        styleToggle(formant_, "Formant correction");
        engine.addRow(formant_, 24);
        formantNote_.setText("Keeps the vowel where it was while the pitch moves. Turning it off "
                             "is cheaper but shifted voices sound chipmunk-like going up.");
        engine.addRow(formantNote_, 30);
        windowLabel_.setText("ANALYSIS WINDOW", look::muted);
        engine.addRow(windowLabel_, 14);
        windowChips_ = std::make_unique<ChipGroup>(
            apvts, P::fftSize, juce::StringArray{"256", "512", "1024", "2048"});
        engine.addRow(*windowChips_, 28);
        windowNote_.setText("");
        engine.addRow(windowNote_, 44);

        // --- Output
        auto& output = addCard("Output");
        outputLabel_.setText("OUTPUT GAIN", look::muted);
        output.addRow(outputLabel_, 14);
        styleSlider(gainSlider_);
        output.addRow(gainSlider_, 24);
        styleToggle(bypass_, "Bypass");
        output.addRow(bypass_, 24);
        bypassNote_.setText("Passes the dry signal through, still delayed by the same amount, so "
                            "switching in and out does not shift the timing.");
        output.addRow(bypassNote_, 30);

        // --- Updates
        auto& updates = addCard("Updates");
        installedRow_ = std::make_unique<look::StatRow>("Installed");
        installedRow_->setValue(PluginUpdater::currentVersionName());
        updates.addRow(*installedRow_, 18);
        updateNote_.setText("Check the releases page for a newer build.");
        updates.addRow(updateNote_, 44);
        updates.addRow(buttonRow_, 30);
        buttonRow_.addAndMakeVisible(checkButton_);
        buttonRow_.addAndMakeVisible(installButton_);
        buttonRow_.addChildComponent(updateBar_);
        buttonRow_.onResize = [this] {
            checkButton_.setBounds(0, 0, 150, buttonRow_.getHeight());
            installButton_.setBounds(158, 0, 190, buttonRow_.getHeight());
            updateBar_.setBounds(158, 4, buttonRow_.getWidth() - 158, buttonRow_.getHeight() - 8);
        };
        checkButton_.onClick = [this] { updater_.checkForUpdates(); };
        installButton_.onClick = [this] { updater_.downloadAndInstall(); };
        installButton_.setVisible(false);

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        aAmount_ = std::make_unique<SA>(apvts, P::qualityAmount, amountSlider_);
        aGain_ = std::make_unique<SA>(apvts, P::outputGain, gainSlider_);
        aFormant_ = std::make_unique<BA>(apvts, P::formant, formant_);
        aAdaptLatency_ = std::make_unique<BA>(apvts, P::adaptLatency, adaptLatency_);
        aAdaptVoices_ = std::make_unique<BA>(apvts, P::adaptVoices, adaptVoices_);
        aBypass_ = std::make_unique<BA>(apvts, P::bypass, bypass_);

        amountSlider_.textFromValueFunction = [](double v) {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        amountSlider_.valueFromTextFunction = [](const juce::String& s) {
            return s.getDoubleValue() / 100.0;
        };
        gainSlider_.textFromValueFunction = [](double v) { return juce::String(v, 2) + " x"; };
        gainSlider_.valueFromTextFunction = [](const juce::String& s) {
            return s.getDoubleValue();
        };
        amountSlider_.updateText();
        gainSlider_.updateText();
    }

    void refresh(const dsp::Metrics& m, double hostRate) {
        const int mode = qualityChips_->selectedIndex();
        qualityNote_.setText(qualityDescription(mode));

        switch (mode) {
            case 0: runningRow_->setValue(juce::String(m.partialsPerVoice) + " partials/voice"); break;
            case 1: runningRow_->setValue(juce::String(juce::roundToInt(m.internalSampleRate)) +
                                          " Hz internal"); break;
            default: runningRow_->setValue(juce::String(m.bitDepth) + "-bit wet path"); break;
        }

        const float set = *processor_.apvts.getRawParameterValue(
            HarmonizerAudioProcessor::ParamId::qualityAmount);
        amountRow_->setValue(juce::String(juce::roundToInt(set * 100.0f)) + " %");
        effectiveRow_->setValue(juce::String(juce::roundToInt(m.effectiveQuality * 100.0f)) + " %");
        adaptiveNote_.setText(m.effectiveQuality > set + 0.02f
                                  ? "An adaptive option is pushing this to " +
                                        juce::String(juce::roundToInt(m.effectiveQuality * 100.0f)) +
                                        " %."
                                  : juce::String(),
                              look::warn);

        // Window length is a fixed number of samples, so its cost in
        // milliseconds depends on whatever rate the host is running.
        const double rate = hostRate > 0.0 ? hostRate : 48000.0;
        const auto ms = [rate](int window) {
            return juce::String((window * 0.75 + 63.0) / rate * 1000.0, 1);
        };
        windowNote_.setText("The biggest lever on latency, and the one thing the quality modes "
                            "never touch. At " + juce::String(juce::roundToInt(rate)) +
                            " Hz: 256 gives " + ms(256) + " ms, 2048 gives " + ms(2048) +
                            " ms. Small windows resolve low notes poorly.");

        refreshUpdatePanel();
    }

private:
    class Holder final : public juce::Component {
    public:
        std::function<void()> onResize;
        void resized() override { if (onResize) onResize(); }
    };

    static juce::String qualityDescription(int mode) {
        switch (mode) {
            case 0:
                return "Resynthesises each voice from fewer spectral peaks, like a vocoder "
                       "reducing a signal to a few bands. 96 partials down to 6. Cost falls "
                       "directly with the count, which is why this is the default.";
            case 1:
                return "Runs the wet path at 48, 24 or 12 kHz. The window shrinks with the rate, "
                       "so the transform gets cheaper while the latency stays put.";
            default:
                return "Quantises the wet path from 24 bits to 4. Worth knowing: this buys tone, "
                       "not speed - everything downstream is 32-bit float, so measured cost at "
                       "4-bit is within noise of 24-bit.";
        }
    }

    void refreshUpdatePanel() {
        const auto s = updater_.status();
        updateProgress_ = s.progress;
        const bool busy = s.stage == PluginUpdater::Stage::Downloading ||
                          s.stage == PluginUpdater::Stage::Installing;
        installButton_.setVisible(s.stage == PluginUpdater::Stage::Available);
        updateBar_.setVisible(busy);
        checkButton_.setEnabled(!busy && s.stage != PluginUpdater::Stage::Checking);

        juce::Colour colour = look::muted;
        juce::String text;
        switch (s.stage) {
            case PluginUpdater::Stage::Idle:
                text = "Check the releases page for a newer build."; break;
            case PluginUpdater::Stage::Available:
                text = s.message + (s.notes.isNotEmpty() ? "\n" + s.notes : juce::String());
                colour = look::accent; break;
            case PluginUpdater::Stage::NeedsRestart:
                text = s.message; colour = look::accent; break;
            case PluginUpdater::Stage::Failed:
                text = s.message; colour = look::warn; break;
            default:
                text = s.message; break;
        }
        updateNote_.setText(text, colour);
    }

    HarmonizerAudioProcessor& processor_;
    PluginUpdater& updater_;

    look::Note qualityIntro_, qualityNote_, adaptiveNote_, adaptLatencyNote_, adaptVoicesNote_,
        formantNote_, windowNote_, bypassNote_, updateNote_, windowLabel_, outputLabel_;
    std::unique_ptr<look::StatRow> runningRow_, amountRow_, effectiveRow_, installedRow_;
    std::unique_ptr<ChipGroup> qualityChips_, windowChips_;
    juce::Slider amountSlider_, gainSlider_;
    juce::ToggleButton adaptLatency_, adaptVoices_, formant_, bypass_;

    Holder buttonRow_;
    juce::TextButton checkButton_{"Check for updates"}, installButton_{"Download and install"};
    double updateProgress_ = 0.0;
    juce::ProgressBar updateBar_{updateProgress_};

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aAmount_, aGain_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aFormant_,
        aAdaptLatency_, aAdaptVoices_, aBypass_;
};

// ---------------------------------------------------------------------------

HarmonizerAudioProcessorEditor::HarmonizerAudioProcessorEditor(HarmonizerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    title_.setText("Harmonizer", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, look::text);
    addAndMakeVisible(title_);

    version_.setText(PluginUpdater::currentVersionName(), juce::dontSendNotification);
    version_.setFont(juce::FontOptions(11.0f));
    version_.setColour(juce::Label::textColourId, look::muted);
    version_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(version_);

    pageButton_.onClick = [this] { showPage(!showingSettings_); };
    addAndMakeVisible(pageButton_);
    panicButton_.onClick = [this] { processor_.allNotesOff(); };
    addAndMakeVisible(panicButton_);

    mainPage_ = std::make_unique<MainPage>(processor_, knobLook_);
    settingsPage_ = std::make_unique<SettingsPage>(processor_, updater_);

    viewport_.setScrollBarsShown(true, false);
    viewport_.setColour(juce::ScrollBar::thumbColourId, look::surfaceVariant);
    addAndMakeVisible(viewport_);
    showPage(false);

    setResizable(true, true);
    setResizeLimits(460, 420, 900, 1200);
    setSize(560, 720);
    startTimerHz(12);
}

HarmonizerAudioProcessorEditor::~HarmonizerAudioProcessorEditor() {
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
}

void HarmonizerAudioProcessorEditor::showPage(bool settings) {
    showingSettings_ = settings;
    pageButton_.setButtonText(settings ? "Back" : "Settings");
    viewport_.setViewedComponent(settings ? static_cast<juce::Component*>(settingsPage_.get())
                                          : static_cast<juce::Component*>(mainPage_.get()),
                                 false);
    resized();
}

void HarmonizerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(look::background);
}

void HarmonizerAudioProcessorEditor::resized() {
    title_.setBounds(16, 10, 240, 30);
    version_.setBounds(getWidth() - 300, 16, 120, 18);
    panicButton_.setBounds(getWidth() - 176, 12, 72, 26);
    pageButton_.setBounds(getWidth() - 98, 12, 86, 26);

    const auto area = juce::Rectangle<int>(12, 48, getWidth() - 24, getHeight() - 60);
    viewport_.setBounds(area);

    const int contentWidth = area.getWidth() - (viewport_.isVerticalScrollBarShown() ? 10 : 0);
    if (auto* page = dynamic_cast<Page*>(viewport_.getViewedComponent())) {
        page->setSize(contentWidth, page->preferredHeight());
    }
}

void HarmonizerAudioProcessorEditor::timerCallback() {
    const auto m = processor_.metrics();
    const double rate = processor_.getSampleRate();
    const int block = processor_.getBlockSize();

    if (showingSettings_) {
        settingsPage_->refresh(m, rate);
    } else {
        mainPage_->refresh(m, processor_.traffic(), rate, block);
    }

    if (auto* page = dynamic_cast<Page*>(viewport_.getViewedComponent())) {
        const int want = page->preferredHeight();
        if (page->getHeight() != want) page->setSize(page->getWidth(), want);
    }
}
