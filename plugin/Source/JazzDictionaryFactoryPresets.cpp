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

// --- Herbie Hancock: sus resolutions in place of pulling dominants (even
// secondary ones), maj9#11 lydian colour, and open fourths -- modal without
// McCoy Tyner's constant quartal stacking.
CustomDictionary buildHerbieHancock() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 11, 14, 18});    // Imaj9#11
    d.major[1] = mk({0, 4, 11, 18});        // bIImaj7#11, borrowed bright passing
    d.major[2] = mk({0, 3, 10, 14, 17});    // ii min11
    d.major[3] = mk({0, 5, 10, 14});        // 7sus9 passing
    d.major[4] = mk({0, 3, 7, 10, 14});     // iii min9
    d.major[5] = mk({0, 4, 11, 14, 18});    // IVmaj9#11
    d.major[6] = mk({0, 5, 10, 15});        // quartal cluster, passing
    d.major[7] = mk({0, 5, 10, 14, 21});    // V, 9sus13 -- sus rather than pulling
    d.major[8] = mk({0, 4, 11, 18});        // bVImaj7#11
    d.major[9] = mk({0, 3, 10, 14, 17});    // vi min11
    d.major[10] = mk({0, 5, 10, 14, 21});   // bVII, 9sus13
    d.major[11] = mk({0, 4, 10, 13});       // leading-tone alt dominant

    d.minor[0] = mk({0, 3, 10, 14, 17});    // i min11
    d.minor[1] = mk({0, 4, 11, 18});        // bIImaj7#11
    d.minor[2] = mk({0, 3, 6, 10, 14});     // iim9b5
    d.minor[3] = mk({0, 4, 11, 14, 18});    // bIIImaj9#11
    d.minor[4] = mk({0, 5, 10, 14});        // III, 9sus secondary -- sus, not altered
    d.minor[5] = mk({0, 3, 10, 14, 17});    // iv min11
    d.minor[6] = mk({0, 5, 10, 15});        // quartal cluster
    d.minor[7] = mk({0, 5, 10, 14, 21});    // v, 9sus13 -- sus cadence, Hancock's signature move
    d.minor[8] = mk({0, 4, 11, 14, 18});    // bVImaj9#11
    d.minor[9] = mk({0, 3, 6, 10, 14});     // dorian 6th, min9b5
    d.minor[10] = mk({0, 5, 10, 14, 21});   // bVII, 9sus13
    d.minor[11] = mk({0, 4, 10, 13});       // leading-tone alt dominant
    return d;
}

// --- Thelonious Monk: deliberately angular -- minor 6ths and diminished
// runs instead of min7/maj7, whole-tone dominants, and both altered
// tensions stacked at once rather than smoothed out.
CustomDictionary buildMonk() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 9, 18});         // 6/#11, angular major tonic
    d.major[1] = mk({0, 4, 8, 10});         // whole-tone dominant (3, #5, b7)
    d.major[2] = mk({0, 3, 7, 9});          // ii min6
    d.major[3] = mk({0, 3, 6, 9});          // passing dim7
    d.major[4] = mk({0, 4, 10, 13});        // 7b9 clashing passing dominant
    d.major[5] = mk({0, 4, 9, 18});         // IV 6/#11
    d.major[6] = mk({0, 4, 8, 10});         // whole-tone dominant
    d.major[7] = mk({0, 4, 10, 13, 15});    // V 7b9#9, both tensions stacked
    d.major[8] = mk({0, 3, 6, 9});          // passing dim7
    d.major[9] = mk({0, 3, 7, 9});          // vi min6
    d.major[10] = mk({0, 4, 10, 18});       // bVII 7#11, angular passing dominant
    d.major[11] = mk({0, 4, 10, 13, 15});   // 7b9#9

    d.minor[0] = mk({0, 3, 7, 9});          // i min6
    d.minor[1] = mk({0, 4, 10, 18});        // bII 7#11, half-step-below substitution
    d.minor[2] = mk({0, 3, 6, 9});          // passing dim7
    d.minor[3] = mk({0, 4, 9, 18});         // bIII 6/#11
    d.minor[4] = mk({0, 4, 10, 13, 15});    // III 7b9#9
    d.minor[5] = mk({0, 3, 7, 9});          // iv min6
    d.minor[6] = mk({0, 4, 8, 10});         // whole-tone dominant
    d.minor[7] = mk({0, 4, 10, 13, 15});    // v 7b9#9
    d.minor[8] = mk({0, 4, 9, 18});         // bVI 6/#11
    d.minor[9] = mk({0, 3, 6, 9});          // passing dim7
    d.minor[10] = mk({0, 4, 10, 18});       // bVII 7#11
    d.minor[11] = mk({0, 4, 10, 13, 15});   // 7b9#9
    return d;
}

// --- Wayne Shorter: melodic-minor colour throughout -- minor-major 7
// tonics, augmented (#5) major chords, and fully altered dominants rather
// than the plain or sus ones elsewhere in this list.
CustomDictionary buildWayneShorter() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 8, 11});         // Imaj7#5, floating augmented major
    d.major[1] = mk({0, 4, 10, 13});        // alt dominant passing
    d.major[2] = mk({0, 3, 6, 10});         // ii min7b5, angular even in major
    d.major[3] = mk({0, 4, 8, 11});         // maj7#5
    d.major[4] = mk({0, 3, 7, 11});         // iii min-maj7, melodic-minor colour
    d.major[5] = mk({0, 4, 11, 18});        // IVmaj7#11
    d.major[6] = mk({0, 4, 10, 13});        // alt dominant passing
    d.major[7] = mk({0, 4, 10, 13, 18});    // V 7alt, b9/#9/#11 stacked
    d.major[8] = mk({0, 4, 8, 11});         // maj7#5
    d.major[9] = mk({0, 3, 7, 11});         // vi min-maj7
    d.major[10] = mk({0, 4, 10, 13});       // alt dominant
    d.major[11] = mk({0, 4, 10, 13, 18});   // 7alt

    d.minor[0] = mk({0, 3, 7, 11});         // i min-maj7, the melodic-minor tonic
    d.minor[1] = mk({0, 4, 8, 11});         // bIImaj7#5
    d.minor[2] = mk({0, 3, 6, 10});         // ii min7b5
    d.minor[3] = mk({0, 4, 11, 18});        // bIIImaj7#11
    d.minor[4] = mk({0, 4, 10, 13, 18});    // III 7alt
    d.minor[5] = mk({0, 3, 7, 11});         // iv min-maj7
    d.minor[6] = mk({0, 4, 10, 13});        // alt dominant passing
    d.minor[7] = mk({0, 4, 10, 13, 18});    // v 7alt
    d.minor[8] = mk({0, 4, 8, 11});         // bVImaj7#5
    d.minor[9] = mk({0, 3, 6, 10});         // vi min7b5
    d.minor[10] = mk({0, 4, 10, 13});       // alt dominant
    d.minor[11] = mk({0, 4, 10, 13, 18});   // 7alt
    return d;
}

// --- Chick Corea: the Spanish/Phrygian-major move (a plain major triad a
// half step above the tonic) as the recurring signature, bright add9
// chords with no 7th, and stacked fourths for the "Spain" intro sound.
CustomDictionary buildChickCorea() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 7, 14});         // I add9, bright, no 7th
    d.major[1] = mk({0, 4, 7, 11});         // bII maj7, the Phrygian move
    d.major[2] = mk({0, 3, 7, 10, 14});     // ii min9
    d.major[3] = mk({0, 4, 7, 11});         // bIII maj7
    d.major[4] = mk({0, 3, 7, 10, 14});     // iii min9
    d.major[5] = mk({0, 4, 7, 14});         // IV add9
    d.major[6] = mk({0, 5, 10, 15});        // quartal, the Spain intro fourths
    d.major[7] = mk({0, 5, 10, 14});        // V 7sus add9, Latin vamp
    d.major[8] = mk({0, 4, 7, 11});         // bVI maj7
    d.major[9] = mk({0, 3, 7, 10, 14});     // vi min9
    d.major[10] = mk({0, 5, 10, 14});       // bVII 7sus add9
    d.major[11] = mk({0, 4, 10, 13});       // alt dominant

    d.minor[0] = mk({0, 3, 7, 10, 14});     // i min9, Spanish/Phrygian minor tonic
    d.minor[1] = mk({0, 4, 7, 11});         // bII maj7 -- the defining "Spain" sound
    d.minor[2] = mk({0, 3, 6, 10});         // ii min7b5
    d.minor[3] = mk({0, 4, 7, 14});         // bIII add9
    d.minor[4] = mk({0, 5, 10, 14});        // III 7sus add9, Latin secondary
    d.minor[5] = mk({0, 3, 7, 10, 14});     // iv min9
    d.minor[6] = mk({0, 5, 10, 15});        // quartal
    d.minor[7] = mk({0, 4, 10, 13});        // v 7b9, flamenco cadence
    d.minor[8] = mk({0, 4, 7, 14});         // bVI add9
    d.minor[9] = mk({0, 3, 6, 10});         // vi min7b5
    d.minor[10] = mk({0, 5, 10, 14});       // bVII 7sus add9
    d.minor[11] = mk({0, 4, 10, 13});       // alt dominant
    return d;
}

// --- Antonio Carlos Jobim (bossa nova): gentler and plainer than
// everything else in this list on purpose -- mostly unadorned maj7/min7,
// a 6/9 tonic, and half-step-down 7b9 dominants for the style's
// characteristic modulations.
CustomDictionary buildJobim() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 7, 9, 14});      // Imaj6/9, the gentle bossa tonic
    d.major[1] = mk({0, 4, 10, 13});        // bII 7b9, half-step-down modulation
    d.major[2] = mk({0, 3, 7, 10});         // ii min7, kept plain
    d.major[3] = mk({0, 4, 7, 11});         // bIII maj7
    d.major[4] = mk({0, 3, 7, 10});         // iii min7
    d.major[5] = mk({0, 4, 7, 11});         // IV maj7
    d.major[6] = mk({0, 3, 6, 10});         // #IV min7b5, the Corcovado move
    d.major[7] = mk({0, 4, 10, 13});        // V 7b9, gentle altered cadence
    d.major[8] = mk({0, 4, 7, 11});         // bVI maj7
    d.major[9] = mk({0, 3, 7, 10});         // vi min7
    d.major[10] = mk({0, 4, 10, 13});       // bVII 7b9
    d.major[11] = mk({0, 4, 10, 13});       // 7b9

    d.minor[0] = mk({0, 3, 7, 9});          // i min6, the classic bossa minor tonic
    d.minor[1] = mk({0, 4, 7, 11});         // bII maj7
    d.minor[2] = mk({0, 3, 6, 10});         // ii min7b5
    d.minor[3] = mk({0, 4, 7, 11});         // bIII maj7
    d.minor[4] = mk({0, 4, 10, 13});        // III 7b9
    d.minor[5] = mk({0, 3, 7, 10});         // iv min7
    d.minor[6] = mk({0, 3, 6, 10});         // min7b5
    d.minor[7] = mk({0, 4, 10, 13});        // v 7b9
    d.minor[8] = mk({0, 4, 7, 11});         // bVI maj7
    d.minor[9] = mk({0, 3, 6, 10});         // vi min7b5
    d.minor[10] = mk({0, 4, 10, 13});       // bVII 7b9
    d.minor[11] = mk({0, 4, 10, 13});       // 7b9
    return d;
}

// --- Stevie Wonder: soul-jazz crossover -- a 6/9 tonic and 9sus/13
// dominants for brightness, with a 7#9 kept in reserve for the harder
// funk tunes.
CustomDictionary buildStevieWonder() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 7, 9, 14});      // Imaj6/9
    d.major[1] = mk({0, 5, 10, 14});        // bII 9sus, soul passing dominant
    d.major[2] = mk({0, 3, 7, 10, 14});     // ii min9
    d.major[3] = mk({0, 4, 7, 9, 14});      // bIII maj6/9, borrowed bright passing
    d.major[4] = mk({0, 3, 7, 10});         // iii min7
    d.major[5] = mk({0, 4, 7, 9, 14});      // IVmaj6/9
    d.major[6] = mk({0, 5, 10, 14});        // #IV 9sus
    d.major[7] = mk({0, 5, 10, 14, 21});    // V 9sus13, the horn-stab dominant
    d.major[8] = mk({0, 4, 7, 9, 14});      // bVImaj6/9
    d.major[9] = mk({0, 3, 7, 10, 14});     // vi min9
    d.major[10] = mk({0, 5, 10, 14, 21});   // bVII 9sus13
    d.major[11] = mk({0, 4, 10, 15});       // 7#9, funky colour

    d.minor[0] = mk({0, 3, 7, 10, 14});     // i min9
    d.minor[1] = mk({0, 4, 7, 9, 14});      // bIImaj6/9
    d.minor[2] = mk({0, 3, 6, 10, 14});     // iim9b5
    d.minor[3] = mk({0, 4, 7, 9, 14});      // bIIImaj6/9
    d.minor[4] = mk({0, 4, 10, 15});        // III 7#9
    d.minor[5] = mk({0, 3, 7, 10, 14});     // iv min9
    d.minor[6] = mk({0, 5, 10, 14});        // #iv 9sus
    d.minor[7] = mk({0, 5, 10, 14, 21});    // v 9sus13
    d.minor[8] = mk({0, 4, 7, 9, 14});      // bVImaj6/9
    d.minor[9] = mk({0, 3, 6, 10, 14});     // dorian 6th, min9b5
    d.minor[10] = mk({0, 5, 10, 14, 21});   // bVII 9sus13
    d.minor[11] = mk({0, 4, 10, 15});       // 7#9
    return d;
}

// --- Steely Dan: the "Mu major" (a plain triad with a 2nd/9th sitting
// right next to the 3rd) as the recurring tonic colour, plus sophisticated
// #11 dominants -- jazz harmony filtered through studio pop.
CustomDictionary buildSteelyDan() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 2, 4, 7});          // I, the Mu major chord (R 2 3 5)
    d.major[1] = mk({0, 4, 10, 18});        // bII 7#11, jazzy passing dominant
    d.major[2] = mk({0, 3, 7, 10, 14});     // ii min9
    d.major[3] = mk({0, 2, 4, 7});          // bIII Mu major, borrowed bright passing
    d.major[4] = mk({0, 3, 7, 10});         // iii min7
    d.major[5] = mk({0, 4, 11, 18});        // IVmaj7#11
    d.major[6] = mk({0, 4, 10, 18});        // #IV 7#11
    d.major[7] = mk({0, 4, 10, 14, 18});    // V 9#11, the sophisticated dominant
    d.major[8] = mk({0, 2, 4, 7});          // bVI Mu major
    d.major[9] = mk({0, 3, 7, 10, 14});     // vi min9
    d.major[10] = mk({0, 4, 10, 14, 18});   // bVII 13#11
    d.major[11] = mk({0, 4, 10, 13, 18});   // 7alt

    d.minor[0] = mk({0, 3, 7, 10, 14});     // i min9
    d.minor[1] = mk({0, 2, 4, 7});          // bII Mu major, ironic bright borrow
    d.minor[2] = mk({0, 3, 6, 10});         // ii min7b5
    d.minor[3] = mk({0, 2, 4, 7});          // bIII Mu major
    d.minor[4] = mk({0, 4, 10, 13, 18});    // III 7alt
    d.minor[5] = mk({0, 3, 7, 10, 14});     // iv min9
    d.minor[6] = mk({0, 4, 10, 18});        // #iv 7#11
    d.minor[7] = mk({0, 4, 10, 13, 18});    // v 7alt
    d.minor[8] = mk({0, 2, 4, 7});          // bVI Mu major
    d.minor[9] = mk({0, 3, 6, 10});         // vi min7b5
    d.minor[10] = mk({0, 4, 10, 14, 18});   // bVII 13#11
    d.minor[11] = mk({0, 4, 10, 13, 18});   // 7alt
    return d;
}

// --- Duke Ellington: pre-bop and lush -- added 6ths and diminished
// passing tones instead of bebop's 13ths and altered dominants, warmer
// and more triadic throughout.
CustomDictionary buildDukeEllington() {
    CustomDictionary d;
    d.useMajor = true;
    d.useMinor = true;
    d.major[0] = mk({0, 4, 7, 9});          // I6
    d.major[1] = mk({0, 3, 6, 9});          // passing dim7
    d.major[2] = mk({0, 3, 7, 10});         // ii min7
    d.major[3] = mk({0, 3, 6, 9});          // passing dim7
    d.major[4] = mk({0, 3, 7, 10});         // iii min7
    d.major[5] = mk({0, 4, 7, 9});          // IV6
    d.major[6] = mk({0, 3, 6, 9});          // passing dim7
    d.major[7] = mk({0, 4, 7, 10, 14});     // V, lush dom9, not altered
    d.major[8] = mk({0, 4, 7, 9});          // bVI6
    d.major[9] = mk({0, 3, 7, 9});          // vi min6, warm relative-minor colour
    d.major[10] = mk({0, 4, 7, 10});        // bVII7
    d.major[11] = mk({0, 3, 6, 9});         // leading-tone dim7

    d.minor[0] = mk({0, 3, 7, 9});          // i min6
    d.minor[1] = mk({0, 4, 7, 9});          // bII6, borrowed warm passing
    d.minor[2] = mk({0, 3, 6, 10});         // ii min7b5
    d.minor[3] = mk({0, 4, 7, 9});          // bIII6
    d.minor[4] = mk({0, 4, 7, 10});         // III7
    d.minor[5] = mk({0, 3, 7, 9});          // iv min6
    d.minor[6] = mk({0, 3, 6, 9});          // passing dim7
    d.minor[7] = mk({0, 4, 7, 10, 14});     // v, lush dom9, kept restrained
    d.minor[8] = mk({0, 4, 7, 9});          // bVI6
    d.minor[9] = mk({0, 3, 6, 10});         // vi min7b5
    d.minor[10] = mk({0, 4, 7, 10});        // bVII7
    d.minor[11] = mk({0, 3, 6, 9});         // leading-tone dim7
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
        {"Herbie Hancock",
         "Sus resolutions in place of pulling dominants -- even secondary "
         "ones -- alongside maj9#11 and open fourths: modal without McCoy "
         "Tyner's constant quartal stacking.",
         buildHerbieHancock()},
        {"Thelonious Monk",
         "Deliberately angular: minor 6ths and diminished runs instead of "
         "min7/maj7, whole-tone dominants, and both altered tensions "
         "stacked at once rather than smoothed out.",
         buildMonk()},
        {"Wayne Shorter",
         "Melodic-minor colour throughout -- minor-major 7 tonics, "
         "augmented major chords, and fully altered dominants rather than "
         "the plain or sus ones elsewhere in this list.",
         buildWayneShorter()},
        {"Chick Corea",
         "The Spanish/Phrygian-major move -- a plain major triad a half "
         "step above the tonic -- as the recurring signature, bright add9 "
         "chords with no 7th, and stacked fourths for the \"Spain\" sound.",
         buildChickCorea()},
        {"Antonio Carlos Jobim (bossa nova)",
         "Gentler and plainer than everything else here on purpose -- "
         "mostly unadorned maj7/min7, a 6/9 tonic, and half-step-down 7b9 "
         "dominants for the style's characteristic modulations.",
         buildJobim()},
        {"Stevie Wonder",
         "Soul-jazz crossover: a 6/9 tonic and 9sus/13 dominants for "
         "brightness, with a 7#9 kept in reserve for the harder funk "
         "tunes.",
         buildStevieWonder()},
        {"Steely Dan",
         "The \"Mu major\" -- a plain triad with a 2nd sitting right next "
         "to the 3rd -- as the recurring tonic colour, plus sophisticated "
         "#11 dominants: jazz harmony filtered through studio pop.",
         buildSteelyDan()},
        {"Duke Ellington",
         "Pre-bop and lush: added 6ths and diminished passing tones "
         "instead of bebop's 13ths and altered dominants, warmer and more "
         "triadic throughout.",
         buildDukeEllington()},
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
