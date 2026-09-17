#include "JazzChordLibrary.h"

namespace jazz {

namespace {

using LV = LibraryVoicing;
using LT = LibraryTheme;
using LQ = LibraryQuality;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Semitone offsets above a root of 0. Nothing here is folded into a
// particular octave -- the same way a hand-built custom entry works, the
// range and voice-leading settings on the Jazz page place the result once it
// is loaded, and the played note still always supplies the root.
constexpr LV kLibrary[] = {
    // --- Rootless: left-hand shapes with no root, the way a pianist voices
    // a chord when a bassist is already covering it.
    {"Rootless A", "3, 5, 7, 9 -- no root, the classic Bill Evans left-hand shape.",
     LT::Rootless, LQ::Maj7, {4, 7, 11, 14}, 4},
    {"Rootless B", "7, 9, 3, #11 -- a fourths-flavoured rootless shape a step up from A.",
     LT::Rootless, LQ::Maj7, {11, 14, 16, 18}, 4},
    {"Rootless A", "3, 5, b7, 9 -- no root, sits under a dominant without naming it.",
     LT::Rootless, LQ::Dom7, {4, 7, 10, 14}, 4},
    {"Rootless alt", "3, b13, b7, b9 -- a compact altered-dominant rootless shape.",
     LT::Rootless, LQ::Dom7, {4, 8, 10, 13}, 4},
    {"Rootless A", "b3, 5, b7, 9 -- no root, the minor seventh counterpart to Rootless A.",
     LT::Rootless, LQ::Min7, {3, 7, 10, 14}, 4},
    {"Rootless B", "b7, 9, b3, 11 -- a minor eleventh flavour with no root.",
     LT::Rootless, LQ::Min7, {10, 14, 15, 17}, 4},
    {"Rootless", "b3, b5, b7, 9 -- a half-diminished shape with a natural 9 on top.",
     LT::Rootless, LQ::Min7b5, {3, 6, 10, 14}, 4},

    // --- Drop 2: an ordinary close-position four-part chord with its
    // second voice from the top dropped an octave -- the most common
    // comping and guitar-chord shape there is.
    {"Drop 2", "Root, 3, 5, 7 with the 5th dropped an octave under the root.",
     LT::Drop2, LQ::Maj7, {-5, 0, 4, 11}, 4},
    {"Drop 2", "Root, 3, 5, b7 with the 5th dropped an octave under the root.",
     LT::Drop2, LQ::Dom7, {-5, 0, 4, 10}, 4},
    {"Drop 2", "Root, b3, 5, b7 with the 5th dropped an octave under the root.",
     LT::Drop2, LQ::Min7, {-5, 0, 3, 10}, 4},
    {"Drop 2", "Root, b3, b5, b7 with the b5 dropped an octave under the root.",
     LT::Drop2, LQ::Min7b5, {-6, 0, 3, 10}, 4},
    {"Drop 2", "Root, b3, b5, bb7 with the b5 dropped an octave under the root.",
     LT::Drop2, LQ::Dim7, {-6, 0, 3, 9}, 4},
    {"Drop 2 + 9", "Root, 3, 5, 7, 9 with the 7th dropped an octave under the root.",
     LT::Drop2, LQ::Maj7, {-1, 0, 4, 7, 14}, 5},

    // --- Drop 3: the third voice from the top dropped an octave instead --
    // a wider stretch than drop 2, and the one that lands the root on top.
    {"Drop 3", "Root, 3, 5, 7 with the 3rd dropped an octave under the root.",
     LT::Drop3, LQ::Maj7, {-8, 0, 7, 11}, 4},
    {"Drop 3", "Root, 3, 5, b7 with the 3rd dropped an octave under the root.",
     LT::Drop3, LQ::Dom7, {-8, 0, 7, 10}, 4},
    {"Drop 3", "Root, b3, 5, b7 with the b3rd dropped an octave under the root.",
     LT::Drop3, LQ::Min7, {-9, 0, 7, 10}, 4},

    // --- Quartal: stacked fourths instead of thirds -- So What, McCoy
    // Tyner, the modal-jazz sound.
    {"So What chord", "Four stacked fourths with a major 3rd on top -- the Kind of Blue voicing.",
     LT::Quartal, LQ::Min7, {0, 5, 10, 15, 19}, 5},
    {"Quartal triad", "Two stacked fourths -- light, ambiguous, works under a sus or min7 feel.",
     LT::Quartal, LQ::Any, {0, 5, 10}, 3},
    {"Quartal sus", "Three stacked fourths -- a bigger version of the quartal triad.",
     LT::Quartal, LQ::Dom7, {0, 5, 10, 15}, 4},
    {"Quartal upper structure", "Fourths stacked from the 3rd -- 3, 13, #9 over the root.",
     LT::Quartal, LQ::Maj7, {4, 9, 14}, 3},

    // --- Shell: the bare minimum needed to say the chord's quality --
    // root, third and seventh, nothing else in the way.
    {"Shell", "Root, 3, 7 -- the bare essentials, leaves the most room for the melody.",
     LT::Shell, LQ::Maj7, {0, 4, 11}, 3},
    {"Shell", "Root, 3, b7 -- the bare essentials of a dominant.",
     LT::Shell, LQ::Dom7, {0, 4, 10}, 3},
    {"Shell", "Root, b3, b7 -- the bare essentials of a minor seventh.",
     LT::Shell, LQ::Min7, {0, 3, 10}, 3},
    {"Shell", "Root, b3, b5, b7 -- the b5 kept in, since it is the whole colour here.",
     LT::Shell, LQ::Min7b5, {0, 3, 6, 10}, 4},
    {"Shell + 9", "Root, 3, 7, 9 -- a shell with the 9th added, 5th left out.",
     LT::Shell, LQ::Maj7, {0, 4, 11, 14}, 4},

    // --- Upper-structure triads: an ordinary major or minor triad, built on
    // an extension, played straight over the root underneath it.
    {"bII triad over the root", "Ab-C-Eb over G -- the classic tritone upper structure, a 7b9#11 sound.",
     LT::UpperStructure, LQ::Dom7, {1, 5, 8}, 3},
    {"Major triad on the 9", "D-F#-A over C -- gives a clean maj9#11 colour.",
     LT::UpperStructure, LQ::Maj7, {2, 6, 9}, 3},
    {"Minor triad on the 5", "G-Bb-D over C -- a rootless dominant 9 flavour hiding inside.",
     LT::UpperStructure, LQ::Dom7, {7, 10, 14}, 3},
    {"Major triad on the b7", "Bb-D-F over C -- gives 9 and 11 without stating the root or 3rd.",
     LT::UpperStructure, LQ::Dom7, {10, 14, 17}, 3},

    // --- Spread / open voicings: alternate tones lifted an octave, so the
    // chord spans wide instead of stacking close.
    {"Spread", "Root, 7 up a 9th, 3 up a 9th above that -- wide open, no two tones close.",
     LT::Spread, LQ::Maj7, {0, 7, 16, 23}, 4},
    {"Spread", "The dominant version of the same open spacing.",
     LT::Spread, LQ::Dom7, {0, 7, 16, 22}, 4},
    {"Spread", "The minor seventh version of the same open spacing.",
     LT::Spread, LQ::Min7, {0, 7, 15, 22}, 4},
    {"Open shell", "Root, 7th a 9th up, 3rd a 9th above that -- a wide three-note shell.",
     LT::Spread, LQ::Maj7, {0, 11, 16}, 3},

    // --- Gospel / neo-soul: added 6ths and 9ths, sus moves and bright
    // close clusters, the church-piano and neo-soul comping vocabulary.
    {"6/9", "Root, 3, 5, 6, 9 -- the classic gospel and pop tonic chord, no 7th at all.",
     LT::Gospel, LQ::Maj7, {0, 4, 7, 9, 14}, 5},
    {"Sus add9", "Root, 4, 5, b7, 9 -- a suspended dominant move before it resolves.",
     LT::Gospel, LQ::Dom7, {0, 5, 7, 10, 14}, 5},
    {"Neo-soul min11", "Root, b3, 5, b7, 9, 11 -- the dense stacked-thirds neo-soul minor chord.",
     LT::Gospel, LQ::Min7, {0, 3, 7, 10, 14, 17}, 6},
    {"Passing diminished", "Root, b3, b5, bb7 -- a symmetric diminished chord for a chromatic walk-up.",
     LT::Gospel, LQ::Dim7, {0, 3, 6, 9}, 4},
    {"Add2 cluster", "Root, 2, 3, 5 -- a bright, close cluster common in gospel comping.",
     LT::Gospel, LQ::Maj7, {0, 2, 4, 7}, 4},

    // --- Blues & altered dominants.
    {"7#9", "Root, 3, 5, b7, #9 -- the \"Hendrix chord,\" a blues and funk dominant.",
     LT::Blues, LQ::Dom7, {0, 4, 7, 10, 15}, 5},
    {"7b9", "Root, 3, 5, b7, b9 -- a tense, classic altered-dominant colour.",
     LT::Blues, LQ::Dom7, {0, 4, 7, 10, 13}, 5},
    {"7alt", "3, b7, b9, #11 -- no root, a compact fully altered dominant.",
     LT::Blues, LQ::Dom7, {4, 10, 13, 18}, 4},
    {"13sus", "Root, 4, 5, b7, 9, 13 -- a big suspended dominant for a blues turnaround.",
     LT::Blues, LQ::Dom7, {0, 5, 7, 10, 14, 21}, 6},
    {"Dominant, no 5th", "Root, 3, b13, b7 -- a lean blues dominant shape.",
     LT::Blues, LQ::Dom7, {0, 4, 8, 10}, 4},

    // --- Minor ii-V-i: shapes built for cadencing into a minor key.
    {"iiø with 9", "Root, b3, b5, b7, 9 -- a half-diminished ii with the 9th kept in.",
     LT::MinorCadence, LQ::Min7b5, {0, 3, 6, 10, 14}, 5},
    {"V7b9, no root", "3, b7, b9, b13 -- the dominant that resolves a minor ii-V.",
     LT::MinorCadence, LQ::Dom7, {4, 10, 13, 20}, 4},
    {"i: minor-major 7", "Root, b3, 5, 7 -- the melodic-minor tonic, a natural 7th on a minor chord.",
     LT::MinorCadence, LQ::Min7, {0, 3, 7, 11}, 4},
    {"i: min6/9", "Root, b3, 5, 6, 9 -- the classic minor tonic \"landing\" chord.",
     LT::MinorCadence, LQ::Min7, {0, 3, 7, 9, 14}, 5},

    // --- Neo-soul: the Robert Glasper / D'Angelo vocabulary -- lydian major
    // (always a #11), dorian minor (a natural 6th instead of the usual b6),
    // and suspended or backdoor dominants that never need to resolve.
    {"Neo-soul min11", "Root, b3, b7, 9, 11 -- no 5th, the core Glasper minor voicing.",
     LT::NeoSoul, LQ::Min7, {0, 3, 10, 14, 17}, 5},
    {"Min6/9", "Root, b3, 5, 6, 9 -- warm dorian colour, also a tonic \"landing\" chord.",
     LT::NeoSoul, LQ::Min7, {0, 3, 7, 9, 14}, 5},
    {"Maj9#11 (lydian)", "Root, 3, 7, 9, #11 -- the bright, floating major sound he leans on constantly.",
     LT::NeoSoul, LQ::Maj7, {0, 4, 11, 14, 18}, 5},
    {"9sus13 (backdoor)", "Root, 4, b7, 9, 13 -- a suspended dominant that never needs to resolve.",
     LT::NeoSoul, LQ::Dom7, {0, 5, 10, 14, 21}, 5},
    {"Add2, no 3rd", "Root, 2, 5 -- a bare open triad, 3rd left out, deliberately ambiguous.",
     LT::NeoSoul, LQ::Any, {0, 2, 7}, 3},
    {"Fourths + 9", "Root, 4, b7, 9 -- a quartal stack with a 9th on top instead of a 3rd.",
     LT::NeoSoul, LQ::Min7, {0, 5, 10, 14}, 4},
    {"Stacked b9/#9", "Root, 3, b7, b9, #9 -- a churchy altered dominant with both tensions in.",
     LT::NeoSoul, LQ::Dom7, {0, 4, 10, 13, 15}, 5},
    {"Open min9", "Root, b3, b7, 9 -- no 5th, leaves room underneath for a bass note.",
     LT::NeoSoul, LQ::Min7, {0, 3, 10, 14}, 4},
};

constexpr int kLibraryCount = static_cast<int>(sizeof(kLibrary) / sizeof(kLibrary[0]));

}  // namespace

int libraryVoicingCount() { return kLibraryCount; }

const LibraryVoicing& libraryVoicing(int index) {
    static const LibraryVoicing empty{"", "", LibraryTheme::Rootless, LibraryQuality::Any, {}, 0};
    if (index < 0 || index >= kLibraryCount) return empty;
    return kLibrary[index];
}

const char* libraryThemeName(LibraryTheme theme) {
    switch (theme) {
        case LibraryTheme::Rootless:       return "Rootless";
        case LibraryTheme::Drop2:          return "Drop 2";
        case LibraryTheme::Drop3:          return "Drop 3";
        case LibraryTheme::Quartal:        return "Quartal";
        case LibraryTheme::Shell:          return "Shell";
        case LibraryTheme::UpperStructure: return "Upper structure";
        case LibraryTheme::Spread:         return "Spread";
        case LibraryTheme::Gospel:         return "Gospel & neo-soul";
        case LibraryTheme::Blues:          return "Blues & altered";
        case LibraryTheme::MinorCadence:   return "Minor ii-V-i";
        case LibraryTheme::NeoSoul:        return "Neo-soul (Glasper)";
        case LibraryTheme::Count:
        default:                          return "?";
    }
}

const char* libraryQualityName(LibraryQuality quality) {
    switch (quality) {
        case LibraryQuality::Maj7:   return "Maj7";
        case LibraryQuality::Dom7:   return "Dom7";
        case LibraryQuality::Min7:   return "Min7";
        case LibraryQuality::Min7b5: return "Min7b5";
        case LibraryQuality::Dim7:   return "Dim7";
        case LibraryQuality::Any:    return "Any";
        case LibraryQuality::Count:
        default:                    return "?";
    }
}

CustomEntry libraryVoicingToEntry(const LibraryVoicing& v) {
    CustomEntry entry;
    entry.count = clampi(v.count, 0, kMaxVoicingNotes);
    for (int i = 0; i < entry.count; ++i) entry.offsets[i] = v.offsets[i];
    return entry;
}

}  // namespace jazz
