#include "fd/music/Key.h"

#include "Spelling.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace fd::music
{
using namespace detail;

namespace
{
constexpr std::array<int, 7> letterFifths{0, 2, 4, -1,
                                          1, 3, 5}; // each letter's place on the circle of fifths

/// A natural, or else a sharp or a flat by the direction asked for.
Spelled plain(int pc, bool flats) noexcept
{
    pc = mod12(pc);
    for (int l = 0; l < 7; ++l)
        if (naturals[static_cast<size_t>(l)] == pc)
            return {l, 0};
    for (int l = 0; l < 7; ++l)
        if (naturals[static_cast<size_t>(l)] == mod12(flats ? pc + 1 : pc - 1))
            return {l, flats ? -1 : 1};
    return {};
}

/// Which degree of the major scale a signature mode starts on (Dorian: the second).
int modeDegree(int signatureMode) noexcept
{
    for (int d = 0; d < 7; ++d)
        if (majorSteps[static_cast<size_t>(d)] == signatureMode)
            return d;
    return 0;
}

bool minorLike(const Scale& scale) noexcept
{
    return (scale.intervals & (1u << 3)) != 0 && (scale.intervals & (1u << 4)) == 0;
}

/// The key signature a key is written with (§4): its major key's place on the circle of fifths
/// (negative: flats), that major's root, and the key's root as written.
struct Signature
{
    int fifths = 0;
    Spelled parent, root;
};

Signature signatureOf(int root, const Scale& scale) noexcept
{
    const int parentPc = mod12(root - scale.signatureMode);
    const int degree = modeDegree(scale.signatureMode);
    Signature best;
    bool found = false;
    for (int l = 0; l < 7; ++l)
    {
        const int acc = accidentalOn(l, parentPc);
        if (acc < -1 || acc > 1)
            continue;
        Signature s;
        s.parent = {l, acc};
        s.fifths = letterFifths[static_cast<size_t>(l)] + 7 * acc;
        const int rootLetter = (l + degree) % 7;
        s.root = {rootLetter, accidentalOn(rootLetter, root)};
        if (!found)
        {
            best = s;
            found = true;
            continue;
        }
        // Fewer sharps or flats wins. At a tie (F♯ or G♭ as the major), a root without an
        // accidental, then flats for minor-like scales and sharps for the rest: F♯ major, E♭ minor.
        const int a = std::abs(s.fifths), b = std::abs(best.fifths);
        if (a != b)
        {
            if (a < b)
                best = s;
            continue;
        }
        if ((s.root.accidental == 0) != (best.root.accidental == 0))
        {
            if (s.root.accidental == 0)
                best = s;
            continue;
        }
        if ((s.fifths < 0) == minorLike(scale))
            best = s;
    }
    return best;
}

/// Every pitch class's spelling in a key.
std::array<Spelled, 12> spellingOf(const Key& key, const Scale& scale)
{
    const auto sig = signatureOf(mod12(key.root), scale);
    std::array<Spelled, 12> table{};
    for (int pc = 0; pc < 12; ++pc)
        table[static_cast<size_t>(pc)] = plain(pc, sig.fifths < 0);

    // The notes of the major the key is written in take its letters.
    const int parentPc = mod12(naturals[static_cast<size_t>(sig.parent.letter)] + sig.parent.accidental);
    for (int i = 0; i < 7; ++i)
    {
        const int letter = (sig.parent.letter + i) % 7;
        const int pc = mod12(parentPc + majorSteps[static_cast<size_t>(i)]);
        table[static_cast<size_t>(pc)] = {letter, accidentalOn(letter, pc)};
    }

    // A 7-note scale uses each letter once from its root, unless that needs a double accidental.
    if (noteCount(scale.intervals) == 7)
    {
        int i = 0;
        for (int step = 0; step < 12; ++step)
        {
            if ((scale.intervals & (1u << step)) == 0)
                continue;
            const int letter = (sig.root.letter + i++) % 7;
            const int pc = mod12(key.root + step);
            const int acc = accidentalOn(letter, pc);
            if (acc >= -1 && acc <= 1)
                table[static_cast<size_t>(pc)] = {letter, acc};
        }
    }
    return table;
}

std::string lowerFirst(std::string_view s)
{
    std::string out(s);
    if (!out.empty())
        out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    return out;
}
} // namespace

namespace detail
{
std::string text(Spelled s, Accidentals style)
{
    std::string out(1, letters[static_cast<size_t>(s.letter)]);
    const char* sharp = style == Accidentals::Glyphs ? "♯" : "#";
    const char* flat = style == Accidentals::Glyphs ? "♭" : "b";
    for (int i = 0; i < s.accidental; ++i)
        out += sharp;
    for (int i = 0; i > s.accidental; --i)
        out += flat;
    return out;
}

Spelled spell(const Key* key, int pitchClass)
{
    if (key != nullptr)
        if (const auto* scale = scaleOf(*key))
            return spellingOf(*key, *scale)[static_cast<size_t>(mod12(pitchClass))];
    return plain(pitchClass, false);
}
} // namespace detail

const Scale* scaleOf(const Key& key) noexcept
{
    return findScale(key.scale);
}

PitchSet notes(const Key& key) noexcept
{
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return 0;
    const int root = mod12(key.root);
    const unsigned rotated = (static_cast<unsigned>(scale->intervals) << root) |
                             (static_cast<unsigned>(scale->intervals) >> (12 - root));
    return static_cast<PitchSet>(rotated & 0xfff);
}

bool contains(const Key& key, int pitch) noexcept
{
    return (notes(key) & (1u << mod12(pitch))) != 0;
}

std::string pitchClassName(const Key* key, int pitchClass, Accidentals style)
{
    return text(spell(key, pitchClass), style);
}

std::string noteName(const Key* key, int pitch, Accidentals style)
{
    const auto s = spell(key, pitch);
    const int natural = pitch - s.accidental; // the letter's own pitch: C♭3 is in C3's octave
    const int octave = static_cast<int>(std::floor(natural / 12.0)) - 2;
    return text(s, style) + std::to_string(octave);
}

std::string rootName(const Key& key, Accidentals style)
{
    return pitchClassName(&key, key.root, style);
}

std::string keyName(const Key& key, Accidentals style)
{
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return pitchClassName(nullptr, key.root, style);
    return rootName(key, style) + " " +
           (scale->properName ? std::string(scale->name) : lowerFirst(scale->name));
}

std::string keyShortName(const Key& key, Accidentals style)
{
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return pitchClassName(nullptr, key.root, style);
    return rootName(key, style) + " " + std::string(scale->shortName);
}

std::vector<std::string> spelledNotes(const Key& key, Accidentals style)
{
    std::vector<std::string> out;
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return out;
    const auto table = spellingOf(key, *scale);
    for (int step = 0; step < 12; ++step)
        if ((scale->intervals & (1u << step)) != 0)
            out.push_back(text(table[static_cast<size_t>(mod12(key.root + step))], style));
    return out;
}

bool usesFlats(const Key& key) noexcept
{
    const auto* scale = scaleOf(key);
    return scale != nullptr && signatureOf(mod12(key.root), *scale).fifths < 0;
}

int scaleStep(const Key& key, int pitch) noexcept
{
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return -1;
    const int interval = mod12(pitch - key.root);
    if ((scale->intervals & (1u << interval)) == 0)
        return -1;
    return noteCount(static_cast<PitchSet>(scale->intervals & ((1u << interval) - 1u)));
}

int moveInKey(const Key& key, int pitch, int steps) noexcept
{
    const PitchSet set = notes(key);
    if (set == 0)
        return pitch + steps;
    const int direction = steps > 0 ? 1 : -1;
    for (int moved = 0; moved != steps; moved += direction)
        do
            pitch += direction;
        while ((set & (1u << mod12(pitch))) == 0);
    return pitch;
}

int nearestInKey(const Key& key, int pitch, bool preferUp) noexcept
{
    const PitchSet set = notes(key);
    if (set == 0 || (set & (1u << mod12(pitch))) != 0)
        return pitch;
    for (int distance = 1; distance <= 6; ++distance)
    {
        const bool up = (set & (1u << mod12(pitch + distance))) != 0;
        const bool down = (set & (1u << mod12(pitch - distance))) != 0;
        if (up && (preferUp || !down))
            return pitch + distance;
        if (down)
            return pitch - distance;
    }
    return pitch;
}

int stepsToKey(const Key& from, const Key& to) noexcept
{
    const PitchSet a = notes(from), b = notes(to);
    if (a == 0 || b == 0)
        return 0;
    int best = 0, most = -1;
    for (int distance = 0; distance <= 6; ++distance)
        for (const int step : {-distance, distance})
        {
            const int m = mod12(step);
            const auto moved = static_cast<PitchSet>(
                ((static_cast<unsigned>(a) << m) | (static_cast<unsigned>(a) >> (12 - m))) & 0xfff);
            if (const int shared = noteCount(static_cast<PitchSet>(moved & b)); shared > most)
            {
                most = shared;
                best = step;
            }
        }
    return best;
}

std::optional<int> parsePitchClass(std::string_view name) noexcept
{
    auto trim = [](std::string_view s)
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        return s;
    };
    name = trim(name);
    if (name.empty())
        return std::nullopt;
    const char first = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
    int pc = -1;
    for (int l = 0; l < 7; ++l)
        if (letters[static_cast<size_t>(l)] == first)
            pc = naturals[static_cast<size_t>(l)];
    if (pc < 0)
        return std::nullopt;
    name.remove_prefix(1);
    name = trim(name);
    if (!name.empty() && (name.front() == '-' || name.front() == '_'))
        name.remove_prefix(1);

    std::string rest;
    for (char c : name)
        rest += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (rest.empty())
        return pc;
    if (rest == "sharp")
        return mod12(pc + 1);
    if (rest == "flat")
        return mod12(pc - 1);

    // Signs, repeated: # or ♯ (U+266F), b or ♭ (U+266D).
    int shift = 0;
    std::string_view r = rest;
    while (!r.empty())
    {
        if (r.front() == '#')
        {
            ++shift;
            r.remove_prefix(1);
        }
        else if (r.front() == 'b')
        {
            --shift;
            r.remove_prefix(1);
        }
        else if (r.starts_with("♯"))
        {
            ++shift;
            r.remove_prefix(3);
        }
        else if (r.starts_with("♭"))
        {
            --shift;
            r.remove_prefix(3);
        }
        else
            return std::nullopt;
    }
    return mod12(pc + shift);
}
} // namespace fd::music
