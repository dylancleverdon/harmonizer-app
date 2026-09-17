#include "JazzDictionaryFactoryPresets.h"

#include <initializer_list>

namespace jazz {

namespace {

/** Builds a CustomEntry from a plain list of semitone offsets above whatever
 *  degree it ends up assigned to -- the same thing typing notes on the
 *  keyboard editor produces, just written out here instead of clicked in. */
CustomEntry mk(std::initializer_list<int> offsets) {
    CustomEntry e;
    for (int off : offsets) {
        if (e.count >= kMaxVoicingNotes) break;
        e.offsets[e.count++] = off;
    }
    return e;
}

// Degree indices throughout, both contexts: 0=I/i, 1=bII, 2=ii, 3=bIII,
// 4=iii/III, 5=IV/iv, 6=#IV/#iv, 7=V/v, 8=bVI, 9=vi/vi(dorian 6th),
// 10=bVII, 11=VII -- the same chromatic-degree convention the built-in
// dictionary and every custom entry already use.

// --- Barry Harris: major 6th chords instead of major7, the "6th diminished
// scale" passing diminished on every chromatic degree, and altered dominants
// resolving with a 13 rather than a plain 9 -- his whole system in miniature.
CustomDictionary buildBarryHarris() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 7, 9});      // I6
    d.major[1] = mk({0, 3, 6, 9});      // passing dim7
    d.major[2] = mk({0, 3, 7, 9});      // ii, min6
    d.major[3] = mk({0, 3, 6, 9});      // passing dim7
    d.major[4] = mk({0, 3, 7, 10});     // iii, min7
    d.major[5] = mk({0, 4, 7, 9});      // IV6
    d.major[6] = mk({0, 3, 6, 9});      // passing dim7
    d.major[7] = mk({0, 4, 10, 14, 21}); // V, 3-b7-9-13
    d.major[8] = mk({0, 3, 6, 9});      // passing dim7
    d.major[9] = mk({0, 3, 7, 9});      // vi6, the mirror of I6
    d.major[10] = mk({0, 4, 7, 10});    // backdoor dominant
    d.major[11] = mk({0, 3, 6, 9});     // leading-tone dim7

    d.minor[0] = mk({0, 3, 7, 9});      // i, min6
    d.minor[1] = mk({0, 3, 6, 9});
    d.minor[2] = mk({0, 3, 6, 10});     // iim7b5
    d.minor[3] = mk({0, 4, 7, 9});      // relative major 6
    d.minor[4] = mk({0, 4, 10, 13});    // secondary dominant, 7b9
    d.minor[5] = mk({0, 3, 7, 9});      // iv6
    d.minor[6] = mk({0, 3, 6, 9});
    d.minor[7] = mk({0, 4, 10, 13});    // V7b9, the classic bebop cadence
    d.minor[8] = mk({0, 4, 7, 9});      // bVI6
    d.minor[9] = mk({0, 3, 6, 9});
    d.minor[10] = mk({0, 4, 7, 10});    // bVII7
    d.minor[11] = mk({0, 3, 6, 9});
    return d;
}

// --- Bill Evans: rootless throughout -- A-form (3-5-7-9) and B-form
// (7-9-3-13) shapes and their minor/altered counterparts, close and smooth.
CustomDictionary buildBillEvans() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({4, 7, 11, 14});    // Imaj9, rootless A
    d.major[1] = mk({4, 8, 10, 13});    // altered dominant, rootless
    d.major[2] = mk({3, 7, 10, 14});    // ii min9, rootless
    d.major[3] = mk({3, 7, 10, 14});
    d.major[4] = mk({3, 7, 10, 14});
    d.major[5] = mk({4, 7, 11, 14});    // IVmaj9, rootless
    d.major[6] = mk({4, 8, 10, 13});
    d.major[7] = mk({4, 10, 14, 21});   // V9/13, rootless
    d.major[8] = mk({4, 7, 11, 14});
    d.major[9] = mk({3, 7, 10, 14});
    d.major[10] = mk({4, 10, 14, 21});
    d.major[11] = mk({4, 8, 10, 13});

    d.minor[0] = mk({3, 7, 10, 14});    // i min9, rootless
    d.minor[1] = mk({4, 7, 11, 14});    // Neapolitan maj9, rootless
    d.minor[2] = mk({3, 6, 10, 14});    // iim7b5 with 9, rootless
    d.minor[3] = mk({4, 7, 11, 14});    // relative major, rootless
    d.minor[4] = mk({4, 8, 10, 13});    // secondary altered dominant
    d.minor[5] = mk({3, 7, 10, 14});    // dorian iv, rootless
    d.minor[6] = mk({4, 8, 10, 13});
    d.minor[7] = mk({4, 10, 13, 20});   // V7b9/13, rootless
    d.minor[8] = mk({4, 7, 11, 14});
    d.minor[9] = mk({3, 6, 10, 14});
    d.minor[10] = mk({4, 10, 14, 21});
    d.minor[11] = mk({4, 8, 10, 13});
    return d;
}

// --- Modal (McCoy Tyner): fourths stacked wherever a chord can carry them,
// the "So What" voicing on every diatonic minor-quality degree.
CustomDictionary buildModal() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 5, 10, 15});
    d.major[1] = mk({0, 5, 10, 15});
    d.major[2] = mk({0, 5, 10, 15, 19});   // So What chord
    d.major[3] = mk({0, 5, 10, 15});
    d.major[4] = mk({0, 5, 10, 15, 19});
    d.major[5] = mk({0, 5, 10, 14});
    d.major[6] = mk({0, 5, 10, 15});
    d.major[7] = mk({0, 5, 10, 15});
    d.major[8] = mk({0, 5, 10, 15});
    d.major[9] = mk({0, 5, 10, 15, 19});   // So What chord, relative minor
    d.major[10] = mk({0, 5, 10, 15});
    d.major[11] = mk({0, 5, 10, 15});

    d.minor[0] = mk({0, 5, 10, 15, 19});   // So What chord, tonic
    d.minor[1] = mk({0, 5, 10, 15});
    d.minor[2] = mk({0, 3, 6, 10});
    d.minor[3] = mk({0, 5, 10, 14});
    d.minor[4] = mk({0, 4, 10, 13});       // secondary dominant, kept tertian
    d.minor[5] = mk({0, 5, 10, 15, 19});   // So What chord, dorian iv
    d.minor[6] = mk({0, 5, 10, 15});
    d.minor[7] = mk({0, 5, 10, 15});
    d.minor[8] = mk({0, 5, 10, 14});
    d.minor[9] = mk({0, 3, 6, 10});
    d.minor[10] = mk({0, 5, 10, 15, 19});  // dorian bVII, big modal colour
    d.minor[11] = mk({0, 5, 10, 15});
    return d;
}

// --- Just the Two of Us: not a transcription -- a movable dictionary built
// from the tune's own vocabulary, so it plays in any key. Bright maj9
// "head" landings on I and IV, the tune's signature borrowed-minor
// turnaround chords sitting on the less usual degrees, and #5 altered
// dominants standing in for its chromatic passing motion.
CustomDictionary buildJustTheTwoOfUs() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 7, 11, 14});   // Imaj9
    d.major[1] = mk({0, 4, 10, 14});      // passing dominant 9
    d.major[2] = mk({0, 3, 7, 10, 14});   // ii min9
    d.major[3] = mk({0, 3, 7, 10});       // turnaround borrowed min7
    d.major[4] = mk({0, 3, 7, 10});       // secondary ii, min7
    d.major[5] = mk({0, 4, 7, 11, 14});   // IVmaj9
    d.major[6] = mk({0, 4, 8, 10});       // chromatic passing dom7#5
    d.major[7] = mk({0, 4, 7, 10, 14});   // V9
    d.major[8] = mk({0, 3, 7, 10});       // turnaround borrowed min7
    d.major[9] = mk({0, 3, 7, 10});       // secondary ii, min7
    d.major[10] = mk({0, 4, 7, 10});      // backdoor dominant
    d.major[11] = mk({0, 4, 8, 10});      // chromatic dom7#5

    d.minor[0] = mk({0, 3, 7, 10, 14});   // i min9
    d.minor[1] = mk({0, 4, 7, 11, 14});   // bright maj9 landing
    d.minor[2] = mk({0, 3, 6, 10});
    d.minor[3] = mk({0, 4, 7, 11, 14});   // relative major, bright landing
    d.minor[4] = mk({0, 4, 8, 10});       // secondary dom7#5
    d.minor[5] = mk({0, 3, 7, 10, 14});   // iv min9
    d.minor[6] = mk({0, 4, 8, 10});
    d.minor[7] = mk({0, 4, 10, 13});      // cadential 7b9
    d.minor[8] = mk({0, 4, 7, 11, 14});   // borrowed bright landing
    d.minor[9] = mk({0, 3, 6, 10});
    d.minor[10] = mk({0, 3, 7, 10});      // turnaround borrowed min7
    d.minor[11] = mk({0, 4, 8, 10});
    return d;
}

// --- Robert Glasper (neo-soul): lydian major (a #11 on every major chord),
// dorian minor (a natural 6th/11th instead of the usual dark colour), and
// suspended or backdoor dominants standing in for plain V7s.
CustomDictionary buildGlasper() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 11, 14, 18});   // Imaj9#11
    d.major[1] = mk({0, 5, 10, 14});       // 7sus9, backdoor passing
    d.major[2] = mk({0, 3, 10, 14, 17});   // ii min11
    d.major[3] = mk({0, 3, 7, 10, 14});    // min9, blue-note passing
    d.major[4] = mk({0, 3, 7, 10, 14});    // iii min9
    d.major[5] = mk({0, 4, 11, 14, 18});   // IVmaj9#11
    d.major[6] = mk({0, 5, 10, 15});       // quartal cluster, outside passing
    d.major[7] = mk({0, 5, 10, 14, 21});   // V, 9sus4/13
    d.major[8] = mk({0, 4, 11, 18});       // bVImaj7#11, borrowed lydian
    d.major[9] = mk({0, 3, 7, 9, 14});     // vi min6/9
    d.major[10] = mk({0, 5, 10, 14, 21});  // bVII, 9sus13 backdoor
    d.major[11] = mk({0, 4, 10, 13});      // leading-tone alt dominant

    d.minor[0] = mk({0, 3, 10, 14, 17});   // i min11
    d.minor[1] = mk({0, 4, 11, 18});       // bIImaj7#11, Neapolitan lydian
    d.minor[2] = mk({0, 3, 6, 10, 14});    // iim9b5
    d.minor[3] = mk({0, 4, 11, 14, 18});   // bIIImaj9#11, relative major
    d.minor[4] = mk({0, 4, 10, 15});       // III7#9, secondary dominant
    d.minor[5] = mk({0, 3, 10, 14, 17});   // iv min11, dorian
    d.minor[6] = mk({0, 5, 10, 15});       // quartal cluster, passing
    d.minor[7] = mk({0, 4, 10, 13});       // v7b9, cadential
    d.minor[8] = mk({0, 4, 11, 14, 18});   // bVImaj9#11, borrowed lydian
    d.minor[9] = mk({0, 3, 6, 10, 14});    // dorian 6th, min9b5
    d.minor[10] = mk({0, 5, 10, 14, 21});  // bVII, 13sus dorian
    d.minor[11] = mk({0, 4, 10, 13});      // leading-tone alt dominant
    return d;
}

// --- Freddie Green: sparse root-3-7 shells, the four-to-the-bar big-band
// rhythm guitar sound -- three or four notes, never more.
CustomDictionary buildFreddieGreen() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 11});
    d.major[1] = mk({0, 4, 10});
    d.major[2] = mk({0, 3, 10});
    d.major[3] = mk({0, 3, 6});
    d.major[4] = mk({0, 3, 10});
    d.major[5] = mk({0, 4, 11});
    d.major[6] = mk({0, 3, 6});
    d.major[7] = mk({0, 4, 10});
    d.major[8] = mk({0, 4, 11});
    d.major[9] = mk({0, 3, 10});
    d.major[10] = mk({0, 4, 10});
    d.major[11] = mk({0, 4, 10});

    d.minor[0] = mk({0, 3, 10});
    d.minor[1] = mk({0, 4, 11});
    d.minor[2] = mk({0, 3, 6, 10});
    d.minor[3] = mk({0, 4, 11});
    d.minor[4] = mk({0, 4, 10});
    d.minor[5] = mk({0, 3, 10});
    d.minor[6] = mk({0, 3, 6});
    d.minor[7] = mk({0, 4, 10});
    d.minor[8] = mk({0, 4, 11});
    d.minor[9] = mk({0, 3, 6, 10});
    d.minor[10] = mk({0, 4, 10});
    d.minor[11] = mk({0, 4, 10});
    return d;
}

/** The single array every accessor below reads from, so the count returned
 *  by factoryPresetCount() can never drift out of sync with what is
 *  actually in it. */
struct PresetTable {
    const FactoryPreset* data;
    int count;
};

PresetTable presets() {
    static const FactoryPreset all[] = {
        {"Barry Harris (bebop)",
         "Major 6th chords in place of maj7, a passing diminished on every "
         "chromatic degree, and dominants resolving with a 13 rather than a "
         "plain 9 -- the \"6th diminished scale\" system in miniature.",
         buildBarryHarris()},
        {"Bill Evans (rootless)",
         "Rootless A/B-form voicings throughout -- 3-5-7-9 and its minor and "
         "altered counterparts, close and smooth, built for a bassist who is "
         "already covering the root.",
         buildBillEvans()},
        {"Modal (McCoy Tyner)",
         "Stacked fourths wherever a chord can carry them, the \"So What\" "
         "voicing on every minor-quality degree -- open, ambiguous, built "
         "for a static vamp rather than a cadence.",
         buildModal()},
        {"Just the Two of Us",
         "Not a transcription -- a movable dictionary in the tune's own "
         "vocabulary: bright maj9 landings, its signature borrowed-minor "
         "turnaround chords, and #5 altered dominants for the chromatic "
         "moves, so it plays the same way in any key.",
         buildJustTheTwoOfUs()},
        {"Robert Glasper (neo-soul)",
         "Lydian major (a #11 on every major chord), dorian minor (a "
         "natural 6th/11th instead of the usual dark colour), and "
         "suspended or backdoor dominants that never need to resolve.",
         buildGlasper()},
        {"Freddie Green (comping shells)",
         "Sparse root-3-7 shells, three or four notes at most -- the "
         "four-to-the-bar big-band rhythm-guitar sound, leaving the most "
         "room there is for whatever else is playing.",
         buildFreddieGreen()},
    };
    return {all, static_cast<int>(sizeof(all) / sizeof(all[0]))};
}

}  // namespace

int factoryPresetCount() { return presets().count; }

const FactoryPreset& factoryPreset(int index) {
    const auto table = presets();
    if (index < 0 || index >= table.count) {
        static const FactoryPreset empty{"", "", CustomDictionary{}};
        return empty;
    }
    return table.data[index];
}

}  // namespace jazz
