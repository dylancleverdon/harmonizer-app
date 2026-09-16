#include "JazzVoicer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace jazz {

namespace {

// --- the chords themselves --------------------------------------------------

struct TypeSpec {
    int third, fifth, seventh, ninth, eleventh, thirteenth;
};

// Semitones above the chord root. The upper structure is spelled the way the
// chord is actually played: a major or dominant chord takes a #11 because the
// natural 11 sits a semitone above its third, and an altered dominant takes
// b9/#11/b13.
constexpr TypeSpec kTypeSpecs[static_cast<int>(ChordType::Count)] = {
    /* Maj7   */ { 4, 7, 11, 14, 18, 21},
    /* Dom7   */ { 4, 7, 10, 14, 18, 21},
    /* Dom7b9 */ { 4, 7, 10, 13, 18, 20},
    /* Min7   */ { 3, 7, 10, 14, 17, 21},
    /* Min7b5 */ { 3, 6, 10, 14, 17, 20},
    /* Dim7   */ { 3, 6,  9, 14, 17, 20},
};

struct DictEntry {
    int rootOffset;       // semitones above the key centre
    ChordType type;
    const char* roman;
};

// The dictionary proper: one chord per chromatic degree of the key, chosen so
// that the played note is always a tone of the chord it selects.
//
// Where a degree could be either the root of its own chord or a colour tone of
// a stronger one, the stronger chord wins -- the major third of the key becomes
// the third of the tonic rather than its own chord, and the leading tone
// becomes the third of V7. That is what makes a line through the scale come out
// as functional harmony rather than a row of parallel chords.
constexpr DictEntry kMajorDict[12] = {
    /*  1  */ { 0, ChordType::Maj7,  "Imaj7"    },   // played note: root
    /*  b2 */ { 1, ChordType::Dom7,  "bII7"     },   // tritone sub of V
    /*  2  */ { 2, ChordType::Min7,  "iim7"     },
    /*  b3 */ { 3, ChordType::Dim7,  "bIIIdim7" },   // passing diminished
    /*  3  */ { 0, ChordType::Maj7,  "Imaj7"    },   // played note: the third
    /*  4  */ { 5, ChordType::Maj7,  "IVmaj7"   },
    /* #4  */ { 2, ChordType::Dom7,  "II7"      },   // V of V; played note: third
    /*  5  */ { 7, ChordType::Dom7,  "V7"       },
    /*  b6 */ { 8, ChordType::Maj7,  "bVImaj7"  },
    /*  6  */ { 9, ChordType::Min7,  "vim7"     },
    /*  b7 */ {10, ChordType::Dom7,  "bVII7"    },   // backdoor dominant
    /*  7  */ { 7, ChordType::Dom7,  "V7"       },   // played note: the third
};

constexpr DictEntry kMinorDict[12] = {
    /*  1  */ { 0, ChordType::Min7,   "im7"      },
    /*  b2 */ { 1, ChordType::Maj7,   "bIImaj7"  },   // Neapolitan
    /*  2  */ { 2, ChordType::Min7b5, "iim7b5"   },
    /*  b3 */ { 3, ChordType::Maj7,   "bIIImaj7" },
    /*  3  */ { 0, ChordType::Dom7,   "I7"       },   // V of iv; played note: third
    /*  4  */ { 5, ChordType::Min7,   "ivm7"     },
    /*  b5 */ { 6, ChordType::Min7b5, "#ivm7b5"  },
    /*  5  */ { 7, ChordType::Dom7b9, "V7b9"     },
    /*  b6 */ { 8, ChordType::Maj7,   "bVImaj7"  },
    /*  6  */ { 5, ChordType::Dom7,   "IV7"      },   // dorian IV; played note: third
    /*  b7 */ {10, ChordType::Dom7,   "bVII7"    },
    /*  7  */ { 7, ChordType::Dom7b9, "V7b9"     },   // played note: the third
};

// Interval names for a root offset on its own, upper and lower case. The
// dictionary above picks between them by hand to show a chord's quality
// (lowercase for a minor-quality chord); the custom dictionary does the same
// thing at runtime, since its chord type is only known once someone picks it.
constexpr const char* kOffsetRomanUpper[12] = {
    "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"
};
constexpr const char* kOffsetRomanLower[12] = {
    "i", "bii", "ii", "biii", "iii", "iv", "#iv", "v", "bvi", "vi", "bvii", "vii"
};

// Generic upper-structure names, independent of chord quality -- what a
// custom voicing's tones are labelled with, since it has no fixed type to
// spell them against the way the built-in dictionary's chords do.
constexpr const char* kIntervalNames[12] = {
    "R", "b9", "9", "b3", "3", "11", "#11", "5", "b13", "13", "b7", "7"
};

// --- small helpers ----------------------------------------------------------

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int pitchClass(int note) {
    const int pc = note % 12;
    return pc < 0 ? pc + 12 : pc;
}

/** Which generic degree (1, 3, 5, 7, 9, 11 or 13) a semitone offset above a
 *  root is nearest to -- how a custom voicing's tones are prioritised when
 *  there are more of them than the voice cap allows, using the same
 *  priority table every other chord in this file already uses. */
int genericDegree(int semitonesAboveRoot) {
    switch (pitchClass(semitonesAboveRoot)) {
        case 0:            return 1;
        case 1: case 2:    return 9;
        case 3: case 4:    return 3;
        case 5: case 6:    return 11;
        case 7:            return 5;
        case 8: case 9:    return 13;
        default:           return 7;   // 10 or 11
    }
}

/** Insertion sort of notes carrying their chord degree along. */
void sortPairs(int* a, int* deg, int n) {
    for (int i = 1; i < n; ++i) {
        const int va = a[i], vd = deg[i];
        int j = i - 1;
        while (j >= 0 && a[j] > va) { a[j + 1] = a[j]; deg[j + 1] = deg[j]; --j; }
        a[j + 1] = va;
        deg[j + 1] = vd;
    }
}

/**
 * What gets thrown away first when a voicing has more tones than voices. The
 * third and seventh say what the chord is, so they go last; the root and fifth
 * say least and go first, which is how a rootless voicing came about in the
 * first place.
 */
int degreePriority(int degree) {
    switch (degree) {
        case 3:  return 0;
        case 7:  return 1;
        case 9:  return 2;
        case 13: return 3;
        case 11: return 4;
        case 1:  return 5;
        default: return 6;   // the fifth
    }
}

struct ToneSet {
    // Big enough for the built-in dictionary's seven tertian tones (root
    // through 13th) and for a fully custom voicing's kMaxVoicingNotes.
    int offset[kMaxVoicingNotes] = {};    // semitones above the chord root, ascending
    int degree[kMaxVoicingNotes] = {};
    int count = 0;
};

ToneSet buildTones(ChordType type, const Settings& s) {
    const TypeSpec& spec = kTypeSpecs[static_cast<int>(type)];
    ToneSet t;
    const auto add = [&t](int offset, int degree) {
        t.offset[t.count] = offset;
        t.degree[t.count] = degree;
        ++t.count;
    };
    add(0, 1);
    add(spec.third, 3);
    add(spec.fifth, 5);
    add(spec.seventh, 7);
    if (s.ninth)      add(spec.ninth, 9);
    if (s.eleventh)   add(spec.eleventh, 11);
    if (s.thirteenth) add(spec.thirteenth, 13);
    return t;
}

/** "ii", "bII", "V" ... for a custom entry -- the bare scale-degree function,
 *  case chosen from whichever third the voicing actually contains. Unlike
 *  the built-in dictionary's roman numerals this carries no chord-quality
 *  suffix: a custom voicing has no fixed type to spell one from, and the
 *  chord symbol above it already gives the detail. */
void buildCustomRoman(int rootOffset, const CustomEntry& entry, char* out, int outSize) {
    bool minorThird = false, majorThird = false;
    for (int i = 0; i < entry.count; ++i) {
        const int pc = pitchClass(entry.offsets[i]);
        if (pc == 3) minorThird = true;
        if (pc == 4) majorThird = true;
    }
    const bool lower = minorThird && !majorThird;
    const char* base = (lower ? kOffsetRomanLower : kOffsetRomanUpper)[clampi(rootOffset, 0, 11)];
    std::snprintf(out, static_cast<size_t>(outSize), "%s", base);
}

/** Which chord degree a pitch class is, given the chord -- for the readout. */
int degreeOfPitchClass(ChordType type, int relativePc) {
    const TypeSpec& spec = kTypeSpecs[static_cast<int>(type)];
    const int offsets[7] = {0, spec.third, spec.fifth, spec.seventh,
                            spec.ninth, spec.eleventh, spec.thirteenth};
    const int degrees[7] = {1, 3, 5, 7, 9, 11, 13};
    for (int i = 0; i < 7; ++i) {
        if (pitchClass(offsets[i]) == relativePc) return degrees[i];
    }
    return 1;
}

/**
 * Turns the chord's tones into a shape: offsets from the root, in the order and
 * the octaves this style puts them in. Returns the number of voices, or 0 when
 * the style cannot be built from these tones (a drop-3 of a three-note chord,
 * say), in which case the caller skips it.
 */
int shapeStyle(Style style, const ToneSet& t, int* offset, int* degree) {
    const int n = t.count;
    for (int i = 0; i < n; ++i) { offset[i] = t.offset[i]; degree[i] = t.degree[i]; }
    int count = n;

    switch (style) {
        case Style::Close:
            break;

        case Style::Drop2:
            if (n < 3) return 0;
            offset[n - 2] -= 12;
            break;

        case Style::Drop3:
            if (n < 4) return 0;
            offset[n - 3] -= 12;
            break;

        case Style::Drop24:
            if (n < 5) return 0;
            offset[n - 2] -= 12;
            offset[n - 4] -= 12;
            break;

        case Style::Rootless: {
            if (n < 4) return 0;
            for (int i = 1; i < n; ++i) { offset[i - 1] = t.offset[i]; degree[i - 1] = t.degree[i]; }
            count = n - 1;
            break;
        }

        case Style::Quartal: {
            // Stack fourths through whatever tones the chord has, starting from
            // the third. A perfect fourth if one is there, otherwise the closest
            // interval to it, which is what gives the So What sound its
            // occasional major third on top.
            bool used[kMaxVoicingNotes] = {};
            int start = -1;
            for (int i = 0; i < n; ++i) if (t.degree[i] == 3) { start = i; break; }
            if (start < 0) start = (n > 1) ? 1 : 0;

            count = 0;
            int currentPc = pitchClass(t.offset[start]);
            int currentOffset = t.offset[start];
            offset[count] = currentOffset;
            degree[count] = t.degree[start];
            used[start] = true;
            ++count;

            for (int voice = 1; voice < 5 && count < n; ++voice) {
                int pick = -1;
                for (int want : {5, 6, 7, 4}) {
                    const int wantPc = (currentPc + want) % 12;
                    for (int i = 0; i < n; ++i) {
                        if (!used[i] && pitchClass(t.offset[i]) == wantPc) { pick = i; break; }
                    }
                    if (pick >= 0) break;
                }
                if (pick < 0) break;

                // Place it in the octave that keeps the stack ascending.
                int placed = pitchClass(t.offset[pick]);
                while (placed <= currentOffset) placed += 12;
                offset[count] = placed;
                degree[count] = t.degree[pick];
                used[pick] = true;
                currentOffset = placed;
                currentPc = pitchClass(placed);
                ++count;
            }
            if (count < 3) return 0;
            break;
        }

        case Style::Shell: {
            // Root, third, seventh -- everything the chord needs to be itself --
            // plus the topmost extension if one is switched on.
            count = 0;
            for (int i = 0; i < n; ++i) {
                const int d = t.degree[i];
                if (d == 1 || d == 3 || d == 7) {
                    offset[count] = t.offset[i];
                    degree[count] = d;
                    ++count;
                }
            }
            int top = -1;
            for (int i = 0; i < n; ++i) if (t.degree[i] >= 9) top = i;
            if (top >= 0) {
                offset[count] = t.offset[top];
                degree[count] = t.degree[top];
                ++count;
            }
            if (count < 3) return 0;
            break;
        }

        case Style::Spread:
            if (n < 3) return 0;
            for (int i = 1; i < n; i += 2) offset[i] += 12;
            break;

        case Style::Cluster:
            // Everything inside one octave of the root.
            for (int i = 0; i < n; ++i) offset[i] = pitchClass(offset[i]);
            break;

        case Style::Count:
        default:
            return 0;
    }

    sortPairs(offset, degree, count);
    return count;
}

/** How idiomatic a style is when the player has not asked for one in particular. */
float autoStyleBias(Style style) {
    switch (style) {
        case Style::Close:    return 0.0f;
        case Style::Drop2:    return 0.10f;
        case Style::Rootless: return 0.15f;
        case Style::Drop3:    return 0.25f;
        case Style::Spread:   return 0.35f;
        case Style::Drop24:   return 0.35f;
        case Style::Shell:    return 0.40f;
        case Style::Quartal:  return 0.50f;
        case Style::Cluster:  return 1.40f;   // deliberate, rarely the default
        case Style::Count:
        default:              return 1.40f;
    }
}

}  // namespace

// ---------------------------------------------------------------------------

void Voicer::reset() {
    prevCount_ = 0;
}

float Voicer::nextRandom() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

bool Voicer::update(const int* keys, int keyCount, int melodyNote, const Settings& s,
                    Voicing& out) {
    if (keys == nullptr || keyCount <= 0 || melodyNote < 0 || melodyNote > 127) return false;

    int lowestKey = keys[0];
    for (int i = 1; i < keyCount; ++i) lowestKey = keys[i] < lowestKey ? keys[i] : lowestKey;

    // One key names a major key centre, two or more a minor one. The lowest key
    // is the centre either way, so a player can add the note above without
    // moving the chord anywhere.
    const bool minor = keyCount >= 2;
    const int keyPc = pitchClass(lowestKey);
    const int degree = pitchClass(melodyNote - lowestKey);

    // The built-in dictionary is the default answer; a custom one only
    // overrides it context by context (major, minor), so a custom table built
    // for major alone still leaves the minor side exactly as it always was.
    const DictEntry& builtIn = (minor ? kMinorDict : kMajorDict)[degree];
    int rootOffset = builtIn.rootOffset;
    ChordType type = builtIn.type;
    const char* roman = builtIn.roman;

    const bool customActive =
        s.useCustomDictionary && (minor ? s.customDict.useMinor : s.customDict.useMajor);
    bool usingCustomVoicing = false;
    ToneSet customTones;
    if (customActive) {
        const CustomEntry& custom = (minor ? s.customDict.minor : s.customDict.major)[degree];
        if (custom.count > 0) {
            // Always rooted on the note being played -- the one rule a custom
            // entry is not free to break, since it is what guarantees the
            // played note is a tone of whatever chord comes out.
            usingCustomVoicing = true;
            rootOffset = degree;
            customTones.count = 0;
            for (int i = 0; i < custom.count && i < kMaxVoicingNotes; ++i) {
                const int off = custom.offsets[i];
                customTones.offset[customTones.count] = off;
                customTones.degree[customTones.count] = genericDegree(off);
                ++customTones.count;
            }
            buildCustomRoman(rootOffset, custom, customRomanBuf_, sizeof(customRomanBuf_));
            roman = customRomanBuf_;
        }
        // count == 0: this degree has nothing of its own yet, so it keeps
        // whatever the built-in dictionary already picked above rather than
        // going silent -- the same per-context fallback extended per degree.
    }

    const int rootPc = (keyPc + rootOffset) % 12;
    const ToneSet tones = usingCustomVoicing ? ToneSet{} : buildTones(type, s);

    // Guard the range: a window narrower than an octave has nowhere to put a
    // chord, and the folding below would spin.
    const int askedLow = clampi(s.rangeLow, 0, 115);
    const int askedHigh = clampi(s.rangeHigh < askedLow + 12 ? askedLow + 12 : s.rangeHigh,
                                 askedLow + 12, 127);
    const int maxNotes = clampi(s.maxNotes, 1, kMaxVoicingNotes);

    // Intersect the range with what the engine can actually reach from the note
    // being played. Without this a chord voiced more than two octaves away comes
    // out at the engine's limit -- an audibly wrong note, while the panel names
    // the one that was intended.
    const int reachLow = clampi(melodyNote - kEngineReachSemitones, 0, 127);
    const int reachHigh = clampi(melodyNote + kEngineReachSemitones, 0, 127);

    int lo = askedLow;
    int hi = askedHigh;
    bool limited = false;
    if (lo < reachLow) { lo = reachLow; limited = true; }
    if (hi > reachHigh) { hi = reachHigh; limited = true; }

    // Range and reach may not overlap at all -- a low range under a high note.
    // Staying in tune wins: a note outside the asked-for window is a preference
    // overridden, a note past the engine's reach is simply the wrong pitch.
    if (hi < lo + 12) {
        lo = clampi(lo, reachLow, (reachHigh - 12 > reachLow) ? reachHigh - 12 : reachLow);
        hi = lo + 12;
        if (hi > reachHigh) { hi = reachHigh; lo = hi - 12; }
        limited = true;
    }

    // Where the voicing wants to sit. The octave switch moves this target; the
    // window is a hard wall, so a tight range is free to overrule it.
    float target = 0.5f * static_cast<float>(lo + hi) + 12.0f * static_cast<float>(s.octaveShift);
    target = static_cast<float>(clampi(static_cast<int>(target), lo + 6, hi - 6));

    bool anyStyleChosen = false;
    for (int i = 0; i < kStyleCount; ++i) anyStyleChosen |= s.styles[i];

    const float smooth = s.smoothness < 0.0f ? 0.0f : (s.smoothness > 1.0f ? 1.0f : s.smoothness);
    const float leadWeight = 0.3f + 3.5f * smooth;
    const float centreWeight = 0.35f * (1.0f - 0.7f * smooth);

    float bestScore = 0.0f;
    bool haveBest = false;
    int bestNotes[kMaxVoicingNotes] = {};
    int bestDegrees[kMaxVoicingNotes] = {};
    int bestCount = 0;
    Style bestStyle = Style::Close;

    // A custom voicing already says exactly which tones make up the chord --
    // no style reshapes it and no extension stacks onto it, since both would
    // silently change tones someone picked, recorded or imported on purpose.
    // It still goes through the one placement search below, so range, octave,
    // inversion and voice leading all still apply to where it sits.
    const int styleIterations = usingCustomVoicing ? 1 : kStyleCount;
    for (int styleIndex = 0; styleIndex < styleIterations; ++styleIndex) {
        Style style = Style::Close;
        int shapeOffset[kMaxVoicingNotes] = {};
        int shapeDegree[kMaxVoicingNotes] = {};
        int shapeCount = 0;
        float bias = 0.0f;

        if (usingCustomVoicing) {
            shapeCount = customTones.count;
            for (int i = 0; i < shapeCount; ++i) {
                shapeOffset[i] = customTones.offset[i];
                shapeDegree[i] = customTones.degree[i];
            }
        } else {
            style = static_cast<Style>(styleIndex);
            if (anyStyleChosen && !s.styles[styleIndex]) continue;

            shapeCount = shapeStyle(style, tones, shapeOffset, shapeDegree);
            if (shapeCount <= 0) continue;

            // One draw per style rather than per placement, so shuffling picks
            // a different voicing rather than a different octave of the same
            // one.
            const float jitter = s.shuffle ? nextRandom() * 1.5f : 0.0f;
            bias = (anyStyleChosen ? 0.0f : autoStyleBias(style)) + jitter;
        }

        // An inversion step moves the chord by one of its own voices rather than
        // by an octave -- the fine control between the octave switch's steps.
        // The register it aims for has to follow the rotation below, or the
        // search for the best-centred placement simply undoes it.
        const int inversion = clampi(s.inversionShift, -4, 4);
        float styleTarget = target + 12.0f * static_cast<float>(inversion) /
                                         static_cast<float>(shapeCount);
        styleTarget = static_cast<float>(clampi(static_cast<int>(styleTarget), lo + 6, hi - 6));

        for (int rootNote = rootPc; rootNote <= 127; rootNote += 12) {
            int note[kMaxVoicingNotes];
            int deg[kMaxVoicingNotes];
            int count = shapeCount;
            for (int i = 0; i < count; ++i) {
                note[i] = rootNote + shapeOffset[i];
                deg[i] = shapeDegree[i];
            }

            // Inversion shift: rotating the voicing, not transposing it. Down an
            // inversion takes the top voice an octave lower, which leaves the
            // player's own note sitting higher inside the chord.
            for (int k = 0; k < inversion; ++k) {
                int lowIndex = 0;
                for (int i = 1; i < count; ++i) if (note[i] < note[lowIndex]) lowIndex = i;
                note[lowIndex] += 12;
            }
            for (int k = 0; k < -inversion; ++k) {
                int highIndex = 0;
                for (int i = 1; i < count; ++i) if (note[i] > note[highIndex]) highIndex = i;
                note[highIndex] -= 12;
            }

            // Fold anything outside the window back in by octaves. This is what
            // makes a narrow range smooth out the voice leading by itself:
            // fewer registers to move between.
            int folds = 0;
            for (int i = 0; i < count; ++i) {
                while (note[i] < lo) { note[i] += 12; ++folds; }
                while (note[i] > hi) { note[i] -= 12; ++folds; }
            }
            sortPairs(note, deg, count);

            // Folding can land two tones on the same note. Keep the one whose
            // degree matters more.
            int dupes = 0;
            for (int i = 1; i < count;) {
                if (note[i] == note[i - 1]) {
                    const int drop = degreePriority(deg[i]) < degreePriority(deg[i - 1]) ? i - 1 : i;
                    for (int j = drop; j < count - 1; ++j) { note[j] = note[j + 1]; deg[j] = deg[j + 1]; }
                    --count;
                    ++dupes;
                } else {
                    ++i;
                }
            }

            // The player is already sounding their own note; doubling it in
            // unison just thickens them against themselves.
            if (!s.doubleMelody) {
                for (int i = 0; i < count;) {
                    if (note[i] == melodyNote) {
                        for (int j = i; j < count - 1; ++j) { note[j] = note[j + 1]; deg[j] = deg[j + 1]; }
                        --count;
                    } else {
                        ++i;
                    }
                }
            }
            if (count <= 0) continue;

            // Too many tones for the voices allowed: shed the ones that say
            // least about the chord.
            while (count > maxNotes) {
                int worst = 0;
                for (int i = 1; i < count; ++i) {
                    if (degreePriority(deg[i]) > degreePriority(deg[worst])) worst = i;
                }
                for (int j = worst; j < count - 1; ++j) { note[j] = note[j + 1]; deg[j] = deg[j + 1]; }
                --count;
            }

            // --- score it
            float lead = 0.0f;
            if (prevCount_ > 0) {
                float forward = 0.0f;
                for (int i = 0; i < count; ++i) {
                    int best = 128;
                    for (int j = 0; j < prevCount_; ++j) {
                        const int d = std::abs(note[i] - prev_[j]);
                        if (d < best) best = d;
                    }
                    forward += static_cast<float>(best);
                }
                float backward = 0.0f;
                for (int j = 0; j < prevCount_; ++j) {
                    int best = 128;
                    for (int i = 0; i < count; ++i) {
                        const int d = std::abs(note[i] - prev_[j]);
                        if (d < best) best = d;
                    }
                    backward += static_cast<float>(best);
                }
                lead = 0.5f * (forward / static_cast<float>(count) +
                               backward / static_cast<float>(prevCount_));
            }

            float sum = 0.0f;
            for (int i = 0; i < count; ++i) sum += static_cast<float>(note[i]);
            const float centre = sum / static_cast<float>(count);
            const float span = static_cast<float>(note[count - 1] - note[0]);

            float clash = 0.0f;
            if (style != Style::Cluster) {
                for (int i = 0; i < count; ++i) {
                    if (std::abs(note[i] - melodyNote) == 1) clash += 1.2f;
                }
            }

            const float score = leadWeight * lead
                              + centreWeight * std::fabs(centre - styleTarget)
                              + 1.2f * static_cast<float>(folds)
                              + 2.0f * static_cast<float>(dupes)
                              + 0.03f * span * smooth
                              + clash
                              + bias;

            if (!haveBest || score < bestScore) {
                haveBest = true;
                bestScore = score;
                bestCount = count;
                bestStyle = style;
                for (int i = 0; i < count; ++i) { bestNotes[i] = note[i]; bestDegrees[i] = deg[i]; }
            }
        }
    }

    if (!haveBest || bestCount <= 0) return false;

    out.count = bestCount;
    for (int i = 0; i < bestCount; ++i) {
        out.notes[i] = bestNotes[i];
        out.degrees[i] = bestDegrees[i];
    }
    out.keyCentrePc = keyPc;
    out.minorKey = minor;
    out.scaleDegree = degree;
    out.chordRootPc = rootPc;
    out.type = type;
    out.customVoicing = usingCustomVoicing;
    out.roman = roman;
    out.style = bestStyle;
    out.melodyNote = melodyNote;
    // A custom voicing is always rooted on the played note by construction
    // (rootOffset == degree above), so the player is always its root.
    out.melodyDegree = usingCustomVoicing ? 1 : degreeOfPitchClass(type, pitchClass(melodyNote - rootPc));
    out.rangeLimited = limited;
    out.windowLow = lo;
    out.windowHigh = hi;

    prevCount_ = bestCount;
    for (int i = 0; i < bestCount; ++i) prev_[i] = bestNotes[i];
    return true;
}

// --- naming -----------------------------------------------------------------

const char* pitchClassName(int pitchClassIn) {
    static const char* names[12] = {"C", "Db", "D", "Eb", "E", "F",
                                    "Gb", "G", "Ab", "A", "Bb", "B"};
    const int pc = pitchClass(pitchClassIn);
    return names[pc];
}

const char* styleName(Style style) {
    switch (style) {
        case Style::Close:    return "Close";
        case Style::Drop2:    return "Drop 2";
        case Style::Drop3:    return "Drop 3";
        case Style::Drop24:   return "Drop 2 & 4";
        case Style::Rootless: return "Rootless";
        case Style::Quartal:  return "Quartal";
        case Style::Shell:    return "Shell";
        case Style::Spread:   return "Spread";
        case Style::Cluster:  return "Cluster";
        case Style::Count:
        default:              return "";
    }
}

const char* degreeName(int degree) {
    switch (degree) {
        case 1:  return "root";
        case 3:  return "3rd";
        case 5:  return "5th";
        case 7:  return "7th";
        case 9:  return "9th";
        case 11: return "11th";
        case 13: return "13th";
        default: return "colour tone";
    }
}

const char* intervalName(int semitonesAboveRoot) {
    return kIntervalNames[pitchClass(semitonesAboveRoot)];
}

int chordTypeTones(ChordType type, int* outOffsets, int maxOffsets) {
    if (outOffsets == nullptr || maxOffsets <= 0) return 0;
    const TypeSpec& spec = kTypeSpecs[clampi(static_cast<int>(type), 0,
                                             static_cast<int>(ChordType::Count) - 1)];
    const int tones[3] = {spec.third, spec.fifth, spec.seventh};
    const int n = clampi(maxOffsets, 0, 3);
    for (int i = 0; i < n; ++i) outOffsets[i] = tones[i];
    return n;
}

void chordSymbol(const Voicing& v, const Settings& s, char* out, int outSize) {
    if (out == nullptr || outSize <= 0) return;
    out[0] = '\0';
    if (v.chordRootPc < 0) return;

    const char* root = pitchClassName(v.chordRootPc);

    if (v.customVoicing) {
        // No fixed chord type to spell against -- name the root followed by
        // whichever of its own tones the voicing actually contains, so this
        // is always correct rather than guessed.
        char body[96] = {};
        size_t used = 0;
        bool seen[12] = {};
        for (int i = 0; i < v.count; ++i) {
            const int pc = pitchClass(v.notes[i] - v.chordRootPc);
            if (seen[pc]) continue;
            seen[pc] = true;
            const char* name = intervalName(pc);
            const size_t len = std::strlen(name);
            if (used + len + 2 >= sizeof(body)) break;
            if (used > 0) body[used++] = ' ';
            std::memcpy(body + used, name, len);
            used += len;
        }
        body[used] = '\0';
        std::snprintf(out, static_cast<size_t>(outSize), "%s%s%s", root,
                     used > 0 ? "  " : "", body);
        return;
    }

    const int top = s.thirteenth ? 13 : (s.eleventh ? 11 : (s.ninth ? 9 : 7));

    char body[24] = {};
    switch (v.type) {
        case ChordType::Maj7:
            if (s.eleventh && top != 13) std::snprintf(body, sizeof(body), "maj%d#11", s.ninth ? 9 : 7);
            else if (s.eleventh)         std::snprintf(body, sizeof(body), "maj13#11");
            else                         std::snprintf(body, sizeof(body), "maj%d", top);
            break;

        case ChordType::Dom7:
            if (s.eleventh && top != 13) std::snprintf(body, sizeof(body), "%d#11", s.ninth ? 9 : 7);
            else if (s.eleventh)         std::snprintf(body, sizeof(body), "13#11");
            else                         std::snprintf(body, sizeof(body), "%d", top);
            break;

        case ChordType::Dom7b9: {
            // An altered dominant is named by what is actually switched on: one
            // alteration gets spelled out, several are just "alt".
            const int alts = (s.ninth ? 1 : 0) + (s.eleventh ? 1 : 0) + (s.thirteenth ? 1 : 0);
            if (alts >= 2)          std::snprintf(body, sizeof(body), "7alt");
            else if (s.ninth)       std::snprintf(body, sizeof(body), "7b9");
            else if (s.eleventh)    std::snprintf(body, sizeof(body), "7#11");
            else if (s.thirteenth)  std::snprintf(body, sizeof(body), "7b13");
            else                    std::snprintf(body, sizeof(body), "7");
            break;
        }

        case ChordType::Min7:
            std::snprintf(body, sizeof(body), "m%d", top);
            break;

        case ChordType::Min7b5:
            std::snprintf(body, sizeof(body), "m%db5", top);
            break;

        case ChordType::Dim7:
        case ChordType::Count:
        default:
            std::snprintf(body, sizeof(body), "dim7");
            break;
    }

    std::snprintf(out, static_cast<size_t>(outSize), "%s%s", root, body);
}

}  // namespace jazz
