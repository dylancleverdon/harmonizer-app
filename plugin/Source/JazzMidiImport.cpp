#include "JazzMidiImport.h"

#include <algorithm>
#include <vector>

namespace jazz {

namespace {

// Krumhansl-Kessler key profiles: the typical relative weight of each scale
// degree in a tonal melody, major and minor. Correlating a pitch-class
// histogram against these, tried at all twelve transpositions, is the
// standard way to guess a key centre from notes alone.
constexpr double kMajorProfile[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                      2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinorProfile[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                      2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

int pc(int note) {
    const int p = note % 12;
    return p < 0 ? p + 12 : p;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct KeyGuess {
    int pc = 0;
    bool minor = false;
    bool operator==(const KeyGuess& o) const { return pc == o.pc && minor == o.minor; }
    bool operator!=(const KeyGuess& o) const { return !(*this == o); }
};

/** Best-correlating key for a pitch-class histogram, or {0, false, false} via
 *  ok=false when the histogram carries no weight at all (a silent window). */
KeyGuess bestKey(const double hist[12], bool& ok) {
    double total = 0.0;
    for (int i = 0; i < 12; ++i) total += hist[i];
    ok = total > 1e-9;
    if (!ok) return {};

    double bestScore = -1.0;
    KeyGuess best;
    for (int root = 0; root < 12; ++root) {
        for (int mode = 0; mode < 2; ++mode) {
            const double* profile = mode == 0 ? kMajorProfile : kMinorProfile;
            double score = 0.0;
            for (int p = 0; p < 12; ++p) {
                const int degree = ((p - root) % 12 + 12) % 12;
                score += hist[p] * profile[degree];
            }
            if (score > bestScore) {
                bestScore = score;
                best = {root, mode == 1};
            }
        }
    }
    return best;
}

struct Window {
    long long startTick = 0;
    long long endTick = 0;
    KeyGuess key;
    bool ok = false;
};

struct Segment {
    long long startTick = 0;
    long long endTick = 0;
    KeyGuess key;
    int windowCount = 0;
};

/** A voicing seen while sampling, and how many times it recurred -- the
 *  final entry for a degree is whichever voicing was most common, on the
 *  idea that a chord played several times in a song is more likely to be
 *  the "real" one than a passing one sampled just once. */
struct VoicingVote {
    int offsets[kMaxVoicingNotes] = {};
    int count = 0;
    int votes = 0;
};

bool sameVoicing(const VoicingVote& v, const int* offsets, int count) {
    if (v.count != count) return false;
    for (int i = 0; i < count; ++i) {
        if (v.offsets[i] != offsets[i]) return false;
    }
    return true;
}

void castVote(std::vector<VoicingVote>& votes, const int* offsets, int count) {
    for (auto& v : votes) {
        if (sameVoicing(v, offsets, count)) {
            ++v.votes;
            return;
        }
    }
    if (votes.size() >= 16) return;   // bounded -- a real song has few distinct voicings per degree
    VoicingVote v;
    v.count = count;
    for (int i = 0; i < count; ++i) v.offsets[i] = offsets[i];
    v.votes = 1;
    votes.push_back(v);
}

}  // namespace

ImportResult analyzeForCustomDictionary(const ImportNote* notes, int count,
                                        int ticksPerQuarterNote) {
    ImportResult result;
    if (notes == nullptr || count <= 0 || ticksPerQuarterNote <= 0) return result;

    long long span = 0;
    for (int i = 0; i < count; ++i) {
        span = std::max(span, notes[i].startTick + std::max<long long>(0, notes[i].durationTicks));
    }
    if (span <= 0) return result;

    // --- Key centre, one guess per two-bar-ish window, a bar apart. A
    // window has to be wide enough to usually catch more than one chord --
    // a single sparse block chord (say, a bare major triad) can correlate
    // better against the wrong key's profile than the right one, simply for
    // lack of enough notes to look like a scale. Two bars of typical harmonic
    // rhythm almost always spans a change, which is what gives the profile
    // enough to go on.
    const long long windowTicks = static_cast<long long>(ticksPerQuarterNote) * 8;
    const long long hopTicks = std::max<long long>(1, static_cast<long long>(ticksPerQuarterNote) * 4);

    std::vector<Window> windows;
    for (long long start = 0; start < span; start += hopTicks) {
        const long long end = start + windowTicks;
        double hist[12] = {};
        for (int i = 0; i < count; ++i) {
            const long long noteStart = notes[i].startTick;
            const long long noteEnd = noteStart + std::max<long long>(0, notes[i].durationTicks);
            const long long overlap = std::min(end, noteEnd) - std::max(start, noteStart);
            if (overlap > 0) hist[pc(notes[i].pitch)] += static_cast<double>(overlap);
        }
        Window w;
        w.startTick = start;
        w.endTick = end;
        w.key = bestKey(hist, w.ok);
        windows.push_back(w);
    }
    if (windows.empty()) return result;

    // A window that disagrees with both neighbours is almost certainly noise
    // -- a single held colour tone tipping the correlation for one window --
    // rather than a real, if brief, modulation. Flip it back before segments
    // are built from runs of agreement.
    for (size_t i = 1; i + 1 < windows.size(); ++i) {
        if (windows[i].ok && windows[i - 1].ok && windows[i + 1].ok &&
            windows[i].key != windows[i - 1].key && windows[i - 1].key == windows[i + 1].key) {
            windows[i].key = windows[i - 1].key;
        }
    }

    // --- Collapse into segments of a single key centre.
    std::vector<Segment> segments;
    for (const auto& w : windows) {
        if (!w.ok) continue;
        if (!segments.empty() && segments.back().key == w.key) {
            segments.back().endTick = w.endTick;
            ++segments.back().windowCount;
        } else {
            segments.push_back({w.startTick, w.endTick, w.key, 1});
        }
    }

    // A segment only one or two windows wide is still more likely to be a
    // sparse chord tipping the correlation than a real, if brief, key
    // change -- fold it into whichever neighbour it more plausibly belongs
    // to (the longer-lived one) rather than let it stand on its own.
    constexpr int kMinSegmentWindows = 2;
    bool merged = true;
    while (merged && segments.size() > 1) {
        merged = false;
        size_t shortest = 0;
        for (size_t i = 1; i < segments.size(); ++i) {
            if (segments[i].windowCount < segments[shortest].windowCount) shortest = i;
        }
        if (segments[shortest].windowCount >= kMinSegmentWindows) break;

        const bool hasLeft = shortest > 0;
        const bool hasRight = shortest + 1 < segments.size();
        if (!hasLeft && !hasRight) break;   // the only segment there is

        size_t mergeInto;
        if (hasLeft && hasRight) {
            mergeInto = segments[shortest - 1].windowCount >= segments[shortest + 1].windowCount
                            ? shortest - 1
                            : shortest + 1;
        } else {
            mergeInto = hasLeft ? shortest - 1 : shortest + 1;
        }

        segments[mergeInto].startTick =
            std::min(segments[mergeInto].startTick, segments[shortest].startTick);
        segments[mergeInto].endTick =
            std::max(segments[mergeInto].endTick, segments[shortest].endTick);
        segments[mergeInto].windowCount += segments[shortest].windowCount;
        segments.erase(segments.begin() + static_cast<long>(shortest));

        // The merge may have left two same-key segments touching each other.
        for (size_t i = 0; i + 1 < segments.size();) {
            if (segments[i].key == segments[i + 1].key) {
                segments[i].endTick = segments[i + 1].endTick;
                segments[i].windowCount += segments[i + 1].windowCount;
                segments.erase(segments.begin() + static_cast<long>(i) + 1);
            } else {
                ++i;
            }
        }
        merged = true;
    }

    result.keySegments = static_cast<int>(segments.size());
    if (segments.empty()) return result;

    // --- Within each segment, sample what is sounding once a beat and learn
    // a voicing for whichever scale degree the bass note of that instant is.
    std::vector<VoicingVote> votes[2][12];   // [context: 0 major, 1 minor][degree]
    const long long sampleTicks = std::max<long long>(1, ticksPerQuarterNote);

    for (const auto& seg : segments) {
        constexpr int kSoundingCap = kMaxVoicingNotes + 4;
        for (long long t = seg.startTick; t < seg.endTick; t += sampleTicks) {
            int sounding[kSoundingCap] = {};
            int soundingCount = 0;
            for (int i = 0; i < count && soundingCount < kSoundingCap; ++i) {
                const long long noteStart = notes[i].startTick;
                const long long noteEnd = noteStart + std::max<long long>(0, notes[i].durationTicks);
                if (t >= noteStart && t < noteEnd) sounding[soundingCount++] = notes[i].pitch;
            }
            if (soundingCount < 2) continue;   // nothing to learn from a single line

            std::sort(sounding, sounding + soundingCount);
            const int root = sounding[0];
            const int degree = pc(root - seg.key.pc);

            int offsets[kMaxVoicingNotes] = {};
            int offsetCount = 0;
            for (int i = 1; i < soundingCount && offsetCount < kMaxVoicingNotes; ++i) {
                const int offset = clampi(sounding[i] - root, -kMaxCustomOffset, kMaxCustomOffset);
                bool dupe = false;
                for (int j = 0; j < offsetCount; ++j) dupe |= offsets[j] == offset;
                if (!dupe) offsets[offsetCount++] = offset;
            }
            if (offsetCount == 0) continue;   // every other voice doubled the root exactly

            ++result.chordsAnalyzed;
            castVote(votes[seg.key.minor ? 1 : 0][degree], offsets, offsetCount);
        }
    }

    for (int ctx = 0; ctx < 2; ++ctx) {
        for (int degree = 0; degree < 12; ++degree) {
            const auto& v = votes[ctx][degree];
            if (v.empty()) continue;
            const auto best =
                std::max_element(v.begin(), v.end(),
                                 [](const VoicingVote& a, const VoicingVote& b) {
                                     return a.votes < b.votes;
                                 });
            CustomEntry& entry = ctx == 1 ? result.dict.minor[degree] : result.dict.major[degree];
            entry.count = best->count;
            for (int i = 0; i < best->count; ++i) entry.offsets[i] = best->offsets[i];
            ++result.degreesFilled;

            // Every distinct shape seen at this degree, not just the winner
            // dict kept -- what lets a caller browse and pick a single
            // chord instead of only ever taking the whole table.
            for (const auto& vote : v) {
                ImportCandidate c;
                c.minor = ctx == 1;
                c.degree = degree;
                c.count = vote.count;
                for (int i = 0; i < vote.count; ++i) c.offsets[i] = vote.offsets[i];
                c.votes = vote.votes;
                result.candidates.push_back(c);
            }
        }
    }
    result.dict.useMajor = true;
    result.dict.useMinor = true;

    std::sort(result.candidates.begin(), result.candidates.end(),
              [](const ImportCandidate& a, const ImportCandidate& b) { return a.votes > b.votes; });
    constexpr size_t kMaxCandidates = 24;
    if (result.candidates.size() > kMaxCandidates) result.candidates.resize(kMaxCandidates);

    return result;
}

}  // namespace jazz
