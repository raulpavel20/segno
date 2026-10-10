// Clip fades and crossfades (M2 step C6, ADR 0036, docs/design/fades.md): curves, where a
// crossfade sits, seamless joins and edge fades, and what each clip plays.
#include "model/Automation.h"
#include "model/Fades.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::model;
using Catch::Approx;

namespace
{
/// A clip of `file` (10 s long) playing [offset, offset + length) at `start`.
FadeClip clip(std::uint64_t id, double start, double length, double offset, std::string file = "a.wav", double sourceLength = 10.0)
{
    return {.id = id, .start = start, .length = length, .offset = offset, .sourceLength = sourceLength, .source = std::move(file)};
}
} // namespace

TEST_CASE("fades: curves are automation segments; crossfade shapes keep their promises", "[fast]")
{
    // A straight fade is a straight ramp of gain; a bend bends it like an automation segment.
    CHECK(fadeInGain(0.0, 0.0f) == Approx(0.0f));
    CHECK(fadeInGain(0.25, 0.0f) == Approx(0.25f));
    CHECK(fadeInGain(1.0, 0.0f) == Approx(1.0f));
    CHECK(fadeOutGain(0.25, 0.0f) == Approx(0.75f));
    for (const float bend : {-0.8f, -0.3f, 0.4f, 0.9f})
        for (double a = 0.0; a <= 1.0; a += 0.1)
        {
            CHECK(fadeInGain(a, bend) == Approx(automationValueAt({{0.0, 0.0f, curveForBend(bend)}, {1.0, 1.0f, 0.0f}}, a)).margin(1e-6));
            CHECK(fadeOutGain(a, bend) == Approx(automationValueAt({{0.0, 1.0f, curveForBend(bend)}, {1.0, 0.0f, 0.0f}}, a)).margin(1e-6));
        }
    CHECK(fadeInGain(0.5, 0.6f) != Approx(0.5f)); // it does bend
    CHECK(fadeInGain(-1.0, 0.0f) == 0.0f);        // clamped
    CHECK(fadeInGain(2.0, 0.0f) == 1.0f);

    for (double a = 0.0; a <= 1.0; a += 0.05)
    {
        const auto power = crossfadeGains(a, CrossfadeShape::EqualPower);
        CHECK(power.out * power.out + power.in * power.in == Approx(1.0f).margin(1e-6)); // constant power
        const auto gain = crossfadeGains(a, CrossfadeShape::EqualGain);
        CHECK(gain.out + gain.in == Approx(1.0f).margin(1e-6)); // constant sum
        const auto s = crossfadeGains(a, CrossfadeShape::SCurve);
        CHECK(s.out + s.in == Approx(1.0f).margin(1e-6));
    }
    CHECK(crossfadeGains(0.0, CrossfadeShape::EqualPower).out == Approx(1.0f));
    CHECK(crossfadeGains(1.0, CrossfadeShape::EqualPower).in == Approx(1.0f));

    CHECK(shapeFromName("Equal power") == CrossfadeShape::EqualPower);
    CHECK(shapeFromName("equal-gain") == CrossfadeShape::EqualGain);
    CHECK(shapeFromName(shapeName(CrossfadeShape::SCurve)) == CrossfadeShape::SCurve);
    CHECK_FALSE(shapeFromName("bouncy"));
}

TEST_CASE("fades: a crossfade sits centred, moves to the spare audio, and needs some", "[fast]")
{
    // A split at 5 s of one file: plenty of spare audio both ways.
    const auto a = clip(1, 0.0, 5.0, 0.0), b = clip(2, 5.0, 5.0, 5.0);
    auto x = placeCrossfade(a, b, 0.01);
    REQUIRE(x);
    CHECK(x->length == Approx(0.01));
    CHECK(x->from == Approx(-0.005)); // centred
    x = placeCrossfade(a, b, 0.01, -0.01); // wholly before the join, as asked
    CHECK(x->from == Approx(-0.01));
    x = placeCrossfade(a, b, 0.01, 0.0); // wholly after
    CHECK(x->from == Approx(0.0));
    CHECK(placeCrossfade(a, b, 0.01, 0.3)->from == Approx(0.0)); // never past the span

    // The second clip starts at its file's beginning: no audio before it, so the crossfade sits
    // after the join, where the first clip has spare audio.
    const auto fresh = clip(3, 5.0, 5.0, 0.0, "b.wav");
    x = placeCrossfade(a, fresh, 0.01);
    REQUIRE(x);
    CHECK(x->length == Approx(0.01));
    CHECK(x->from == Approx(0.0));
    // The first clip ends at its file's end: the crossfade sits before the join.
    const auto whole = clip(4, 0.0, 10.0, 0.0);
    const auto later = clip(5, 10.0, 5.0, 2.0, "b.wav");
    x = placeCrossfade(whole, later, 0.01);
    CHECK(x->from == Approx(-0.01));
    // Short spare audio: as long as it allows.
    const auto little = clip(6, 5.0, 5.0, 0.002, "c.wav");
    x = placeCrossfade(clip(7, 0.0, 5.0, 0.0, "d.wav", 5.001), little, 0.01);
    REQUIRE(x);
    CHECK(x->length == Approx(0.003).margin(1e-9));
    // None: files edge to edge, or clips that don't meet.
    CHECK_FALSE(placeCrossfade(clip(8, 0.0, 5.0, 0.0, "e.wav", 5.0), clip(9, 5.0, 5.0, 0.0, "f.wav"), 0.01));
    CHECK_FALSE(placeCrossfade(a, clip(10, 6.0, 4.0, 6.0), 0.01));

    // A crossfade that has to shrink keeps its centre.
    x = placeCrossfade(clip(11, 0.0, 5.0, 0.0, "g.wav", 5.004), clip(12, 5.0, 5.0, 1.0), 0.01, -0.005);
    REQUIRE(x);
    CHECK(x->length == Approx(0.01));
    CHECK(x->from == Approx(-0.006)); // only 4 ms after the join: shifted to fit, not shortened
}

TEST_CASE("fades: edge fades on cut edges, none at seamless joins or a file's own edges", "[fast]")
{
    // A whole file: its own start and end aren't cut.
    std::vector<FadeClip> track{clip(1, 0.0, 10.0, 0.0)};
    auto p = clipPlayback(track, 0, true);
    CHECK_FALSE(p.any());

    // Trimmed in at both ends: 4 ms edge fades.
    track = {clip(1, 2.0, 5.0, 1.0)};
    p = clipPlayback(track, 0, true);
    CHECK(p.in.kind == Ramp::Kind::Fade);
    CHECK(p.in.edge);
    CHECK(p.in.length == Approx(kEdgeFadeSeconds));
    CHECK(p.out.edge);
    CHECK_FALSE(clipPlayback(track, 0, false).any()); // the project's toggle

    // Split in two: the join is seamless, the outer cut edges still fade.
    track = {clip(1, 2.0, 2.0, 1.0), clip(2, 4.0, 3.0, 3.0)};
    CHECK(seamless(track[0], track[1]));
    p = clipPlayback(track, 0, true);
    CHECK(p.in.edge);
    CHECK(p.out.kind == Ramp::Kind::None);
    p = clipPlayback(track, 1, true);
    CHECK(p.in.kind == Ramp::Kind::None);
    CHECK(p.out.edge);
    // Moved apart, or a muted neighbour: cut edges again.
    track[1].muted = true;
    CHECK(clipPlayback(track, 0, true).out.edge);
    CHECK_FALSE(clipPlayback(track, 1, true).any()); // a muted clip plays nothing

    // A longer fade replaces the edge fade; a shorter one is floored to it.
    track = {clip(1, 2.0, 5.0, 1.0)};
    track[0].in = {.length = 0.5, .bend = 0.3f};
    track[0].out = {.length = 0.001};
    p = clipPlayback(track, 0, true);
    CHECK(p.in.length == Approx(0.5));
    CHECK(p.in.bend == 0.3f);
    CHECK_FALSE(p.in.edge);
    CHECK(p.out.length == Approx(kEdgeFadeSeconds));
    CHECK(p.out.edge);
    // A fade on an uncut edge plays as drawn.
    track = {clip(1, 0.0, 10.0, 0.0)};
    track[0].in = {.length = 0.002};
    CHECK(clipPlayback(track, 0, true).in.length == Approx(0.002));

    // Fades longer than the clip share it.
    track = {clip(1, 0.0, 1.0, 0.0)};
    track[0].in = {.length = 1.0};
    track[0].out = {.length = 1.0};
    p = clipPlayback(track, 0, true);
    CHECK(p.in.length + p.out.length == Approx(1.0));
}

TEST_CASE("fades: a crossfade extends both clips into their spare audio, and goes when they part", "[fast]")
{
    std::vector<FadeClip> track{clip(1, 0.0, 5.0, 0.0), clip(2, 5.0, 5.0, 5.0, "b.wav")};
    track[0].crossOut = Crossfade{.length = 0.01, .from = -0.005, .shape = CrossfadeShape::EqualGain};
    auto a = clipPlayback(track, 0, true), b = clipPlayback(track, 1, true);
    CHECK(a.extendEnd == Approx(0.005));
    CHECK(a.out.kind == Ramp::Kind::Cross);
    CHECK(a.out.length == Approx(0.01));
    CHECK(a.out.shape == CrossfadeShape::EqualGain);
    CHECK(b.extendStart == Approx(0.005));
    CHECK(b.in.kind == Ramp::Kind::Cross);
    REQUIRE(crossfadeOut(track, 0));
    // Across the crossfade the two gains sum to one (equal gain), and outside it each is whole.
    for (double t = 4.99; t <= 5.01; t += 0.0005)
        CHECK(playbackGain(track[0], a, t) + playbackGain(track[1], b, t) == Approx(1.0f).margin(1e-5));
    CHECK(playbackGain(track[0], a, 4.0) == 1.0f);
    CHECK(playbackGain(track[0], a, 5.006) == 0.0f);
    CHECK(playbackGain(track[1], b, 4.994) == 0.0f);

    // Moved apart: no crossfade; the stored one waits, unused.
    track[1].start = 6.0;
    CHECK_FALSE(crossfadeOut(track, 0));
    a = clipPlayback(track, 0, true);
    CHECK(a.extendEnd == 0.0);
    CHECK(a.out.kind == Ramp::Kind::Fade); // its cut end fades
    // Muted: no crossfade either.
    track[1].start = 5.0;
    track[1].muted = true;
    CHECK_FALSE(crossfadeOut(track, 0));
}
