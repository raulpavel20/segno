// Clip loops (M3 step C10, ADR 0048, docs/design/clip-loops.md): what a looped clip plays where,
// its laps and seams, and where switching or changing a loop puts it.
#include "model/ClipLoops.h"
#include "model/Editing.h"
#include "model/Snapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
ClipInfo audio(double start, double length, double offset, double sourceLength = 4.0, ClipLoop loop = {})
{
    ClipInfo c;
    c.id = 1;
    c.kind = ClipInfo::Kind::Audio;
    c.start = start;
    c.length = length;
    c.offset = offset;
    c.sourceLength = sourceLength;
    c.loop = loop;
    return c;
}

ClipInfo midi(double start, double length, double offset, ClipLoop loop = {})
{
    auto c = audio(start, length, offset, 0.0, loop);
    c.kind = ClipInfo::Kind::Midi;
    return c;
}

/// What a placement plays at `seconds`.
double playsAt(const LoopPlacement& p, double seconds)
{
    return contentAt(p.start, p.offset, p.loop, seconds);
}
} // namespace

TEST_CASE("clip loops: a looped clip plays its loop on every lap, from its phase", "[fast]")
{
    // A 1 s loop from 0.5 s into the content, starting 0.25 s into it, at 1 s for 3 s.
    const auto c = audio(1.0, 3.0, 0.25, 4.0, {0.5, 1.0});
    CHECK(contentAt(c, 1.0) == Approx(0.75));
    CHECK(contentAt(c, 1.74) == Approx(1.49));
    CHECK(contentAt(c, 1.75) == Approx(0.5)); // the first seam: back to the loop's start
    CHECK(contentAt(c, 3.9) == Approx(0.65));
    CHECK(lapAnchor(c) == Approx(0.75));
    CHECK(lapCount(c) == 4);

    const auto all = laps(c, 0.0, 10.0);
    REQUIRE(all.size() == 4);
    CHECK(all[0].from == Approx(1.0));
    CHECK(all[0].to == Approx(1.75));
    CHECK(all[0].content == Approx(0.75));
    CHECK(all[1].from == Approx(1.75));
    CHECK(all[1].content == Approx(0.5));
    CHECK(all[3].from == Approx(3.75));
    CHECK(all[3].to == Approx(4.0));
    // A window: laps cut to it.
    const auto some = laps(c, 2.0, 3.0);
    REQUIRE(some.size() == 2);
    CHECK(some[0].from == Approx(2.0));
    CHECK(some[0].content == Approx(0.75));
    CHECK(some[1].from == Approx(2.75));

    const auto s = seams(c);
    REQUIRE(s.size() == 3);
    CHECK(s[0].at == Approx(1.75));
    CHECK(s[0].number == 1);
    CHECK(s[2].at == Approx(3.75));
    CHECK(s[2].number == 3);
    REQUIRE(seamAt(c, 2.76, 0.02));
    CHECK(seamAt(c, 2.76, 0.02)->number == 2);
    CHECK_FALSE(seamAt(c, 2.5, 0.02));
    CHECK_FALSE(seamAt(c, 1.0, 0.02)); // the clip's own start isn't a seam
    CHECK_FALSE(seamAt(c, 4.0, 0.02)); // nor its end

    // The engine hands back phases unwrapped (a split adds to them): the same clip.
    auto unwrapped = c;
    unwrapped.offset = 0.25 + 3.0;
    CHECK(laps(unwrapped, 0.0, 10.0) == all);
    CHECK(seams(unwrapped) == s);
    CHECK(phaseOf(-0.75, c.loop) == Approx(0.25));

    // A clip that doesn't loop is one lap, its offset seconds into the file.
    const auto plain = audio(1.0, 2.0, 0.5);
    CHECK(laps(plain, 0.0, 10.0) == std::vector<Lap>{{1.0, 3.0, 0.5}});
    CHECK(seams(plain).empty());
    CHECK(lapCount(plain) == 1);
    CHECK(contentAt(plain, 2.0) == Approx(1.5));
}

TEST_CASE("clip loops: switching Loop on moves nothing; off removes the repeats", "[fast]")
{
    const auto plain = audio(2.0, 1.5, 0.5);
    const auto on = loopOn(plain);
    CHECK(on.loop == ClipLoop{0.5, 1.5});
    CHECK(on.offset == 0.0);
    CHECK(on.start == plain.start);
    CHECK(on.length == plain.length);
    for (const double t : {2.0, 2.7, 3.49})
        CHECK(playsAt(on, t) == Approx(contentAt(plain, t)));

    // Off again: the clip as it was.
    auto looped = plain;
    looped.offset = on.offset;
    looped.loop = on.loop;
    CHECK(loopOff(looped) == placementOf(plain));

    // Off on an extended loop starting mid-loop: the clip ends where its first lap ends, playing
    // from the same point.
    const auto extended = audio(1.0, 3.0, 0.25, 4.0, {0.5, 1.0});
    const auto off = loopOff(extended);
    CHECK_FALSE(off.loop.on());
    CHECK(off.offset == Approx(0.75));
    CHECK(off.length == Approx(0.75));
    CHECK(playsAt(off, 1.5) == Approx(contentAt(extended, 1.5)));

    // Already on or off: unchanged.
    CHECK(loopOn(extended) == placementOf(extended));
    CHECK(loopOff(plain) == placementOf(plain));
}

TEST_CASE("clip loops: a new loop keeps the clip's start when it can; a new length keeps its laps", "[fast]")
{
    const auto c = audio(1.0, 3.0, 0.25, 4.0, {0.5, 1.0}); // plays 0.75 at its start
    // The content at the start is inside the new loop: it keeps starting there.
    const auto inside = withLoop(c, {0.5, 2.0});
    CHECK(inside.offset == Approx(0.25));
    CHECK(playsAt(inside, 1.0) == Approx(0.75));
    // Outside it: the clip starts at the loop's start.
    const auto outside = withLoop(c, {1.0, 0.5});
    CHECK(outside.offset == 0.0);
    CHECK(playsAt(outside, 1.0) == Approx(1.0));
    CHECK(outside.start == c.start);
    CHECK(outside.length == c.length);
    // Kept inside the file and at least the minimum long.
    CHECK(withLoop(c, {3.5, 2.0}).loop == ClipLoop{3.5, 0.5});
    CHECK(withLoop(c, {-1.0, 0.0}).loop == ClipLoop{0.0, minClipLength});
    // A clip that doesn't loop yet (the brace drawn on it): looped from there.
    const auto drawn = withLoop(audio(2.0, 1.0, 0.5), {0.25, 1.0});
    CHECK(drawn.loop == ClipLoop{0.25, 1.0});
    CHECK(drawn.offset == Approx(0.25));
    // MIDI content has no end.
    CHECK(withLoop(midi(0.0, 2.0, 0.0), {8.0, 16.0}).loop == ClipLoop{8.0, 16.0});

    // A notch dragged: the loop's start and the first lap stay.
    const auto longer = withLoopLength(c, 1.5);
    CHECK(longer.loop == ClipLoop{0.5, 1.5});
    CHECK(longer.offset == Approx(0.25));
    CHECK(playsAt(longer, 1.0) == Approx(0.75));
    CHECK(withLoopLength(c, 0.2).offset == Approx(0.05)); // shorter than the phase: wrapped
    CHECK(withLoopLength(c, 9.0).loop.length == Approx(3.5)); // not past the file
}

TEST_CASE("clip loops: a looped clip's edges go anywhere; its content stays where it is", "[fast]")
{
    // Within the source while it doesn't loop.
    const auto plain = audio(2.0, 1.0, 0.5, 4.0);
    CHECK(trimmedStart(plain, 0.0).start == Approx(1.5)); // the file starts there
    CHECK(trimmedEnd(plain, 9.0).length == Approx(3.5));  // and ends there
    CHECK(trimmedEnd(plain, 2.0).length == Approx(minClipLength));

    const auto c = audio(1.0, 3.0, 0.25, 4.0, {0.5, 1.0});
    const auto longer = trimmedEnd(c, 20.0);
    CHECK(longer.length == Approx(19.0));
    CHECK(longer.offset == c.offset);
    const auto earlier = trimmedStart(c, 0.1);
    CHECK(earlier.start == Approx(0.1));
    CHECK(earlier.length == Approx(3.9));
    CHECK(earlier.offset >= 0.0);
    CHECK(earlier.offset < 1.0); // wrapped into the loop
    for (const double t : {1.0, 1.8, 3.9})
        CHECK(playsAt(earlier, t) == Approx(contentAt(c, t)));
    CHECK(trimmedStart(c, -5.0).start == 0.0); // never before time zero
    const auto later = trimmedStart(c, 2.2);
    CHECK(playsAt(later, 2.5) == Approx(contentAt(c, 2.5)));
}

TEST_CASE("clip loops: what can't loop says why", "[fast]")
{
    TrackInfo track;
    auto c = audio(0.0, 1.0, 0.0);
    CHECK(whyCantLoop(c, track).empty());
    CHECK(whyCantLoop(midi(0.0, 1.0, 0.0), track).empty());

    auto missing = c;
    missing.sourceLength = 0.0;
    CHECK(whyCantLoop(missing, track).find("missing") != std::string::npos);

    // Two takes or more: keep one first. One take: fine.
    TakeStackInfo stack{.id = 7};
    stack.takes.push_back({.id = 1, .name = "Take 1"});
    c.stack = 7;
    track.stacks.push_back(stack);
    CHECK(whyCantLoop(c, track).empty());
    track.stacks[0].takes.push_back({.id = 2, .name = "Take 2"});
    CHECK(whyCantLoop(c, track).find("takes") != std::string::npos);
}

TEST_CASE("clip loops: the clip view shows the lap the clip starts in, never before time zero", "[fast]")
{
    CHECK(clipViewOrigin(midi(3.0, 2.0, 1.0)) == Approx(2.0)); // not looping: start less offset
    CHECK(clipViewTime(midi(3.0, 2.0, 1.0), 4.0) == Approx(4.0));

    // A loop on bar lines, from the clip's start: as before (content zero where the clip starts,
    // less the loop's start).
    const auto c = midi(4.0, 8.0, 0.0, {2.0, 2.0});
    CHECK(clipViewOrigin(c) == Approx(2.0));
    // Later laps show in the loop: 4 s later is the same place in it.
    CHECK(clipViewTime(c, 9.0) == Approx(clipViewTime(c, 5.0)));
    CHECK(clipViewTime(c, 9.0) == Approx(5.0));
    // Where the lap would put content zero before time zero, a later lap.
    const auto early = midi(0.0, 4.0, 0.5, {4.0, 2.0}); // anchor −0.5: zero at −4.5
    CHECK(clipViewOrigin(early) == Approx(1.5));
    CHECK(clipViewOrigin(early) + early.loop.start >= 0.0);
}

TEST_CASE("clip loops: notes written past a MIDI loop's end lengthen it to the bar", "[fast]")
{
    const TempoMap tempo; // 120 BPM, 4/4: a bar is 2 s
    const auto c = midi(0.0, 8.0, 0.0, {0.0, 2.0});
    CHECK_FALSE(lengthenedLoop(c, {{.pitch = 60, .start = 1.0, .length = 1.0}}, tempo)); // inside
    const auto longer = lengthenedLoop(c, {{.pitch = 60, .start = 4.5, .length = 0.5}}, tempo); // ends at 2.5 s
    REQUIRE(longer);
    CHECK(*longer == Approx(4.0)); // to the end of bar 2
    CHECK_FALSE(lengthenedLoop(midi(0.0, 8.0, 0.0), {{.pitch = 60, .start = 20.0, .length = 1.0}}, tempo)); // no loop
    CHECK_FALSE(lengthenedLoop(c, {}, tempo));
}
