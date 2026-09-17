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

// The engine shifts a voice at most two octaves away from the pitch it is given
// (its ratio is clamped to 0.25..4). A note voiced further out than this would
// sound at that limit instead of where it was placed -- the wrong pitch, under a
// display naming a note nobody is hearing. So it is a bound on the voicing, not
// a detail of the engine.
inline constexpr int kEngineReachSemitones = 24;

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

// How far a custom voicing's tones may sit from the root, in semitones --
// four octaves either way. The engine folds anything outside the range
// window back in regardless, so this is only a bound on how the value is
// stored (a keyboard editor, record mode, and MIDI import all need one).
inline constexpr int kMaxCustomOffset = 48;

/**
 * A user-built alternative to the dictionary baked into JazzVoicer.cpp: for
 * each of the twelve chromatic scale degrees, an explicit voicing rather
 * than a chord type. The chord is always rooted on the note you actually
 * play -- which is what keeps the one rule the built-in dictionary never
 * breaks intact here too: the played note is always a tone of the chord
 * (its root, in this case). It is defined once in terms of the key centre,
 * exactly the way the built-in dictionary is, so building it once already
 * covers all twelve keys -- naming a key centre just transposes it, the
 * same as it always did.
 *
 * Offsets are exact semitones above that root, not folded into an octave --
 * a voicing built two octaves wide keeps that spread until the range and
 * voice-leading settings below refold it, the same as any other jazz chord.
 * They carry no shape or extension of their own: whatever was picked on a
 * keyboard, recorded, or read out of a MIDI file plays exactly as given,
 * which is what makes the same entry serve manual editing, a recorded
 * voicing and an imported one without three different representations.
 */
struct CustomEntry {
    int offsets[kMaxVoicingNotes] = {};
    int count = 0;   // 0 = nothing chosen -- this degree falls back to the
                     // built-in dictionary rather than sounding no harmony
};

struct CustomDictionary {
    // Off means "fall back to the built-in dictionary for that context" -- a
    // custom major table with minor left off still gives you the ordinary
    // minor dictionary the moment a second key is held. The same fallback
    // happens per degree: a CustomEntry with count == 0 uses the built-in
    // chord for its degree even while the rest of the table overrides theirs.
    bool useMajor = false;
    bool useMinor = false;
    CustomEntry major[12];
    CustomEntry minor[12];
};

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

    // Nudges the placement search away from packing notes tight together
    // below mudCeiling (a MIDI note), where a close interval reads as mush
    // rather than a chord. A preference, not a wall -- style, range and voice
    // leading can still outweigh it.
    bool avoidMud = false;
    int  mudCeiling = 55;       // G3

    // Adds one extra voice a register below the rest of the chord, always the
    // chord's root. Counts against maxNotes -- turning this on with the count
    // already at the cap sheds the chord's least important tone to make room
    // rather than silently exceeding what was asked for.
    bool addBassNote = false;

    // A custom voicing normally gets re-registered every chord: which octave
    // it lands in is chosen fresh each time to lead smoothly from whatever
    // came before and to sit near the middle of the range below. Turning this
    // on skips that search for custom voicings and always places them at the
    // one octave nearest the middle of rangeLow..rangeHigh, so a voicing
    // built to sit in a specific register (a bass note on C3, say) stays
    // there instead of drifting to chase the melody or the previous chord.
    // Still folded back in if that register is further from the played note
    // than the engine can reach -- staying in tune wins over staying put.
    bool customVoicingFixedRegister = false;

    // A custom dictionary replaces the built-in chord-per-degree lookup,
    // context by context (see CustomDictionary above). Everything else in
    // this struct -- extensions, register, smoothness, style and voice count
    // -- still applies on top of whichever dictionary answers the lookup.
    bool useCustomDictionary = false;
    CustomDictionary customDict;
};

struct Voicing {
    int  notes[kMaxVoicingNotes] = {};     // MIDI notes to sound, ascending
    int  degrees[kMaxVoicingNotes] = {};   // 1, 3, 5, 7, 9, 11 or 13
    int  count = 0;

    int  keyCentrePc = -1;       // pitch class named by the held key(s)
    bool minorKey = false;
    int  scaleDegree = 0;        // 0..11, played note above the key centre
    int  chordRootPc = -1;
    ChordType type = ChordType::Maj7;   // meaningless when customVoicing is set
    const char* roman = "";      // "iim7", "V7", "bVII7" ...

    // True when a custom dictionary entry (not the built-in dictionary)
    // named this chord -- it has no fixed chord type, so the display has to
    // spell it from its actual notes instead of the usual type-based symbol.
    bool customVoicing = false;

    Style style = Style::Close;
    int  melodyNote = -1;        // what the player is actually sounding
    int  melodyDegree = 1;       // which chord tone that turns out to be

    // The range asked for was further from the played note than the engine can
    // shift, so the chord was brought closer to stay in tune. The panel says so
    // rather than quietly disobeying the range.
    bool rangeLimited = false;
    int  windowLow = 0;          // the window actually voiced into
    int  windowHigh = 127;
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

    // A custom dictionary entry has no static string to point Voicing::roman
    // at, so one is built into this buffer as it is looked up. It stays valid
    // for the life of the Voicer, which is what the published UI pointer
    // actually needs -- see PluginProcessor's jvRoman_.
    char customRomanBuf_[24] = {};

    float nextRandom();
};

// --- naming, for the display -----------------------------------------------

/** Flats, because that is how these chords are spelled on a lead sheet. */
const char* pitchClassName(int pitchClass);

/** "Dm9", "G13", "Ebmaj7#11" -- the chord as written, extensions included.
 *  For a custom voicing (v.customVoicing) there is no fixed chord type to
 *  spell it from, so this names the root followed by its actual tones
 *  instead -- always correct rather than guessed. */
void chordSymbol(const Voicing& v, const Settings& s, char* out, int outSize);

const char* styleName(Style style);

/** "root", "3rd", "b9" ... -- what the player's own note is inside the chord. */
const char* degreeName(int degree);

/** "R", "b9", "9", "b3", "3", "11", "#11", "5", "b13", "13", "b7", "7" -- the
 *  generic upper-structure name of a semitone offset above a chord root,
 *  independent of chord quality. A custom voicing has no chord type to name
 *  its tones against, so this is what labels them instead: on the keyboard
 *  editor, in a recorded voicing, and in the custom chord symbol above. */
const char* intervalName(int semitonesAboveRoot);

/**
 * The plain, unextended tones of a built-in chord type -- third, fifth,
 * seventh, in that order -- as semitones above the root. Writes at most
 * maxOffsets of them into outOffsets and returns how many. Used to seed a
 * custom entry with a sensible starting voicing (turning the custom
 * dictionary on for the first time, or loading a preset saved by an older,
 * chord-type-based version of it) without duplicating the type table.
 */
int chordTypeTones(ChordType type, int* outOffsets, int maxOffsets);

}  // namespace jazz
