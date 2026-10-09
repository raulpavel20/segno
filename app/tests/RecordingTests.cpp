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
