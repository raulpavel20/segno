// Recording's rules (M2 step C5, docs/design/recording.md).

#include "model/Recording.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;

TEST_CASE("recording: what a track records follows its inputs unless chosen", "[fast]")
{
    InputInfo in;
    CHECK(recordKind(in, true) == RecordKind::Midi); // an instrument and every MIDI input
    in.audio = "Input 1 + 2";
    CHECK(recordKind(in, false) == RecordKind::Audio);
    CHECK(recordKind(in, true) == RecordKind::Midi); // both inputs: MIDI with an instrument
    in.records = RecordKind::Audio;
    CHECK(recordKind(in, true) == RecordKind::Audio); // chosen (a vocoder: the voice)
    in = {.midi = ""};
    CHECK(recordKind(in, true) == RecordKind::Midi);
}

TEST_CASE("recording: a track hears its input by its monitoring and arm state", "[fast]")
{
    InputInfo in;
    CHECK_FALSE(hearsInput(in));
    in.softArmed = true;
    CHECK(hearsInput(in)); // selected instrument track: live MIDI
    in = {.armed = true};
    CHECK(hearsInput(in));
    in.monitor = Monitor::Off;
    CHECK_FALSE(hearsInput(in));
    in = {.monitor = Monitor::In};
    CHECK(hearsInput(in));
}

TEST_CASE("recording: Save As makes a project folder named after the project", "[fast]")
{
    CHECK(projectFileFor("/music/Song.segno") == "/music/Song/Song.segno");
    CHECK(projectFileFor("/music/Song") == "/music/Song/Song.segno");
    CHECK(projectFileFor("/music/Song/Song.segno") == "/music/Song/Song.segno"); // already in its folder
    CHECK(projectFileFor("/music/Song/Take two.segno") == "/music/Song/Take two/Take two.segno");
}

TEST_CASE("recording: a track's input in a few words", "[fast]")
{
    CHECK(inputSummary({}, true) == "All MIDI");
    CHECK(inputSummary({.midi = "keyboard", .channel = 2}, true) == "Keyboard \xC2\xB7 Ch 2");
    CHECK(inputSummary({.audio = "Input 1"}, false) == "Input 1");
    CHECK(inputSummary({.midi = ""}, true) == "No input");
}

#include "model/Takes.h"

namespace
{
ClipInfo takeClip(std::uint64_t id, double start, double length)
{
    return {.id = id, .start = start, .length = length};
}
} // namespace

TEST_CASE("takes: what the main lane plays of each take, and stepping through them", "[fast]")
{
    // Three laps over 4..6 s, and a pass over 8..10 s.
    std::vector<TakeInfo> takes{{.id = 1, .name = "Take 1", .clips = {takeClip(11, 4.0, 2.0)}},
                                {.id = 2, .name = "Take 2", .clips = {takeClip(12, 4.0, 2.0)}},
                                {.id = 3, .name = "Take 3", .clips = {takeClip(13, 4.0, 2.0), takeClip(14, 8.0, 2.0)}}};
    // The main lane plays take 3 over 4..6 and part of take 1's... nothing else; take 3's second clip.
    std::vector<ClipInfo> main{{.id = 100, .start = 4.0, .length = 2.0, .fromTake = 13},
                               {.id = 101, .start = 8.5, .length = 1.0, .fromTake = 14}};
    CHECK(inUse(takes[2].clips[0], main) == std::vector{std::pair{4.0, 6.0}});
    CHECK(inUse(takes[2].clips[1], main) == std::vector{std::pair{8.5, 9.5}});
    CHECK(inUse(takes[0].clips[0], main).empty());
    CHECK(isUsed(takes[2], main));
    CHECK_FALSE(isUsed(takes[1], main));
    CHECK(takeAt(takes, main, 5.0) == size_t{2});
    CHECK_FALSE(takeAt(takes, main, 7.0));

    // Alt+↑ / Alt+↓ over 4..6: through the takes that cover it, wrapping.
    CHECK(stepTake(takes, size_t{2}, 4.0, 6.0, 1) == size_t{0});
    CHECK(stepTake(takes, size_t{0}, 4.0, 6.0, -1) == size_t{2});
    CHECK(stepTake(takes, size_t{1}, 4.0, 6.0, -1) == size_t{0});
    // Over 8..10 only take 3 has a clip.
    CHECK(stepTake(takes, size_t{2}, 8.0, 10.0, 1) == size_t{2});
    CHECK_FALSE(stepTake(takes, std::nullopt, 20.0, 21.0, 1));

    CHECK(nextTakeNumber(takes) == 4);
    takes.push_back({.id = 4, .name = "Before take 4"});
    CHECK(nextTakeNumber(takes) == 4);
    CHECK(nextTakeNumber({}) == 1);
}
