// Capture MIDI (recording.md §8): the last phrase, where it goes and where it lands.
#include "model/Capture.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
MidiEvent on(double time, int note, int channel = 1, std::optional<double> song = std::nullopt)
{
    return {.time = time, .song = song, .bytes = {static_cast<unsigned char>(0x90 + channel - 1), static_cast<unsigned char>(note), 100}, .size = 3};
}

MidiEvent off(double time, int note, int channel = 1, std::optional<double> song = std::nullopt)
{
    return {.time = time, .song = song, .bytes = {static_cast<unsigned char>(0x80 + channel - 1), static_cast<unsigned char>(note), 0}, .size = 3};
}

MidiEvent pedal(double time, bool down)
{
    return {.time = time, .bytes = {0xb0, 64, static_cast<unsigned char>(down ? 127 : 0)}, .size = 3};
}
} // namespace

TEST_CASE("capture: the last phrase starts after a long enough pause", "[fast]")
{
    // A note, 30 s of silence, then a phrase: only the phrase.
    const std::vector events{on(0.0, 60), off(0.5, 60), on(30.0, 62), off(30.4, 62), on(30.5, 64), off(31.0, 64)};
    auto phrase = lastPhrase(events, 4.0, -1e9, 0.5);
    REQUIRE(phrase.size() == 4);
    CHECK(phrase.front().time == 30.0);
    // Short pauses stay in one phrase.
    phrase = lastPhrase(events, 40.0, -1e9, 0.5);
    CHECK(phrase.size() == 6);
    // What a capture (or a pass, or starting to play) used up doesn't count.
    phrase = lastPhrase(events, 40.0, 10.0, 0.5);
    CHECK(phrase.front().time == 30.0);
    CHECK(lastPhrase(events, 4.0, 40.0, 0.5).empty());
    CHECK(lastPhrase({pedal(0.0, true)}, 4.0, -1e9, 0.5).empty()); // no note: nothing
}

TEST_CASE("capture: a held note or pedal bridges a pause; controllers just before come along", "[fast]")
{
    // A note held through 10 s: one phrase.
    std::vector events{on(0.0, 60), on(10.0, 64), off(10.5, 64), off(11.0, 60)};
    CHECK(lastPhrase(events, 4.0, -1e9, 0.5).size() == 4);
    // The pedal held through it too.
    events = {pedal(0.0, true), on(0.1, 60), off(0.5, 60), on(10.0, 64), off(10.5, 64), pedal(11.0, false)};
    CHECK(lastPhrase(events, 4.0, -1e9, 0.5).front().time == 0.0);
    // A pedal pressed just before a phrase's first note comes with it.
    events = {on(0.0, 60), off(0.5, 60), pedal(9.8, true), on(10.0, 64), off(10.5, 64)};
    const auto phrase = lastPhrase(events, 4.0, -1e9, 0.5);
    REQUIRE(phrase.size() == 3);
    CHECK(phrase.front().isSustain());
}

TEST_CASE("capture: a track's input takes its own device and channel", "[fast]")
{
    InputInfo in;
    CHECK(inputTakes(in, "keyboard", 1)); // "all"
    CHECK(inputTakes(in, "Keystation", 5));
    in.midi = "keyboard";
    CHECK(inputTakes(in, "keyboard", 1));
    CHECK_FALSE(inputTakes(in, "Keystation", 1));
    in.midi = "Keystation";
    in.channel = 2;
    CHECK(inputTakes(in, "Keystation", 2));
    CHECK_FALSE(inputTakes(in, "Keystation", 1));
    CHECK(inputTakes(in, "Keystation", 0)); // not a channel message
    in.midi.clear();
    CHECK_FALSE(inputTakes(in, "keyboard", 1));
}

TEST_CASE("capture: armed MIDI tracks, else the selected track, else a new track", "[fast]")
{
    Snapshot s;
    s.tracks = {{.id = 1}, {.id = 2, .instrument = "Synth"}, {.id = 3}};
    s.returns = {{.id = 9, .role = TrackInfo::Role::Return}};
    CHECK(captureDestinations(s, 1).track == 1u); // no instrument: still the selected track (ADR 0033)
    CHECK(captureDestinations(s, 9).newTrack);    // a return holds no clips
    CHECK(captureDestinations(s, std::nullopt).newTrack);

    s.tracks[1].input.armed = true;
    CHECK(captureDestinations(s, 1).armed == std::vector<std::uint64_t>{2});
    // An armed track recording audio isn't one.
    s.tracks[1].input.records = RecordKind::Audio;
    CHECK(captureDestinations(s, 1).track == 1u);
}

TEST_CASE("capture: where a phrase lands", "[fast]")
{
    TempoMap tempo; // 120 BPM: a beat is half a second
    // Played while stopped: its first note on the playhead's beat, the rest as played.
    auto clip = placeCapture({on(100.0, 60), off(100.3, 60), on(100.6, 62), off(101.2, 62)}, tempo, 8.2, 102.0);
    REQUIRE(clip.times.size() == 4);
    CHECK(clip.times[0] == Approx(8.0));
    CHECK(clip.times[2] == Approx(8.6));
    CHECK(clip.start == Approx(8.0));
    CHECK(clip.end == Approx(9.5)); // the beat after the last note
    // Played while playing: where it was, from the beat before.
    clip = placeCapture({on(100.0, 60, 1, 3.3), off(100.2, 60, 1, 3.5)}, tempo, 20.0, 102.0);
    CHECK(clip.times[0] == Approx(3.3));
    CHECK(clip.start == Approx(3.0));
    CHECK(clip.end == Approx(3.5));
    // Across a loop's wrap (song times fold back): kept as played.
    clip = placeCapture({on(100.0, 60, 1, 5.8), off(100.4, 60, 1, 4.2)}, tempo, 20.0, 102.0);
    CHECK(clip.times[1] == Approx(6.2));
    // A note still held ends now; at least a beat long.
    clip = placeCapture({on(100.0, 60)}, tempo, 0.0, 101.1);
    CHECK(clip.end == Approx(1.5));
    clip = placeCapture({on(100.0, 60), off(100.05, 60)}, tempo, 0.0, 100.1);
    CHECK(clip.end - clip.start == Approx(0.5));
}
