#pragma once

/**
 * Jazz chord mode -- the plugin's own layer on top of the shared engine.
 *
 * The workflow it exists for: a horn player holds a key (or two) on a
 * controller to name the key centre, then plays. Whatever note arrives on the
 * audio input is read as a scale degree of that key, a chord is looked up for
 * that degree, and the rest of that chord is voiced around the played note.
 *
 * Nothing in here knows about JUCE, the engine, or audio. It is integer music
 * theory: keys and a played note in, a set of MIDI notes out. That keeps it
 * testable on its own and keeps it out of the Android app, which does not ship
 * this mode.
 *
 * It allocates nothing and is cheap enough to run from the audio thread.
 */

namespace jazz {

// Ten engine voices exist, but a seven-tone chord minus the note the player is
// already covering never needs more than this.
inline constexpr int kMaxVoicingNotes = 8;

enum class ChordType {
    Maj7 = 0,   // 1 3 5 7        -- extensions 9, #11, 13
    Dom7,       // 1 3 5 b7       -- extensions 9, #11, 13
    Dom7b9,     // 1 3 5 b7       -- altered: b9, #11, b13
    Min7,       // 1 b3 5 b7      -- 9, 11, 13
    Min7b5,     // 1 b3 b5 b7     -- 9, 11, b13
    Dim7,       // 1 b3 b5 bb7    -- 9, 11, b13
    Count
};

/**
 * How the chord tones are spread out. Everything except Auto is a voicing a
 * pianist would recognise; Auto is "none selected", where every style is a
 * candidate and the one that leads best from the last chord wins.
 */
enum class Style {
    Close = 0,   // stacked thirds from the root
    Drop2,       // close, second voice from the top dropped an octave
    Drop3,       // close, third voice from the top dropped an octave
    Drop24,      // close, second and fourth from the top dropped
    Rootless,    // 3-5-7-9 -- the bass player's job left to the bass player
    Quartal,     // stacked fourths, So What
    Shell,       // root, third, seventh, plus the top extension
    Spread,      // open position: alternate voices lifted an octave
    Cluster,     // every tone packed inside one octave
    Count
};
inline constexpr int kStyleCount = static_cast<int>(Style::Count);

struct Settings {
    // Sevenths are always in. These stack on top of them.
    bool ninth = false;
    bool eleventh = false;
    bool thirteenth = false;

    // Where the chord sits. The octave shift moves the register the voicer aims
    // for; the inversion shift rotates the voicing itself -- pushing it down an
    // inversion drops the top voice an octave, which leaves the played note
    // sitting higher in the chord than it was.
    int octaveShift = 0;        // -2 .. +2
    int inversionShift = 0;     // -3 .. +3

    // Hard bounds on every note that sounds. Tightening this is the main way to
    // make the voice leading smoother: with less room, successive chords have
    // to reuse the same few registers.
    int rangeLow = 50;          // D3
    int rangeHigh = 79;         // G5

    // 0 = voice each chord in its own best register, 1 = move as little as
    // possible from the chord before it.
    float smoothness = 0.5f;

    // None set means "work it out" -- every style is considered.
    bool styles[kStyleCount] = {};

    // With more than one style in play, vary the pick rather than always
    // taking the highest score.
    bool shuffle = false;

    // The played note is already in the room, so its unison is dropped from the
    // voicing unless this is on.
    bool doubleMelody = false;

    int maxNotes = kMaxVoicingNotes;
};

struct Voicing {
    int  notes[kMaxVoicingNotes] = {};     // MIDI notes to sound, ascending
    int  degrees[kMaxVoicingNotes] = {};   // 1, 3, 5, 7, 9, 11 or 13
    int  count = 0;

    int  keyCentrePc = -1;       // pitch class named by the held key(s)
    bool minorKey = false;
    int  scaleDegree = 0;        // 0..11, played note above the key centre
    int  chordRootPc = -1;
    ChordType type = ChordType::Maj7;
    const char* roman = "";      // "iim7", "V7", "bVII7" ...

    Style style = Style::Close;
    int  melodyNote = -1;        // what the player is actually sounding
    int  melodyDegree = 1;       // which chord tone that turns out to be
};

/**
 * Holds the previous chord so the next one can lead smoothly from it. One
 * instance per plugin; not thread safe, and meant to live on the audio thread.
 */
class Voicer {
public:
    void reset();
    void setSeed(unsigned seed) { rng_ = seed ? seed : 1u; }

    /**
     * keys       held MIDI notes, any order. One key names a major key centre;
     *            two or more name a minor one, on the lowest key held.
     * melodyNote the played note, as MIDI.
     *
     * Returns false when there is nothing to voice (no keys, no played note, or
     * every chord tone collapsed onto the player's own note).
     */
    bool update(const int* keys, int keyCount, int melodyNote, const Settings& s,
                Voicing& out);

private:
    int  prev_[kMaxVoicingNotes] = {};
    int  prevCount_ = 0;
    unsigned rng_ = 0x9E3779B9u;

    float nextRandom();
};

// --- naming, for the display -----------------------------------------------

/** Flats, because that is how these chords are spelled on a lead sheet. */
const char* pitchClassName(int pitchClass);

/** "Dm9", "G13", "Ebmaj7#11" -- the chord as written, extensions included. */
void chordSymbol(const Voicing& v, const Settings& s, char* out, int outSize);

const char* styleName(Style style);

/** "root", "3rd", "b9" ... -- what the player's own note is inside the chord. */
const char* degreeName(int degree);

}  // namespace jazz
