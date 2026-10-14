#include "fd/music/Detect.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::music;

namespace
{
void add(std::vector<DetectNote>& notes, std::initializer_list<int> pitches, double length)
{
    for (int p : pitches)
        notes.push_back({p, length});
}
} // namespace

TEST_CASE("A C major melody with a cadence is found in C major", "[fast]")
{
    std::vector<DetectNote> notes;
    add(notes, {60, 62, 64, 65, 67, 69, 71, 72}, 1.0); // the scale
    add(notes, {60, 64, 67}, 2.0);                     // I
    add(notes, {65, 69, 72}, 2.0);                     // IV
    add(notes, {67, 71, 74}, 2.0);                     // V
    add(notes, {48, 60, 64, 67}, 4.0);                 // I
    const auto keys = detectKeys(notes);
    REQUIRE(!keys.empty());
    CHECK(keys.front().key == Key{0, "major"});
    CHECK(keys.size() <= 3);
    CHECK(keys.front().fit > 0.5);
    CHECK(keys.front().fit <= 1.0);
    for (size_t i = 1; i < keys.size(); ++i)
        CHECK(keys[i - 1].fit >= keys[i].fit);
}

TEST_CASE("An A minor phrase with its raised seventh is found in A minor", "[fast]")
{
    std::vector<DetectNote> notes;
    add(notes, {69, 71, 72, 74, 76, 77, 80, 81}, 1.0); // A harmonic minor
    add(notes, {57, 69, 72, 76}, 2.0);                 // i
    add(notes, {64, 68, 71, 74}, 2.0);                 // V7
    add(notes, {57, 69, 72, 76}, 4.0);                 // i
    const auto keys = detectKeys(notes);
    REQUIRE(!keys.empty());
    CHECK(keys.front().key == Key{9, "minor"});
}

TEST_CASE("Too few notes to judge find nothing", "[fast]")
{
    std::vector<DetectNote> few;
    add(few, {60, 62, 64, 65, 67, 69, 71}, 1.0); // seven notes
    CHECK(detectKeys(few).empty());

    std::vector<DetectNote> narrow;
    add(narrow, {60, 64, 67, 72, 60, 64, 67, 72, 60, 64}, 1.0); // ten notes, three pitch classes
    CHECK(detectKeys(narrow).empty());

    std::vector<DetectNote> silent;
    add(silent, {60, 62, 64, 65, 67, 69, 71, 72}, 0.0);
    CHECK(detectKeys(silent).empty());
}

TEST_CASE("Detection returns at most the number asked for", "[fast]")
{
    std::vector<DetectNote> notes;
    add(notes, {60, 62, 64, 65, 67, 69, 71, 72}, 1.0);
    CHECK(detectKeys(notes, 1).size() == 1);
    CHECK(detectKeys(notes, 0).empty());
    CHECK(detectKeys(notes, 5).size() == 5);
}
