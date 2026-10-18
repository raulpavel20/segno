// Bounce (M2 step C7, ADR 0035, docs/design/bounce.md): which clips can be bounced, the runs
// they render in, where each bounce ends, and the names.
#include "model/Bounce.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <limits>

using namespace fd::model;
using Catch::Approx;

namespace
{
ClipInfo midi(std::uint64_t id, double start, double length, std::string name = "Clip")
{
    return {.id = id, .kind = ClipInfo::Kind::Midi, .name = std::move(name), .start = start, .length = length};
}

ClipInfo audio(std::uint64_t id, double start, double length)
{
    return {.id = id, .kind = ClipInfo::Kind::Audio, .name = "Audio", .start = start, .length = length};
}

DeviceSlot synth()
{
    return {.id = 50, .name = "Synth", .deviceId = "subtractive-synth", .instrument = true};
}

DeviceSlot effect(std::uint64_t id = 51)
{
    return {.id = id, .name = "EQ", .deviceId = "eq"};
}

TrackInfo track(std::uint64_t id, std::vector<ClipInfo> clips, std::vector<DeviceSlot> devices = {synth(), effect()})
{
    return {.id = id, .role = TrackInfo::Role::Track, .name = "Bass", .clips = std::move(clips), .devices = std::move(devices)};
}

/// Levels at 10 ms blocks over `seconds`: `loud` (linear) wherever `sounding` says so.
BlockLevels levels(double seconds, const std::function<bool(double)>& sounding, float loud = 0.5f)
{
    BlockLevels l{.blockSeconds = 0.01};
    for (double t = 0.0; t < seconds - 1e-9; t += l.blockSeconds)
        l.peaks.push_back(sounding(t) ? loud : 0.0f);
    return l;
}

constexpr double kNone = std::numeric_limits<double>::infinity();
} // namespace

TEST_CASE("bounce: which clips can be bounced, and why not", "[fast]")
{
    auto t = track(1, {midi(10, 0.0, 2.0), audio(11, 4.0, 2.0)});
    CHECK(bounceRefusal(t, t.clips[0], BounceMode::InPlace) == BounceRefusal::None);
    CHECK(bounceRefusal(t, t.clips[1], BounceMode::InPlace) == BounceRefusal::None);

    // No instrument: a MIDI clip is silent (ADR 0033); audio still bounces.
    auto noSynth = track(1, t.clips, {effect()});
    CHECK(bounceRefusal(noSynth, noSynth.clips[0], BounceMode::InPlace) == BounceRefusal::NoInstrument);
    CHECK(bounceRefusal(noSynth, noSynth.clips[0], BounceMode::NewTrack) == BounceRefusal::NoInstrument);
    CHECK(bounceRefusal(noSynth, noSynth.clips[1], BounceMode::InPlace) == BounceRefusal::None);
    CHECK_FALSE(refusalText(BounceRefusal::NoInstrument).empty());

    // An audio module before the instrument: not in place (on or off), but to a new track.
    auto before = track(1, t.clips, {effect(), synth()});
    CHECK(bounceRefusal(before, before.clips[0], BounceMode::InPlace) == BounceRefusal::ModulesBeforeInstrument);
    CHECK(bounceRefusal(before, before.clips[0], BounceMode::NewTrack) == BounceRefusal::None);
    before.devices[0].enabled = false;
    CHECK(bounceRefusal(before, before.clips[0], BounceMode::InPlace) == BounceRefusal::ModulesBeforeInstrument);
    // ... while a MIDI module there shapes the notes and is fine.
    before.devices[0].midiEffect = true;
    CHECK(bounceRefusal(before, before.clips[0], BounceMode::InPlace) == BounceRefusal::None);
    // Audio clips enter before every module: always in place.
    before.devices[0].midiEffect = false;
    CHECK(bounceRefusal(before, before.clips[1], BounceMode::InPlace) == BounceRefusal::None);

    // Muted clips play nothing.
    t.clips[1].muted = true;
    CHECK(bounceRefusal(t, t.clips[1], BounceMode::InPlace) == BounceRefusal::Muted);
    CHECK(bounceRefusal(t, t.clips[1], BounceMode::NewTrack) == BounceRefusal::Muted);
}

TEST_CASE("bounce: selected clips form runs per track, within the tail allowance", "[fast]")
{
    Snapshot s;
    // Bass: MIDI at 0-2, 3-5 (within 10 s), 30-32 (far), and audio at 40-42.
    s.tracks.push_back(track(1, {midi(10, 0.0, 2.0, "A"), midi(11, 3.0, 2.0, "B"), midi(12, 30.0, 2.0, "C"), audio(13, 40.0, 2.0)}));
    // Lead: no instrument, so its MIDI clip is refused and left out.
    s.tracks.push_back(track(2, {midi(20, 0.0, 1.0)}, {}));

    auto runs = bounceRuns(s, {10, 11, 12, 13, 20}, BounceMode::InPlace);
    REQUIRE(runs.size() == 3);
    CHECK(runs[0].track == 1);
    CHECK(runs[0].kind == ClipInfo::Kind::Midi);
    REQUIRE(runs[0].pieces.size() == 2);
    CHECK(runs[0].start == 0.0);
    CHECK(runs[0].renderEnd == Approx(5.0 + kBounceTailSeconds));
    CHECK(runs[0].pieces[0].name == "A");
    CHECK(runs[0].pieces[0].limit == 3.0); // the next clip
    CHECK(runs[0].pieces[0].limitName == "B");
    CHECK(runs[0].pieces[0].limitIsBounced);
    CHECK(runs[0].pieces[1].limit == 30.0);
    CHECK_FALSE(runs[0].pieces[1].limitIsBounced);
    CHECK(runs[1].pieces.size() == 1);
    CHECK(runs[1].start == 30.0);
    // Audio in place: no tail allowance, a run of its own.
    CHECK(runs[2].kind == ClipInfo::Kind::Audio);
    CHECK(runs[2].renderEnd == 42.0);
    CHECK(runs[2].pieces[0].limit == kNone);

    // Unselected clips limit the bounce in place but aren't rendered.
    runs = bounceRuns(s, {10}, BounceMode::InPlace);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].pieces[0].limit == 3.0);
    CHECK_FALSE(runs[0].pieces[0].limitIsBounced);

    // To a new track: only the next piece of the run limits a bounce.
    runs = bounceRuns(s, {10, 12}, BounceMode::NewTrack);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].pieces[0].limit == kNone);
    runs = bounceRuns(s, {10, 11}, BounceMode::NewTrack);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].pieces[0].limit == 3.0);
    CHECK(runs[0].pieces[0].limitIsBounced);
    CHECK(runs[0].pieces[1].limit == kNone);
}

TEST_CASE("bounce: a clip with takes is one piece", "[fast]")
{
    Snapshot s;
    auto t = track(1, {midi(10, 0.0, 1.0, "Comp"), midi(11, 1.0, 1.0, "Comp"), midi(12, 4.0, 1.0)});
    t.clips[0].stack = t.clips[1].stack = 100;
    t.stacks.push_back({.id = 100, .start = 0.0, .end = 2.0, .parts = {10, 11}});
    s.tracks.push_back(t);

    const auto runs = bounceRuns(s, {11}, BounceMode::InPlace); // one part selects the clip
    REQUIRE(runs.size() == 1);
    REQUIRE(runs[0].pieces.size() == 1);
    CHECK(runs[0].pieces[0].parts == std::vector<std::uint64_t>{10, 11});
    CHECK(runs[0].pieces[0].start == 0.0);
    CHECK(runs[0].pieces[0].end == 2.0);
    CHECK(runs[0].pieces[0].limit == 4.0);
}

TEST_CASE("bounce: a stretch of time is one piece per track, gaps and all, cut at its edges", "[fast]")
{
    Snapshot s;
    // Bass: A at 0-2, B at 3-6 (crossing the stretch's end at 5), C at 8-9. Keys: nothing there.
    s.tracks.push_back(track(1, {midi(10, 0.0, 2.0, "A"), midi(11, 3.0, 3.0, "B"), midi(12, 8.0, 1.0, "C")}));
    s.tracks.push_back(track(2, {midi(20, 6.0, 1.0)}));

    // 1 .. 5: A crosses the start and B the end; both are in, the piece spans the stretch.
    auto runs = stretchRuns(s, {1, 2}, 1.0, 5.0, BounceMode::InPlace);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].track == 1);
    CHECK(runs[0].start == 1.0);
    CHECK(runs[0].renderEnd == Approx(5.0 + kBounceTailSeconds));
    REQUIRE(runs[0].pieces.size() == 1);
    const auto& piece = runs[0].pieces[0];
    CHECK(piece.parts == std::vector<std::uint64_t>{10, 11});
    CHECK(piece.start == 1.0);
    CHECK(piece.end == 5.0);
    CHECK(piece.cutAtEdges);
    CHECK(piece.name == "A");
    // In place it stops where what's left of B starts; to a new track nothing limits it.
    CHECK(piece.limit == 5.0);
    CHECK(piece.limitName == "B");
    CHECK(stretchRuns(s, {1}, 1.0, 5.0, BounceMode::NewTrack)[0].pieces[0].limit == kNone);
    // Wholly inside: nothing to cut, and the next clip limits it.
    runs = stretchRuns(s, {1}, 0.0, 7.0, BounceMode::InPlace);
    REQUIRE(runs.size() == 1);
    CHECK_FALSE(runs[0].pieces[0].cutAtEdges);
    CHECK(runs[0].pieces[0].limit == 8.0);

    // Nothing there: none, and why.
    std::string why;
    CHECK(stretchRuns(s, {1, 2}, 10.0, 12.0, BounceMode::InPlace, &why).empty());
    CHECK(why == "No clip in the selection to bounce");
    CHECK(stretchRuns(s, {1}, 5.0, 4.0, BounceMode::InPlace).empty());
}

TEST_CASE("bounce: a stretch in place refuses takes, and MIDI with audio; a new track takes both", "[fast]")
{
    Snapshot s;
    auto t = track(1, {midi(10, 0.0, 1.0, "Comp"), midi(11, 1.0, 1.0, "Comp"), audio(12, 3.0, 1.0)});
    t.clips[0].stack = t.clips[1].stack = 100;
    t.stacks.push_back({.id = 100, .start = 0.0, .end = 2.0, .parts = {10, 11}});
    s.tracks.push_back(t);
    s.tracks.push_back(track(2, {midi(20, 0.0, 1.0), audio(21, 2.0, 1.0)}));
    s.tracks.push_back(track(3, {midi(30, 0.0, 1.0)}, {})); // no instrument

    std::string why;
    CHECK(stretchRuns(s, {1}, 0.0, 2.0, BounceMode::InPlace, &why).empty());
    CHECK(why.find("takes") != std::string::npos);
    why.clear();
    CHECK(stretchRuns(s, {2}, 0.0, 4.0, BounceMode::InPlace, &why).empty());
    CHECK(why.find("MIDI and audio") != std::string::npos);
    why.clear();
    CHECK(stretchRuns(s, {3}, 0.0, 4.0, BounceMode::NewTrack, &why).empty());
    CHECK(why == refusalText(BounceRefusal::NoInstrument));
    // The tracks that can be bounced are, the others left out.
    CHECK(stretchRuns(s, {1, 2, 3}, 0.0, 4.0, BounceMode::InPlace).empty());
    const auto runs = stretchRuns(s, {1, 2, 3}, 0.0, 4.0, BounceMode::NewTrack);
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].pieces[0].parts == std::vector<std::uint64_t>{10, 11, 12});
    CHECK(runs[0].kind == ClipInfo::Kind::Midi); // MIDI and audio: rendered through the chain with the tail
    CHECK(runs[1].track == 2);
}

TEST_CASE("bounce: a tail runs until silent, within the allowance and its limit", "[fast]")
{
    Snapshot s;
    s.tracks.push_back(track(1, {midi(10, 0.0, 2.0), midi(11, 6.0, 2.0)}));

    // Alone: the tail sounds until 3.5 s, well before the next clip.
    auto runs = bounceRuns(s, {10}, BounceMode::InPlace);
    auto l = levels(12.0, [](double t) { return t < 3.5; });
    auto end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.end == Approx(3.5).margin(0.011));
    CHECK_FALSE(end.cut);
    CHECK(keptLength(runs[0], l, BounceMode::InPlace) == Approx(3.5).margin(0.011));

    // Silent at once: it ends where its clip does, never earlier.
    l = levels(12.0, [](double t) { return t < 1.0; });
    end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.end == Approx(2.0));
    CHECK(keptLength(runs[0], l, BounceMode::InPlace) == Approx(2.0));

    // Still sounding where the next clip (not bounced) starts: cut there, audible, the file keeps it.
    l = levels(12.0, [](double t) { return t < 9.0; });
    end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.end == Approx(6.0));
    CHECK(end.cut);
    CHECK(end.audible);
    CHECK(keptLength(runs[0], l, BounceMode::InPlace) == Approx(9.0).margin(0.011));
    // Quiet there: cut, but nothing to say.
    l = levels(12.0, [](double t) { return t < 9.0; }, 0.0001f);
    end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.cut);
    CHECK_FALSE(end.audible);
    // Below the silence threshold: no tail at all.
    l = levels(12.0, [](double t) { return t < 9.0; }, 0.000001f);
    end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.end == Approx(2.0));
    CHECK_FALSE(end.cut);

    // Bounced together: the first ends where the second starts, and the tail carries on there.
    runs = bounceRuns(s, {10, 11}, BounceMode::InPlace);
    REQUIRE(runs.size() == 1);
    l = levels(18.0, [](double t) { return t < 9.0; });
    end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.end == Approx(6.0));
    CHECK_FALSE(end.cut);
    end = bounceEnd(runs[0], runs[0].pieces[1], l, BounceMode::InPlace);
    CHECK(end.end == Approx(9.0).margin(0.011));

    // Never more than the allowance.
    runs = bounceRuns(s, {11}, BounceMode::InPlace);
    l = levels(25.0, [](double) { return true; });
    l.peaks.resize(static_cast<size_t>((runs[0].renderEnd - runs[0].start) / l.blockSeconds));
    end = bounceEnd(runs[0], runs[0].pieces[0], l, BounceMode::InPlace);
    CHECK(end.end == Approx(8.0 + kBounceTailSeconds).margin(0.011));
}

TEST_CASE("bounce: names", "[fast]")
{
    CHECK(bounceTakeName({}) == "Bounce");
    CHECK(bounceTakeName({{.name = "Take 1"}, {.name = "Bounce"}}) == "Bounce 2");
    CHECK(bounceTakeName({{.name = "Bounce 2"}, {.name = "Before bounce"}}) == "Bounce 3");
    CHECK(bounceFileName("Bass", "Bass 1") == "Bass - Bass 1 - Bounce");
    CHECK(bounceTrackName("Bass") == "Bass bounce");
}
