#include "fd/music/Arpeggio.h"
#include "fd/music/Following.h"
#include "fd/music/Key.h"

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

using namespace fd::music;

namespace
{
const PitchSet cMajor = notes(Key{.root = 0, .scale = "major"});
const PitchSet aMinor = notes(Key{.root = 9, .scale = "minor"});

std::vector<int> voiced(int played, int root, PitchSet set, Place place, Voicing voicing,
                        const std::vector<int>& previous = {})
{
    std::array<int, 16> out{};
    const auto n = voiceAround(played, root, set, place, voicing, previous, out);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n)};
}
} // namespace

TEST_CASE("following: steps of a key, and a note outside it keeps its offset", "[fast]")
{
    CHECK(stepBy(60, cMajor, 2) == 64); // C → E
    CHECK(stepBy(64, cMajor, 1) == 65); // E → F
    CHECK(stepBy(60, cMajor, -1) == 59);
    CHECK(stepBy(60, cMajor, 7) == 72);
    CHECK(stepBy(61, cMajor, 1) == 63); // C♯ moves as C does, a semitone above D
    CHECK(stepBy(60, 0, 5) == 65);      // no set: semitones
}

TEST_CASE("following: fit to a set, up, down or nearest", "[fast]")
{
    const PitchSet cMaj7 = rotated(0b100010010001, 0); // C E G B
    CHECK(fitTo(62, cMaj7, Toward::Near) == 60);       // D: C and E equally near, down
    CHECK(fitTo(62, cMaj7, Toward::Near, 1) == 64);
    CHECK(fitTo(62, cMaj7, Toward::Up) == 64);
    CHECK(fitTo(65, cMaj7, Toward::Near) == 64); // F → E
    CHECK(fitTo(66, cMaj7, Toward::Near) == 67); // F♯ → G
    CHECK(fitTo(67, cMaj7, Toward::Down) == 67); // already in
    CHECK(fitTo(61, cMajor, Toward::Near) == 60);
}

TEST_CASE("following: mirror in the key, around an axis", "[fast]")
{
    CHECK(mirror(64, 60, cMajor) == 57); // E, two steps over C → A, two under
    CHECK(mirror(57, 60, cMajor) == 64);
    CHECK(mirror(60, 60, cMajor) == 60);
    CHECK(mirror(67, 60, 0) == 53); // chromatic
    CHECK(mirror(mirror(71, 64, aMinor), 64, aMinor) == 71);
}

TEST_CASE("following: the key's chords stack thirds of the key", "[fast]")
{
    std::array<int, 5> out{};
    REQUIRE(stackThirds(62, cMajor, 4, out) == 4);
    CHECK(out[0] == 62);
    CHECK(out[1] == 65);
    CHECK(out[2] == 69);
    CHECK(out[3] == 72); // Dm7
    CHECK(onBeat(3.0));
    CHECK(onBeat(2.99));
    CHECK_FALSE(onBeat(2.5));
    CHECK_FALSE(onBeat(2.25));
}

TEST_CASE("following: a chord voiced around the note played", "[fast]")
{
    const PitchSet aMin7 = rotated(0b010010001001, 9); // A C E G
    SECTION("the note on top, the chord under it")
    {
        const auto v = voiced(64, 9, aMin7, Place::Top, Voicing::Close);
        REQUIRE(!v.empty());
        CHECK(v.back() == 64);
        CHECK(v.size() == 4);
        CHECK(v.front() > 64 - 12);
    }
    SECTION("the note at the bottom")
    {
        const auto v = voiced(57, 9, aMin7, Place::Bottom, Voicing::Close);
        CHECK(v.front() == 57);
        CHECK(v == std::vector<int>{57, 60, 64, 67});
    }
    SECTION("smooth: the voicing nearest the chord before")
    {
        const PitchSet fMaj7 = rotated(0b100010010001, 5); // F A C E
        const std::vector<int> before{57, 60, 64, 67};     // Am7
        const auto v = voiced(53, 5, fMaj7, Place::Smooth, Voicing::Close, before);
        CHECK(std::set<int>(v.begin(), v.end()).contains(53)); // the note played sounds
        CHECK(movement(before, v) <= 8);
    }
    SECTION("a note outside the chord still sounds, the chord under it")
    {
        const auto v = voiced(62, 9, aMin7, Place::Top, Voicing::Close);
        CHECK(v.back() == 62);
    }
    SECTION("drop 2 and spread move notes, never the one played")
    {
        for (const auto voicing : {Voicing::Open, Voicing::Drop2, Voicing::Spread})
        {
            const auto v = voiced(64, 9, aMin7, Place::Top, voicing);
            CHECK(std::set<int>(v.begin(), v.end()).contains(64));
            CHECK(v.back() - v.front() > 7);
        }
    }
}

TEST_CASE("following: an arpeggio's orders", "[fast]")
{
    const auto take = [](ArpStyle style, int count, int steps)
    {
        std::vector<int> out;
        for (int n = 0; n < steps; ++n)
            out.push_back(arpIndex(style, count, n, 42));
        return out;
    };
    CHECK(take(ArpStyle::Up, 3, 6) == std::vector<int>{0, 1, 2, 0, 1, 2});
    CHECK(take(ArpStyle::Down, 3, 4) == std::vector<int>{2, 1, 0, 2});
    CHECK(take(ArpStyle::UpDown, 4, 8) == std::vector<int>{0, 1, 2, 3, 2, 1, 0, 1});
    CHECK(take(ArpStyle::DownUp, 3, 5) == std::vector<int>{2, 1, 0, 1, 2});
    CHECK(take(ArpStyle::Converge, 5, 5) == std::vector<int>{0, 4, 1, 3, 2});
    CHECK(take(ArpStyle::Diverge, 5, 5) == std::vector<int>{2, 3, 1, 4, 0});
    CHECK(arpIndex(ArpStyle::Chord, 3, 7, 1) == -1);
    // Shuffled: every note once a round, the same order every round and for a jump.
    const auto shuffled = take(ArpStyle::Shuffled, 5, 10);
    CHECK(std::set<int>(shuffled.begin(), shuffled.begin() + 5) == std::set<int>{0, 1, 2, 3, 4});
    CHECK(std::vector<int>(shuffled.begin(), shuffled.begin() + 5) ==
          std::vector<int>(shuffled.begin() + 5, shuffled.end()));
    // Random: the same at a step whenever it's asked, other rolls another.
    CHECK(arpIndex(ArpStyle::Random, 7, 1234, 9) == arpIndex(ArpStyle::Random, 7, 1234, 9));
    int differs = 0;
    for (int n = 0; n < 32; ++n)
        differs += arpIndex(ArpStyle::Random, 7, n, 9) != arpIndex(ArpStyle::Random, 7, n, 10) ? 1 : 0;
    CHECK(differs > 10);
}

TEST_CASE("following: a chord voiced near a note, led from the last", "[fast]")
{
    std::array<int, 8> out{};
    const PitchSet dMinor = rotated(0b000010001001, 2);
    const auto n = voiceNear(2, dMinor, 60, Voicing::Close, {}, out);
    REQUIRE(n == 3);
    CHECK(std::abs((out[0] + out[2]) / 2 - 60) <= 7);
    // G major after C major (C E G around C3): the nearest inversion, a common tone kept.
    const std::vector<int> before{60, 64, 67};
    const auto g = voiceNear(7, rotated(0b000010010001, 7), 60, Voicing::Close, before, out);
    const std::vector<int> led(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(g));
    CHECK(std::find(led.begin(), led.end(), 67) != led.end());
    CHECK(movement(before, led) <= 3);
}

TEST_CASE("following: a chord of a kind, from the chord and then the key", "[fast]")
{
    const PitchSet dMinor = rotated(0b000010001001, 2);
    CHECK(chordOfKind(2, dMinor, cMajor, 3) == dMinor);
    CHECK(chordOfKind(2, dMinor, cMajor, 4) == (dMinor | (1u << 0)));             // + C
    CHECK(chordOfKind(2, dMinor, cMajor, 5) == (dMinor | (1u << 0) | (1u << 4))); // + C, E
    const PitchSet dm7 = rotated(0b010010001001, 2);
    CHECK(chordOfKind(2, dm7, cMajor, 3) == dMinor); // a triad leaves its 7th out
    const PitchSet sus4 = rotated(0b000010100001, 7);
    CHECK(chordOfKind(7, sus4, cMajor, 3) == sus4); // a sus chord keeps its 4th
    CHECK(chordOfKind(7, rotated(0b000010000001, 7), cMajor, 3) ==
          rotated(0b000010010001, 7)); // a fifth: the key's third
}

TEST_CASE("following: Pitch's note, moved then fitted and kept in range", "[fast]")
{
    CHECK(pitchMoved(60, {.steps = 2}, cMajor, 0) == 64);
    CHECK(pitchMoved(64, {.mirror = true, .axis = 60}, cMajor, 0) == 57);
    // Chance 1: always moves, by 1 to Spread steps, the way it's told.
    for (std::uint64_t d = 0; d < 50; ++d)
    {
        const int up = pitchMoved(60, {.chance = 1.0, .spread = 2, .way = 0}, cMajor, mix(1, d));
        CHECK((up == 62 || up == 64));
        const int down = pitchMoved(60, {.chance = 1.0, .spread = 1, .way = 1}, cMajor, mix(1, d));
        CHECK(down == 59);
    }
    CHECK(pitchMoved(60, {.chance = 0.0}, cMajor, 12345) == 60);
    const PitchSet cTriad = rotated(0b000010010001, 0);
    CHECK(pitchFitted(62, {.fit = 2}, cMajor, cTriad, true, -1) == 60);
    CHECK(pitchFitted(62, {.fit = 2}, cMajor, cTriad, false, -1) == 62); // passing, off the beat
    CHECK(pitchFitted(62, {.fit = 2, .passing = false}, cMajor, cTriad, false, 1) == 64);
    CHECK(pitchFitted(30, {.lowest = 48, .highest = 72}, cMajor, 0, true, -1) == 54);
    CHECK(pitchFitted(30, {.lowest = 48, .highest = 72, .outside = 1}, cMajor, 0, true, -1) == -1);
    CHECK(pitchFitted(80, {.lowest = 48, .highest = 70, .outside = 2}, cMajor, 0, true, -1) == 69);
    CHECK(pitchFitted(61, {.lowest = 60, .highest = 62, .outside = 0}, cMajor, 0, true, -1) == 61);
    CHECK(pitchFitted(70, {.lowest = 60, .highest = 62, .outside = 0}, cMajor, 0, true, -1) ==
          62); // narrow: the edge
}
