#pragma once

#include "JazzVoicer.h"

#include <vector>

/**
 * Builds a custom chord dictionary from a MIDI performance instead of by
 * hand: finds the key centre (or centres, if the performance modulates) and,
 * within each, what chord was actually played over which scale degree,
 * filling in the same CustomDictionary the keyboard editor and record mode
 * build note by note.
 *
 * Nothing in here knows about JUCE or file formats -- it consumes an already
 *-parsed list of notes in MIDI ticks, the same separation JazzVoicer keeps
 * from the engine, so the analysis itself can be tested on its own
 * (JazzHarness.cpp) without a MIDI file on disk. The JUCE-facing loader that
 * turns an actual .mid file into this list lives in PluginProcessor.cpp,
 * next to the rest of the message-thread-only editor plumbing.
 */
namespace jazz {

/** One note, already resolved from matched note-on/note-off pairs. */
struct ImportNote {
    long long startTick = 0;
    long long durationTicks = 0;
    int pitch = 0;   // 0..127
};

/** One distinct voicing actually seen while sampling the performance, before
 *  it gets collapsed down to one winner per degree -- what lets a caller
 *  pull out a single signature chord instead of taking the whole
 *  dictionary. minor/degree describe which scale degree it was played on,
 *  the same way a CustomEntry's own slot does; offsets/count are the
 *  voicing itself, relative to its own root exactly like a CustomEntry's
 *  are. There is no key centre recorded -- a custom voicing is always
 *  rooted on the note actually played, so (like a CustomEntry) this sounds
 *  identical regardless of which key it happened to be sampled under. */
struct ImportCandidate {
    bool minor = false;
    int degree = 0;
    int offsets[kMaxVoicingNotes] = {};
    int count = 0;
    int votes = 0;   // how many sampled instants matched this exact voicing
};

struct ImportResult {
    CustomDictionary dict;
    int keySegments = 0;      // distinct stretches of a single key centre found
    int chordsAnalyzed = 0;   // sampled instants with two or more notes sounding
    int degreesFilled = 0;    // how many of the 24 (12 major + 12 minor) ended up with a voicing

    // Every distinct voicing sampled anywhere in the file, most-played
    // first -- a superset of what dict ended up keeping, since dict only
    // keeps one (the top vote) per degree. Capped well below every possible
    // (context, degree) slot's own cap so a real song's handful of
    // recognisable chords aren't buried in near-duplicates.
    std::vector<ImportCandidate> candidates;
};

/**
 * notes need not be sorted. ticksPerQuarterNote must be positive -- SMPTE-
 * timed files have no such value, and the caller is expected to reject those
 * before this is reached rather than pass a synthesized one.
 */
ImportResult analyzeForCustomDictionary(const ImportNote* notes, int count,
                                        int ticksPerQuarterNote);

}  // namespace jazz
