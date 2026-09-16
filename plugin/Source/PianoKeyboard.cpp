#include "PianoKeyboard.h"

namespace harmonizer {

namespace {

bool isBlackKey(int note) {
    switch (((note % 12) + 12) % 12) {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

constexpr int kDefaultLow = 36;    // C2
constexpr int kDefaultHigh = 84;   // C6

}  // namespace

PianoKeyboardComponent::PianoKeyboardComponent() {
    setWantsKeyboardFocus(false);
}

void PianoKeyboardComponent::setHighlightedNotes(const juce::Array<int>& notes, int rootNote) {
    highlighted_ = notes;
    rootNote_ = rootNote;
    repaint();
}

void PianoKeyboardComponent::setRange(int lowestNote, int highestNote) {
    lowestNote = juce::jlimit(0, 115, lowestNote);
    highestNote = juce::jlimit(lowestNote + 12, 127, highestNote);
    // At most eight octaves, so the keys stay wide enough to click accurately.
    if (highestNote - lowestNote > 96) highestNote = lowestNote + 96;
    if (lowNote_ == lowestNote && highNote_ == highestNote) return;
    lowNote_ = lowestNote;
    highNote_ = highestNote;
    rebuildLayout();
    repaint();
}

void PianoKeyboardComponent::shiftOctaves(int deltaOctaves) {
    setRange(lowNote_ + deltaOctaves * 12, highNote_ + deltaOctaves * 12);
}

void PianoKeyboardComponent::growByOctave(int deltaOctaves) {
    setRange(lowNote_ - deltaOctaves * 12, highNote_ + deltaOctaves * 12);
}

void PianoKeyboardComponent::resetRange() { setRange(kDefaultLow, kDefaultHigh); }

void PianoKeyboardComponent::resized() { rebuildLayout(); }

void PianoKeyboardComponent::rebuildLayout() {
    whiteKeys_.clear();
    blackKeys_.clear();
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty()) return;

    int whiteCount = 0;
    for (int n = lowNote_; n <= highNote_; ++n) {
        if (!isBlackKey(n)) ++whiteCount;
    }
    if (whiteCount <= 0) return;
    const float whiteWidth = bounds.getWidth() / static_cast<float>(whiteCount);

    int whiteIndex = 0;
    for (int n = lowNote_; n <= highNote_; ++n) {
        if (isBlackKey(n)) continue;
        const juce::Rectangle<float> r(bounds.getX() + static_cast<float>(whiteIndex) * whiteWidth,
                                       bounds.getY(), whiteWidth, bounds.getHeight());
        whiteKeys_.push_back({n, r, false});
        ++whiteIndex;
    }

    // A black key sits between the two white keys either side of it -- find
    // where its lower white neighbour landed and offset from there.
    const float blackWidth = whiteWidth * 0.62f;
    const float blackHeight = bounds.getHeight() * 0.62f;
    whiteIndex = 0;
    for (int n = lowNote_; n <= highNote_; ++n) {
        if (isBlackKey(n)) continue;
        if (n + 1 <= highNote_ && isBlackKey(n + 1)) {
            const float centre = bounds.getX() + static_cast<float>(whiteIndex + 1) * whiteWidth;
            blackKeys_.push_back({n + 1,
                                  {centre - blackWidth * 0.5f, bounds.getY(), blackWidth, blackHeight},
                                  true});
        }
        ++whiteIndex;
    }
}

int PianoKeyboardComponent::noteAt(juce::Point<float> position) const {
    for (const auto& k : blackKeys_) if (k.bounds.contains(position)) return k.note;
    for (const auto& k : whiteKeys_) if (k.bounds.contains(position)) return k.note;
    return -1;
}

void PianoKeyboardComponent::mouseDown(const juce::MouseEvent& e) {
    const int note = noteAt(e.position);
    if (note >= 0 && onNoteClicked) onNoteClicked(note);
}

void PianoKeyboardComponent::paint(juce::Graphics& g) {
    const juce::Colour white = juce::Colour(0xfff2efe9);
    const juce::Colour black = juce::Colour(0xff1a1a1a);
    const juce::Colour on = juce::Colour(0xffe0b64d);       // an ordinary voiced tone
    const juce::Colour rootOn = juce::Colour(0xffe0574d);   // the root, apart from the rest

    const auto drawKey = [&](const KeyRect& k) {
        const bool isOn = highlighted_.contains(k.note);
        const bool isRoot = k.note == rootNote_;
        juce::Colour fill = k.isBlack ? black : white;
        if (isOn) fill = isRoot ? rootOn : on;
        g.setColour(fill);
        g.fillRect(k.bounds);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRect(k.bounds, 1.0f);

        if (!k.isBlack && k.note % 12 == 0) {
            auto label = k.bounds;
            label.removeFromTop(label.getHeight() - 14.0f);
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("C" + juce::String(k.note / 12 - 1), label, juce::Justification::centred);
        }
    };

    for (const auto& k : whiteKeys_) drawKey(k);
    for (const auto& k : blackKeys_) drawKey(k);
}

}  // namespace harmonizer
