#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonizer {

/**
 * A plain piano strip: white and black keys across a configurable MIDI note
 * range, each key clickable, and any subset paintable as "on" -- with a
 * root note, if one is given, drawn in its own colour so it reads apart from
 * the rest. Used everywhere the plugin needs to show or build a set of notes
 * by hand: the custom chord dictionary's voicing editor, its record-into-a-
 * key mode, and the chord readout it can show alongside either of them.
 *
 * Pure UI -- it knows nothing about jazz mode or its parameters. The owner
 * pushes highlighted notes in with setHighlightedNotes() and reads clicks
 * back out through onNoteClicked.
 */
class PianoKeyboardComponent final : public juce::Component {
public:
    PianoKeyboardComponent();

    /** Which notes to show pressed. rootNote, if >= 0, is drawn apart from
     *  the rest so a voicing's root reads at a glance. */
    void setHighlightedNotes(const juce::Array<int>& notes, int rootNote = -1);

    /** The displayed range, inclusive -- clamped to a sane span (at least
     *  one octave, at most eight) and to the MIDI note range 0..127. */
    void setRange(int lowestNote, int highestNote);
    int lowestNote() const { return lowNote_; }
    int highestNote() const { return highNote_; }

    /** Moves the visible range by whole octaves without changing its width. */
    void shiftOctaves(int deltaOctaves);

    /** Grows (positive) or shrinks (negative) the visible range by one
     *  octave on each end, keeping it centred where it is. */
    void growByOctave(int deltaOctaves);

    /** Back to the default 4 octaves centred on middle C. */
    void resetRange();

    /** Fired on every click, with the MIDI note clicked. The owner decides
     *  what that means -- toggling a voicing note, capturing a recorded
     *  note, or nothing if the keyboard is read-only for the moment. */
    std::function<void(int midiNote)> onNoteClicked;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    struct KeyRect {
        int note = 0;
        juce::Rectangle<float> bounds;
        bool isBlack = false;
    };

    void rebuildLayout();
    int noteAt(juce::Point<float> position) const;

    int lowNote_ = 36;    // C2
    int highNote_ = 84;   // C6 -- 4 octaves, centred on middle C (60)
    juce::Array<int> highlighted_;
    int rootNote_ = -1;

    std::vector<KeyRect> whiteKeys_, blackKeys_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoKeyboardComponent)
};

}  // namespace harmonizer
