// Checks on the jazz chord dictionary and the voicer.
//
// This is deliberately free of JUCE and of the engine: the voicer is integer
// music theory, so it can be built with a single compiler invocation and run
// anywhere. What it asserts is the musical contract -- the played note is always
// a tone of the chord chosen for it, nothing sounds outside the range, the
// octave and inversion switches move the chord the way the panel says they do,
// and asking for smoother voice leading actually reduces how far the voices
// travel.

#include "JazzChordLibrary.h"
#include "JazzDictionaryFactoryPresets.h"
#include "JazzMidiImport.h"
#include "JazzVoicer.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

void checkf(bool ok, const char* fmt, ...) {
    char buf[420];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    check(ok, buf);
}

int pitchClass(int n) { return ((n % 12) + 12) % 12; }

jazz::Settings defaults() {
    jazz::Settings s;
    s.rangeLow = 50;     // D3
    s.rangeHigh = 79;    // G5
    s.smoothness = 0.5f;
    return s;
}

/** Pitch classes the chord contains, given the extensions switched on. */
std::vector<int> chordPitchClasses(const jazz::Voicing& v, const jazz::Settings& s) {
    // Rebuilt here from the published interval spelling rather than reaching
    // into the voicer, so a change to the tables has to agree with this too.
    struct Spec { int third, fifth, seventh, ninth, eleventh, thirteenth; };
    static const Spec specs[] = {
        { 4, 7, 11, 14, 18, 21},   // maj7
        { 4, 7, 10, 14, 18, 21},   // dom7
        { 4, 7, 10, 13, 18, 20},   // dom7b9
        { 3, 7, 10, 14, 17, 21},   // m7
        { 3, 6, 10, 14, 17, 20},   // m7b5
        { 3, 6,  9, 14, 17, 20},   // dim7
    };
    const Spec& spec = specs[static_cast<int>(v.type)];
    std::vector<int> out{pitchClass(v.chordRootPc),
                         pitchClass(v.chordRootPc + spec.third),
                         pitchClass(v.chordRootPc + spec.fifth),
                         pitchClass(v.chordRootPc + spec.seventh)};
    if (s.ninth) out.push_back(pitchClass(v.chordRootPc + spec.ninth));
    if (s.eleventh) out.push_back(pitchClass(v.chordRootPc + spec.eleventh));
    if (s.thirteenth) out.push_back(pitchClass(v.chordRootPc + spec.thirteenth));
    return out;
}

bool contains(const std::vector<int>& v, int value) {
    for (int x : v) if (x == value) return true;
    return false;
}

/** Total semitone travel of a melodic line through the voicer. */
float lineMotion(const jazz::Settings& s, const std::vector<int>& melody,
                 const std::vector<int>& keys) {
    jazz::Voicer voicer;
    voicer.reset();
    jazz::Voicing v;
    std::vector<int> previous;
    float motion = 0.0f;

    for (int note : melody) {
        if (!voicer.update(keys.data(), static_cast<int>(keys.size()), note, s, v)) continue;
        std::vector<int> current(v.notes, v.notes + v.count);
        if (!previous.empty()) {
            for (int c : current) {
                int best = 128;
                for (int p : previous) best = std::min(best, std::abs(c - p));
                motion += static_cast<float>(best);
            }
        }
        previous = current;
    }
    return motion;
}

float median(const jazz::Voicing& v) {
    if (v.count == 0) return 0.0f;
    return static_cast<float>(v.notes[v.count / 2]);
}

}  // namespace

int main() {
    std::printf("=============================================\n");
    std::printf(" Jazz chord mode checks\n");
    std::printf("=============================================\n");

    // --- The key centre comes from the keys held, as specced.
    std::printf("\n-- One key is a major key, two keys are a minor one --\n");
    {
        jazz::Voicer voicer;
        jazz::Voicing v;
        auto s = defaults();

        const int oneKey[] = {60};                 // C
        check(voicer.update(oneKey, 1, 64, s, v) && !v.minorKey && v.keyCentrePc == 0,
              "a single C names C major");

        voicer.reset();
        const int twoKeys[] = {69, 76};            // A and the E above it
        check(voicer.update(twoKeys, 2, 72, s, v) && v.minorKey && v.keyCentrePc == 9,
              "A plus E names A minor, on the lower key");

        voicer.reset();
        const int reversed[] = {76, 69};           // same two keys, other order
        check(voicer.update(reversed, 2, 72, s, v) && v.minorKey && v.keyCentrePc == 9,
              "the order the keys arrive in does not matter");

        voicer.reset();
        const int threeKeys[] = {62, 69, 74};      // D, A, D
        check(voicer.update(threeKeys, 3, 77, s, v) && v.minorKey && v.keyCentrePc == 2,
              "three keys still name a minor key on the lowest");

        check(!voicer.update(oneKey, 0, 64, s, v), "no keys held produces nothing");
        check(!voicer.update(oneKey, 1, -1, s, v), "no played note produces nothing");
    }

    // --- The whole point of the dictionary: whatever is played is in the chord.
    std::printf("\n-- Every played note is a tone of the chord chosen for it --\n");
    {
        auto s = defaults();
        int misses = 0;
        int minorMisses = 0;

        for (int keyPc = 0; keyPc < 12; ++keyPc) {
            for (int degree = 0; degree < 12; ++degree) {
                for (int minor = 0; minor < 2; ++minor) {
                    jazz::Voicer voicer;
                    jazz::Voicing v;
                    const int keys[2] = {48 + keyPc, 48 + keyPc + 7};
                    const int melody = 60 + keyPc + degree;
                    if (!voicer.update(keys, minor ? 2 : 1, melody, s, v)) { ++misses; continue; }
                    const auto pcs = chordPitchClasses(v, s);
                    if (!contains(pcs, pitchClass(melody))) {
                        (minor ? minorMisses : misses)++;
                        std::printf("      %s %s, degree %d -> %s%s\n",
                                    jazz::pitchClassName(keyPc), minor ? "minor" : "major",
                                    degree, jazz::pitchClassName(v.chordRootPc), v.roman);
                    }
                }
            }
        }
        checkf(misses == 0 && minorMisses == 0,
               "all 12 degrees of all 12 keys, major and minor (%d misses)",
               misses + minorMisses);
    }

    // --- Extensions.
    std::printf("\n-- Sevenths by default, 9ths, 11ths and 13ths on demand --\n");
    {
        auto s = defaults();
        jazz::Voicer voicer;
        jazz::Voicing v;
        const int keys[] = {60};                  // C major
        const int melody = 62;                    // D -> iim7 = Dm7

        voicer.reset();
        check(voicer.update(keys, 1, melody, s, v), "a plain seventh chord voices");
        bool sevenths = true;
        for (int i = 0; i < v.count; ++i) sevenths &= v.degrees[i] <= 7;
        char symbol[32];
        jazz::chordSymbol(v, s, symbol, sizeof(symbol));
        checkf(sevenths && std::string(symbol) == "Dm7",
               "D over C major is %s, sevenths only", symbol);

        const struct { bool ninth, eleventh, thirteenth; const char* want; } cases[] = {
            {true,  false, false, "Dm9"},
            {true,  true,  false, "Dm11"},
            {true,  true,  true,  "Dm13"},
            {false, true,  false, "Dm11"},
        };
        for (const auto& c : cases) {
            s.ninth = c.ninth;
            s.eleventh = c.eleventh;
            s.thirteenth = c.thirteenth;
            voicer.reset();
            voicer.update(keys, 1, melody, s, v);
            jazz::chordSymbol(v, s, symbol, sizeof(symbol));

            const auto pcs = chordPitchClasses(v, s);
            bool present = true;
            if (c.ninth) present &= contains(pcs, pitchClass(v.chordRootPc + 14));
            if (c.eleventh) present &= contains(pcs, pitchClass(v.chordRootPc + 17));
            if (c.thirteenth) present &= contains(pcs, pitchClass(v.chordRootPc + 21));
            checkf(present && std::string(symbol) == c.want,
                   "9:%d 11:%d 13:%d -> %s", c.ninth, c.eleventh, c.thirteenth, symbol);
        }

        // A dominant takes a #11, never the natural 11 a semitone off its third.
        s = defaults();
        s.eleventh = true;
        voicer.reset();
        voicer.update(keys, 1, 67, s, v);                 // G over C major -> V7
        jazz::chordSymbol(v, s, symbol, sizeof(symbol));
        const auto pcs = chordPitchClasses(v, s);
        checkf(contains(pcs, pitchClass(v.chordRootPc + 18)) &&
                   !contains(pcs, pitchClass(v.chordRootPc + 17)),
               "the dominant's 11th is sharp: %s", symbol);
    }

    // --- Nothing may sound outside the range, unless honouring it would put a
    // voice further away than the engine can shift -- which is reported.
    std::printf("\n-- The range is a hard wall --\n");
    {
        const struct { int low, high; } windows[] = {
            {36, 84}, {50, 79}, {55, 67}, {60, 72}, {48, 60},
        };
        bool allInside = true;
        bool noDuplicates = true;
        for (const auto& w : windows) {
            auto s = defaults();
            s.rangeLow = w.low;
            s.rangeHigh = w.high;
            s.ninth = s.eleventh = s.thirteenth = true;
            for (int octave = -2; octave <= 2; ++octave) {
                for (int inversion = -3; inversion <= 3; ++inversion) {
                    s.octaveShift = octave;
                    s.inversionShift = inversion;
                    jazz::Voicer voicer;
                    jazz::Voicing v;
                    const int keys[] = {57};
                    for (int melody = 55; melody <= 84; ++melody) {
                        if (!voicer.update(keys, 1, melody, s, v)) continue;
                        for (int i = 0; i < v.count; ++i) {
                            // Always inside the window the voicing says it used,
                            // and inside the asked-for range whenever that window
                            // was not pulled in to stay within the engine's reach.
                            allInside &= v.notes[i] >= v.windowLow && v.notes[i] <= v.windowHigh;
                            if (!v.rangeLimited) {
                                allInside &= v.notes[i] >= w.low && v.notes[i] <= w.high;
                            }
                            if (i > 0) noDuplicates &= v.notes[i] != v.notes[i - 1];
                        }
                    }
                }
            }
        }
        check(allInside, "every note lands inside the range, or inside the reduced window "
                         "the voicing reports when the range was out of the engine's reach");
        check(noDuplicates, "no voicing sounds the same note twice");
    }

    // --- The engine can only shift a voice so far. A note voiced past that
    // would sound at the limit instead, so the voicer must not place one there.
    std::printf("\n-- Nothing is voiced further than the engine can shift it --\n");
    {
        int beyond = 0;
        int worst = 0;
        int limitedCases = 0;
        const struct { int low, high; } windows[] = {
            {50, 79},   // the default
            {36, 96},   // wide
            {50, 62},   // low and narrow, under a high note
            {72, 84},   // high and narrow, over a low note
        };
        for (const auto& w : windows) {
            for (int octave = -2; octave <= 2; ++octave) {
                for (int melody = 40; melody <= 96; ++melody) {
                    auto s = defaults();
                    s.rangeLow = w.low;
                    s.rangeHigh = w.high;
                    s.octaveShift = octave;
                    s.ninth = s.thirteenth = true;
                    for (int keyPc = 0; keyPc < 12; ++keyPc) {
                        const int keys[2] = {48 + keyPc, 48 + keyPc + 7};
                        for (int minor = 0; minor < 2; ++minor) {
                            jazz::Voicer voicer;
                            jazz::Voicing v;
                            if (!voicer.update(keys, minor ? 2 : 1, melody, s, v)) continue;
                            if (v.rangeLimited) ++limitedCases;
                            for (int i = 0; i < v.count; ++i) {
                                const int d = std::abs(v.notes[i] - melody);
                                if (d > jazz::kEngineReachSemitones) {
                                    ++beyond;
                                    worst = std::max(worst, d);
                                }
                            }
                        }
                    }
                }
            }
        }
        checkf(beyond == 0, "every voice stays within two octaves of the played note "
                            "(%d beyond, worst %d semitones)", beyond, worst);
        checkf(limitedCases > 0,
               "and the voicing reports when the range had to give way for it (%d cases)",
               limitedCases);

        // Where range and reach do overlap, the range still wins outright.
        auto s = defaults();
        s.rangeLow = 55;
        s.rangeHigh = 74;
        bool inside = true;
        bool everLimited = false;
        for (int melody = 60; melody <= 72; ++melody) {
            const int keys[] = {60};
            jazz::Voicer voicer;
            jazz::Voicing v;
            if (!voicer.update(keys, 1, melody, s, v)) continue;
            everLimited |= v.rangeLimited;
            for (int i = 0; i < v.count; ++i) {
                inside &= v.notes[i] >= 55 && v.notes[i] <= 74;
            }
        }
        check(inside && !everLimited,
              "a range within reach of the played note is used untouched");
    }

    // --- Octave and inversion switches.
    std::printf("\n-- Octave and inversion move the chord --\n");
    {
        auto s = defaults();
        s.rangeLow = 36;
        s.rangeHigh = 96;                     // wide, so the shifts have room
        s.ninth = true;
        const int keys[] = {60};
        const int melody = 62;

        float centre[5] = {};
        for (int octave = -2; octave <= 2; ++octave) {
            jazz::Voicer voicer;
            jazz::Voicing v;
            s.octaveShift = octave;
            voicer.update(keys, 1, melody, s, v);
            float sum = 0.0f;
            for (int i = 0; i < v.count; ++i) sum += static_cast<float>(v.notes[i]);
            centre[octave + 2] = sum / static_cast<float>(v.count);
        }
        // How far the switch can carry the chord is bounded by how far the
        // engine can shift a voice from the played note, so two octaves of travel
        // either way is not reachable at the extremes -- an octave-scale move in
        // the right direction is what this is asserting.
        checkf(centre[0] < centre[2] - 12.0f && centre[4] > centre[2] + 12.0f,
               "two octaves down and up move the chord's centre: %.1f / %.1f / %.1f",
               centre[0], centre[2], centre[4]);

        s.octaveShift = 0;
        float top[7] = {};
        float med[7] = {};
        for (int inversion = -3; inversion <= 3; ++inversion) {
            jazz::Voicer voicer;
            jazz::Voicing v;
            s.inversionShift = inversion;
            voicer.update(keys, 1, melody, s, v);
            top[inversion + 3] = static_cast<float>(v.notes[v.count - 1]);
            med[inversion + 3] = median(v);
        }
        checkf(top[0] < top[6] && med[0] < med[6],
               "inverting down sits the chord lower around the played note "
               "(top %.0f -> %.0f, middle %.0f -> %.0f)",
               top[0], top[6], med[0], med[6]);
    }

    // --- Voice leading.
    std::printf("\n-- Smoothness and range both tighten the voice leading --\n");
    {
        // A line up the scale, so each step picks a different chord.
        const std::vector<int> line = {60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62};
        const std::vector<int> keys = {48};

        auto loose = defaults();
        loose.rangeLow = 40;
        loose.rangeHigh = 88;
        loose.smoothness = 0.0f;
        loose.ninth = true;

        auto smooth = loose;
        smooth.smoothness = 1.0f;

        const float looseMotion = lineMotion(loose, line, keys);
        const float smoothMotion = lineMotion(smooth, line, keys);
        checkf(smoothMotion < looseMotion,
               "smoothness 1 moves the voices less than smoothness 0 (%.0f vs %.0f semitones)",
               smoothMotion, looseMotion);

        auto wide = defaults();
        wide.rangeLow = 36;
        wide.rangeHigh = 96;
        wide.smoothness = 0.35f;
        wide.ninth = true;

        auto tight = wide;
        tight.rangeLow = 57;
        tight.rangeHigh = 74;

        const float wideMotion = lineMotion(wide, line, keys);
        const float tightMotion = lineMotion(tight, line, keys);
        checkf(tightMotion < wideMotion,
               "a tighter range leads more smoothly on its own (%.0f vs %.0f semitones)",
               tightMotion, wideMotion);
    }

    // --- Styles.
    std::printf("\n-- Voicing styles --\n");
    {
        auto s = defaults();
        s.ninth = true;
        const int keys[] = {60};
        const int melody = 62;

        // Asking for one style gets that style.
        for (int i = 0; i < jazz::kStyleCount; ++i) {
            auto only = s;
            for (int j = 0; j < jazz::kStyleCount; ++j) only.styles[j] = (i == j);
            jazz::Voicer voicer;
            jazz::Voicing v;
            const bool ok = voicer.update(keys, 1, melody, only, v);
            checkf(ok && static_cast<int>(v.style) == i, "%s is used when it is the one selected",
                   jazz::styleName(static_cast<jazz::Style>(i)));
        }

        // Rootless leaves the root out; shell keeps the chord's identity in
        // three or four voices.
        {
            auto only = s;
            only.styles[static_cast<int>(jazz::Style::Rootless)] = true;
            only.doubleMelody = true;
            jazz::Voicer voicer;
            jazz::Voicing v;
            voicer.update(keys, 1, melody, only, v);
            bool hasRoot = false;
            for (int i = 0; i < v.count; ++i) hasRoot |= v.degrees[i] == 1;
            check(!hasRoot, "a rootless voicing does not sound the root");
        }
        {
            auto only = s;
            only.styles[static_cast<int>(jazz::Style::Shell)] = true;
            only.doubleMelody = true;
            jazz::Voicer voicer;
            jazz::Voicing v;
            voicer.update(keys, 1, melody, only, v);
            bool third = false, seventh = false;
            for (int i = 0; i < v.count; ++i) {
                third |= v.degrees[i] == 3;
                seventh |= v.degrees[i] == 7;
            }
            checkf(third && seventh && v.count <= 4, "a shell voicing is %d notes with the guide tones",
                   v.count);
        }

        // Nothing selected: the voicer picks, and picks consistently.
        {
            jazz::Voicer a, b;
            jazz::Voicing va, vb;
            check(a.update(keys, 1, melody, s, va), "with no style selected a voicing is still chosen");
            b.update(keys, 1, melody, s, vb);
            bool same = va.count == vb.count && va.style == vb.style;
            for (int i = 0; i < va.count && same; ++i) same &= va.notes[i] == vb.notes[i];
            check(same, "the same input gives the same voicing when shuffle is off");
        }

        // Shuffling between several selected styles does vary the answer.
        {
            auto several = s;
            several.styles[static_cast<int>(jazz::Style::Close)] = true;
            several.styles[static_cast<int>(jazz::Style::Drop2)] = true;
            several.styles[static_cast<int>(jazz::Style::Rootless)] = true;
            several.styles[static_cast<int>(jazz::Style::Spread)] = true;
            several.shuffle = true;

            jazz::Voicer voicer;
            voicer.setSeed(12345u);
            bool seen[jazz::kStyleCount] = {};
            int distinct = 0;
            for (int i = 0; i < 40; ++i) {
                jazz::Voicing v;
                // Alternate the played note so each call is a fresh decision.
                if (!voicer.update(keys, 1, (i % 2) ? 62 : 64, several, v)) continue;
                const int index = static_cast<int>(v.style);
                if (!seen[index]) { seen[index] = true; ++distinct; }
            }
            checkf(distinct > 1, "shuffle visits more than one of the selected styles (%d of 4)",
                   distinct);

            // ... and never a style that was not selected.
            bool onlySelected = true;
            for (int i = 0; i < jazz::kStyleCount; ++i) {
                if (seen[i] && !several.styles[i]) onlySelected = false;
            }
            check(onlySelected, "shuffle never reaches for a style that was not selected");
        }
    }

    // --- The player's own note.
    std::printf("\n-- The played note is left to the player --\n");
    {
        auto s = defaults();
        s.ninth = true;
        const int keys[] = {60};

        bool everDoubled = false;
        for (int melody = 55; melody <= 79; ++melody) {
            jazz::Voicer voicer;
            jazz::Voicing v;
            if (!voicer.update(keys, 1, melody, s, v)) continue;
            for (int i = 0; i < v.count; ++i) everDoubled |= v.notes[i] == melody;
        }
        check(!everDoubled, "no voice is generated in unison with the played note");

        s.doubleMelody = true;
        bool doubledSomewhere = false;
        for (int melody = 55; melody <= 79; ++melody) {
            jazz::Voicer voicer;
            jazz::Voicing v;
            if (!voicer.update(keys, 1, melody, s, v)) continue;
            for (int i = 0; i < v.count; ++i) doubledSomewhere |= v.notes[i] == melody;
        }
        check(doubledSomewhere, "doubling the played note puts it back when asked for");
    }

    // --- Named the way a lead sheet would name it.
    std::printf("\n-- Chord symbols --\n");
    {
        auto s = defaults();
        jazz::Voicer voicer;
        jazz::Voicing v;
        char symbol[32];

        const struct { int keys[2]; int keyCount; int melody; const char* want; } cases[] = {
            {{60, 0}, 1, 60, "Cmaj7"},      // tonic
            {{60, 0}, 1, 64, "Cmaj7"},      // the third of the tonic
            {{60, 0}, 1, 67, "G7"},         // the fifth is the dominant
            {{60, 0}, 1, 71, "G7"},         // leading tone: third of V7
            {{60, 0}, 1, 70, "Bb7"},        // backdoor
            {{57, 64}, 2, 57, "Am7"},       // minor tonic
            {{57, 64}, 2, 64, "E7"},        // the fifth of a minor key: its dominant
            {{57, 64}, 2, 61, "A7"},        // major third in minor: V of iv
        };
        for (const auto& c : cases) {
            voicer.reset();
            voicer.update(c.keys, c.keyCount, c.melody, s, v);
            jazz::chordSymbol(v, s, symbol, sizeof(symbol));
            checkf(std::string(symbol) == c.want, "note %d -> %s (%s)", c.melody, symbol, v.roman);
        }
    }

    // --- A custom dictionary is a per-context override, not a separate mode:
    // off (the default) has to leave the built-in dictionary untouched, and
    // leaving one context off has to leave that context's built-in chords
    // untouched too. A custom entry is an explicit voicing -- semitone
    // offsets above the root -- rather than a chord type, so it can carry
    // exactly what a keyboard editor, a recording, or a MIDI import gave it.
    std::printf("\n-- Custom chord dictionary --\n");
    {
        auto s = defaults();
        const int keys[] = {60};       // C major
        const int melody = 62;         // D -> iim7 built in

        jazz::Voicer plain;
        jazz::Voicing before;
        check(plain.update(keys, 1, melody, s, before), "built-in dictionary voices D over C");
        checkf(before.chordRootPc == 2 && std::string(before.roman) == "iim7" && !before.customVoicing,
               "before any custom dictionary exists, D over C is %s%s",
               jazz::pitchClassName(before.chordRootPc), before.roman);

        // A dictionary that has been filled in but not switched on changes
        // nothing at all -- that is how you get back to how it always was.
        for (auto& e : s.customDict.major) { e.offsets[0] = 4; e.offsets[1] = 7; e.offsets[2] = 10; e.count = 3; }
        s.customDict.useMajor = true;
        jazz::Voicer stillOff;
        jazz::Voicing offResult;
        stillOff.update(keys, 1, melody, s, offResult);
        checkf(offResult.chordRootPc == before.chordRootPc &&
                   std::string(offResult.roman) == before.roman && !offResult.customVoicing,
               "a filled-in dictionary that is not switched on changes nothing (%s%s)",
               jazz::pitchClassName(offResult.chordRootPc), offResult.roman);

        // Switched on, it overrides the degree it was built for -- always
        // rooted on the note played, D, rather than the built-in's root --
        // and plays exactly the tones the entry was given (a dominant-quality
        // voicing here: major third, fifth, minor seventh).
        s.useCustomDictionary = true;
        jazz::Voicer custom;
        jazz::Voicing after;
        check(custom.update(keys, 1, melody, s, after), "the custom entry voices too");
        checkf(after.chordRootPc == 2 && after.customVoicing,
               "custom major degree 2 -> root %s, custom voicing used",
               jazz::pitchClassName(after.chordRootPc));
        checkf(std::string(after.roman) == "II",
               "a custom entry's roman numeral is the bare scale degree: %s", after.roman);

        bool haveThird = false, haveFifth = false, haveSeventh = false;
        for (int i = 0; i < after.count; ++i) {
            switch (pitchClass(after.notes[i] - after.chordRootPc)) {
                case 4:  haveThird = true; break;
                case 7:  haveFifth = true; break;
                case 10: haveSeventh = true; break;
                default: break;
            }
        }
        check(haveThird && haveFifth && haveSeventh,
              "the voicing actually contains the tones the custom entry was given");

        // The played note is still a tone of whatever chord the custom
        // dictionary names -- the one contract that never gets to break. It
        // is always the chord's root (offset 0), whether or not that root
        // itself ends up sounding.
        checkf(pitchClass(after.chordRootPc) == pitchClass(melody),
               "the played note is still the root of the custom chord (root %s, played %s)",
               jazz::pitchClassName(after.chordRootPc), jazz::pitchClassName(pitchClass(melody)));

        // That holds for every degree and every voicing a custom entry could
        // be given, in both major and minor -- not just the one case above.
        // Since a custom entry is always rooted on the played note, this is
        // really checking that the rooting rule itself is applied
        // consistently, in every key.
        {
            int misses = 0;
            for (int keyPc = 0; keyPc < 12; ++keyPc) {
                for (int minorCtx = 0; minorCtx < 2; ++minorCtx) {
                    auto cs = defaults();
                    cs.useCustomDictionary = true;
                    if (minorCtx) cs.customDict.useMinor = true; else cs.customDict.useMajor = true;
                    for (int degree = 0; degree < 12; ++degree) {
                        const auto t = static_cast<jazz::ChordType>(
                            degree % static_cast<int>(jazz::ChordType::Count));
                        jazz::CustomEntry entry;
                        entry.count = jazz::chordTypeTones(t, entry.offsets, 3);
                        (minorCtx ? cs.customDict.minor[degree] : cs.customDict.major[degree]) = entry;
                    }
                    const int ks[2] = {48 + keyPc, 48 + keyPc + 7};
                    for (int degree = 0; degree < 12; ++degree) {
                        jazz::Voicer v2;
                        jazz::Voicing out2;
                        const int mel = 60 + keyPc + degree;
                        if (!v2.update(ks, minorCtx ? 2 : 1, mel, cs, out2)) { ++misses; continue; }
                        if (!out2.customVoicing) { ++misses; continue; }
                        if (pitchClass(out2.chordRootPc) != pitchClass(mel)) ++misses;
                    }
                }
            }
            checkf(misses == 0,
                   "every custom entry in every key, major and minor, keeps the played note "
                   "(%d misses)", misses);
        }

        // A degree left blank (count == 0) falls back to the built-in
        // dictionary for just that one degree, rather than going silent --
        // the per-context fallback applied per degree too.
        {
            auto blankS = defaults();
            blankS.useCustomDictionary = true;
            blankS.customDict.useMajor = true;   // every entry defaults to count == 0
            jazz::Voicer blankVoicer;
            jazz::Voicing blankResult;
            blankVoicer.update(keys, 1, melody, blankS, blankResult);
            checkf(blankResult.chordRootPc == before.chordRootPc &&
                       std::string(blankResult.roman) == before.roman && !blankResult.customVoicing,
                   "a blank custom entry falls back to the built-in chord for its degree (%s%s)",
                   jazz::pitchClassName(blankResult.chordRootPc), blankResult.roman);
        }

        // Minor was never turned on, so two keys still get the built-in minor
        // dictionary rather than silently reusing the major table.
        const int minorKeys[] = {60, 67};   // C, G -> C minor
        jazz::Voicer minorVoicer;
        jazz::Voicing minorResult;
        minorVoicer.update(minorKeys, 2, 62, s, minorResult);   // D over Cm -> iim7b5 built in
        checkf(minorResult.type == jazz::ChordType::Min7b5 && !minorResult.customVoicing,
               "minor left off still uses the built-in minor dictionary (got %s)",
               jazz::styleName(minorResult.style));

        // Turning the whole thing back off is the way back to how jazz mode
        // has always behaved -- not a one-way door.
        s.useCustomDictionary = false;
        jazz::Voicer restored;
        jazz::Voicing restoredResult;
        restored.update(keys, 1, melody, s, restoredResult);
        checkf(restoredResult.chordRootPc == before.chordRootPc &&
                   std::string(restoredResult.roman) == before.roman && !restoredResult.customVoicing,
               "switching the dictionary back off restores %s (got %s%s)", before.roman,
               jazz::pitchClassName(restoredResult.chordRootPc), restoredResult.roman);
    }

    // --- MIDI import: finding a key centre and the chords played over it
    // from notes alone, with no dictionary to consult -- the reverse of
    // everything above. A synthetic performance with a known answer: four
    // bars of Cmaj7/Dm7 in C major, then four bars of Gmaj7/Am7 in G major
    // (ii-I in G is the same shape as ii-I in C, shifted -- a deliberate
    // check that the two keys reinforce the same degree rather than
    // splitting it).
    std::printf("\n-- MIDI import --\n");
    {
        const int tpq = 480;
        const int bar = tpq * 4;
        std::vector<jazz::ImportNote> notes;
        const auto addChord = [&](long long startBar, std::initializer_list<int> pitches) {
            for (int p : pitches) notes.push_back({startBar * bar, bar, p});
        };
        addChord(0, {60, 64, 67, 71});   // Cmaj7
        addChord(1, {62, 65, 69, 72});   // Dm7
        addChord(2, {60, 64, 67, 71});   // Cmaj7
        addChord(3, {62, 65, 69, 72});   // Dm7
        addChord(4, {67, 71, 74, 78});   // Gmaj7
        addChord(5, {69, 72, 76, 79});   // Am7
        addChord(6, {67, 71, 74, 78});   // Gmaj7
        addChord(7, {69, 72, 76, 79});   // Am7

        const auto result =
            jazz::analyzeForCustomDictionary(notes.data(), static_cast<int>(notes.size()), tpq);

        checkf(result.keySegments == 2, "found two key centres, C major then G major (got %d)",
               result.keySegments);

        const auto& tonic = result.dict.major[0];   // I: root position, maj7 tones
        bool haveThird = false, haveFifth = false, haveSeventh = false;
        for (int i = 0; i < tonic.count; ++i) {
            switch (tonic.offsets[i]) {
                case 4:  haveThird = true; break;
                case 7:  haveFifth = true; break;
                case 11: haveSeventh = true; break;
                default: break;
            }
        }
        checkf(tonic.count == 3 && haveThird && haveFifth && haveSeventh,
               "the tonic chord (Cmaj7 and Gmaj7, both degree 1) came back as a maj7 voicing "
               "(%d notes)", tonic.count);

        const auto& two = result.dict.major[2];   // ii: root position, min7 tones
        bool haveMinorThird = false, haveMinorFifth = false, haveMinorSeventh = false;
        for (int i = 0; i < two.count; ++i) {
            switch (two.offsets[i]) {
                case 3:  haveMinorThird = true; break;
                case 7:  haveMinorFifth = true; break;
                case 10: haveMinorSeventh = true; break;
                default: break;
            }
        }
        checkf(two.count == 3 && haveMinorThird && haveMinorFifth && haveMinorSeventh,
               "the ii chord (Dm7 and Am7, both degree 2 of their own key) came back as a min7 "
               "voicing, reinforced by both keys rather than split between them (%d notes)",
               two.count);

        check(result.chordsAnalyzed > 0, "sampled at least one chord");

        // Nothing at all: no notes, or ticksPerQuarterNote <= 0 (an SMPTE-
        // timed file, which this does not understand).
        const auto empty = jazz::analyzeForCustomDictionary(nullptr, 0, tpq);
        check(empty.keySegments == 0 && empty.chordsAnalyzed == 0, "no notes analyses to nothing");
        const auto badTiming = jazz::analyzeForCustomDictionary(notes.data(),
                                                                 static_cast<int>(notes.size()), 0);
        check(badTiming.keySegments == 0, "an invalid ticks-per-quarter-note analyses to nothing");
    }

    // --- The chord library: a transcription error here (an offset past what
    // the custom dictionary can store, a count that doesn't match the real
    // array, an empty name) would otherwise only surface as a broken row in
    // the browser or a silently truncated voicing once loaded.
    std::printf("\n-- Chord library --\n");
    {
        const int total = jazz::libraryVoicingCount();
        check(total > 0, "the library has entries");

        bool allValid = true;
        bool allNamed = true;
        for (int i = 0; i < total; ++i) {
            const auto& v = jazz::libraryVoicing(i);
            if (v.name == nullptr || v.name[0] == '\0' || v.description == nullptr ||
                v.description[0] == '\0') {
                allNamed = false;
            }
            if (v.count <= 0 || v.count > jazz::kMaxVoicingNotes) { allValid = false; continue; }
            for (int j = 0; j < v.count; ++j) {
                if (v.offsets[j] < -jazz::kMaxCustomOffset || v.offsets[j] > jazz::kMaxCustomOffset) {
                    allValid = false;
                }
            }
            const auto entry = jazz::libraryVoicingToEntry(v);
            if (entry.count != v.count) allValid = false;
            for (int j = 0; j < v.count; ++j) {
                if (entry.offsets[j] != v.offsets[j]) allValid = false;
            }
        }
        checkf(allValid, "every entry has a valid, in-range count and offsets (%d entries)", total);
        check(allNamed, "every entry has a name and a description");

        // Every theme is actually used -- a theme nobody voices for would
        // sit in the browser's filter chips with an empty list behind it.
        bool everyThemeUsed = true;
        for (int t = 0; t < jazz::kLibraryThemeCount; ++t) {
            bool used = false;
            for (int i = 0; i < total && !used; ++i) {
                used = jazz::libraryVoicing(i).theme == static_cast<jazz::LibraryTheme>(t);
            }
            everyThemeUsed &= used;
        }
        check(everyThemeUsed, "every theme has at least one voicing filed under it");
    }

    // --- Factory dictionary presets: the same class of transcription error
    // as the library above, but across a whole 24-degree table per preset
    // rather than one voicing at a time.
    std::printf("\n-- Factory chord dictionaries --\n");
    {
        const int total = jazz::factoryPresetCount();
        check(total > 0, "there is at least one factory preset");

        bool allNamed = true;
        bool allValid = true;
        bool everyPresetFillsMost = true;
        for (int i = 0; i < total; ++i) {
            const auto& preset = jazz::factoryPreset(i);
            if (preset.name == nullptr || preset.name[0] == '\0' || preset.description == nullptr ||
                preset.description[0] == '\0') {
                allNamed = false;
            }

            int filled = 0;
            for (int ctx = 0; ctx < 2; ++ctx) {
                for (int degree = 0; degree < 12; ++degree) {
                    const auto& entry = ctx == 1 ? preset.dict.minor[degree] : preset.dict.major[degree];
                    if (entry.count < 0 || entry.count > jazz::kMaxVoicingNotes) { allValid = false; continue; }
                    if (entry.count > 0) ++filled;
                    for (int j = 0; j < entry.count; ++j) {
                        if (entry.offsets[j] < -jazz::kMaxCustomOffset ||
                            entry.offsets[j] > jazz::kMaxCustomOffset) {
                            allValid = false;
                        }
                    }
                }
            }
            // A preset that leaves most degrees blank would just be the
            // built-in dictionary with extra steps -- the whole point of a
            // factory preset is that it actually has an opinion on nearly
            // every degree.
            if (filled < 20) everyPresetFillsMost = false;
        }
        checkf(allValid, "every preset's entries have a valid, in-range count and offsets (%d presets)",
              total);
        check(allNamed, "every preset has a name and a description");
        check(everyPresetFillsMost, "every preset fills at least 20 of its 24 degrees");
    }

    std::printf("\n=============================================\n");
    if (g_failures == 0) std::printf(" ALL JAZZ CHECKS PASSED\n");
    else std::printf(" %d CHECK(S) FAILED\n", g_failures);
    std::printf("=============================================\n");
    return g_failures != 0;
}
