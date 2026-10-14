#include "fd/music/Key.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace fd::music;

namespace
{
Key key(int root, const char* scale) { return Key{root, scale}; }

/// A spelled note's letter.
char letterOf(const std::string& name) { return name.front(); }

/// How many accidentals a spelled name carries (♯ and ♭ are 3 bytes each in UTF-8).
int accidentalsIn(const std::string& name) { return static_cast<int>((name.size() - 1) / 3); }
} // namespace

TEST_CASE("A key's notes are its scale counted up from its root", "[fast]")
{
    CHECK(notes(key(0, "major")) == notes(key(9, "minor"))); // relative keys share their notes
    CHECK(key(0, "major") != key(9, "minor"));                // but aren't the same key
    CHECK(contains(key(9, "minor"), 69));
    CHECK(contains(key(9, "minor"), 72 + 7));
    CHECK_FALSE(contains(key(9, "minor"), 68));
    CHECK(contains(key(9, "harmonic-minor"), 68));
    PitchSet bMajor = 0;
    for (int pc : {11, 1, 3, 4, 6, 8, 10}) // B C♯ D♯ E F♯ G♯ A♯
        bMajor = static_cast<PitchSet>(bMajor | (1u << pc));
    CHECK(notes(key(11, "major")) == bMajor);
    CHECK(notes(Key{0, "no-such-scale"}) == 0);
}

TEST_CASE("Every major and minor key uses each letter once", "[fast]")
{
    for (const char* scale : {"major", "minor"})
        for (int root = 0; root < 12; ++root)
        {
            const auto k = key(root, scale);
            INFO(keyName(k));
            const auto spelled = spelledNotes(k);
            REQUIRE(spelled.size() == 7);
            std::set<char> letters;
            for (const auto& n : spelled)
            {
                letters.insert(letterOf(n));
                CHECK(accidentalsIn(n) <= 1);
            }
            CHECK(letters.size() == 7);
        }
}

TEST_CASE("Roots on black keys take the name with fewer sharps or flats", "[fast]")
{
    CHECK(keyName(key(1, "major")) == "D♭ major");
    CHECK(keyName(key(1, "minor")) == "C♯ minor");
    CHECK(keyName(key(3, "major")) == "E♭ major");
    CHECK(keyName(key(3, "minor")) == "E♭ minor"); // a tie with D♯ minor: flats
    CHECK(keyName(key(6, "major")) == "F♯ major"); // a tie with G♭ major: sharps
    CHECK(keyName(key(6, "minor")) == "F♯ minor");
    CHECK(keyName(key(8, "major")) == "A♭ major");
    CHECK(keyName(key(8, "minor")) == "G♯ minor");
    CHECK(keyName(key(10, "major")) == "B♭ major");
    CHECK(keyName(key(10, "minor")) == "B♭ minor");
    CHECK(keyName(key(1, "dorian")) == "C♯ Dorian"); // from B major's five sharps
    CHECK(keyName(key(5, "locrian")) == "F Locrian");     // a natural root at a tie
}

TEST_CASE("Notes are spelled by the key", "[fast]")
{
    const auto fMajor = key(5, "major");
    CHECK(spelledNotes(fMajor) == std::vector<std::string>{"F", "G", "A", "B♭", "C", "D", "E"});
    CHECK(pitchClassName(&fMajor, 10) == "B♭");
    const auto eMajor = key(4, "major");
    CHECK(pitchClassName(&eMajor, 3) == "D♯");
    const auto fSharp = key(6, "major");
    CHECK(spelledNotes(fSharp)
          == std::vector<std::string>{"F♯", "G♯", "A♯", "B", "C♯", "D♯", "E♯"});
    const auto eFlatMinor = key(3, "minor");
    CHECK(spelledNotes(eFlatMinor).at(5) == "C♭");
    CHECK(spelledNotes(key(9, "minor")) == std::vector<std::string>{"A", "B", "C", "D", "E", "F", "G"});
    CHECK(spelledNotes(key(2, "harmonic-minor")).back() == "C♯");
    // Notes outside the key take its direction.
    CHECK(pitchClassName(&fMajor, 1) == "D♭");
    CHECK(pitchClassName(&eMajor, 10) == "A♯");
    // Without a key, sharps, as before.
    CHECK(pitchClassName(nullptr, 10) == "A♯");
}

TEST_CASE("No key needs a double sharp or flat", "[fast]")
{
    for (const auto& scale : scales())
        for (int root = 0; root < 12; ++root)
        {
            const Key k{root, std::string(scale.id)};
            for (int pc = 0; pc < 12; ++pc)
            {
                INFO(keyName(k) << ", pitch class " << pc);
                CHECK(accidentalsIn(pitchClassName(&k, pc)) <= 1);
            }
        }
    // G♯ harmonic minor's leading tone would be F𝄪: it's written G.
    CHECK(spelledNotes(key(8, "harmonic-minor")).back() == "G");
}

TEST_CASE("Note names carry the octave of their letter", "[fast]")
{
    CHECK(noteName(nullptr, 60) == "C3");
    CHECK(noteName(nullptr, 61) == "C♯3");
    CHECK(noteName(nullptr, 0) == "C-2");
    const auto fMajor = key(5, "major");
    CHECK(noteName(&fMajor, 70) == "B♭3");
    const auto eFlatMinor = key(3, "minor");
    CHECK(noteName(&eFlatMinor, 59) == "C♭3"); // B2 sounding, written in C3's octave
    const auto fSharp = key(6, "major");
    CHECK(noteName(&fSharp, 65) == "E♯3");
    CHECK(noteName(&fMajor, 70, Accidentals::Ascii) == "Bb3");
}

TEST_CASE("Keys are named for people and assistants", "[fast]")
{
    CHECK(keyName(key(9, "minor")) == "A minor");
    CHECK(keyName(key(0, "major-pentatonic")) == "C major pentatonic");
    CHECK(keyName(key(2, "dorian")) == "D Dorian");
    CHECK(keyName(key(6, "phrygian-dominant")) == "F♯ Phrygian dominant");
    CHECK(keyName(key(4, "hirajoshi")) == "E Hirajoshi");
    CHECK(keyShortName(key(9, "minor")) == "A min");
    CHECK(keyName(key(6, "major"), Accidentals::Ascii) == "F# major");
    CHECK(rootName(key(10, "minor"), Accidentals::Ascii) == "Bb");
    CHECK(keyName(Key{2, "no-such-scale"}) == "D");
    CHECK(usesFlats(key(5, "major")));
    CHECK_FALSE(usesFlats(key(7, "major")));
    CHECK_FALSE(usesFlats(key(0, "major")));
}

TEST_CASE("Note names are read back however they're written", "[fast]")
{
    CHECK(parsePitchClass("C") == 0);
    CHECK(parsePitchClass("c") == 0);
    CHECK(parsePitchClass("F#") == 6);
    CHECK(parsePitchClass("Gb") == 6);
    CHECK(parsePitchClass("G♭") == 6);
    CHECK(parsePitchClass("F♯") == 6);
    CHECK(parsePitchClass("e flat") == 3);
    CHECK(parsePitchClass(" A sharp ") == 10);
    CHECK(parsePitchClass("Bb") == 10);
    CHECK(parsePitchClass("Cb") == 11);
    CHECK(parsePitchClass("B#") == 0);
    CHECK_FALSE(parsePitchClass("H").has_value());
    CHECK_FALSE(parsePitchClass("").has_value());
    CHECK_FALSE(parsePitchClass("C major").has_value());
    // Every name the spelling writes reads back to its pitch class.
    for (const auto& scale : scales())
        for (int root = 0; root < 12; ++root)
        {
            const Key k{root, std::string(scale.id)};
            for (int pc = 0; pc < 12; ++pc)
            {
                CHECK(parsePitchClass(pitchClassName(&k, pc)) == pc);
                CHECK(parsePitchClass(pitchClassName(&k, pc, Accidentals::Ascii)) == pc);
            }
        }
}
