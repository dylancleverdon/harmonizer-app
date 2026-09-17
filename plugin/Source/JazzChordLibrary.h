#pragma once

#include "JazzVoicer.h"

/**
 * A curated library of named jazz voicings, browsable from the Jazz page and
 * loadable straight into a custom-dictionary degree -- separate from that
 * dictionary itself, which stays exactly what the player built by hand.
 *
 * Each entry uses the same representation a custom dictionary entry does
 * (semitone offsets above a root, jazz::CustomEntry-compatible), so loading
 * one is exactly the same operation as clicking those notes on the keyboard
 * editor. Nothing in here is affected by which key or degree is being
 * edited -- these offsets are written once, relative to a root, the same way
 * the custom dictionary itself is.
 */
namespace jazz {

/** How a library voicing is grouped for browsing. */
enum class LibraryTheme {
    Rootless = 0,     // left-hand shapes with no root -- the bass covers it
    Drop2,
    Drop3,
    Quartal,          // stacked fourths -- So What, McCoy Tyner
    Shell,            // the bare minimum: root, third, seventh
    UpperStructure,    // a triad built on an extension, played over the root
    Spread,           // open voicings, alternate tones lifted an octave
    Gospel,           // added 6ths/9ths, sus moves, close bright clusters
    Blues,            // altered and blues-flavoured dominants
    MinorCadence,     // ii-V-i shapes in a minor key
    Count
};
inline constexpr int kLibraryThemeCount = static_cast<int>(LibraryTheme::Count);

/** Which chord quality a voicing is written to sit under. Independent of
 *  jazz::ChordType -- this only steers the library's own filter, and "Any"
 *  covers a shape (quartal fourths, for instance) that reads reasonably over
 *  more than one quality. */
enum class LibraryQuality {
    Maj7 = 0,
    Dom7,
    Min7,
    Min7b5,
    Dim7,
    Any,
    Count
};
inline constexpr int kLibraryQualityCount = static_cast<int>(LibraryQuality::Count);

struct LibraryVoicing {
    const char* name;
    const char* description;
    LibraryTheme theme;
    LibraryQuality quality;
    int offsets[kMaxVoicingNotes];
    int count;
};

int libraryVoicingCount();
const LibraryVoicing& libraryVoicing(int index);

const char* libraryThemeName(LibraryTheme theme);
const char* libraryQualityName(LibraryQuality quality);

/** Builds an ordinary CustomEntry out of a library voicing -- the exact
 *  thing "load into editor" writes to a degree. */
CustomEntry libraryVoicingToEntry(const LibraryVoicing& v);

}  // namespace jazz
