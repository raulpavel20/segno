#include "fd/music/Key.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace fd::music
{
namespace
{
constexpr std::array<int, 7> naturals{0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
constexpr std::array<char, 7> letters{'C', 'D', 'E', 'F', 'G', 'A', 'B'};
constexpr std::array<int, 7> letterFifths{0, 2, 4, -1,
                                          1, 3, 5}; // each letter's place on the circle of fifths
constexpr std::array<int, 7> majorSteps{0, 2, 4, 5, 7, 9, 11};

int mod12(int x) noexcept
{
    return ((x % 12) + 12) % 12;
}

/// A note written as a letter and an accidental (−2 … 2: double flat … double sharp).
struct Spelled
{
    int letter = 0;
    int accidental = 0;
};

/// The accidental that puts `letter` on pitch class `pc`, from −6 to 5.
int accidentalOn(int letter, int pc) noexcept
{
    const int a = mod12(pc - naturals[static_cast<size_t>(letter)]);
    return a > 6 ? a - 12 : a;
}

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

Spelled spell(const Key* key, int pitchClass)
{
    if (key != nullptr)
        if (const auto* scale = scaleOf(*key))
            return spellingOf(*key, *scale)[static_cast<size_t>(mod12(pitchClass))];
    return plain(pitchClass, false);
}

std::string lowerFirst(std::string_view s)
{
    std::string out(s);
    if (!out.empty())
        out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    return out;
}
} // namespace

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
