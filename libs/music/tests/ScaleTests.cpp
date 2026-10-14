#include "fd/music/Scales.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

using namespace fd::music;

TEST_CASE("The scale IDs are the permanent list of docs/ids.md", "[fast]")
{
    // Stored in projects: never rename, reorder freely, add at will (docs/ids.md, ADR 0057).
    const std::vector<std::string> ids{"major",
                                       "minor",
                                       "harmonic-minor",
                                       "melodic-minor",
                                       "major-pentatonic",
                                       "minor-pentatonic",
                                       "blues",
                                       "major-blues",
                                       "dorian",
                                       "phrygian",
                                       "lydian",
                                       "mixolydian",
                                       "locrian",
                                       "harmonic-major",
                                       "dorian-sharp-4",
                                       "phrygian-dominant",
                                       "lydian-augmented",
                                       "lydian-dominant",
                                       "super-locrian",
                                       "hungarian-minor",
                                       "double-harmonic",
                                       "spanish-8-tone",
                                       "whole-tone",
                                       "diminished-half-whole",
                                       "diminished-whole-half",
                                       "messiaen-3",
                                       "messiaen-4",
                                       "messiaen-5",
                                       "messiaen-6",
                                       "messiaen-7",
                                       "hirajoshi",
                                       "in-sen",
                                       "iwato",
                                       "kumoi",
                                       "pelog-selisir",
                                       "pelog-tembung"};
    std::vector<std::string> catalogue;
    for (const auto& s : scales())
        catalogue.emplace_back(s.id);
    CHECK(catalogue == ids);
}

TEST_CASE("Every scale is whole and in a known group", "[fast]")
{
    const auto groups = scaleGroups();
    std::set<std::string> names;
    for (const auto& s : scales())
    {
        INFO(std::string(s.id));
        CHECK((s.intervals & 1u) != 0); // the root
        CHECK((s.intervals & ~0xfffu) == 0);
        CHECK(noteCount(s.intervals) >= 5);
        CHECK(!s.name.empty());
        CHECK(!s.shortName.empty());
        CHECK(std::find(groups.begin(), groups.end(), s.group) != groups.end());
        CHECK(names.insert(std::string(s.name)).second);
    }
    CHECK(groups.front() == "Common");
}

TEST_CASE("Scales hold the notes the design lists", "[fast]")
{
    auto set = [](std::initializer_list<int> steps)
    {
        PitchSet s = 0;
        for (int i : steps)
            s = static_cast<PitchSet>(s | (1u << i));
        return s;
    };
    CHECK(findScale("major")->intervals == set({0, 2, 4, 5, 7, 9, 11}));
    CHECK(findScale("minor")->intervals == set({0, 2, 3, 5, 7, 8, 10}));
    CHECK(findScale("blues")->intervals == set({0, 3, 5, 6, 7, 10}));
    CHECK(findScale("messiaen-7")->intervals == set({0, 1, 2, 3, 5, 6, 7, 8, 9, 11}));
    CHECK(noteCount(findScale("messiaen-7")->intervals) == 10);
    CHECK(findScale("pelog-tembung")->intervals == set({0, 1, 5, 7, 8}));
}

TEST_CASE("A scale is found by its ID, its name or its other name", "[fast]")
{
    CHECK(findScale("harmonic-minor")->name == "Harmonic minor");
    CHECK(findScale("Harmonic minor")->id == "harmonic-minor");
    CHECK(findScale("harmonic MINOR")->id == "harmonic-minor");
    CHECK(findScale("Bhairav")->id == "double-harmonic");
    CHECK(findScale("altered")->id == "super-locrian");
    CHECK(findScale("chromatic") == nullptr);
    CHECK(findScale("") == nullptr);
}
