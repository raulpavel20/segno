#include "fd/music/Chords.h"

#include "Spelling.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <utility>

namespace fd::music
{
using namespace detail;

namespace
{
constexpr ChordKind make(std::string_view id, std::string_view symbol, std::string_view roman,
                         std::string_view name, bool minor, std::initializer_list<int> tones)
{
    ChordKind k{id, symbol, roman, name, minor, {}, 0};
    for (int t : tones)
        k.tones[static_cast<std::size_t>(k.toneCount++)] = static_cast<std::int8_t>(t);
    return k;
}

// The order is the one a name prefers when notes read two ways and nothing else decides (Am7/E
// over C6/E). Indices are used below by name; keep them in step.
constexpr std::array<ChordKind, 28> kinds{{
    make("maj", "", "", "Major", false, {0, 4, 7}),                                  // 0
    make("m", "m", "", "Minor", true, {0, 3, 7}),                                    // 1
    make("dim", "°", "°", "Diminished", true, {0, 3, 6}),                            // 2
    make("aug", "+", "+", "Augmented", false, {0, 4, 8}),                            // 3
    make("sus4", "sus4", "sus4", "Suspended fourth", false, {0, 5, 7}),              // 4
    make("sus2", "sus2", "sus2", "Suspended second", false, {0, 2, 7}),              // 5
    make("7", "7", "7", "Dominant seventh", false, {0, 4, 7, 10}),                   // 6
    make("maj7", "maj7", "maj7", "Major seventh", false, {0, 4, 7, 11}),             // 7
    make("m7", "m7", "7", "Minor seventh", true, {0, 3, 7, 10}),                     // 8
    make("m7b5", "m7♭5", "ø7", "Half-diminished seventh", true, {0, 3, 6, 10}),      // 9
    make("dim7", "°7", "°7", "Diminished seventh", true, {0, 3, 6, 9}),              // 10
    make("m(maj7)", "m(maj7)", "(maj7)", "Minor major seventh", true, {0, 3, 7, 11}), // 11
    make("7sus4", "7sus4", "7sus4", "Seventh, suspended fourth", false, {0, 5, 7, 10}), // 12
    make("aug7", "+7", "+7", "Augmented seventh", false, {0, 4, 8, 10}),             // 13
    make("maj7#5", "maj7♯5", "+maj7", "Augmented major seventh", false, {0, 4, 8, 11}), // 14
    make("6", "6", "6", "Sixth", false, {0, 4, 7, 9}),                               // 15
    make("m6", "m6", "6", "Minor sixth", true, {0, 3, 7, 9}),                        // 16
    make("add9", "add9", "add9", "Added ninth", false, {0, 4, 7, 14}),               // 17
    make("m(add9)", "m(add9)", "(add9)", "Minor, added ninth", true, {0, 3, 7, 14}), // 18
    make("9", "9", "9", "Ninth", false, {0, 4, 7, 10, 14}),                          // 19
    make("maj9", "maj9", "maj9", "Major ninth", false, {0, 4, 7, 11, 14}),           // 20
    make("m9", "m9", "9", "Minor ninth", true, {0, 3, 7, 10, 14}),                   // 21
    make("6/9", "6/9", "6/9", "Six-nine", false, {0, 4, 7, 9, 14}),                  // 22
    make("7b9", "7♭9", "7♭9", "Seventh, flat ninth", false, {0, 4, 7, 10, 13}),      // 23
    make("7#9", "7♯9", "7♯9", "Seventh, sharp ninth", false, {0, 4, 7, 10, 15}),     // 24
    make("9sus4", "9sus4", "9sus4", "Ninth, suspended fourth", false, {0, 5, 7, 10, 14}), // 25
    make("m11", "m11", "11", "Minor eleventh", true, {0, 3, 7, 10, 14, 17}),         // 26
    make("13", "13", "13", "Thirteenth", false, {0, 4, 7, 10, 14, 21}),              // 27
}};

enum KindIndex
{
    kMajor = 0,
    kMinor = 1,
    kDim = 2,
    kAug = 3,
    kSus4 = 4,
    kSus2 = 5,
    kDom7 = 6,
    kMaj7 = 7,
    kMin7 = 8,
    kHalfDim7 = 9,
    kDim7 = 10,
    kMinMaj7 = 11,
    kAdd9 = 17,
    kMinAdd9 = 18,
    kDom9 = 19,
    kMaj9 = 20,
    kMin9 = 21,
};

/// Other ways people write a kind.
constexpr std::array<std::pair<std::string_view, int>, 30> aliases{{
    {"M", kMajor},     {"major", kMajor},   {"min", kMinor},     {"-", kMinor},
    {"minor", kMinor}, {"mi", kMinor},      {"o", kDim},         {"aug", kAug},
    {"sus", kSus4},    {"M7", kMaj7},       {"Δ", kMaj7},        {"Δ7", kMaj7},
    {"ma7", kMaj7},    {"min7", kMin7},     {"-7", kMin7},       {"ø", kHalfDim7},
    {"ø7", kHalfDim7}, {"-7b5", kHalfDim7}, {"min7b5", kHalfDim7}, {"o7", kDim7},
    {"mM7", kMinMaj7}, {"mmaj7", kMinMaj7}, {"minmaj7", kMinMaj7}, {"-maj7", kMinMaj7},
    {"7sus", 12},      {"add2", kAdd9},     {"madd9", kMinAdd9}, {"69", 22},
    {"maj", kMajor},   {"dim", kDim},
}};

/// A set of intervals or pitch classes from a list.
constexpr PitchSet bits(std::initializer_list<int> values)
{
    PitchSet s = 0;
    for (int v : values)
        s = static_cast<PitchSet>(s | (1u << v));
    return s;
}

constexpr PitchSet majorScale = bits({0, 2, 4, 5, 7, 9, 11});
constexpr PitchSet minorScale = bits({0, 2, 3, 5, 7, 8, 10});

bool has(PitchSet set, int interval) noexcept
{
    return (set & (1u << mod12(interval))) != 0;
}

PitchSet rotate(PitchSet set, int by) noexcept
{
    by = mod12(by);
    const unsigned s = set & 0xfffu;
    return static_cast<PitchSet>(((s << by) | (s >> (12 - by))) & 0xfffu);
}

/// A kind's tones as intervals within an octave.
PitchSet kindSet(const ChordKind& k) noexcept
{
    PitchSet s = 0;
    for (int i = 0; i < k.toneCount; ++i)
        s = static_cast<PitchSet>(s | (1u << mod12(k.tones[static_cast<std::size_t>(i)])));
    return s;
}

bool hasSeventh(const ChordKind& k) noexcept
{
    const auto s = kindSet(k);
    return has(s, 10) || has(s, 11) || &k == &kinds[kDim7];
}

/// Whether an interval can be a tension added to a kind ("(♭9)").
bool tensionAllowed(PitchSet kindTones, int interval) noexcept
{
    if (has(kindTones, interval))
        return false;
    switch (interval)
    {
    case 1:
    case 2:
    case 5: return true;
    case 3: return has(kindTones, 4); // the ♯9 beside a major third
    case 6:
    case 8: return has(kindTones, 7); // the ♯11 and ♭13 beside a perfect fifth
    case 9: return has(kindTones, 10) || has(kindTones, 11); // the 13th of a seventh chord
    default: return false;
    }
}

/// How a root and kind explain a set of intervals above it.
struct Fit
{
    int kind = 0;
    PitchSet tensions = 0;
    bool omitsFifth = false;
    int cost = std::numeric_limits<int>::max();
};

/// The kind that explains the intervals above a root best: exactly, else with the fewest
/// tensions added, else without the fifth.
Fit fitRoot(PitchSet intervals) noexcept
{
    Fit best;
    for (int k = 0; k < static_cast<int>(kinds.size()); ++k)
    {
        const auto& kind = kinds[static_cast<std::size_t>(k)];
        const PitchSet tones = kindSet(kind);
        for (int omit = 0; omit < 2; ++omit)
        {
            PitchSet need = tones;
            if (omit == 1)
            {
                if (kind.toneCount < 4 || !has(tones, 7) || has(intervals, 7))
                    continue;
                need = static_cast<PitchSet>(need & ~(1u << 7));
            }
            if ((intervals & need) != need)
                continue;
            const PitchSet extra = static_cast<PitchSet>(intervals & ~need);
            bool allowed = true;
            for (int i = 0; i < 12 && allowed; ++i)
                if (has(extra, i) && !tensionAllowed(tones, i))
                    allowed = false;
            if (!allowed)
                continue;
            const int cost = noteCount(extra) * 4 + omit * 3;
            if (cost < best.cost)
                best = {k, extra, omit == 1, cost};
        }
    }
    return best;
}

/// The seven-note scale a key's chords are numbered and spelled by: its own, or for other scales
/// the major or natural minor on its root, by its third.
PitchSet referenceScale(const Key& key) noexcept
{
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return majorScale;
    if (noteCount(scale->intervals) == 7)
        return scale->intervals;
    return has(scale->intervals, 3) && !has(scale->intervals, 4) ? minorScale : majorScale;
}

std::array<int, 7> stepsOf(PitchSet scale) noexcept
{
    std::array<int, 7> steps{};
    std::size_t n = 0;
    for (int i = 0; i < 12 && n < 7; ++i)
        if (has(scale, i))
            steps[n++] = i;
    return steps;
}

/// A pitch class's degree (0–6) in the key's reference scale, and its alteration: ♭VII in C major
/// is {6, −1}. Between two degrees, a flat, except a raised lowered degree (♯vii in minor), the
/// raised fourth, and the root of a diminished chord, which leads up (♯iv°, ♯i°7).
std::pair<int, int> degreeOf(const Key& key, int pc, bool leading = false) noexcept
{
    const auto steps = stepsOf(referenceScale(key));
    const int interval = mod12(pc - key.root);
    for (int d = 0; d < 7; ++d)
        if (steps[static_cast<std::size_t>(d)] == interval)
            return {d, 0};
    int below = 0;
    for (int d = 0; d < 7; ++d)
        if (steps[static_cast<std::size_t>(d)] < interval)
            below = d;
    const int above = (below + 1) % 7;
    const int belowGap = interval - steps[static_cast<std::size_t>(below)];
    const int aboveGap = (above == 0 ? 12 : steps[static_cast<std::size_t>(above)]) - interval;
    const bool lowered = steps[static_cast<std::size_t>(below)] < majorSteps[static_cast<std::size_t>(below)];
    const bool raisedFourth = interval == 6 && steps[3] == 5;
    if (belowGap == 1 && (aboveGap != 1 || lowered || raisedFourth || leading))
        return {below, 1};
    if (aboveGap == 1)
        return {above, -1};
    return belowGap <= aboveGap ? std::pair{below, belowGap} : std::pair{above, -aboveGap};
}

const Key& keyOrC(const Key* key) noexcept
{
    static const Key cMajor{0, "major"};
    return key != nullptr && scaleOf(*key) != nullptr ? *key : cMajor;
}

/// Whether a kind is diminished: its root leads up a semitone.
bool leadsUp(int kind) noexcept
{
    return kind == kDim || kind == kDim7 || kind == kHalfDim7;
}

/// A chord's root as the key writes it, by its degree (♭VII in C major is B♭).
Spelled spellRoot(const Key& key, int pc, bool leading)
{
    const auto [degree, alteration] = degreeOf(key, pc, leading);
    (void)alteration;
    const auto keyRoot = spell(&key, key.root);
    const int letter = (keyRoot.letter + degree) % 7;
    const int accidental = accidentalOn(letter, pc);
    if (accidental < -1 || accidental > 1)
        return spell(&key, pc);
    return {letter, accidental};
}

/// How many letters above a chord's root a tone is written (a ♯9 is a second, a ♭10 isn't).
int letterSteps(const ChordKind& kind, int interval) noexcept
{
    const PitchSet tones = kindSet(kind);
    switch (mod12(interval))
    {
    case 0: return 0;
    case 1:
    case 2: return 1;
    case 3: return has(tones, 4) ? 1 : 2;
    case 4: return 2;
    case 5: return 3;
    case 6: return has(tones, 7) ? 3 : 4;
    case 7: return 4;
    case 8: return has(tones, 7) ? 5 : 4;
    case 9: return &kind == &kinds[kDim7] ? 6 : 5;
    default: return 6;
    }
}

std::string tensionText(const Chord& chord, Accidentals style)
{
    if (chord.tensions == 0)
        return {};
    const bool glyphs = style == Accidentals::Glyphs;
    std::string out = "(";
    if (!hasSeventh(kindOf(chord)))
        out += "add";
    bool first = true;
    for (int i : {1, 2, 3, 5, 6, 8, 9})
    {
        if (!has(chord.tensions, i))
            continue;
        if (!first)
            out += ",";
        first = false;
        switch (i)
        {
        case 1: out += glyphs ? "♭9" : "b9"; break;
        case 2: out += "9"; break;
        case 3: out += glyphs ? "♯9" : "#9"; break;
        case 5: out += "11"; break;
        case 6: out += glyphs ? "♯11" : "#11"; break;
        case 8: out += glyphs ? "♭13" : "b13"; break;
        default: out += "13"; break;
        }
    }
    return out + ")";
}

std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

/// Tensions written in parentheses: "b9", "#11", "add13", separated by commas.
std::optional<PitchSet> parseTensions(std::string_view text)
{
    PitchSet set = 0;
    while (!text.empty())
    {
        const auto comma = text.find(',');
        auto part = trim(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        if (part.starts_with("add"))
            part.remove_prefix(3);
        int shift = 0;
        if (part.starts_with("b") || part.starts_with("#"))
        {
            shift = part.front() == '#' ? 1 : -1;
            part.remove_prefix(1);
        }
        else if (part.starts_with("♭") || part.starts_with("♯"))
        {
            shift = part.starts_with("♯") ? 1 : -1;
            part.remove_prefix(3);
        }
        int interval = -1;
        if (part == "9")
            interval = 2;
        else if (part == "11")
            interval = 5;
        else if (part == "13")
            interval = 9;
        if (interval < 0)
            return std::nullopt;
        set = static_cast<PitchSet>(set | (1u << mod12(interval + shift)));
    }
    return set;
}

/// A kind with tensions after it: "m7", "7(b9,#11)".
std::optional<std::pair<int, PitchSet>> parseKind(std::string_view text)
{
    if (const auto k = findChordKind(text))
        return std::pair{*k, PitchSet{0}};
    const auto open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(")"))
        return std::nullopt;
    const auto k = findChordKind(text.substr(0, open));
    const auto tensions = parseTensions(text.substr(open + 1, text.size() - open - 2));
    if (!k || !tensions)
        return std::nullopt;
    return std::pair{*k, *tensions};
}

/// A Roman numeral's letters: its degree (0–6), and whether it's lower case. None for others.
std::optional<std::pair<int, bool>> parseNumeral(std::string_view text) noexcept
{
    static constexpr std::array<std::string_view, 7> numerals{"I", "II", "III", "IV", "V", "VI", "VII"};
    if (text.empty())
        return std::nullopt;
    const bool lower = std::islower(static_cast<unsigned char>(text.front())) != 0;
    std::string upper;
    for (char c : text)
    {
        if ((std::islower(static_cast<unsigned char>(c)) != 0) != lower)
            return std::nullopt;
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (int d = 0; d < 7; ++d)
        if (numerals[static_cast<std::size_t>(d)] == upper)
            return std::pair{d, lower};
    return std::nullopt;
}

/// The length of a leading accidental ("b", "#", "♭", "♯") and its shift, or {0, 0}.
std::pair<std::size_t, int> leadingAccidental(std::string_view text) noexcept
{
    if (text.starts_with("b"))
        return {1, -1};
    if (text.starts_with("#"))
        return {1, 1};
    if (text.starts_with("♭"))
        return {3, -1};
    if (text.starts_with("♯"))
        return {3, 1};
    return {0, 0};
}

bool startsNumeral(std::string_view text) noexcept
{
    const auto [length, shift] = leadingAccidental(text);
    (void)shift;
    text.remove_prefix(length);
    return !text.empty() && std::string_view("IViv").find(text.front()) != std::string_view::npos;
}

/// A chord of a kind with tensions; tensions that make a kind of their own become it (7(♭9) is 7♭9).
Chord withTensions(int root, int kind, PitchSet tensions) noexcept
{
    if (tensions != 0)
        if (const auto fit = fitRoot(static_cast<PitchSet>(kindSet(kinds[static_cast<std::size_t>(kind)]) | tensions));
            fit.cost < std::numeric_limits<int>::max())
            return {.root = root, .kind = fit.kind, .tensions = fit.tensions};
    return {.root = root, .kind = kind, .tensions = tensions};
}

/// A Roman numeral chord ("ii7", "bVII", "viiø7") in a key whose root and reference scale are given.
std::optional<Chord> parseRoman(std::string_view text, int keyRoot, PitchSet reference)
{
    const auto [accLength, alteration] = leadingAccidental(text);
    text.remove_prefix(accLength);
    std::size_t n = 0;
    while (n < text.size() && std::string_view("IViv").find(text[n]) != std::string_view::npos)
        ++n;
    const auto numeral = parseNumeral(text.substr(0, n));
    if (!numeral)
        return std::nullopt;
    const auto [degree, lower] = *numeral;
    auto suffix = text.substr(n);
    PitchSet tensions = 0;
    if (const auto open = suffix.find('('); open != std::string_view::npos && suffix.ends_with(")"))
        if (const auto t = parseTensions(suffix.substr(open + 1, suffix.size() - open - 2)))
        {
            tensions = *t;
            suffix = suffix.substr(0, open);
        }
    std::optional<int> kind;
    for (int k = 0; k < static_cast<int>(kinds.size()) && !kind; ++k)
    {
        const auto& candidate = kinds[static_cast<std::size_t>(k)];
        if (candidate.minor == lower && candidate.roman == suffix)
            kind = k;
    }
    if (!kind && suffix == "o")
        kind = kDim;
    if (!kind && suffix == "o7")
        kind = kDim7;
    if (const auto named = findChordKind(suffix); !kind && named && kinds[static_cast<std::size_t>(*named)].minor == lower)
        kind = named; // "viim7b5", "V7b9": a kind's own name
    if (!kind && lower) // "imaj7", "iadd9": a minor chord's own kind
        kind = findChordKind("m" + std::string(suffix));
    if (!kind)
        return std::nullopt;
    const auto steps = stepsOf(reference);
    return withTensions(mod12(keyRoot + steps[static_cast<std::size_t>(degree)] + alteration), *kind, tensions);
}

/// The chords a shape tries, in order, for a note outside the key or a scale without seven notes.
std::span<const int> shapeKinds(ChordShape shape) noexcept
{
    static constexpr std::array<int, 6> triads{kMajor, kMinor, kSus4, kSus2, kDim, kAug};
    static constexpr std::array<int, 9> sevenths{kMaj7, kDom7, kMin7, kHalfDim7, kDim7, kMinMaj7, 12, 15, 16};
    static constexpr std::array<int, 7> ninths{kMaj9, kDom9, kMin9, 22, kAdd9, kMinAdd9, 25};
    switch (shape)
    {
    case ChordShape::Seventh: return sevenths;
    case ChordShape::Ninth: return ninths;
    default: return triads;
    }
}

/// A triad made into the shape: its sus or add9 form.
Chord reshape(Chord triad, ChordShape shape) noexcept
{
    switch (shape)
    {
    case ChordShape::Sus2: return {.root = triad.root, .kind = kSus2};
    case ChordShape::Sus4: return {.root = triad.root, .kind = kSus4};
    case ChordShape::Add9:
        if (triad.kind == kMajor)
            return {.root = triad.root, .kind = kAdd9};
        if (triad.kind == kMinor)
            return {.root = triad.root, .kind = kMinAdd9};
        triad.tensions = static_cast<PitchSet>(triad.tensions | (1u << 2));
        return triad;
    default: return triad;
    }
}

/// The chord of the kinds tried whose notes are most in the set (a tie: the first).
Chord mostInSet(int root, PitchSet set, std::span<const int> tried, bool allRequired)
{
    std::optional<Chord> best;
    int bestCount = -1;
    for (int k : tried)
    {
        const Chord c{.root = mod12(root), .kind = k};
        const auto pcs = pitchClasses(c);
        const int inSet = noteCount(static_cast<PitchSet>(pcs & set));
        if (allRequired && inSet != noteCount(pcs))
            continue;
        if (inSet > bestCount)
        {
            best = c;
            bestCount = inSet;
        }
    }
    return best.value_or(Chord{.root = mod12(root), .kind = allRequired ? -1 : kMajor});
}

bool majorLike(const Key& key) noexcept
{
    return has(referenceScale(key), 4);
}

/// Why a move between two steps is usual, said plainly (§7.3: every suggestion explained).
std::string_view whyMove(int from, int to) noexcept
{
    if (to == 0)
        return from == 4 || from == 6 ? "Home: a cadence" : from == 3 ? "Home by the plagal cadence" : "Home";
    if (from == 4 && to == 5)
        return "Deceptive: the dominant not going home";
    switch ((to - from + 7) % 7)
    {
    case 3: return "Down a fifth: the strongest move";
    case 4: return to == 4 ? "To the dominant" : "Up a fifth";
    case 1: return to == 4 ? "To the dominant" : "A step up";
    case 6: return "A step down";
    case 5: return "A third down";
    default: return "A third up";
    }
}

void addUnique(std::vector<ChordSuggestion>& out, const Chord& chord, std::string_view why, const Chord& before)
{
    if (chord.root == before.root && chord.kind == before.kind)
        return;
    for (const auto& s : out)
        if (s.chord.root == chord.root && s.chord.kind == chord.kind)
            return;
    out.push_back({chord, why});
}

/// The dominant (a seventh, or a ninth for the Ninth shape) on a pitch class.
Chord dominantOn(int pc, ChordShape shape) noexcept
{
    return {.root = mod12(pc), .kind = shape == ChordShape::Ninth ? kDom9 : kDom7};
}

/// Removes tones from a stacked chord down to `count`: the fifth first, then the highest.
void omitTo(std::vector<int>& tones, int count)
{
    while (static_cast<int>(tones.size()) > std::max(1, count))
    {
        const auto fifth = std::find(tones.begin() + 1, tones.end(), 7);
        if (fifth != tones.end() && tones.size() > 3)
            tones.erase(fifth);
        else
            tones.pop_back();
    }
}

/// Sorts and moves any note on a pitch already taken up an octave.
void spreadUnique(std::vector<int>& pitches)
{
    std::sort(pitches.begin(), pitches.end());
    for (bool again = true; again;)
    {
        again = false;
        for (std::size_t i = 1; i < pitches.size(); ++i)
            if (pitches[i] == pitches[i - 1])
            {
                pitches[i] += 12;
                again = true;
            }
        std::sort(pitches.begin(), pitches.end());
    }
}

double mean(std::span<const int> pitches) noexcept
{
    double sum = 0.0;
    for (int p : pitches)
        sum += p;
    return pitches.empty() ? 0.0 : sum / static_cast<double>(pitches.size());
}
} // namespace

std::span<const ChordKind> chordKinds() noexcept
{
    return kinds;
}

std::optional<int> findChordKind(std::string_view idOrSymbol) noexcept
{
    idOrSymbol = trim(idOrSymbol);
    for (int k = 0; k < static_cast<int>(kinds.size()); ++k)
    {
        const auto& kind = kinds[static_cast<std::size_t>(k)];
        if (kind.id == idOrSymbol || kind.symbol == idOrSymbol)
            return k;
    }
    for (const auto& [alias, k] : aliases)
        if (alias == idOrSymbol)
            return k;
    return std::nullopt;
}

const ChordKind& kindOf(const Chord& chord) noexcept
{
    return kinds[static_cast<std::size_t>(std::clamp(chord.kind, 0, static_cast<int>(kinds.size()) - 1))];
}

PitchSet pitchClasses(const Chord& chord) noexcept
{
    auto set = rotate(static_cast<PitchSet>(kindSet(kindOf(chord)) | chord.tensions), chord.root);
    if (chord.bass)
        set = static_cast<PitchSet>(set | (1u << mod12(*chord.bass)));
    return set;
}

std::vector<int> stackedTones(const Chord& chord)
{
    const auto& kind = kindOf(chord);
    std::vector<int> tones;
    for (int i = 0; i < kind.toneCount; ++i)
        tones.push_back(kind.tones[static_cast<std::size_t>(i)]);
    for (int i = 1; i < 12; ++i)
        if (has(chord.tensions, i))
        {
            int t = i + 12;
            while (t <= tones.back())
                t += 12;
            tones.push_back(t);
        }
    return tones;
}

std::optional<Chord> nameChord(std::span<const int> pitches)
{
    if (pitches.empty())
        return std::nullopt;
    PitchSet set = 0;
    for (int p : pitches)
        set = static_cast<PitchSet>(set | (1u << mod12(p)));
    if (noteCount(set) < 3)
        return std::nullopt;
    const int bass = mod12(*std::min_element(pitches.begin(), pitches.end()));

    std::optional<Chord> best;
    int bestScore = std::numeric_limits<int>::max();
    for (int root = 0; root < 12; ++root)
    {
        if (!has(set, root))
            continue;
        const auto fit = fitRoot(rotate(set, -root));
        if (fit.cost == std::numeric_limits<int>::max())
            continue;
        // An exact reading first, then one whose root is in the bass, then the kinds' order.
        const int score = (fit.cost * 2 + (root == bass ? 0 : 1)) * 64 + fit.kind;
        if (score < bestScore)
        {
            bestScore = score;
            best = Chord{.root = root,
                         .kind = fit.kind,
                         .tensions = fit.tensions,
                         .bass = root == bass ? std::nullopt : std::optional<int>(bass)};
        }
    }
    // A bass that's only an added tone (the D under C E G) reads as a slash under the rest: C/D.
    if (best && best->bass)
    {
        const int interval = mod12(*best->bass - best->root);
        const auto& kind = kindOf(*best);
        bool added = has(best->tensions, interval);
        for (int i = 0; i < kind.toneCount; ++i)
            if (kind.tones[static_cast<std::size_t>(i)] > 12 && mod12(kind.tones[static_cast<std::size_t>(i)]) == interval)
                added = true;
        const auto rest = static_cast<PitchSet>(set & ~(1u << *best->bass));
        if (added && noteCount(rest) >= 3)
        {
            std::optional<Chord> under;
            for (int root = 0; root < 12 && !under; ++root)
                if (has(rest, root))
                    if (const auto fit = fitRoot(rotate(rest, -root)); fit.cost == 0)
                        under = Chord{.root = root, .kind = fit.kind, .bass = *best->bass};
            if (under)
                return under;
        }
    }
    return best;
}

std::string chordName(const Chord& chord, const Key* key, Accidentals style)
{
    const auto& k = keyOrC(key);
    const auto& kind = kindOf(chord);
    const auto root = spellRoot(k, chord.root, leadsUp(chord.kind));
    std::string out = text(root, style);
    out += style == Accidentals::Glyphs ? std::string(kind.symbol) : chord.kind == kMajor ? std::string() : std::string(kind.id);
    out += tensionText(chord, style);
    if (chord.bass && mod12(*chord.bass) != mod12(chord.root))
    {
        const int letter = (root.letter + letterSteps(kind, *chord.bass - chord.root)) % 7;
        const int accidental = accidentalOn(letter, *chord.bass);
        out += "/" + text(accidental >= -1 && accidental <= 1 ? Spelled{letter, accidental} : spell(&k, *chord.bass), style);
    }
    return out;
}

std::string romanNumeral(const Chord& chord, const Key& key, Accidentals style)
{
    static constexpr std::array<std::string_view, 7> upper{"I", "II", "III", "IV", "V", "VI", "VII"};
    static constexpr std::array<std::string_view, 7> lower{"i", "ii", "iii", "iv", "v", "vi", "vii"};
    if (scaleOf(key) == nullptr)
        return {};
    const auto& kind = kindOf(chord);
    const auto [degree, alteration] = degreeOf(key, chord.root, leadsUp(chord.kind));
    const bool glyphs = style == Accidentals::Glyphs;
    std::string out;
    for (int i = 0; i < alteration; ++i)
        out += glyphs ? "♯" : "#";
    for (int i = 0; i > alteration; --i)
        out += glyphs ? "♭" : "b";
    out += (kind.minor ? lower : upper)[static_cast<std::size_t>(degree)];
    std::string suffix(kind.roman);
    if (!glyphs)
    {
        if (suffix == "°")
            suffix = "o";
        else if (suffix == "°7")
            suffix = "o7";
        else if (suffix == "ø7")
            suffix = "m7b5";
        else if (suffix == "7♭9")
            suffix = "7b9";
        else if (suffix == "7♯9")
            suffix = "7#9";
    }
    return out + suffix + tensionText(chord, style);
}

std::optional<Chord> parseChord(std::string_view text, const Key* key)
{
    text = trim(text);
    if (text.empty())
        return std::nullopt;

    // A slash: a bass after a name, or after a Roman numeral, the step it's the dominant of
    // ("V7/V"). "6/9" is a kind, not a slash.
    std::string_view main = text, after;
    if (const auto slash = text.rfind('/'); slash != std::string_view::npos)
    {
        const auto tail = trim(text.substr(slash + 1));
        if (parsePitchClass(tail) || startsNumeral(tail))
        {
            main = trim(text.substr(0, slash));
            after = tail;
        }
    }

    if (startsNumeral(main))
    {
        if (key == nullptr || scaleOf(*key) == nullptr)
            return std::nullopt;
        if (after.empty())
            return parseRoman(main, key->root, referenceScale(*key));
        const auto target = parseRoman(after, key->root, referenceScale(*key));
        if (!target)
            return std::nullopt;
        return parseRoman(main, target->root, majorScale);
    }

    // A name: a letter and its accidentals, then the kind.
    std::size_t n = 1;
    if (std::string_view("ABCDEFGabcdefg").find(main.front()) == std::string_view::npos)
        return std::nullopt;
    for (;;)
    {
        const auto rest = main.substr(n);
        if (rest.starts_with("#") || rest.starts_with("b"))
            n += 1;
        else if (rest.starts_with("♯") || rest.starts_with("♭"))
            n += 3;
        else
            break;
    }
    const auto root = parsePitchClass(main.substr(0, n));
    const auto kind = parseKind(main.substr(n));
    if (!root || !kind)
        return std::nullopt;
    Chord chord = withTensions(*root, kind->first, kind->second);
    if (!after.empty())
    {
        const auto bass = parsePitchClass(after);
        if (!bass)
            return std::nullopt;
        if (*bass != chord.root)
            chord.bass = *bass;
    }
    return chord;
}

Chord keyChord(const Key* key, int pitchClass, ChordShape shape)
{
    pitchClass = mod12(pitchClass);
    const auto& k = keyOrC(key);
    const PitchSet keyNotes = notes(k);
    const auto* scale = scaleOf(k);

    if (key == nullptr || scaleOf(*key) == nullptr)
    {
        // As in C major, but only major and minor triads (the rule the Wavetable's Key Chord uses):
        // B is minor, and notes outside C major take the triad more of whose notes are in it.
        static constexpr std::array<int, 2> majorMinor{kMajor, kMinor};
        Chord triad = pitchClass == 11 ? Chord{.root = 11, .kind = kMinor}
                      : has(majorScale, pitchClass) ? keyChord(&k, pitchClass, ChordShape::Triad)
                                                    : mostInSet(pitchClass, keyNotes, majorMinor, false);
        if (shape == ChordShape::Seventh || shape == ChordShape::Ninth)
        {
            const bool minor = triad.kind == kMinor;
            if (pitchClass == 11)
                return {.root = 11, .kind = shape == ChordShape::Seventh ? kMin7 : kMin9};
            if (has(majorScale, pitchClass))
                return keyChord(&k, pitchClass, shape);
            return {.root = pitchClass,
                    .kind = shape == ChordShape::Seventh ? (minor ? kMin7 : kDom7) : (minor ? kMin9 : kDom9)};
        }
        return reshape(triad, shape);
    }

    if (!has(keyNotes, pitchClass))
    {
        // A note outside the key: the kind most of whose notes are in it.
        if (shape == ChordShape::Sus2 || shape == ChordShape::Sus4)
            return reshape({.root = pitchClass}, shape);
        static constexpr std::array<int, 3> triads{kMajor, kMinor, kDim};
        static constexpr std::array<int, 4> sevenths{kDom7, kMin7, kMaj7, kHalfDim7};
        static constexpr std::array<int, 3> ninths{kDom9, kMin9, kMaj9};
        if (shape == ChordShape::Seventh)
            return mostInSet(pitchClass, keyNotes, sevenths, false);
        if (shape == ChordShape::Ninth)
            return mostInSet(pitchClass, keyNotes, ninths, false);
        return reshape(mostInSet(pitchClass, keyNotes, triads, false), shape);
    }

    if (noteCount(scale->intervals) == 7)
    {
        // The scale's thirds: steps 0, 2, 4 (6, 8) from the chord's root.
        std::array<int, 14> pcs{};
        int found = 0;
        for (int i = 0; i < 24 && found < 14; ++i)
            if (has(keyNotes, pitchClass + i))
                pcs[static_cast<std::size_t>(found++)] = i;
        const int stack = shape == ChordShape::Seventh ? 4 : shape == ChordShape::Ninth ? 5 : 3;
        PitchSet intervals = 0;
        for (int t = 0; t < stack; ++t)
            intervals = static_cast<PitchSet>(intervals | (1u << mod12(pcs[static_cast<std::size_t>(t * 2)])));
        if (const auto fit = fitRoot(intervals); fit.cost < std::numeric_limits<int>::max())
        {
            const Chord chord{.root = pitchClass, .kind = fit.kind, .tensions = fit.tensions};
            return stack == 3 ? reshape(chord, shape) : chord;
        }
    }

    // Scales without seven notes (or whose thirds make no kind): the chord their notes make, by the
    // shape's kinds in order.
    auto triad = mostInSet(pitchClass, keyNotes, shapeKinds(ChordShape::Triad), true);
    if (triad.kind < 0)
        triad = mostInSet(pitchClass, keyNotes, std::array<int, 3>{kMajor, kMinor, kDim}, false);
    if (shape == ChordShape::Seventh || shape == ChordShape::Ninth)
    {
        auto seventh = mostInSet(pitchClass, keyNotes, shapeKinds(ChordShape::Seventh), true);
        if (shape == ChordShape::Ninth)
        {
            const auto ninth = mostInSet(pitchClass, keyNotes, shapeKinds(ChordShape::Ninth), true);
            if (ninth.kind >= 0)
                return ninth;
        }
        return seventh.kind >= 0 ? seventh : triad;
    }
    return reshape(triad, shape);
}

std::vector<Chord> keyChords(const Key& key, ChordShape shape)
{
    std::vector<Chord> out;
    const PitchSet keyNotes = notes(key);
    for (int i = 0; i < 12; ++i)
        if (has(keyNotes, key.root + i))
            out.push_back(keyChord(&key, key.root + i, shape));
    return out;
}

Chord harmonise(const Key* key, int pitchClass, ChordShape shape, const Chord* before)
{
    pitchClass = mod12(pitchClass);
    if (key == nullptr || scaleOf(*key) == nullptr || !contains(*key, pitchClass))
        return keyChord(key, pitchClass, shape);

    std::vector<Chord> candidates;
    for (const auto& c : keyChords(*key, shape))
        if (has(pitchClasses(c), pitchClass))
            candidates.push_back(c);
    if (candidates.empty())
        return keyChord(key, pitchClass, shape);

    auto withRoot = [&](int root) -> const Chord*
    {
        for (const auto& c : candidates)
            if (c.root == mod12(root))
                return &c;
        return nullptr;
    };
    if (before != nullptr)
        for (const auto& s : nextChords(*key, *before, shape))
            if (const auto* c = withRoot(s.chord.root))
                return *c;
    // The most usual chords first: I, IV, V, vi, ii, iii, vii° (in minor: i, iv, v, VI, III, VII).
    static constexpr std::array<int, 7> majorOrder{0, 3, 4, 5, 1, 2, 6};
    static constexpr std::array<int, 7> minorOrder{0, 3, 4, 5, 2, 6, 1};
    const auto steps = stepsOf(referenceScale(*key));
    for (int d : majorLike(*key) ? majorOrder : minorOrder)
        if (const auto* c = withRoot(key->root + steps[static_cast<std::size_t>(d)]))
            return *c;
    return candidates.front();
}

std::vector<ChordSuggestion> nextChords(const Key& key, const Chord& before, ChordShape shape)
{
    std::vector<ChordSuggestion> out;
    const auto* scale = scaleOf(key);
    if (scale == nullptr)
        return out;
    const PitchSet keyNotes = notes(key);
    const auto chords = keyChords(key, shape);
    const auto& kind = kindOf(before);

    auto onStep = [&](int degree) -> Chord
    {
        const auto steps = stepsOf(referenceScale(key));
        return keyChord(&key, key.root + steps[static_cast<std::size_t>(degree)], shape);
    };

    if (noteCount(scale->intervals) == 7)
    {
        // The usual moves between steps (§7.3): from each step, the steps most often next.
        static constexpr std::array<std::array<int, 4>, 7> majorMoves{{
            {3, 4, 5, 1}, // I: IV, V, vi, ii
            {4, 6, 0, 3}, // ii: V, vii°, I, IV
            {5, 3, 1, 0}, // iii: vi, IV, ii, I
            {4, 0, 1, 5}, // IV: V, I, ii, vi
            {0, 5, 3, 2}, // V: I, vi, IV, iii
            {1, 3, 4, 2}, // vi: ii, IV, V, iii
            {0, 2, 5, 4}, // vii°: I, iii, vi, V
        }};
        static constexpr std::array<std::array<int, 4>, 7> minorMoves{{
            {3, 5, 6, 4}, // i: iv, VI, VII, v
            {4, 0, 6, 2}, // ii°: v, i, VII, III
            {5, 3, 6, 0}, // III: VI, iv, VII, i
            {4, 0, 6, 2}, // iv: v, i, VII, III
            {0, 5, 3, 6}, // v: i, VI, iv, VII
            {6, 3, 2, 0}, // VI: VII, iv, III, i
            {2, 0, 5, 3}, // VII: III, i, VI, iv
        }};
        const auto [degree, alteration] = degreeOf(key, before.root);
        // Diatonic: its root and the triad over it are the key's.
        bool diatonic = alteration == 0;
        for (int i = 0; i < std::min(3, kind.toneCount) && diatonic; ++i)
            diatonic = has(keyNotes, before.root + kind.tones[static_cast<std::size_t>(i)]);
        if (diatonic)
        {
            const auto& moves = (majorLike(key) ? majorMoves : minorMoves)[static_cast<std::size_t>(degree)];
            for (int to : moves)
                addUnique(out, onStep(to), whyMove(degree, to), before);
            return out;
        }

        // Borrowed or secondary: a dominant resolves down a fifth; ♭VII and iv go home; ♭VI on to ♭VII.
        const int interval = mod12(before.root - key.root);
        const auto beforeTones = kindSet(kind);
        if (has(beforeTones, 4) && has(keyNotes, before.root + 5))
            addUnique(out, keyChord(&key, before.root + 5, shape), "A dominant resolves down a fifth", before);
        if (majorLike(key))
        {
            if (interval == 10 || (interval == 5 && kind.minor))
                addUnique(out, onStep(0), "Home: borrowed from the minor", before);
            if (interval == 8)
                addUnique(out, Chord{.root = mod12(key.root + 10), .kind = kMajor}, "On up a step, then home", before);
        }
        addUnique(out, onStep(0), "Home", before);
        addUnique(out, onStep(4), "To the dominant", before);
        addUnique(out, onStep(3), "To the subdominant", before);
        if (out.size() > 4)
            out.resize(4);
        return out;
    }

    // Other scales: their chords by how the root moves, strongest first.
    static constexpr std::array<std::pair<int, std::string_view>, 11> motions{{
        {5, "Down a fifth: the strongest move"},
        {7, "Up a fifth"},
        {2, "A step up"},
        {10, "A step down"},
        {9, "A third down"},
        {8, "A third down"},
        {3, "A third up"},
        {4, "A third up"},
        {1, "A step up"},
        {11, "A step down"},
        {6, "A tritone away"},
    }};
    for (const auto& [motion, why] : motions)
        for (const auto& c : chords)
            if (mod12(c.root - before.root) == motion)
                addUnique(out, c, why, before);
    if (out.size() > 4)
        out.resize(4);
    return out;
}

std::vector<ChordSuggestion> borrowedChords(const Key& key, ChordShape shape)
{
    std::vector<ChordSuggestion> out;
    if (scaleOf(key) == nullptr)
        return out;
    const PitchSet keyNotes = notes(key);
    const bool major = majorLike(key);
    const Key parallel{key.root, major ? "minor" : "major"};
    const auto steps = stepsOf(major ? minorScale : majorScale);
    const Chord none{.root = -1};
    auto add = [&](const Chord& c, std::string_view why)
    {
        if ((pitchClasses(c) & ~keyNotes & 0xfff) != 0) // not one the key already has
            addUnique(out, c, why, none);
    };
    if (major)
        for (int degree : {6, 3, 5, 2}) // ♭VII, iv, ♭VI, ♭III
            add(keyChord(&parallel, key.root + steps[static_cast<std::size_t>(degree)], shape),
                "From the parallel minor");
    else
        for (int degree : {3, 4}) // IV, V
            add(keyChord(&parallel, key.root + steps[static_cast<std::size_t>(degree)], shape),
                "From the parallel major");
    add(dominantOn(key.root + 2, shape), "The dominant of V");
    return out;
}

std::vector<int> voiceChord(const Chord& chord, const VoiceRequest& request)
{
    // The tones to voice: the chord's own, its bass first when it's one of them.
    auto tones = stackedTones(chord);
    std::optional<int> foreignBass;
    int bassIndex = -1;
    if (chord.bass)
    {
        for (std::size_t i = 0; i < tones.size(); ++i)
            if (mod12(tones[i]) == mod12(*chord.bass - chord.root))
                bassIndex = static_cast<int>(i);
        if (bassIndex < 0)
            foreignBass = mod12(*chord.bass);
    }
    const int wanted = request.count > 0 ? request.count - (foreignBass ? 1 : 0) : static_cast<int>(tones.size());
    if (wanted < static_cast<int>(tones.size()))
    {
        const int bassTone = bassIndex >= 0 ? tones[static_cast<std::size_t>(bassIndex)] : 0;
        omitTo(tones, wanted);
        if (bassIndex >= 0)
        {
            const auto it = std::find(tones.begin(), tones.end(), bassTone);
            bassIndex = it == tones.end() ? -1 : static_cast<int>(it - tones.begin());
        }
    }
    const int distinct = static_cast<int>(tones.size());
    const int doublings = std::max(0, wanted - distinct);

    const bool smooth = request.voicing == Voicing::Smooth && !request.before.empty();
    int onlyInversion = -1;
    if (request.inversion != Inversion::Auto)
        onlyInversion = std::min(static_cast<int>(request.inversion) - 1, distinct - 1);
    else if (bassIndex >= 0)
        onlyInversion = bassIndex;
    const double centre = smooth ? mean(request.before) : request.centre;

    std::vector<int> best;
    double bestCost = std::numeric_limits<double>::max();
    for (int inv = 0; inv < distinct; ++inv)
    {
        if (onlyInversion >= 0 && inv != onlyInversion)
            continue;
        // The inversion in close position: its bass, the rest above it.
        std::vector<int> shape;
        const int bass = tones[static_cast<std::size_t>(inv)];
        for (int i = 0; i < distinct; ++i)
        {
            int t = tones[static_cast<std::size_t>(i)];
            while (t < bass)
                t += 12;
            shape.push_back(t);
        }
        for (int j = 0; j < doublings; ++j)
            shape.push_back(shape[static_cast<std::size_t>(j % distinct)] + 12 * (1 + j / distinct));
        spreadUnique(shape);

        const auto n = shape.size();
        switch (request.voicing)
        {
        case Voicing::Open:
        case Voicing::Spread:
            if (n >= 3)
                shape[1] += 12;
            spreadUnique(shape);
            if (request.voicing == Voicing::Spread)
                shape[0] -= 12;
            break;
        case Voicing::Drop2:
        case Voicing::Drop3:
            if (n >= 4 && request.voicing == Voicing::Drop3)
                shape[n - 3] -= 12;
            else if (n >= 3)
                shape[n - 2] -= 12;
            break;
        case Voicing::Smooth:
        case Voicing::Close: break;
        }
        spreadUnique(shape);

        for (int octave = -2; octave <= 11; ++octave)
        {
            std::vector<int> pitches;
            const int base = chord.root + octave * 12;
            for (int t : shape)
                pitches.push_back(base + t);
            if (foreignBass)
            {
                int b = pitches.front() - 1;
                while (mod12(b) != *foreignBass)
                    --b;
                pitches.insert(pitches.begin(), b);
            }
            if (pitches.front() < 0 || pitches.back() > 127)
                continue;
            if (request.including &&
                std::find(pitches.begin(), pitches.end(), *request.including) == pitches.end())
                continue;
            if (request.top && pitches.back() != *request.top)
                continue;
            double cost = std::abs(mean(pitches) - centre);
            if (smooth)
                cost = voiceLeadingDistance(request.before, pitches) + 0.01 * cost;
            else if (onlyInversion < 0)
                cost += 1000.0 * inv; // Auto: root position wherever it can be
            cost += 0.001 * inv;
            if (cost < bestCost)
            {
                bestCost = cost;
                best = std::move(pitches);
            }
        }
    }
    return best;
}

double voiceLeadingDistance(std::span<const int> from, std::span<const int> to)
{
    if (from.empty() || to.empty())
        return 0.0;
    std::vector<int> a(from.begin(), from.end()), b(to.begin(), to.end());
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    double sum = 0.0;
    if (a.size() == b.size())
    {
        for (std::size_t i = 0; i < a.size(); ++i)
            sum += std::abs(a[i] - b[i]);
        return sum;
    }
    const auto& more = a.size() > b.size() ? a : b;
    const auto& fewer = a.size() > b.size() ? b : a;
    for (int p : more)
    {
        int nearest = std::numeric_limits<int>::max();
        for (int q : fewer)
            nearest = std::min(nearest, std::abs(p - q));
        sum += nearest;
    }
    return sum;
}
} // namespace fd::music
