// Checks on the jazz chord dictionary and the voicer.
//
// This is deliberately free of JUCE and of the engine: the voicer is integer
// music theory, so it can be built with a single compiler invocation and run
// anywhere. What it asserts is the musical contract -- the played note is always
// a tone of the chord chosen for it, nothing sounds outside the range, the
// octave and inversion switches move the chord the way the panel says they do,
// and asking for smoother voice leading actually reduces how far the voices
// travel.

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

    // --- Nothing may sound outside the range, ever.
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
                            allInside &= v.notes[i] >= w.low && v.notes[i] <= w.high;
                            if (i > 0) noDuplicates &= v.notes[i] != v.notes[i - 1];
                        }
                    }
                }
            }
        }
        check(allInside, "every note of every voicing lands inside the range");
        check(noDuplicates, "no voicing sounds the same note twice");
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
        checkf(centre[0] < centre[2] - 18.0f && centre[4] > centre[2] + 18.0f,
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

    std::printf("\n=============================================\n");
    if (g_failures == 0) std::printf(" ALL JAZZ CHECKS PASSED\n");
    else std::printf(" %d CHECK(S) FAILED\n", g_failures);
    std::printf("=============================================\n");
    return g_failures != 0;
}
