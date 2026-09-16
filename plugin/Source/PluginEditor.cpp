#include "PluginEditor.h"

using namespace harmonizer;

namespace {

juce::String noteName(int note) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#",
                                  "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

/** The same note spelled in flats, as the jazz chord symbols are. */
juce::String flatNoteName(int note) {
    if (note < 0) return "-";
    return juce::String(jazz::pitchClassName(note)) + juce::String(note / 12 - 1);
}

void styleToggle(juce::ToggleButton& b, const juce::String& label) {
    b.setButtonText(label);
    b.setColour(juce::ToggleButton::textColourId, look::text);
    b.setColour(juce::ToggleButton::tickColourId, look::accent);
    b.setColour(juce::ToggleButton::tickDisabledColourId, look::surfaceVariant);
}

void styleCombo(juce::ComboBox& c) {
    c.setColour(juce::ComboBox::backgroundColourId, look::surfaceVariant);
    c.setColour(juce::ComboBox::textColourId, look::text);
    c.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    c.setColour(juce::ComboBox::arrowColourId, look::accent);
}

/** "1 (root)", "b2", "2" ... the scale degree a row of the custom dictionary
 *  editor is for -- the note the player has to play to reach that chord. */
const juce::String& customDegreeLabel(int degree) {
    static const juce::StringArray labels {
        "1 (root)", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"
    };
    return labels.getReference(juce::jlimit(0, 11, degree));
}

/** The same degree, short enough to fit on a small selector button. */
const juce::String& shortDegreeLabel(int degree) {
    static const juce::StringArray labels {
        "1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"
    };
    return labels.getReference(juce::jlimit(0, 11, degree));
}

void styleSmallButton(juce::TextButton& b) {
    b.setColour(juce::TextButton::buttonColourId, look::surfaceVariant);
    b.setColour(juce::TextButton::buttonOnColourId, look::accent);
    b.setColour(juce::TextButton::textColourOffId, look::text);
    b.setColour(juce::TextButton::textColourOnId, look::text);
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

void Grid::add(juce::Component& c) {
    items_.push_back(&c);
    addAndMakeVisible(c);
}

int Grid::preferredHeight() const {
    const int rows = (static_cast<int>(items_.size()) + columns_ - 1) / columns_;
    return rows * rowHeight_;
}

void Grid::resized() {
    if (items_.empty()) return;
    const int columnWidth = getWidth() / columns_;
    for (size_t i = 0; i < items_.size(); ++i) {
        const int column = static_cast<int>(i) % columns_;
        const int row = static_cast<int>(i) / columns_;
        items_[i]->setBounds(column * columnWidth, row * rowHeight_, columnWidth - 6,
                             rowHeight_ - 4);
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
        const auto jazz = processor_.jazzView();
        const auto d = diagnose(m, t, mainLive, sideLive, midiEver, mode, jazz);
        diagnosis_.setText(d.first, d.second);

        mixRow_->setValue(juce::String(juce::roundToInt(
            *processor_.apvts.getRawParameterValue(HarmonizerAudioProcessor::ParamId::wetDry)
            * 100.0f)) + " % wet");

        latencyRow_->setValue(juce::String(m.algorithmicLatencyMs, 1) + " ms");
        loadRow_->setValue(juce::String(juce::roundToInt(m.cpuLoad * 100.0f)) + " %");
        loadMeter_.setLevel(m.cpuLoad);
        hostRow_->setValue(juce::String(juce::roundToInt(hostRate)) + " Hz / " +
                           juce::String(hostBlock) + " frames");

        if (jazz.enabled) {
            harmonyNote_.setText("Jazz chord mode is running -- see the Jazz page. It picks the "
                                 "chord and feeds the engine the notes itself, so these three "
                                 "modes stand down until you switch it off.",
                                 look::accent);
        } else {
            harmonyNote_.setText(harmonyDescription(mode));
        }

        const bool chord = mode == 2 && !jazz.enabled;
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
        bool sideLive, bool midiEver, int mode,
        const HarmonizerAudioProcessor::JazzView& jazz) const {
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
            if (jazz.enabled) {
                if (jazz.heldKeys == 0) {
                    return {"Jazz chord mode: hold a key to name the key centre.", look::warn};
                }
                return {"Jazz chord mode: waiting for a steady pitch on the input.", look::warn};
            }
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
// Jazz page: the chord dictionary, and everything about how its chords are
// voiced. This mode exists only in the plugin -- the app does not ship it.
// ---------------------------------------------------------------------------

class JazzPage final : public Page {
public:
    explicit JazzPage(HarmonizerAudioProcessor& p) : processor_(p) {
        // Belt and braces against the plugin window closing while record mode
        // is on: it lives on the processor, not this page, so it would
        // otherwise keep swallowing MIDI that should be playing normally,
        // invisibly, with the one indicator of it turned off.
        processor_.setJazzCustomRecording(false);
        auto& apvts = processor_.apvts;
        using P = HarmonizerAudioProcessor::ParamId;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

        // --- What it is doing right now.
        auto& mode = addCard("Jazz chord mode");
        styleToggle(enable_, "Jazz chord mode");
        mode.addRow(enable_, 24);
        intro_.setText("Hold one key to name a major key centre, or two for a minor one on the "
                       "lower key. Whatever you play into the input is read as a degree of that "
                       "key, and the chord this dictionary keeps for that degree is voiced "
                       "around your note.");
        mode.addRow(intro_, 58);

        transposeLabel_.setText("KEYS TRANSPOSE", look::muted);
        mode.addRow(transposeLabel_, 14);
        styleSlider(transposeSlider_);
        mode.addRow(transposeSlider_, 24);
        transposeNote_.setText(
            "Shifts the keys you hold before they name a key centre -- for real, not just the "
            "label: hold what your instrument's chart calls the tonic and the chord actually "
            "builds in that key. Set this to your instrument's transposition, e.g. Bb to play "
            "in concert Bb by holding a written C.");
        mode.addRow(transposeNote_, 30);

        transposeAudioLabel_.setText("AUDIO IN TRANSPOSE", look::muted);
        mode.addRow(transposeAudioLabel_, 14);
        styleSlider(transposeAudioSlider_);
        mode.addRow(transposeAudioSlider_, 24);
        transposeAudioNote_.setText(
            "Display only -- never touches the chord or how it sounds, since the note you're "
            "playing live is measured from real audio and can't be shifted for real. Just "
            "renames what the row below prints, so a concert Bb into the input reads as C, "
            "matching what a Bb instrument's own part would call it. Set this to the live "
            "instrument's transposition.");
        mode.addRow(transposeAudioNote_, 30);

        styleToggle(latch_, "Latch key centre");
        mode.addRow(latch_, 24);
        latchNote_.setText(
            "Freezes the key centre against releases: once engaged, only a fresh key press "
            "changes it, never lifting one. Off, releasing down to one key from a held minor "
            "chord reads as major the instant you lift a finger -- on, it stays minor until you "
            "release every key and press a single one again.");
        mode.addRow(latchNote_, 58);

        keyRow_ = std::make_unique<look::StatRow>("Key centre", true);
        playingRow_ = std::make_unique<look::StatRow>("You are playing");
        chordRow_ = std::make_unique<look::StatRow>("Chord", true);
        voicingRow_ = std::make_unique<look::StatRow>("Voicing");
        mode.addRow(*keyRow_, 20);
        mode.addRow(*playingRow_, 18);
        mode.addRow(*chordRow_, 20);
        mode.addRow(*voicingRow_, 18);
        status_.setText("");
        mode.addRow(status_, 30);

        // --- Which tones are in the chord.
        auto& tones = addCard("Chord tones");
        tonesNote_.setText("Sevenths are always in. These stack on top of them, and the chord "
                           "symbol above follows what is switched on.");
        tones.addRow(tonesNote_, 28);
        styleToggle(ninth_, "9ths");
        styleToggle(eleventh_, "11ths");
        styleToggle(thirteenth_, "13ths");
        toneGrid_.add(ninth_);
        toneGrid_.add(eleventh_);
        toneGrid_.add(thirteenth_);
        tones.addRow(toneGrid_, toneGrid_.preferredHeight());
        voicesLabel_.setText("HARMONY VOICES", look::muted);
        tones.addRow(voicesLabel_, 14);
        styleSlider(voicesSlider_);
        tones.addRow(voicesSlider_, 24);
        styleToggle(voicesAuto_, "Auto -- use exactly as many voices as the chord has");
        tones.addRow(voicesAuto_, 24);
        voicesNote_.setText("How many notes the chord may sound. Past this the fifth goes first, "
                            "then the root -- the tones that say least about the chord. Auto skips "
                            "this cap entirely and plays every tone the chord actually has -- a "
                            "custom voicing's own note count, or the built-in chord's three plus "
                            "whichever extensions are switched on.");
        tones.addRow(voicesNote_, 44);

        // --- Register.
        auto& sits = addCard("Where the chord sits");
        octaveLabel_.setText("OCTAVE", look::muted);
        sits.addRow(octaveLabel_, 14);
        octaveChips_ = std::make_unique<ChipGroup>(
            apvts, P::jazzOctave, juce::StringArray{"-2", "-1", "0", "+1", "+2"});
        sits.addRow(*octaveChips_, 28);

        inversionLabel_.setText("INVERSION", look::muted);
        sits.addRow(inversionLabel_, 14);
        inversionChips_ = std::make_unique<ChipGroup>(
            apvts, P::jazzInversion,
            juce::StringArray{"-3", "-2", "-1", "0", "+1", "+2", "+3"});
        sits.addRow(*inversionChips_, 28);
        shiftNote_.setText("The octave moves the chord a whole octave; an inversion moves it by "
                           "one of its own voices, taking the top note down instead of the whole "
                           "chord. Down an inversion leaves your note sitting higher in the "
                           "harmony; up an inversion buries it.");
        sits.addRow(shiftNote_, 58);

        rangeLabel_.setText("RANGE", look::muted);
        sits.addRow(rangeLabel_, 14);
        styleSlider(lowSlider_);
        styleSlider(highSlider_);
        sits.addRow(lowSlider_, 24);
        sits.addRow(highSlider_, 24);
        rangeRow_ = std::make_unique<look::StatRow>("Chords live between");
        sits.addRow(*rangeRow_, 18);
        rangeNote_.setText("Nothing sounds outside this window. Widening it lets each chord find "
                           "its own best register; tightening it forces successive chords to "
                           "share registers, which is the bluntest way to smooth the voice "
                           "leading.");
        sits.addRow(rangeNote_, 58);

        // --- Voice leading.
        auto& leading = addCard("Voice leading");
        styleSlider(smoothSlider_);
        leading.addRow(smoothSlider_, 24);
        smoothRow_ = std::make_unique<look::StatRow>("Smoothness");
        leading.addRow(*smoothRow_, 18);
        smoothNote_.setText("At 0 every chord is voiced in its own best register, wherever that "
                            "leaves the last one. At 100 the voicing that moves least from the "
                            "chord before it wins, even if that means an odd register.");
        leading.addRow(smoothNote_, 44);

        glideLabel_.setText("GLIDE", look::muted);
        leading.addRow(glideLabel_, 14);
        styleSlider(glideSlider_);
        leading.addRow(glideSlider_, 24);
        glideNote_.setText(
            "How long a chord change cross-fades between the tones leaving and the tones "
            "arriving, instead of the engine's ordinary quick click-avoidance fade. Off is the "
            "same snap it has always been; raising it trades a little immediacy for a smoother "
            "hand-off between chords. A tone common to both chords is never touched by this -- it "
            "was already sustaining through the change.");
        leading.addRow(glideNote_, 58);

        // --- Styles.
        auto& styles = addCard("Voicing style");
        stylesNote_.setText("Choose as many as you like and the best of them for the moment is "
                            "used. Choose none and every style is a candidate -- which is the "
                            "setting to leave it on if you would rather not think about it.");
        styles.addRow(stylesNote_, 44);
        for (int i = 0; i < jazz::kStyleCount; ++i) {
            styleToggle(*styleToggles_.add(new juce::ToggleButton()),
                        HarmonizerAudioProcessor::kJazzStyleNames[i]);
            styleGrid_.add(*styleToggles_[i]);
        }
        styles.addRow(styleGrid_, styleGrid_.preferredHeight());
        styleToggle(shuffle_, "Shuffle between the chosen styles");
        styles.addRow(shuffle_, 24);
        shuffleNote_.setText("Varies which of the chosen styles a new chord gets, instead of "
                             "always taking the highest-scoring one. It only ever picks from "
                             "what you have selected, and never mid-chord.");
        styles.addRow(shuffleNote_, 30);
        styleToggle(double_, "Double your own note");
        styles.addRow(double_, 24);
        doubleNote_.setText("Off, the chord tone you are already playing is left out, so the "
                            "harmony sits around you. On, it is resynthesised too -- worth it "
                            "when you are running fully wet.");
        styles.addRow(doubleNote_, 30);

        // --- Custom chord dictionary: a user-built alternative to the one
        // above. It replaces which chord is picked for a degree, nothing else
        // -- range, voice leading and voice count still apply on top of it.
        auto& customCard = addCard("Custom chord dictionary");
        customIntro_.setText(
            "A dictionary you build yourself, in place of the one above -- an exact voicing per "
            "note of the key, played on the keyboard below. Always rooted on the note you play, "
            "so you are always a tone of it, and written once in terms of the key centre so it "
            "already covers all twelve keys -- it doesn't matter which octave you play the note "
            "in either, only which one it is. Pick which key to show it over below, and the "
            "degree buttons relabel as the concrete notes of that key: to see exactly what plays "
            "for a C5 on your horn over a held G, select \"G\" as the key and then \"D\" as the "
            "degree (a fifth above G). It is only a display and editing convenience -- what is "
            "stored never depends on which key you preview through, and it transposes exactly the "
            "same for whichever key you actually hold live. Range and voice leading still apply on "
            "top of a custom voicing; extensions and voicing style do not, since a voicing you "
            "built yourself already says exactly what it wants to be.");
        customCard.addRow(customIntro_, 96);
        styleToggle(customOn_, "Use custom dictionary");
        customCard.addRow(customOn_, 24);
        customStatus_.setText("");
        customCard.addRow(customStatus_, 30);

        styleToggle(customUseMajor_, "Use for major keys");
        styleToggle(customUseMinor_, "Use for minor keys");
        customContextGrid_.add(customUseMajor_);
        customContextGrid_.add(customUseMinor_);
        customCard.addRow(customContextGrid_, customContextGrid_.preferredHeight());
        customContextNote_.setText(
            "Leaving a context off keeps the built-in dictionary for it -- a custom major table "
            "with minor left off still gives you the ordinary minor chords the moment a second "
            "key is held. A degree left blank below does the same for just that one note. Turn "
            "this whole card off to get back to how jazz mode always worked.");
        customCard.addRow(customContextNote_, 44);

        // Which context's table the keyboard below is editing.
        editContextMajor_.setButtonText("Editing: major");
        editContextMinor_.setButtonText("Editing: minor");
        styleSmallButton(editContextMajor_);
        styleSmallButton(editContextMinor_);
        editContextMajor_.onClick = [this] { setEditContext(false); };
        editContextMinor_.onClick = [this] { setEditContext(true); };
        editContextGrid_.add(editContextMajor_);
        editContextGrid_.add(editContextMinor_);
        customCard.addRow(editContextGrid_, editContextGrid_.preferredHeight());

        // Which key the degree buttons and the keyboard below are shown
        // relative to -- purely for a concrete example to look at and edit
        // through, since the dictionary itself is key-agnostic and this
        // changes nothing about what is stored. Defaults to C.
        previewKeyLabel_.setText("SHOW IT OVER THIS KEY", look::muted);
        customCard.addRow(previewKeyLabel_, 14);
        for (int i = 0; i < 12; ++i) previewKeyCombo_.addItem(jazz::pitchClassName(i), i + 1);
        previewKeyCombo_.setSelectedId(1, juce::dontSendNotification);
        styleCombo(previewKeyCombo_);
        previewKeyCombo_.onChange = [this] {
            previewKeyPc_ = juce::jlimit(0, 11, previewKeyCombo_.getSelectedId() - 1);
            relabelDegreeButtons();
            refreshCustomEditor();
        };
        customCard.addRow(previewKeyCombo_, 26);

        // Which degree of that context the keyboard below is editing --
        // labelled as the concrete note it would be over the key selected
        // above, so "what plays for a C5 over a held G" is just "select G
        // above, then click D" (the degree a fifth above G).
        for (int i = 0; i < 12; ++i) {
            auto* b = degreeButtons_.add(new juce::TextButton(jazz::pitchClassName(i)));
            styleSmallButton(*b);
            b->setTooltip("Scale degree " + shortDegreeLabel(i) + " above the key you hold.");
            b->onClick = [this, i] { setEditDegree(i); };
            degreeGrid_.add(*b);
        }
        customCard.addRow(degreeGrid_, degreeGrid_.preferredHeight());

        customVoicingLabel_.setText("VOICING", look::muted);
        customCard.addRow(customVoicingLabel_, 14);
        customVoicingStatus_.setText("");
        customVoicingRow_.addAndMakeVisible(customVoicingStatus_);
        customVoicingRow_.addAndMakeVisible(clearVoicingButton_);
        clearVoicingButton_.setButtonText("Clear");
        clearVoicingButton_.onClick = [this] {
            processor_.clearJazzCustomVoicing(editMinor_, editDegree_);
            refreshCustomEditor();
        };
        customVoicingRow_.onResize = [this] {
            clearVoicingButton_.setBounds(customVoicingRow_.getWidth() - 74, 0, 74,
                                          customVoicingRow_.getHeight());
            customVoicingStatus_.setBounds(0, 0, customVoicingRow_.getWidth() - 82,
                                           customVoicingRow_.getHeight());
        };
        customCard.addRow(customVoicingRow_, 26);

        // --- Record mode: build the voicing by ear on a real controller
        // instead of clicking the keyboard below. Every note played while it
        // is on joins the chord; Reset clears what has been captured so far
        // without leaving record mode; Save commits it to the selected
        // degree, the same result a run of keyboard clicks would give.
        recordRow_.addAndMakeVisible(recordButton_);
        recordRow_.addAndMakeVisible(recordResetButton_);
        recordRow_.addAndMakeVisible(recordSaveButton_);
        recordButton_.setButtonText("Record");
        recordResetButton_.setButtonText("Reset");
        recordSaveButton_.setButtonText("Save");
        styleSmallButton(recordButton_);
        styleSmallButton(recordResetButton_);
        styleSmallButton(recordSaveButton_);
        recordButton_.onClick = [this] {
            processor_.setJazzCustomRecording(!processor_.jazzCustomRecording());
            refreshCustomEditor();
        };
        recordResetButton_.onClick = [this] {
            processor_.clearJazzCustomRecordedNotes();
            refreshCustomEditor();
        };
        recordSaveButton_.onClick = [this] {
            const auto notes = processor_.jazzCustomRecordedNotes();
            const int root = previewRootNote();
            processor_.clearJazzCustomVoicing(editMinor_, editDegree_);
            for (const int note : notes) {
                processor_.setJazzCustomVoicingNote(editMinor_, editDegree_, note - root, true);
            }
            processor_.setJazzCustomRecording(false);
            refreshCustomEditor();
        };
        recordRow_.onResize = [this] {
            const int w = recordRow_.getWidth() / 3;
            recordButton_.setBounds(0, 0, w - 4, recordRow_.getHeight());
            recordResetButton_.setBounds(w, 0, w - 4, recordRow_.getHeight());
            recordSaveButton_.setBounds(2 * w, 0, w - 4, recordRow_.getHeight());
        };
        customCard.addRow(recordRow_, 28);
        recordNote_.setText(
            "Record plays through your MIDI controller instead of the keyboard below -- play the "
            "chord as if the key you were holding were the one selected above. Save writes it to "
            "the selected degree; it does not touch the named preset library below until you save "
            "one there too.");
        customCard.addRow(recordNote_, 44);

        customCard.addRow(customKeyboard_, 96);
        customKeyboard_.onNoteClicked = [this](int note) {
            if (processor_.jazzCustomRecording()) return;   // showing the live capture, not the saved entry
            const int offset = note - previewRootNote();
            if (offset < -jazz::kMaxCustomOffset || offset > jazz::kMaxCustomOffset) return;
            const auto entry = processor_.jazzCustomEntry(editMinor_, editDegree_);
            bool present = false;
            for (int i = 0; i < entry.count; ++i) present |= entry.offsets[i] == offset;
            processor_.setJazzCustomVoicingNote(editMinor_, editDegree_, offset, !present);
            refreshCustomEditor();
        };

        rangeButtonsRow_.addAndMakeVisible(octaveOutButton_);
        rangeButtonsRow_.addAndMakeVisible(octaveInButton_);
        rangeButtonsRow_.addAndMakeVisible(shiftLeftButton_);
        rangeButtonsRow_.addAndMakeVisible(shiftRightButton_);
        rangeButtonsRow_.addAndMakeVisible(resetRangeButton_);
        octaveOutButton_.setButtonText("- Octave");
        octaveInButton_.setButtonText("+ Octave");
        shiftLeftButton_.setButtonText("<");
        shiftRightButton_.setButtonText(">");
        resetRangeButton_.setButtonText("Reset");
        for (auto* b : {&octaveOutButton_, &octaveInButton_, &shiftLeftButton_, &shiftRightButton_,
                        &resetRangeButton_}) {
            styleSmallButton(*b);
        }
        octaveOutButton_.onClick = [this] { customKeyboard_.growByOctave(-1); };
        octaveInButton_.onClick = [this] { customKeyboard_.growByOctave(1); };
        shiftLeftButton_.onClick = [this] { customKeyboard_.shiftOctaves(-1); };
        shiftRightButton_.onClick = [this] { customKeyboard_.shiftOctaves(1); };
        resetRangeButton_.onClick = [this] { customKeyboard_.resetRange(); };
        rangeButtonsRow_.onResize = [this] {
            const int w = rangeButtonsRow_.getWidth() / 5;
            int x = 0;
            for (auto* b : {&octaveOutButton_, &octaveInButton_, &shiftLeftButton_,
                            &shiftRightButton_, &resetRangeButton_}) {
                b->setBounds(x, 0, w - 4, rangeButtonsRow_.getHeight());
                x += w;
            }
        };
        customCard.addRow(rangeButtonsRow_, 26);
        customKeyboardNote_.setText(
            "Click a key to add or remove it from the voicing for the selected degree. The "
            "keyboard's own range is just for editing -- move or resize it freely, it does not "
            "change what plays.");
        customCard.addRow(customKeyboardNote_, 30);

        // --- Copying a voicing to another note or context, shifted rather
        // than retyped by hand.
        copyLabel_.setText("COPY THIS VOICING TO", look::muted);
        customCard.addRow(copyLabel_, 14);
        for (int i = 0; i < 12; ++i) {
            copyTarget_.addItem(juce::String("Major: ") + jazz::pitchClassName(i), i + 1);
        }
        for (int i = 0; i < 12; ++i) {
            copyTarget_.addItem(juce::String("Minor: ") + jazz::pitchClassName(i), i + 13);
        }
        copyTarget_.setSelectedId(1, juce::dontSendNotification);
        styleCombo(copyTarget_);
        copyButton_.setButtonText("Copy");
        copyRow_.addAndMakeVisible(copyTarget_);
        copyRow_.addAndMakeVisible(copyButton_);
        copyRow_.onResize = [this] {
            copyButton_.setBounds(copyRow_.getWidth() - 74, 0, 74, copyRow_.getHeight());
            copyTarget_.setBounds(0, 0, copyRow_.getWidth() - 82, copyRow_.getHeight());
        };
        copyButton_.onClick = [this] {
            const int id = copyTarget_.getSelectedId();
            if (id <= 0) return;
            const bool toMinor = id > 12;
            const int toDegree = (toMinor ? id - 13 : id - 1);
            processor_.copyJazzCustomVoicing(editMinor_, editDegree_, toMinor, toDegree);
            customVoicingStatus_.setText(
                juce::String("Copied to ") + jazz::pitchClassName(toDegree) +
                    (toMinor ? " (minor)." : " (major)."),
                look::accent);
        };
        customCard.addRow(copyRow_, 28);

        copyTableMajorToMinor_.setButtonText("Copy whole major table to minor");
        copyTableMinorToMajor_.setButtonText("Copy whole minor table to major");
        styleSmallButton(copyTableMajorToMinor_);
        styleSmallButton(copyTableMinorToMajor_);
        copyTableMajorToMinor_.onClick = [this] {
            processor_.copyJazzCustomTable(false, true);
            refreshCustomEditor();
        };
        copyTableMinorToMajor_.onClick = [this] {
            processor_.copyJazzCustomTable(true, false);
            refreshCustomEditor();
        };
        copyTableGrid_.add(copyTableMajorToMinor_);
        copyTableGrid_.add(copyTableMinorToMajor_);
        customCard.addRow(copyTableGrid_, copyTableGrid_.preferredHeight());

        relabelDegreeButtons();
        setEditContext(false);
        setEditDegree(0);

        // --- Generate from MIDI: analyse a file instead of building the
        // table by hand.
        auto& midiCard = addCard("Generate from MIDI");
        midiImportNote_.setText(
            "Feeds a MIDI performance through the same key-and-chord analysis, instead of "
            "building the table above by hand. Finds the key centre (or centres, if it "
            "modulates) and, within each, what chord was played over which scale degree -- "
            "filling in as much of the table as the file gives evidence for. A degree it never "
            "touched stays blank, using the built-in chord the same way an unfilled hand-built "
            "entry does. This replaces the whole custom dictionary, so save a preset first if you "
            "want to keep what is there now. It is a heuristic, not a transcription -- check the "
            "keyboard above afterwards and adjust anything it got wrong.");
        midiCard.addRow(midiImportNote_, 86);
        midiImportButton_.setButtonText("Choose a MIDI file...");
        styleSmallButton(midiImportButton_);
        midiCard.addRow(midiImportButton_, 28);
        midiImportStatus_.setText("");
        midiCard.addRow(midiImportStatus_, 30);

        midiImportButton_.onClick = [this] {
            midiChooser_ = std::make_unique<juce::FileChooser>("Choose a MIDI file", juce::File(),
                                                               "*.mid;*.midi");
            const auto flags = juce::FileBrowserComponent::openMode |
                               juce::FileBrowserComponent::canSelectFiles;
            midiChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
                const auto file = chooser.getResult();
                if (!file.existsAsFile()) return;

                const auto summary = processor_.importJazzCustomDictionaryFromMidiFile(file);
                if (summary.ok) {
                    midiImportStatus_.setText(
                        "Found " + juce::String(summary.keySegments) + " key centre" +
                            (summary.keySegments == 1 ? "" : "s") + ", filled " +
                            juce::String(summary.degreesFilled) + " of 24 degrees from " +
                            juce::String(summary.chordsAnalyzed) + " sampled chords.",
                        look::accent);
                    setEditContext(false);
                    setEditDegree(0);
                } else {
                    midiImportStatus_.setText(summary.error, look::warn);
                }
            });
        };

        // --- Presets for that dictionary: a named library on disk, so one
        // built for a project can be brought into another.
        auto& presetCard = addCard("Custom dictionary presets");
        presetsNote_.setText("Save the dictionary above under a name to come back to later, "
                             "independent of any particular project.");
        presetCard.addRow(presetsNote_, 30);

        presetNameEditor_.setTextToShowWhenEmpty("Preset name", look::muted);
        presetNameEditor_.setColour(juce::TextEditor::backgroundColourId, look::surfaceVariant);
        presetNameEditor_.setColour(juce::TextEditor::textColourId, look::text);
        presetNameEditor_.setColour(juce::TextEditor::outlineColourId,
                                    juce::Colours::transparentBlack);
        saveButton_.setButtonText("Save As");
        saveRow_.addAndMakeVisible(presetNameEditor_);
        saveRow_.addAndMakeVisible(saveButton_);
        saveRow_.onResize = [this] {
            saveButton_.setBounds(saveRow_.getWidth() - 90, 0, 90, saveRow_.getHeight());
            presetNameEditor_.setBounds(0, 0, saveRow_.getWidth() - 98, saveRow_.getHeight());
        };
        presetCard.addRow(saveRow_, 28);

        styleCombo(presetList_);
        loadButton_.setButtonText("Load");
        deleteButton_.setButtonText("Delete");
        loadRow_.addAndMakeVisible(presetList_);
        loadRow_.addAndMakeVisible(loadButton_);
        loadRow_.addAndMakeVisible(deleteButton_);
        loadRow_.onResize = [this] {
            deleteButton_.setBounds(loadRow_.getWidth() - 74, 0, 74, loadRow_.getHeight());
            loadButton_.setBounds(loadRow_.getWidth() - 156, 0, 74, loadRow_.getHeight());
            presetList_.setBounds(0, 0, loadRow_.getWidth() - 164, loadRow_.getHeight());
        };
        presetCard.addRow(loadRow_, 28);
        presetStatus_.setText("");
        presetCard.addRow(presetStatus_, 24);

        saveButton_.onClick = [this] {
            const auto name = presetNameEditor_.getText().trim();
            if (name.isEmpty()) {
                presetStatus_.setText("Type a name first.", look::warn);
                return;
            }
            if (processor_.saveJazzDictionaryPreset(name)) {
                presetStatus_.setText("Saved as \"" + name + "\".", look::accent);
                presetNameEditor_.clear();
                refreshPresetList();
            } else {
                presetStatus_.setText("Could not save that preset.", look::warn);
            }
        };
        loadButton_.onClick = [this] {
            const auto name = presetList_.getText();
            if (name.isEmpty()) return;
            if (processor_.loadJazzDictionaryPreset(name)) {
                presetStatus_.setText("Loaded \"" + name + "\".", look::accent);
            } else {
                presetStatus_.setText("Could not load that preset.", look::warn);
            }
        };
        deleteButton_.onClick = [this] {
            const auto name = presetList_.getText();
            if (name.isEmpty()) return;
            processor_.deleteJazzDictionaryPreset(name);
            presetStatus_.setText("Deleted \"" + name + "\".", look::muted);
            refreshPresetList();
        };
        refreshPresetList();

        aEnable_ = std::make_unique<BA>(apvts, P::jazzMode, enable_);
        aNinth_ = std::make_unique<BA>(apvts, P::jazzNinth, ninth_);
        aEleventh_ = std::make_unique<BA>(apvts, P::jazzEleventh, eleventh_);
        aThirteenth_ = std::make_unique<BA>(apvts, P::jazzThirteenth, thirteenth_);
        aShuffle_ = std::make_unique<BA>(apvts, P::jazzShuffle, shuffle_);
        aDouble_ = std::make_unique<BA>(apvts, P::jazzDouble, double_);
        for (int i = 0; i < jazz::kStyleCount; ++i) {
            aStyles_.add(new BA(apvts, P::jazzStyle[i], *styleToggles_[i]));
        }
        aLow_ = std::make_unique<SA>(apvts, P::jazzRangeLow, lowSlider_);
        aHigh_ = std::make_unique<SA>(apvts, P::jazzRangeHigh, highSlider_);
        aSmooth_ = std::make_unique<SA>(apvts, P::jazzSmoothness, smoothSlider_);
        aVoices_ = std::make_unique<SA>(apvts, P::jazzVoices, voicesSlider_);
        aVoicesAuto_ = std::make_unique<BA>(apvts, P::jazzVoicesAuto, voicesAuto_);
        aTranspose_ = std::make_unique<SA>(apvts, P::jazzTranspose, transposeSlider_);
        aTransposeAudio_ = std::make_unique<SA>(apvts, P::jazzTransposeAudioIn, transposeAudioSlider_);
        aLatch_ = std::make_unique<BA>(apvts, P::jazzLatchKeys, latch_);
        aGlide_ = std::make_unique<SA>(apvts, P::jazzGlideMs, glideSlider_);

        aCustomOn_ = std::make_unique<BA>(apvts, P::jazzCustomOn, customOn_);
        aCustomUseMajor_ = std::make_unique<BA>(apvts, P::jazzCustomUseMajor, customUseMajor_);
        aCustomUseMinor_ = std::make_unique<BA>(apvts, P::jazzCustomUseMinor, customUseMinor_);

        lowSlider_.textFromValueFunction = [](double v) {
            return flatNoteName(static_cast<int>(v));
        };
        highSlider_.textFromValueFunction = lowSlider_.textFromValueFunction;
        smoothSlider_.textFromValueFunction = [](double v) {
            return juce::String(juce::roundToInt(v * 100.0)) + " %";
        };
        smoothSlider_.valueFromTextFunction = [](const juce::String& s) {
            return s.getDoubleValue() / 100.0;
        };
        transposeSlider_.textFromValueFunction = [](double v) {
            switch (static_cast<int>(v)) {
                case 0:  return juce::String("Concert (C)");
                case -2: return juce::String("Bb");
                case 3:  return juce::String("Eb");
                case 5:  return juce::String("F");
                default: return (v > 0 ? juce::String("+") : juce::String("")) +
                                juce::String(static_cast<int>(v)) + " st";
            }
        };
        transposeAudioSlider_.textFromValueFunction = transposeSlider_.textFromValueFunction;
        glideSlider_.textFromValueFunction = [](double v) {
            return v < 1.0 ? juce::String("Off") : juce::String(juce::roundToInt(v)) + " ms";
        };
        lowSlider_.updateText();
        highSlider_.updateText();
        transposeSlider_.updateText();
        transposeAudioSlider_.updateText();
        glideSlider_.updateText();
        smoothSlider_.updateText();
    }

    ~JazzPage() override { processor_.setJazzCustomRecording(false); }

    void refresh() {
        const auto view = processor_.jazzView();
        const auto settings = processor_.jazzSettings();

        voicesSlider_.setEnabled(!voicesAuto_.getToggleState());

        // Keys Transpose already moved keyCentrePc for real (see
        // collectKeys()), so it prints straight, no further conversion here.
        // Audio In Transpose can't touch melodyNote -- it's read from real
        // sound -- so the editor is the only place it's ever applied, and
        // only to what gets printed: written = concert -
        // melodyTransposeSemitones, matching the slider's own convention
        // that -2/+3/+5 are Bb/Eb/F.
        const int melodyTranspose = view.melodyTransposeSemitones;

        if (view.keyCentrePc >= 0 && (view.heldKeys > 0 || view.keyLatched)) {
            juce::String tag;
            if (view.keyLatched) tag += "  (latched)";
            if (view.sustainHeld) tag += "  [sustain]";
            keyRow_->setValue(juce::String(jazz::pitchClassName(view.keyCentrePc)) +
                              (view.minorKey ? " minor" : " major") + tag);
        } else {
            keyRow_->setValue(view.sustainHeld ? "sustain down, but nothing latched yet"
                                               : "hold a key");
        }

        if (view.melodyNote >= 0 && view.melodyHz > 0.0f) {
            playingRow_->setValue(
                flatNoteName(juce::jlimit(0, 127, view.melodyNote - melodyTranspose)) + "  " +
                juce::String(juce::roundToInt(view.melodyHz)) + " Hz  -  the " +
                jazz::degreeName(view.melodyDegree));
        } else {
            playingRow_->setValue("no pitch yet");
        }

        if (view.sounding && view.chordRootPc >= 0) {
            // Rebuilt from the published snapshot rather than kept as a string,
            // so the audio thread never has to format anything. A custom
            // voicing needs its actual notes too -- it has no fixed chord
            // type to spell a symbol from the way the built-in dictionary's
            // chords do.
            jazz::Voicing voicing;
            voicing.chordRootPc = view.chordRootPc;
            voicing.type = static_cast<jazz::ChordType>(view.typeIndex);
            voicing.customVoicing = view.customVoicing;
            voicing.count = view.noteCount;
            for (int i = 0; i < view.noteCount; ++i) voicing.notes[i] = view.notes[i];
            char symbol[64] = {};
            jazz::chordSymbol(voicing, settings, symbol, sizeof(symbol));
            chordRow_->setValue(juce::String(symbol) + "   " + juce::String(view.roman));

            juce::String notes;
            for (int i = 0; i < view.noteCount; ++i) {
                if (view.notes[i] < 0) continue;
                notes += (notes.isEmpty() ? "" : " ") + flatNoteName(view.notes[i]);
            }
            const juce::String styleLabel =
                view.customVoicing ? juce::String("Custom")
                                   : juce::String(jazz::styleName(
                                         static_cast<jazz::Style>(view.styleIndex)));
            voicingRow_->setValue(styleLabel + "  -  " + notes);
        } else {
            chordRow_->setValue("-");
            voicingRow_->setValue("-");
        }

        if (!view.enabled) {
            status_.setText("Switched off. The harmony mode on the main page is in charge.",
                            look::muted);
        } else if (view.heldKeys == 0 && !view.keyLatched) {
            status_.setText(view.sustainHeld
                                ? "Sustain is down, but no key has ever been held to latch."
                                : "Hold a key on your controller to set the key centre.",
                            look::warn);
        } else if (view.melodyHz <= 0.0f) {
            status_.setText("Key centre set. Play a note into the input and the chord follows it.",
                            look::warn);
        } else if (view.sustainHeld) {
            status_.setText("Sustain is down -- holding the chord out. Let go and press it again "
                            "for a new one.", look::accent);
        } else if (!view.sounding) {
            status_.setText("Waiting for a steady pitch to build a chord on.", look::warn);
        } else {
            status_.setText("Running - " + juce::String(view.noteCount) + " harmony voice" +
                                (view.noteCount == 1 ? "." : "s."), look::accent);
        }

        rangeRow_->setValue(flatNoteName(settings.rangeLow) + " to " +
                            flatNoteName(settings.rangeHigh) + "  (" +
                            juce::String(settings.rangeHigh - settings.rangeLow) + " semitones)");
        rangeRow_->setLabel(view.rangeLimited ? "Using instead" : "Chords live between");
        if (view.rangeLimited && view.sounding) {
            rangeRow_->setValue(flatNoteName(view.windowLow) + " to " +
                                flatNoteName(view.windowHigh) + "  (held near your note)");
        }
        smoothRow_->setValue(juce::String(juce::roundToInt(settings.smoothness * 100.0f)) + " %");

        bool anyStyle = false;
        for (int i = 0; i < jazz::kStyleCount; ++i) anyStyle |= settings.styles[i];
        stylesNote_.setText(
            anyStyle ? "Choosing more than one lets the plugin take whichever of them leads best "
                       "from the chord before it. Clear them all to let it consider every style."
                     : "Nothing selected: every style is a candidate and the one that leads best "
                       "from the chord before it wins.",
            anyStyle ? look::muted : look::accent);

        // Two ways the range does not get used as written, both worth saying out
        // loud rather than leaving the player to wonder why it sounds wrong.
        if (settings.rangeHigh - settings.rangeLow < 12) {
            rangeNote_.setText("That range is narrower than an octave, so an octave is used. "
                               "Widen it to get control back.", look::warn);
        } else if (view.rangeLimited && view.sounding) {
            rangeNote_.setText("This range is more than two octaves from the note you are "
                               "playing, which is further than the engine can shift a voice. "
                               "The chord is being held closer to you so that it stays in tune. "
                               "Move the range nearer your own register to use it as written.",
                               look::warn);
        } else {
            rangeNote_.setText("Nothing sounds outside this window. Widening it lets each chord "
                               "find its own best register; tightening it forces successive "
                               "chords to share registers, which is the bluntest way to smooth "
                               "the voice leading.", look::muted);
        }

        if (!settings.useCustomDictionary) {
            customStatus_.setText("Off -- using the built-in dictionary, as jazz mode always has.",
                                  look::muted);
        } else if (!settings.customDict.useMajor && !settings.customDict.useMinor) {
            customStatus_.setText("On, but neither context below is selected, so nothing changes "
                                  "yet -- turn on major, minor, or both.", look::warn);
        } else {
            const juce::String which = settings.customDict.useMajor && settings.customDict.useMinor
                                            ? "major and minor keys"
                                            : (settings.customDict.useMajor ? "major keys"
                                                                            : "minor keys");
            customStatus_.setText("On, overriding the chord dictionary for " + which + ".",
                                  look::accent);
        }

        refreshCustomEditor();
    }

private:
    class Holder final : public juce::Component {
    public:
        std::function<void()> onResize;
        void resized() override { if (onResize) onResize(); }
    };

    /** Where the keyboard editor's root is drawn: the actual note the chord
     *  would be rooted on if you held previewKeyPc_ and played editDegree_
     *  semitones above it, placed in the octave just above middle C. Purely
     *  a display and editing convenience -- the dictionary itself stores
     *  offsets from the root, not from any particular key, so this can be
     *  changed freely without touching what is saved. */
    int previewRootNote() const {
        const int rootPc = ((previewKeyPc_ + editDegree_) % 12 + 12) % 12;
        return 60 + rootPc;
    }

    /** Relabels the twelve degree buttons as concrete notes over whichever
     *  key previewKeyPc_ names, so "what plays for a C5 over a held G" is
     *  just "select G above, then click D". */
    void relabelDegreeButtons() {
        for (int i = 0; i < degreeButtons_.size(); ++i) {
            const int pc = ((previewKeyPc_ + i) % 12 + 12) % 12;
            degreeButtons_[i]->setButtonText(jazz::pitchClassName(pc));
        }
    }

    void setEditContext(bool minor) {
        editMinor_ = minor;
        editContextMajor_.setToggleState(!minor, juce::dontSendNotification);
        editContextMinor_.setToggleState(minor, juce::dontSendNotification);
        refreshCustomEditor();
    }

    void setEditDegree(int degree) {
        editDegree_ = juce::jlimit(0, 11, degree);
        for (int i = 0; i < degreeButtons_.size(); ++i) {
            degreeButtons_[i]->setToggleState(i == editDegree_, juce::dontSendNotification);
        }
        refreshCustomEditor();
    }

    /** Pulls the currently selected degree's voicing back from the processor
     *  and repaints the keyboard and summary from it -- called after any
     *  edit, and every timer tick so a preset load or host automation shows
     *  up here too. */
    void refreshCustomEditor() {
        const bool recording = processor_.jazzCustomRecording();
        recordButton_.setToggleState(recording, juce::dontSendNotification);
        recordButton_.setButtonText(recording ? "Stop" : "Record");

        const int root = previewRootNote();
        const juce::String context = editMinor_ ? "minor" : "major";
        const juce::String keyName = jazz::pitchClassName(previewKeyPc_);
        const juce::String degree =
            juce::String(jazz::pitchClassName(((previewKeyPc_ + editDegree_) % 12 + 12) % 12)) +
            " (" + shortDegreeLabel(editDegree_) + ")";

        if (recording) {
            const auto recorded = processor_.jazzCustomRecordedNotes();
            customKeyboard_.setHighlightedNotes(recorded, root);
            customVoicingStatus_.setText(
                degree + " over a held " + keyName + ", " + context + ":  recording -- " +
                    juce::String(recorded.size()) + " note" + (recorded.size() == 1 ? "" : "s") +
                    " captured. Play more, Reset to clear, or Save to write it to this degree.",
                look::warn);
            return;
        }

        const auto entry = processor_.jazzCustomEntry(editMinor_, editDegree_);
        juce::Array<int> notes;
        juce::String summary;
        for (int i = 0; i < entry.count; ++i) {
            notes.add(root + entry.offsets[i]);
            summary += (summary.isEmpty() ? "" : " ") +
                       juce::String(jazz::intervalName(entry.offsets[i]));
        }
        customKeyboard_.setHighlightedNotes(notes, root);

        if (entry.count == 0) {
            customVoicingStatus_.setText(
                degree + " over a held " + keyName + ", " + context +
                    " -- blank, using the built-in chord for now.",
                look::muted);
        } else {
            customVoicingStatus_.setText(
                degree + " over a held " + keyName + ", " + context + ":  " + summary, look::accent);
        }
    }

    void refreshPresetList() {
        const auto current = presetList_.getText();
        presetList_.clear(juce::dontSendNotification);
        const auto names = processor_.jazzDictionaryPresetNames();
        for (int i = 0; i < names.size(); ++i) presetList_.addItem(names[i], i + 1);
        if (names.contains(current)) presetList_.setText(current, juce::dontSendNotification);
        else if (!names.isEmpty()) presetList_.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    HarmonizerAudioProcessor& processor_;

    juce::ToggleButton enable_, ninth_, eleventh_, thirteenth_, shuffle_, double_;
    juce::OwnedArray<juce::ToggleButton> styleToggles_;
    Grid toneGrid_{3, 26}, styleGrid_{3, 26};

    juce::Slider lowSlider_, highSlider_, smoothSlider_, voicesSlider_, transposeSlider_,
        transposeAudioSlider_, glideSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aGlide_;
    look::Note glideLabel_, glideNote_;
    juce::ToggleButton voicesAuto_, latch_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aVoicesAuto_, aLatch_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aTranspose_,
        aTransposeAudio_;
    std::unique_ptr<ChipGroup> octaveChips_, inversionChips_;
    std::unique_ptr<look::StatRow> keyRow_, playingRow_, chordRow_, voicingRow_, rangeRow_,
        smoothRow_;
    look::Note intro_, status_, tonesNote_, voicesNote_, voicesLabel_, octaveLabel_,
        inversionLabel_, shiftNote_, rangeLabel_, rangeNote_, smoothNote_, stylesNote_,
        shuffleNote_, doubleNote_, transposeLabel_, transposeNote_, transposeAudioLabel_,
        transposeAudioNote_, latchNote_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aEnable_, aNinth_,
        aEleventh_, aThirteenth_, aShuffle_, aDouble_;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> aStyles_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aLow_, aHigh_,
        aSmooth_, aVoices_;

    // --- Custom chord dictionary.
    juce::ToggleButton customOn_, customUseMajor_, customUseMinor_;
    Grid customContextGrid_{2, 26};
    look::Note customIntro_, customStatus_, customContextNote_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aCustomOn_,
        aCustomUseMajor_, aCustomUseMinor_;

    // Which (context, degree) the keyboard editor below is showing right now
    // -- pure UI state, not a parameter; the dictionary itself is written
    // once per context and covers all twelve keys regardless of which one is
    // selected here.
    bool editMinor_ = false;
    int editDegree_ = 0;
    int previewKeyPc_ = 0;
    juce::TextButton editContextMajor_, editContextMinor_;
    Grid editContextGrid_{2, 26};
    look::Note previewKeyLabel_;
    juce::ComboBox previewKeyCombo_;
    juce::OwnedArray<juce::TextButton> degreeButtons_;
    Grid degreeGrid_{6, 24};
    look::Note customVoicingLabel_, customVoicingStatus_, customKeyboardNote_;
    juce::TextButton clearVoicingButton_;
    Holder customVoicingRow_;
    juce::TextButton recordButton_, recordResetButton_, recordSaveButton_;
    Holder recordRow_;
    look::Note recordNote_;
    harmonizer::PianoKeyboardComponent customKeyboard_;
    juce::TextButton octaveOutButton_, octaveInButton_, shiftLeftButton_, shiftRightButton_,
        resetRangeButton_;
    Holder rangeButtonsRow_;

    look::Note copyLabel_;
    juce::ComboBox copyTarget_;
    juce::TextButton copyButton_;
    Holder copyRow_;
    juce::TextButton copyTableMajorToMinor_, copyTableMinorToMajor_;
    Grid copyTableGrid_{2, 26};

    look::Note midiImportNote_, midiImportStatus_;
    juce::TextButton midiImportButton_;
    std::unique_ptr<juce::FileChooser> midiChooser_;

    // --- Custom dictionary presets.
    juce::TextEditor presetNameEditor_;
    juce::ComboBox presetList_;
    juce::TextButton saveButton_, loadButton_, deleteButton_;
    look::Note presetsNote_, presetStatus_;
    Holder saveRow_, loadRow_;
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

        // --- Version history: every build stays available on its own release,
        // so an update that turns out to break something can be undone.
        auto& history = addCard("Version history");
        historyNote_.setText("Every build stays available, so an update that causes a problem "
                             "can be undone. Load the list, pick a version, and it installs the "
                             "same way an update does.");
        history.addRow(historyNote_, 44);
        history.addRow(loadHistoryRow_, 30);
        loadHistoryRow_.addAndMakeVisible(loadHistoryButton_);
        loadHistoryRow_.onResize = [this] {
            loadHistoryButton_.setBounds(0, 0, 190, loadHistoryRow_.getHeight());
        };
        loadHistoryButton_.onClick = [this] { updater_.loadVersionHistory(); };

        historyStatus_.setText("Not loaded yet.");
        history.addRow(historyStatus_, 18);

        history.addRow(rollBackRow_, 28);
        styleCombo(historyCombo_);
        rollBackRow_.addAndMakeVisible(historyCombo_);
        rollBackRow_.addAndMakeVisible(rollBackButton_);
        rollBackRow_.onResize = [this] {
            rollBackButton_.setBounds(rollBackRow_.getWidth() - 150, 0, 150,
                                      rollBackRow_.getHeight());
            historyCombo_.setBounds(0, 0, rollBackRow_.getWidth() - 158, rollBackRow_.getHeight());
        };
        rollBackButton_.setButtonText("Install this version");
        rollBackButton_.setEnabled(false);
        rollBackButton_.onClick = [this] {
            const int index = historyCombo_.getSelectedItemIndex();
            if (index < 0 || index >= historyTags_.size()) return;
            updater_.installVersion(historyTags_[index]);
        };

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

        // Version history rides the same background worker and busy flag as
        // the ordinary update flow above -- only one job runs at a time, and a
        // rollback in progress shows through updateNote_ exactly like a normal
        // install does, since both write the same Status fields.
        loadHistoryButton_.setEnabled(!busy);

        if (!s.historyLoaded) {
            historyStatus_.setText("Not loaded yet.", look::muted);
        } else if (s.historyError.isNotEmpty()) {
            historyStatus_.setText(s.historyError, look::warn);
        } else if (s.history.isEmpty()) {
            historyStatus_.setText("No past versions found.", look::muted);
        } else {
            historyStatus_.setText(juce::String(s.history.size()) + " version(s) available.",
                                   look::muted);
        }

        // Repopulate only when the set actually changed, so a selection made
        // mid-browse is not reset by every refresh at 12 Hz.
        if (s.history.size() != historyTags_.size()) {
            historyCombo_.clear(juce::dontSendNotification);
            historyTags_.clear();
            for (int i = 0; i < s.history.size(); ++i) {
                const auto& v = s.history.getReference(i);
                juce::String label = v.versionName;
                if (v.versionCode == PluginUpdater::currentVersionCode()) label += "  (installed)";
                historyCombo_.addItem(label, i + 1);
                historyTags_.add(v.tag);
            }
        }
        rollBackButton_.setEnabled(!busy && !historyTags_.isEmpty());
    }

    HarmonizerAudioProcessor& processor_;
    PluginUpdater& updater_;

    look::Note qualityIntro_, qualityNote_, adaptiveNote_, adaptLatencyNote_, adaptVoicesNote_,
        formantNote_, windowNote_, bypassNote_, updateNote_, windowLabel_, outputLabel_,
        historyNote_, historyStatus_;
    std::unique_ptr<look::StatRow> runningRow_, amountRow_, effectiveRow_, installedRow_;
    std::unique_ptr<ChipGroup> qualityChips_, windowChips_;
    juce::Slider amountSlider_, gainSlider_;
    juce::ToggleButton adaptLatency_, adaptVoices_, formant_, bypass_;

    Holder buttonRow_;
    juce::TextButton checkButton_{"Check for updates"}, installButton_{"Download and install"};
    double updateProgress_ = 0.0;
    juce::ProgressBar updateBar_{updateProgress_};

    Holder loadHistoryRow_, rollBackRow_;
    juce::TextButton loadHistoryButton_{"Load version history"}, rollBackButton_;
    juce::ComboBox historyCombo_;
    juce::StringArray historyTags_;   // parallel to historyCombo_'s items

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

    pageButton_.onClick = [this] {
        showPage(page_ == PageId::Settings ? PageId::Main : PageId::Settings);
    };
    addAndMakeVisible(pageButton_);
    jazzButton_.onClick = [this] {
        showPage(page_ == PageId::Jazz ? PageId::Main : PageId::Jazz);
    };
    addAndMakeVisible(jazzButton_);
    panicButton_.onClick = [this] { processor_.allNotesOff(); };
    addAndMakeVisible(panicButton_);

    mainPage_ = std::make_unique<MainPage>(processor_, knobLook_);
    jazzPage_ = std::make_unique<JazzPage>(processor_);
    settingsPage_ = std::make_unique<SettingsPage>(processor_, updater_);

    viewport_.setScrollBarsShown(true, false);
    viewport_.setColour(juce::ScrollBar::thumbColourId, look::surfaceVariant);
    addAndMakeVisible(viewport_);
    showPage(PageId::Main);

    setResizable(true, true);
    setResizeLimits(460, 420, 900, 1200);
    setSize(560, 720);
    startTimerHz(12);
}

HarmonizerAudioProcessorEditor::~HarmonizerAudioProcessorEditor() {
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
}

void HarmonizerAudioProcessorEditor::showPage(PageId page) {
    page_ = page;
    pageButton_.setButtonText(page == PageId::Settings ? "Back" : "Settings");
    jazzButton_.setButtonText(page == PageId::Jazz ? "Back" : "Jazz");

    juce::Component* view = mainPage_.get();
    if (page == PageId::Jazz) view = jazzPage_.get();
    else if (page == PageId::Settings) view = settingsPage_.get();
    viewport_.setViewedComponent(view, false);
    resized();
}

void HarmonizerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(look::background);
}

void HarmonizerAudioProcessorEditor::resized() {
    // Three destinations now, so the version moves under the title rather than
    // competing with them for the strip along the top.
    title_.setBounds(16, 6, 200, 26);
    version_.setBounds(18, 30, 160, 14);
    panicButton_.setBounds(getWidth() - 232, 12, 62, 26);
    jazzButton_.setBounds(getWidth() - 164, 12, 62, 26);
    pageButton_.setBounds(getWidth() - 96, 12, 84, 26);

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

    switch (page_) {
        case PageId::Settings: settingsPage_->refresh(m, rate); break;
        case PageId::Jazz:     jazzPage_->refresh(); break;
        default:               mainPage_->refresh(m, processor_.traffic(), rate, block); break;
    }

    if (auto* page = dynamic_cast<Page*>(viewport_.getViewedComponent())) {
        const int want = page->preferredHeight();
        if (page->getHeight() != want) page->setSize(page->getWidth(), want);
    }
}
