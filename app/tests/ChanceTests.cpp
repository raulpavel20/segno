// Chance and controllers (docs/design/piano-roll.md §10, §11): each note's fate from the seed, the
// clip, the note and the lap; a controller lane's MIDI events, and its points back from them.

#include "model/Chance.h"
#include "model/Controllers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;

namespace
{
Note note(int pitch, double start, float chance = 1.0f, float spread = 0.0f)
{
    Note n{.pitch = pitch, .start = start, .length = 0.5, .velocity = 100.0f / 127.0f};
    n.chance = chance;
    n.velocitySpread = spread;
    return n;
}
} // namespace

TEST_CASE("A note's fate: the same for the same seed, clip, note and lap", "[fast]")
{
    const auto n = note(60, 1.0, 0.5f, 0.2f);
    for (int lap = 0; lap < 8; ++lap)
    {
        const auto a = noteFate(n, 1.0, lap, 7, 42), b = noteFate(n, 1.0, lap, 7, 42);
        CHECK(a.plays == b.plays);
        CHECK(a.velocity == b.velocity);
    }
    // Full chance and no spread: always, as written.
    const auto sure = note(60, 1.0);
    for (int lap = 0; lap < 100; ++lap)
    {
        const auto f = noteFate(sure, 1.0, lap, static_cast<std::uint32_t>(lap * 31), 9);
        CHECK(f.plays);
        CHECK(f.velocity == sure.velocity);
    }
    // No chance: never.
    for (int lap = 0; lap < 100; ++lap)
        CHECK_FALSE(noteFate(note(60, 1.0, 0.0f), 1.0, lap, 3, 9).plays);
}

TEST_CASE("Chance plays a note about as often as it says; spread stays within its range", "[fast]")
{
    for (const float chance : {0.1f, 0.25f, 0.5f, 0.8f})
    {
        int played = 0;
        constexpr int laps = 4000;
        for (int lap = 0; lap < laps; ++lap)
            played += noteFate(note(62, 2.0, chance), 2.0, lap, 1234, 77).plays ? 1 : 0;
        CHECK(static_cast<double>(played) / laps == Catch::Approx(chance).margin(0.03));
    }
    const auto n = note(64, 0.0, 1.0f, 20.0f / 127.0f);
    float lowest = 1.0f, highest = 0.0f;
    for (int lap = 0; lap < 2000; ++lap)
    {
        const float v = noteFate(n, 0.0, lap, 5, 6).velocity;
        CHECK(v * 127.0f == Catch::Approx(std::round(v * 127.0f))); // in MIDI steps
        lowest = std::min(lowest, v);
        highest = std::max(highest, v);
    }
    CHECK(lowest * 127.0f >= 80.0f - 0.01f);
    CHECK(highest * 127.0f <= 120.0f + 0.01f);
    CHECK(highest - lowest > 30.0f / 127.0f); // it does spread
    // Never silent or past the top.
    const auto loud = note(64, 0.0, 1.0f, 1.0f);
    for (int lap = 0; lap < 500; ++lap)
    {
        const float v = noteFate(loud, 0.0, lap, 5, 6).velocity;
        CHECK(v >= 1.0f / 127.0f);
        CHECK(v <= 1.0f);
    }
}

TEST_CASE("Chance: a new seed, another clip, another lap or another note decide afresh", "[fast]")
{
    // Over many notes, changing any one input changes some of the fates.
    const auto fates = [](std::uint32_t seed, std::uint64_t clip, int lap, int pitchOffset)
    {
        std::vector<bool> out;
        for (int k = 0; k < 64; ++k)
            out.push_back(
                noteFate(note(48 + pitchOffset + k % 12, k * 0.25, 0.5f), k * 0.25, lap, seed, clip).plays);
        return out;
    };
    const auto base = fates(1, 10, 0, 0);
    CHECK(base != fates(2, 10, 0, 0));
    CHECK(base != fates(1, 11, 0, 0));
    CHECK(base != fates(1, 10, 1, 0));
    CHECK(base != fates(1, 10, 0, 1));
    CHECK(usesChance({note(60, 0.0), note(62, 1.0, 0.5f)}));
    CHECK(usesChance({note(60, 0.0, 1.0f, 0.1f)}));
    CHECK_FALSE(usesChance({note(60, 0.0), note(62, 1.0)}));
}

TEST_CASE("Chance rolls again each pass and each loop lap; a kept roll plays the same (ADR 0062)", "[fast]")
{
    // A pass's seed: the same for the same seed, roll and lap; another roll or lap, another seed.
    CHECK(passSeed(7, 100, 0) == passSeed(7, 100, 0));
    CHECK(passSeed(7, 100, 0) != passSeed(7, 101, 0));
    CHECK(passSeed(7, 100, 0) != passSeed(7, 100, 1));
    CHECK(passSeed(7, 100, 0) != passSeed(8, 100, 0)); // Re-roll still changes a kept clip

    // Over many passes a note at half chance plays about half of them; two passes differ somewhere.
    Note n{.pitch = 60, .start = 0.0, .length = 1.0, .velocity = 0.8f, .chance = 0.5f};
    int played = 0;
    for (std::uint64_t roll = 1; roll <= 2000; ++roll)
        played += noteFate(n, 0.0, 0, passSeed(3, roll, 0), 42).plays ? 1 : 0;
    CHECK(played > 900);
    CHECK(played < 1100);
}

TEST_CASE("Controllers: names, ranges and the pedals' steps", "[fast]")
{
    CHECK(controllerName(controllers::modWheel) == "Mod wheel");
    CHECK(controllerName(controllers::sustain) == "Sustain");
    CHECK(controllerName(controllers::pitchBend) == "Pitch bend");
    CHECK(controllerName(controllers::aftertouch) == "Aftertouch");
    CHECK(controllerName(74) == "CC 74");
    CHECK(controllerRules(controllers::sustain).stepped());
    CHECK_FALSE(controllerRules(controllers::modWheel).stepped());
    CHECK(controllerValue(controllers::pitchBend, 0.5f) == 8192); // the wheel at rest
    CHECK(controllerValue(controllers::pitchBend, 1.0f) == 16383);
    CHECK(controllerValue(controllers::pitchBend, 0.0f) == 0);
    CHECK(controllerLevel(controllers::pitchBend, 8192) == 0.5f);
    CHECK(controllerLevel(controllers::pitchBend, 16383) == 1.0f);
    CHECK(controllerValue(controllers::modWheel, 1.0f) == 127);
    CHECK(controllerLevel(controllers::modWheel, 127) == 1.0f);
    CHECK(restingValue(controllers::pitchBend) == 0.5f);
    CHECK(isController(0));
    CHECK(isController(controllers::aftertouch));
    CHECK_FALSE(isController(130));
}

TEST_CASE("Controllers: a lane's events ramp between points, hold on steps, and read back as its points",
          "[fast]")
{
    // The mod wheel up from 0 to 127 over a beat, held, then a step down.
    const ControllerLane lane{.controller = controllers::modWheel,
                              .points = {{0.0, 0.0f, 0.0f}, {1.0, 1.0f, kStepCurve}, {2.0, 0.0f, 0.0f}}};
    const auto events = controllerEvents(lane);
    REQUIRE(events.size() > 3);
    CHECK(events.front() == ControllerEvent{0.0, 0, ControllerEvent::Kind::Point, 0.0f});
    CHECK(events.back().kind == ControllerEvent::Kind::Point);
    CHECK(events.back().beat == 2.0);
    int ramps = 0, last = -1;
    for (const auto& e : events)
    {
        if (e.kind == ControllerEvent::Kind::Ramp)
        {
            ++ramps;
            CHECK(e.beat > 0.0);
            CHECK(e.beat < 1.0); // only on the straight segment: the step holds
        }
        if (e.beat < 1.0)
        {
            CHECK(e.value > last); // rising, an event only where the value changes
            last = e.value;
        }
    }
    CHECK(ramps >= 60); // at most every 64th of a beat
    // Back to the points as written.
    CHECK(controllerPoints(lane.controller, events) == lane.points);
    // Played events (recorded) are points that hold.
    const std::vector<ControllerEvent> played{{0.5, 64, ControllerEvent::Kind::Played},
                                              {1.5, 0, ControllerEvent::Kind::Played}};
    const auto points = controllerPoints(controllers::sustain, played);
    REQUIRE(points.size() == 2);
    CHECK(points[0].curve == kStepCurve);
    CHECK(points[0].value == Catch::Approx(64.0f / 127.0f));
}
