#include "fd/music/Following.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace fd::music
{
namespace
{
int pitchClass(int pitch) noexcept
{
    return ((pitch % 12) + 12) % 12;
}

/// The highest pitch of `set` at or below `pitch` (a non-empty set).
int atOrBelow(int pitch, PitchSet set) noexcept
{
    int p = pitch;
    while (!inSet(set, p))
        --p;
    return p;
}

/// The next pitch of `set` above (`dir` +1) or below (−1) `pitch` (a non-empty set).
int next(int pitch, PitchSet set, int dir) noexcept
{
    int p = pitch + dir;
    while (!inSet(set, p))
        p += dir;
    return p;
}

std::uint64_t splitmix(std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
} // namespace

PitchSet rotated(PitchSet intervals, int root) noexcept
{
    PitchSet out = 0;
    for (int i = 0; i < 12; ++i)
        if ((intervals >> i) & 1u)
            out = static_cast<PitchSet>(out | (1u << pitchClass(i + root)));
    return out;
}

int stepBy(int pitch, PitchSet set, int steps) noexcept
{
    if ((set & allPitchClasses) == 0)
        return pitch + steps;
    const int anchor = atOrBelow(pitch, set);
    int p = anchor;
    for (int s = 0; s < std::abs(steps); ++s)
        p = next(p, set, steps > 0 ? 1 : -1);
    return p + (pitch - anchor);
}

int fitTo(int pitch, PitchSet set, Toward toward, int tie) noexcept
{
    if ((set & allPitchClasses) == 0 || inSet(set, pitch))
        return pitch;
    const int up = next(pitch, set, 1), down = next(pitch, set, -1);
    switch (toward)
    {
    case Toward::Up:
        return up;
    case Toward::Down:
        return down;
    case Toward::Near:
        break;
    }
    if (up - pitch != pitch - down)
        return up - pitch < pitch - down ? up : down;
    return tie > 0 ? up : down;
}

int mirror(int pitch, int axis, PitchSet set) noexcept
{
    if ((set & allPitchClasses) == 0)
        return 2 * axis - pitch;
    const int a = atOrBelow(axis, set);
    const int anchor = atOrBelow(pitch, set);
    // The steps of the set from the axis to the pitch's anchor, signed.
    int steps = 0;
    for (int p = a; p < anchor; p = next(p, set, 1))
        ++steps;
    for (int p = a; p > anchor; p = next(p, set, -1))
        --steps;
    return stepBy(a, set, -steps) - (pitch - anchor);
}

bool onBeat(double beat) noexcept
{
    const double frac = beat - std::floor(beat);
    return frac < 0.025 || frac > 0.975;
}

std::size_t stackThirds(int pitch, PitchSet set, int notes, std::span<int> out) noexcept
{
    std::size_t n = 0;
    for (int i = 0; i < notes && n < out.size(); ++i)
        out[n++] = stepBy(pitch, set, 2 * i);
    return n;
}

std::size_t chordTones(int root, PitchSet set, std::span<int> out) noexcept
{
    std::size_t n = 0;
    for (int d = 0; d < 12 && n < out.size(); ++d)
        if (inSet(set, root + d))
            out[n++] = pitchClass(root + d);
    return n;
}

int movement(std::span<const int> from, std::span<const int> to) noexcept
{
    int total = 0;
    for (const int t : to)
    {
        int best = 128;
        for (const int f : from)
            best = std::min(best, std::abs(t - f));
        total += from.empty() ? 0 : best;
    }
    return total;
}

std::size_t voiceAround(int played, int root, PitchSet set, Place place, Voicing voicing,
                        std::span<const int> previous, std::span<int> out) noexcept
{
    std::array<int, 12> tones{};
    const std::size_t count = chordTones(root, set, tones);
    const bool inChord = inSet(set, played);
    std::array<int, 16> notes{};
    std::size_t n = 0;
    const auto push = [&](int p)
    {
        if (n < notes.size() && std::find(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(n), p) ==
                                    notes.begin() + static_cast<std::ptrdiff_t>(n))
            notes[n++] = p;
    };
    // Each tone as the bass, as close as can be under the note played: the played note is in it
    // (its tone's pitch in [bass, bass + 12)).
    const auto closeFrom = [&](int bassTone, std::array<int, 16>& v) -> std::size_t
    {
        const int bass = atOrBelow(played, static_cast<PitchSet>(1u << bassTone));
        std::size_t k = 0;
        for (std::size_t i = 0; i < count && k < v.size(); ++i)
        {
            int p = bass + pitchClass(tones[i] - bassTone);
            v[k++] = p;
        }
        std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k));
        return k;
    };
    if (count == 0)
        push(played);
    else if (place == Place::Bottom)
    {
        push(played);
        for (std::size_t i = 0; i < count; ++i)
            if (tones[i] != pitchClass(played))
                push(next(played, static_cast<PitchSet>(1u << tones[i]), 1));
    }
    else
    {
        // Top: the bass whose voicing has the played note on top; Smooth: the one moving least.
        std::array<int, 16> best{};
        std::size_t bestCount = 0;
        int bestMove = 1 << 30;
        for (std::size_t b = 0; b < count; ++b)
        {
            std::array<int, 16> v{};
            const std::size_t k = closeFrom(tones[b], v);
            bool ok = true;
            int move = 0;
            if (place == Place::Smooth && !previous.empty())
                move = movement(previous, std::span<const int>(v.data(), k));
            else if (inChord)
                ok = v[k - 1] == played; // Top
            else
                move = std::abs(v[k - 1] - played); // the chord just under the note played
            if (ok && move < bestMove)
            {
                bestMove = move;
                best = v;
                bestCount = k;
            }
        }
        if (bestCount == 0) // Top with the note outside the chord: the chord under it
        {
            bestCount = closeFrom(tones[0], best);
            for (std::size_t i = 0; i < bestCount; ++i)
                while (best[i] >= played)
                    best[i] -= 12;
        }
        for (std::size_t i = 0; i < bestCount; ++i)
            push(best[i]);
        push(played);
    }
    std::sort(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(n));

    // The spread, never moving the note played.
    const auto shift = [&](std::size_t i, int by)
    {
        if (i >= n)
            return;
        if (notes[i] == played) // the neighbour instead, inward
            i = i + 1 < n ? i + 1 : (i > 0 ? i - 1 : i);
        if (notes[i] != played)
            notes[i] += by;
    };
    switch (voicing)
    {
    case Voicing::Open:
        shift(1, 12);
        break;
    case Voicing::Drop2:
        if (n >= 3)
            shift(n - 2, -12);
        break;
    case Voicing::Drop3:
        if (n >= 4)
            shift(n - 3, -12);
        break;
    case Voicing::Spread:
        shift(1, 12);
        std::sort(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(n));
        shift(0, -12);
        break;
    case Voicing::Smooth:
    case Voicing::Close:
        break;
    }
    std::sort(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(n));
    std::size_t written = 0;
    for (std::size_t i = 0; i < n && written < out.size(); ++i)
        if (notes[i] >= 0 && notes[i] <= 127 && (written == 0 || out[written - 1] != notes[i]))
            out[written++] = notes[i];
    return written;
}

std::size_t voiceNear(int root, PitchSet set, int center, Voicing voicing, std::span<const int> previous,
                      std::span<int> out) noexcept
{
    std::array<int, 12> tones{};
    const std::size_t count = chordTones(root, set, tones);
    if (count == 0 || out.empty())
        return 0;
    std::array<int, 16> best{};
    std::size_t bestCount = 0;
    int bestScore = 1 << 30;
    // Each tone as the bass, in the octaves around the centre: close voicings, scored.
    for (std::size_t b = 0; b < count; ++b)
        for (int octave = -1; octave <= 1; ++octave)
        {
            std::array<int, 16> v{};
            const int bass = atOrBelow(center, static_cast<PitchSet>(1u << tones[b])) + 12 * octave;
            std::size_t k = 0;
            for (std::size_t i = 0; i < count && k < v.size(); ++i)
                v[k++] = bass + pitchClass(tones[i] - tones[b]);
            std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k));
            const int middle = (v[0] + v[k - 1]) / 2;
            if (std::abs(middle - center) > 7 || v[0] < 0 || v[k - 1] > 127)
                continue;
            const int score = previous.empty() ? std::abs(middle - center)
                                               : movement(previous, std::span<const int>(v.data(), k));
            if (score < bestScore)
            {
                bestScore = score;
                best = v;
                bestCount = k;
            }
        }
    if (bestCount == 0)
        return 0;
    switch (voicing)
    {
    case Voicing::Open:
        if (bestCount >= 3)
            best[1] += 12;
        break;
    case Voicing::Drop2:
        if (bestCount >= 3)
            best[bestCount - 2] -= 12;
        break;
    case Voicing::Drop3:
        if (bestCount >= 4)
            best[bestCount - 3] -= 12;
        break;
    case Voicing::Spread:
        if (bestCount >= 3)
            best[1] += 12;
        best[0] -= 12;
        break;
    case Voicing::Smooth:
    case Voicing::Close:
        break;
    }
    std::sort(best.begin(), best.begin() + static_cast<std::ptrdiff_t>(bestCount));
    std::size_t written = 0;
    for (std::size_t i = 0; i < bestCount && written < out.size(); ++i)
        if (best[i] >= 0 && best[i] <= 127 && (written == 0 || out[written - 1] != best[i]))
            out[written++] = best[i];
    return written;
}

PitchSet chordOfKind(int root, PitchSet chord, PitchSet key, int notes) noexcept
{
    // Each degree: the intervals above the root that can fill it, the chord's first, then the key's.
    struct Degree
    {
        std::array<int, 4> fromChord;
        std::array<int, 2> fromKey;
    };
    static constexpr std::array<Degree, 4> degrees{{
        {{4, 3, 5, 2}, {4, 3}},      // third (a sus chord's 4th or 2nd)
        {{7, 6, 8, -1}, {7, 6}},     // fifth
        {{10, 11, 9, -1}, {10, 11}}, // seventh
        {{2, 1, -1, -1}, {2, 1}},    // ninth
    }};
    PitchSet out = static_cast<PitchSet>(1u << pitchClass(root));
    const int wanted = std::clamp(notes, 1, 5) - 1;
    for (int d = 0; d < wanted; ++d)
    {
        const auto& degree = degrees[static_cast<std::size_t>(d)];
        int found = -1;
        for (const int i : degree.fromChord)
            if (i >= 0 && inSet(chord, root + i))
            {
                found = i;
                break;
            }
        if (found < 0)
            for (const int i : degree.fromKey)
                if (inSet(key, root + i))
                {
                    found = i;
                    break;
                }
        if (found >= 0)
            out = static_cast<PitchSet>(out | (1u << pitchClass(root + found)));
    }
    return out;
}

int pitchMoved(int note, const PitchMoves& m, PitchSet key, std::uint64_t dice) noexcept
{
    int p = note;
    if (m.mirror)
        p = mirror(p, m.axis, key);
    p = stepBy(p + m.semitones, key, m.steps);
    if (m.chance > 0.0 && unit(dice) < m.chance)
    {
        const int spread = std::max(1, m.spread);
        const int amount = 1 + static_cast<int>(mix(dice, 1) % static_cast<std::uint64_t>(spread));
        const int way = m.way == 0 ? 1 : m.way == 1 ? -1 : ((mix(dice, 2) & 1u) != 0 ? 1 : -1);
        p = stepBy(p, key, way * amount);
    }
    return p;
}

int pitchFitted(int moved, const PitchFit& f, PitchSet key, PitchSet chord, bool onTheBeat, int tie) noexcept
{
    int p = moved;
    if (f.fit == 1 || (f.fit == 2 && f.passing && !onTheBeat))
        p = fitTo(p, key, f.toward, tie);
    else if (f.fit == 2)
        p = fitTo(p, chord, f.toward, tie);
    const int lo = std::clamp(std::min(f.lowest, f.highest), 0, 127);
    const int hi = std::clamp(std::max(f.lowest, f.highest), 0, 127);
    if (p >= lo && p <= hi)
        return p;
    if (f.outside == 1)
        return -1;
    const bool below = p < lo;
    if (f.outside == 0)
    {
        // By octaves into the range; a range under an octave may not hold it, and limits it.
        while (p < lo)
            p += 12;
        while (p > hi)
            p -= 12;
        if (p >= lo)
            return p;
    }
    // The edge: the key's note nearest it, inside the range.
    const int edge = below ? fitTo(lo, key, Toward::Up) : fitTo(hi, key, Toward::Down);
    return std::clamp(edge, lo, hi);
}

std::uint64_t mix(std::uint64_t roll, std::uint64_t a, std::uint64_t b) noexcept
{
    return splitmix(splitmix(splitmix(roll) ^ a) ^ b);
}

double unit(std::uint64_t hashed) noexcept
{
    return static_cast<double>(hashed >> 11) * 0x1.0p-53;
}
} // namespace fd::music
