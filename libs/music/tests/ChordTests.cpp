#include "fd/music/Chords.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>
#include <vector>

using namespace fd::music;

namespace
{
const Key cMajor{0, "major"};
const Key aMinor{9, "minor"};

std::string name(std::vector<int> pitches, const Key* key = &cMajor)
{
    const auto chord = nameChord(pitches);
    return chord ? chordName(*chord, key) : std::string("–");
}

std::string roman(std::vector<int> pitches, const Key& key = cMajor)
{
    const auto chord = nameChord(pitches);
    return chord ? romanNumeral(*chord, key) : std::string("–");
}

std::vector<std::string> names(const std::vector<Chord>& chords, const Key* key)
{
    std::vector<std::string> out;
    for (const auto& c : chords)
        out.push_back(chordName(c, key));
    return out;
}

std::vector<std::string> names(const std::vector<ChordSuggestion>& suggestions, const Key* key)
{
    std::vector<std::string> out;
    for (const auto& s : suggestions)
        out.push_back(chordName(s.chord, key));
    return out;
}

Chord parsed(const char* text, const Key* key = &cMajor)
{
    const auto chord = parseChord(text, key);
    REQUIRE(chord);
    return *chord;
}
} // namespace

TEST_CASE("Every kind of chord is named from its notes, in any order and octave", "[fast]")
{
    for (int k = 0; k < static_cast<int>(chordKinds().size()); ++k)
        for (int root = 0; root < 12; ++root)
        {
            const Chord chord{.root = root, .kind = k};
            std::vector<int> pitches;
            for (int t : stackedTones(chord))
                pitches.push_back(48 + root + t);
            std::reverse(pitches.begin(), pitches.end()); // the order doesn't matter, the lowest does
            INFO(chordKinds()[static_cast<std::size_t>(k)].id << " on " << root);
            const auto named = nameChord(pitches);
            REQUIRE(named);
            // Kinds that are the same notes as another (C6 is Am7's notes) are read by their bass.
            CHECK(named->root == root);
            CHECK(named->bass == std::nullopt);
            CHECK(pitchClasses(*named) == pitchClasses(chord));
        }
}

TEST_CASE("Chord names", "[fast]")
{
    CHECK(name({57, 60, 64}) == "Am");
    CHECK(name({60, 64, 67, 71}) == "Cmaj7");
    CHECK(name({62, 67, 69}) == "Dsus4");
    CHECK(name({67, 71, 74, 77}) == "G7");
    CHECK(name({71, 74, 77}) == "B°");
    CHECK(name({71, 74, 77, 81}) == "Bm7♭5");
    CHECK(name({60, 63, 66, 69}) == "C°7");
    CHECK(name({60, 64, 68}) == "C+");
    CHECK(name({60, 64, 67, 74}) == "Cadd9");
    CHECK(name({67, 71, 74, 77, 80}) == "G7♭9");
    SECTION("Inversions are slash chords")
    {
        CHECK(name({64, 67, 72}) == "C/E");
        CHECK(name({55, 60, 64}) == "C/G");
        CHECK(name({53, 57, 60, 64}) == "Fmaj7");
        CHECK(name({57, 60, 65, 64}) == "Fmaj7/A");
        CHECK(name({62, 64, 67, 72}) == "C/D"); // read as an added ninth over D would be worse
    }
    SECTION("Notes that read two ways are read by their bass")
    {
        CHECK(name({60, 64, 67, 69}) == "C6");
        CHECK(name({57, 60, 64, 67}) == "Am7");
        CHECK(name({64, 69, 72, 79}) == "Am7/E");
    }
    SECTION("Two pitch classes, or a cluster no kind explains, have no name")
    {
        CHECK_FALSE(nameChord(std::vector<int>{60, 67, 72}));
        CHECK_FALSE(nameChord(std::vector<int>{60}));
        CHECK_FALSE(nameChord(std::vector<int>{60, 61, 62}));
        CHECK_FALSE(nameChord(std::vector<int>{}));
    }
    SECTION("Tensions beyond a kind are written in parentheses")
    {
        CHECK(name({64, 67, 71, 74, 77}) == "Em7(♭9)"); // C major's ninth chord on iii
        CHECK(name({60, 64, 70}) == "C7");              // without its fifth
    }
}

TEST_CASE("Chord roots and basses are spelled for the key", "[fast]")
{
    const Chord bFlat{.root = 10, .kind = 0};
    CHECK(chordName(bFlat, &cMajor) == "B♭");                   // ♭VII, not A♯
    CHECK(chordName(bFlat, nullptr) == "B♭");                   // as in C major
    CHECK(chordName(Chord{.root = 6, .kind = 2}, &cMajor) == "F♯°"); // ♯iv°
    CHECK(chordName(Chord{.root = 8, .kind = 0}, &aMinor) == "G♯"); // the raised seventh in minor
    CHECK(chordName(Chord{.root = 4, .kind = 0, .bass = 8}, &cMajor) == "E/G♯");
    const Key fMajor{5, "major"};
    CHECK(chordName(Chord{.root = 4, .kind = 6, .bass = 8}, &fMajor) == "E7/G♯"); // the third, not A♭
    const Key eFlatMinor{3, "minor"};
    CHECK(chordName(Chord{.root = 6, .kind = 0}, &eFlatMinor) == "G♭");
    CHECK(chordName(Chord{.root = 2, .kind = 0}, &eFlatMinor) == "D"); // its V
    CHECK(chordName(Chord{.root = 10, .kind = 8}, &cMajor, Accidentals::Ascii) == "Bbm7");
    CHECK(chordName(Chord{.root = 1, .kind = 10}, &cMajor) == "C♯°7"); // a diminished chord leads up
    CHECK(chordName(Chord{.root = 0, .kind = 0}, &cMajor, Accidentals::Ascii) == "C");
}

TEST_CASE("Roman numerals: the chord's step in the key", "[fast]")
{
    CHECK(roman({57, 60, 64}) == "vi");
    CHECK(roman({53, 57, 60}) == "IV");
    CHECK(roman({67, 71, 74, 77}) == "V7");
    CHECK(roman({62, 65, 69, 72}) == "ii7");
    CHECK(roman({71, 74, 77}) == "vii°");
    CHECK(roman({71, 74, 77, 81}) == "viiø7");
    CHECK(roman({60, 64, 67, 71}) == "Imaj7");
    CHECK(roman({64, 67, 72}) == "I"); // an inversion is the same step
    SECTION("Borrowed chords carry their accidental")
    {
        CHECK(roman({58, 62, 65}) == "♭VII");
        CHECK(roman({56, 60, 63}) == "♭VI");
        CHECK(roman({53, 56, 60}) == "iv");
        CHECK(roman({62, 66, 69}) == "II"); // V/V
    }
    SECTION("In a minor key")
    {
        CHECK(roman({57, 60, 64}, aMinor) == "i");
        CHECK(roman({53, 57, 60}, aMinor) == "VI");
        CHECK(roman({64, 68, 71}, aMinor) == "V");
        CHECK(roman({67, 71, 74, 77}, aMinor) == "VII7");
        CHECK(roman({68, 71, 74}, aMinor) == "♯vii°");
    }
    CHECK(romanNumeral(Chord{.root = 11, .kind = 9}, cMajor, Accidentals::Ascii) == "viim7b5");
    CHECK(romanNumeral(Chord{.root = 10, .kind = 0}, cMajor, Accidentals::Ascii) == "bVII");
    CHECK(romanNumeral(Chord{}, Key{0, "no-such-scale"}).empty());
}

TEST_CASE("Chords are read from names and Roman numerals", "[fast]")
{
    CHECK(parsed("Dm7") == Chord{.root = 2, .kind = 8});
    CHECK(parsed("C/E") == Chord{.root = 0, .kind = 0, .bass = 4});
    CHECK(parsed("Bbmaj7") == Chord{.root = 10, .kind = 7});
    CHECK(parsed("F#m7b5") == Chord{.root = 6, .kind = 9});
    CHECK(parsed("F♯m7♭5") == Chord{.root = 6, .kind = 9});
    CHECK(parsed("Cmin") == Chord{.root = 0, .kind = 1});
    CHECK(parsed("C6/9") == Chord{.root = 0, .kind = 22});
    CHECK(parsed("G7(b9)") == Chord{.root = 7, .kind = 23});
    CHECK(parsed("Em7(b9)") == Chord{.root = 4, .kind = 8, .tensions = 1u << 1});
    CHECK(parsed("ii7") == Chord{.root = 2, .kind = 8});
    CHECK(parsed("V") == Chord{.root = 7, .kind = 0});
    CHECK(parsed("bVII") == Chord{.root = 10, .kind = 0});
    CHECK(parsed("viiø7") == Chord{.root = 11, .kind = 9});
    CHECK(parsed("vii°") == Chord{.root = 11, .kind = 2});
    CHECK(parsed("V7/V") == Chord{.root = 2, .kind = 6});
    CHECK(parsed("i", &aMinor) == Chord{.root = 9, .kind = 1});
    CHECK(parsed("bm") == Chord{.root = 11, .kind = 1});
    CHECK_FALSE(parseChord("ii", nullptr));
    CHECK_FALSE(parseChord("H7", &cMajor));
    CHECK_FALSE(parseChord("Cfoo", &cMajor));
    CHECK_FALSE(parseChord("", &cMajor));
    SECTION("Names round-trip")
    {
        for (int k = 0; k < static_cast<int>(chordKinds().size()); ++k)
            for (int root = 0; root < 12; ++root)
            {
                const Chord chord{.root = root, .kind = k};
                for (auto style : {Accidentals::Glyphs, Accidentals::Ascii})
                {
                    INFO(chordName(chord, &cMajor, style));
                    CHECK(parseChord(chordName(chord, &cMajor, style), &cMajor) == chord);
                    CHECK(parseChord(romanNumeral(chord, cMajor, style), &cMajor) == chord);
                }
            }
    }
}

TEST_CASE("The key's chords on each step", "[fast]")
{
    CHECK(names(keyChords(cMajor), &cMajor) == std::vector<std::string>{"C", "Dm", "Em", "F", "G", "Am", "B°"});
    CHECK(names(keyChords(aMinor), &aMinor) == std::vector<std::string>{"Am", "B°", "C", "Dm", "Em", "F", "G"});
    CHECK(names(keyChords(cMajor, ChordShape::Seventh), &cMajor) ==
          std::vector<std::string>{"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7♭5"});
    CHECK(names(keyChords(cMajor, ChordShape::Ninth), &cMajor) ==
          std::vector<std::string>{"Cmaj9", "Dm9", "Em7(♭9)", "Fmaj9", "G9", "Am9", "Bm7♭5(♭9)"});
    CHECK(names(keyChords(cMajor, ChordShape::Sus4), &cMajor) ==
          std::vector<std::string>{"Csus4", "Dsus4", "Esus4", "Fsus4", "Gsus4", "Asus4", "Bsus4"});
    CHECK(names(keyChords(cMajor, ChordShape::Add9), &cMajor) ==
          std::vector<std::string>{"Cadd9", "Dm(add9)", "Em(add9)", "Fadd9", "Gadd9", "Am(add9)", "B°(add9)"});
    const Key harmonic{9, "harmonic-minor"};
    CHECK(names(keyChords(harmonic), &harmonic) == std::vector<std::string>{"Am", "B°", "C+", "Dm", "E", "F", "G♯°"});
    CHECK(names(keyChords(harmonic, ChordShape::Seventh), &harmonic)[0] == "Am(maj7)");
    SECTION("Scales without seven notes: the chords their notes make")
    {
        const Key pentatonic{0, "major-pentatonic"};
        CHECK(names(keyChords(pentatonic), &pentatonic) == std::vector<std::string>{"C", "Dsus4", "Em", "Gsus4", "Am"});
        const Key wholeTone{0, "whole-tone"};
        for (const auto& c : keyChords(wholeTone))
            CHECK(kindOf(c).id == "aug");
    }
    SECTION("Every step of every scale has a chord on it, for every shape")
    {
        for (const auto& scale : scales())
            for (int root = 0; root < 12; ++root)
            {
                const Key key{root, std::string(scale.id)};
                for (auto shape : {ChordShape::Triad, ChordShape::Seventh, ChordShape::Ninth, ChordShape::Sus2,
                                   ChordShape::Sus4, ChordShape::Add9})
                {
                    const auto chords = keyChords(key, shape);
                    REQUIRE(static_cast<int>(chords.size()) == noteCount(notes(key)));
                    for (const auto& c : chords)
                    {
                        INFO(keyName(key) << ": " << chordName(c, &key));
                        CHECK(contains(key, c.root));
                        CHECK(noteCount(pitchClasses(c)) >= 3);
                        CHECK(!chordName(c, &key).empty());
                        CHECK(!romanNumeral(c, key).empty());
                    }
                }
            }
    }
}

TEST_CASE("A chord on a note outside the key, or without a key", "[fast]")
{
    CHECK(chordName(keyChord(&cMajor, 10), &cMajor) == "B♭"); // B♭ D F: two of its notes are C major's
    CHECK(chordName(keyChord(&cMajor, 6), &cMajor) == "F♯°");
    CHECK(chordName(keyChord(&cMajor, 10, ChordShape::Seventh), &cMajor) == "B♭maj7"); // A is C major's
    SECTION("Without a key: a major or minor triad by the note's place in C major")
    {
        CHECK(chordName(keyChord(nullptr, 0), nullptr) == "C");
        CHECK(chordName(keyChord(nullptr, 2), nullptr) == "Dm");
        CHECK(chordName(keyChord(nullptr, 11), nullptr) == "Bm");
        CHECK(chordName(keyChord(nullptr, 3), nullptr) == "E♭");
        CHECK(chordName(keyChord(nullptr, 6), nullptr) == "F♯m");
        CHECK(chordName(keyChord(nullptr, 7, ChordShape::Seventh), nullptr) == "G7");
    }
}

TEST_CASE("Harmonising a note: the key's chord with it", "[fast]")
{
    auto under = [](int pc, const Chord* before = nullptr)
    { return chordName(harmonise(&cMajor, pc, ChordShape::Triad, before), &cMajor); };
    CHECK(under(4) == "C");  // E: I has it
    CHECK(under(2) == "G");  // D: V
    CHECK(under(9) == "F");  // A: IV
    CHECK(under(11) == "G"); // B: V
    const Chord g{.root = 7, .kind = 0};
    CHECK(under(4, &g) == "C");  // after V, I has E
    CHECK(under(0, &g) == "C");
    CHECK(under(9, &g) == "Am"); // after V, the deceptive vi has A before IV does
    CHECK(under(1) == "C♯°");    // outside the key: the chord on it
}

TEST_CASE("Suggestions after a chord", "[fast]")
{
    const auto afterV = nextChords(cMajor, Chord{.root = 7, .kind = 0});
    REQUIRE(!afterV.empty());
    CHECK(chordName(afterV.front().chord, &cMajor) == "C");
    CHECK(afterV.front().why == "Home: a cadence");
    CHECK(names(afterV, &cMajor) == std::vector<std::string>{"C", "Am", "F", "Em"});
    CHECK(afterV[1].why == "Deceptive: the dominant not going home");
    CHECK(names(nextChords(cMajor, Chord{.root = 2, .kind = 1}), &cMajor).front() == "G"); // ii to V
    CHECK(names(nextChords(cMajor, Chord{.root = 5, .kind = 0}), &cMajor) ==
          std::vector<std::string>{"G", "C", "Dm", "Am"});
    CHECK(names(nextChords(aMinor, Chord{.root = 9, .kind = 1}), &aMinor) ==
          std::vector<std::string>{"Dm", "F", "G", "Em"});
    CHECK(names(nextChords(cMajor, Chord{.root = 7, .kind = 6}, ChordShape::Seventh), &cMajor).front() == "Cmaj7");
    SECTION("A borrowed or secondary chord resolves")
    {
        CHECK(names(nextChords(cMajor, Chord{.root = 2, .kind = 6}), &cMajor).front() == "G"); // V/V to V
        CHECK(names(nextChords(cMajor, Chord{.root = 10, .kind = 0}), &cMajor).front() == "C"); // ♭VII home
        CHECK(names(nextChords(cMajor, Chord{.root = 5, .kind = 1}), &cMajor).front() == "C");  // iv home
        CHECK(names(nextChords(cMajor, Chord{.root = 8, .kind = 0}), &cMajor).front() == "B♭"); // ♭VI on to ♭VII
        CHECK(names(nextChords(aMinor, Chord{.root = 4, .kind = 0}), &aMinor).front() == "Am"); // V home
    }
    SECTION("Never the chord before, never twice")
    {
        for (const auto& scale : scales())
        {
            const Key key{2, std::string(scale.id)};
            for (const auto& before : keyChords(key))
            {
                const auto next = nextChords(key, before);
                CHECK(!next.empty());
                CHECK(next.size() <= 4);
                for (std::size_t i = 0; i < next.size(); ++i)
                {
                    CHECK(!(next[i].chord.root == before.root && next[i].chord.kind == before.kind));
                    CHECK(!next[i].why.empty());
                    for (std::size_t j = 0; j < i; ++j)
                        CHECK(!(next[i].chord == next[j].chord));
                }
            }
        }
    }
}

TEST_CASE("Borrowed chords", "[fast]")
{
    CHECK(names(borrowedChords(cMajor), &cMajor) == std::vector<std::string>{"B♭", "Fm", "A♭", "E♭", "D7"});
    CHECK(names(borrowedChords(aMinor), &aMinor) == std::vector<std::string>{"D", "E", "B7"});
    CHECK(borrowedChords(cMajor).front().why == "From the parallel minor");
    CHECK(borrowedChords(cMajor).back().why == "The dominant of V");
    const Key mixolydian{7, "mixolydian"};
    for (const auto& s : borrowedChords(mixolydian)) // its ♭VII (F) is its own
        CHECK(chordName(s.chord, &mixolydian) != "F");
    const Key harmonic{9, "harmonic-minor"};
    CHECK(names(borrowedChords(harmonic), &harmonic) == std::vector<std::string>{"D", "B7"}); // V is its own
}

TEST_CASE("Voicings and inversions", "[fast]")
{
    const Chord c{.root = 0, .kind = 0};
    const Chord cmaj7{.root = 0, .kind = 7};
    auto voice = [](const Chord& chord, Voicing v, Inversion i = Inversion::Auto, double centre = 64.0)
    { return voiceChord(chord, {.voicing = v, .inversion = i, .centre = centre}); };
    CHECK(voice(c, Voicing::Close) == std::vector<int>{60, 64, 67});
    CHECK(voice(c, Voicing::Close, Inversion::First) == std::vector<int>{64, 67, 72});
    CHECK(voice(c, Voicing::Close, Inversion::Second) == std::vector<int>{55, 60, 64});
    CHECK(voice(c, Voicing::Close, Inversion::Third) == std::vector<int>{55, 60, 64}); // a triad's last
    CHECK(voice(cmaj7, Voicing::Close) == std::vector<int>{60, 64, 67, 71});
    CHECK(voice(cmaj7, Voicing::Open) == std::vector<int>{60, 67, 71, 76});
    CHECK(voice(cmaj7, Voicing::Open, Inversion::Auto, 70.0) == std::vector<int>{60, 67, 71, 76});
    CHECK(voice(cmaj7, Voicing::Drop2) == std::vector<int>{55, 60, 64, 71});
    CHECK(voice(cmaj7, Voicing::Drop3) == std::vector<int>{52, 60, 67, 71});
    CHECK(voice(c, Voicing::Spread, Inversion::Auto, 60.0) == std::vector<int>{48, 67, 76});
    SECTION("The chord's bass, when it's one of its tones, is its inversion")
    {
        CHECK(voiceChord(Chord{.root = 0, .kind = 0, .bass = 4}, {.voicing = Voicing::Close}) ==
              std::vector<int>{64, 67, 72});
    }
    SECTION("A bass outside the chord goes under it")
    {
        const auto v = voiceChord(Chord{.root = 0, .kind = 0, .bass = 2}, {.voicing = Voicing::Close});
        REQUIRE(v.size() == 4);
        CHECK(v[0] % 12 == 2);
        CHECK(v[1] - v[0] < 12);
    }
    SECTION("More notes double, fewer omit the fifth first")
    {
        CHECK(voiceChord(c, {.voicing = Voicing::Close, .centre = 66.0, .count = 4}) == std::vector<int>{60, 64, 67, 72});
        CHECK(voiceChord(cmaj7, {.voicing = Voicing::Close, .count = 3}) == std::vector<int>{60, 64, 71});
        CHECK(voiceChord(Chord{.root = 0, .kind = 19}, {.voicing = Voicing::Close, .count = 4}) ==
              std::vector<int>{60, 64, 70, 74});
    }
    SECTION("A pitch it must have, or have on top")
    {
        CHECK(voiceChord(c, {.voicing = Voicing::Close, .including = 48}) == std::vector<int>{48, 52, 55});
        CHECK(voiceChord(c, {.voicing = Voicing::Close, .top = 64}) == std::vector<int>{55, 60, 64});
        CHECK(voiceChord(c, {.voicing = Voicing::Close, .top = 67}) == std::vector<int>{60, 64, 67});
        CHECK(voiceChord(c, {.voicing = Voicing::Close, .including = 61}).empty());
    }
    SECTION("Every voicing of every kind keeps its notes and stays in range")
    {
        for (int k = 0; k < static_cast<int>(chordKinds().size()); ++k)
            for (auto v : {Voicing::Smooth, Voicing::Close, Voicing::Open, Voicing::Drop2, Voicing::Drop3, Voicing::Spread})
                for (double centre : {10.0, 64.0, 120.0})
                {
                    const Chord chord{.root = 5, .kind = k};
                    const auto pitches = voiceChord(chord, {.voicing = v, .centre = centre});
                    REQUIRE(pitches.size() == stackedTones(chord).size());
                    CHECK(std::is_sorted(pitches.begin(), pitches.end()));
                    CHECK(std::adjacent_find(pitches.begin(), pitches.end()) == pitches.end());
                    CHECK(pitches.front() >= 0);
                    CHECK(pitches.back() <= 127);
                    PitchSet set = 0;
                    for (int p : pitches)
                        set = static_cast<PitchSet>(set | (1u << (p % 12)));
                    CHECK(set == pitchClasses(chord));
                }
    }
}

TEST_CASE("Smooth voicing moves least from the chord before", "[fast]")
{
    const std::vector<int> c{60, 64, 67};
    // C to F: C stays, E up to F, G up to A: F/C.
    CHECK(voiceChord(Chord{.root = 5, .kind = 0}, {.before = c}) == std::vector<int>{60, 65, 69});
    // C to G: G stays, C down to B, E down to D: G/B.
    CHECK(voiceChord(Chord{.root = 7, .kind = 0}, {.before = c}) == std::vector<int>{59, 62, 67});
    // C to Am: A stays near: C E A.
    CHECK(voiceChord(Chord{.root = 9, .kind = 1}, {.before = c}) == std::vector<int>{60, 64, 69});

    SECTION("Over progressions, no other voicing moves less")
    {
        const std::vector<std::vector<Chord>> progressions{
            {{0, 0}, {9, 1}, {5, 0}, {7, 0}},              // I vi IV V
            {{2, 8}, {7, 6}, {0, 7}, {9, 8}},              // ii7 V7 Imaj7 vi7
            {{9, 1}, {5, 0}, {0, 0}, {7, 0}, {4, 0}},      // i VI III VII V in A minor
            {{0, 0}, {10, 0}, {5, 0}, {0, 0}},             // I ♭VII IV I
        };
        for (const auto& progression : progressions)
        {
            std::vector<int> before = voiceChord(progression.front(), {.voicing = Voicing::Close});
            for (std::size_t i = 1; i < progression.size(); ++i)
            {
                const auto smooth = voiceChord(progression[i], {.before = before});
                const double moved = voiceLeadingDistance(before, smooth);
                for (auto inversion : {Inversion::Root, Inversion::First, Inversion::Second, Inversion::Third})
                    for (double centre = 40.0; centre <= 90.0; centre += 1.0)
                    {
                        const auto other = voiceChord(progression[i], {.voicing = Voicing::Close,
                                                                        .inversion = inversion,
                                                                        .centre = centre});
                        CHECK(moved <= voiceLeadingDistance(before, other));
                    }
                CHECK(std::abs(smooth.front() - before.front()) <= 12); // it doesn't wander
                before = smooth;
            }
        }
    }
    SECTION("The note clicked stays")
    {
        const auto f = voiceChord(Chord{.root = 5, .kind = 0}, {.before = c, .including = 53});
        CHECK(std::find(f.begin(), f.end(), 53) != f.end());
        const auto under = voiceChord(Chord{.root = 5, .kind = 0}, {.before = c, .top = 72});
        CHECK(under.back() == 72);
    }
}

TEST_CASE("Voice-leading distance", "[fast]")
{
    CHECK(voiceLeadingDistance(std::vector<int>{60, 64, 67}, std::vector<int>{60, 65, 69}) == 3.0);
    CHECK(voiceLeadingDistance(std::vector<int>{60, 64, 67}, std::vector<int>{60, 64, 67, 72}) == 5.0);
    CHECK(voiceLeadingDistance(std::vector<int>{}, std::vector<int>{60}) == 0.0);
}
