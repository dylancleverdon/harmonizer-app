#pragma once

#include "JazzVoicer.h"

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

struct ImportResult {
    CustomDictionary dict;
    int keySegments = 0;      // distinct stretches of a single key centre found
    int chordsAnalyzed = 0;   // sampled instants with two or more notes sounding
    int degreesFilled = 0;    // how many of the 24 (12 major + 12 minor) ended up with a voicing
};

/**
 * notes need not be sorted. ticksPerQuarterNote must be positive -- SMPTE-
 * timed files have no such value, and the caller is expected to reject those
 * before this is reached rather than pass a synthesized one.
 */
ImportResult analyzeForCustomDictionary(const ImportNote* notes, int count,
                                        int ticksPerQuarterNote);

}  // namespace jazz
