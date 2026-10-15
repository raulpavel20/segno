// Clip fades and crossfades (M2 step C6, ADR 0036, docs/design/fades.md): curves, where a
// crossfade sits, seamless joins and edge fades, and what each clip plays.
#include "model/Automation.h"
#include "model/FadeEditing.h"
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

namespace
{
ClipInfo audioClip(std::uint64_t id, double start, double length, double offset, std::string file, double sourceLength = 10.0)
{
    return {.id = id, .kind = ClipInfo::Kind::Audio, .start = start, .length = length, .offset = offset, .sourceLength = sourceLength,
            .sourcePath = std::move(file)};
}

const FadeHandle* handleOf(const std::vector<FadeHandle>& handles, FadeHandle::Kind kind, std::uint64_t clip)
{
    for (const auto& h : handles)
        if (h.kind == kind && h.clip == clip)
            return &h;
    return nullptr;
}
} // namespace

TEST_CASE("fade editing: handles at the fades' ends, bend dots on fades, a crossfade's grips at a join", "[fast]")
{
    using K = FadeHandle::Kind;
    TrackInfo t{.id = 1};
    t.clips = {audioClip(10, 0.0, 2.0, 0.0, "a.wav"), audioClip(11, 2.0, 2.0, 5.0, "b.wav")};
    t.clips[0].fadeIn = {.length = 0.5, .bend = 0.4f};
    auto h = fadeHandles(t);
    REQUIRE(handleOf(h, K::FadeIn, 10) != nullptr);
    CHECK(handleOf(h, K::FadeIn, 10)->time == Approx(0.5));
    CHECK(handleOf(h, K::FadeIn, 10)->height == 0.0f);
    REQUIRE(handleOf(h, K::BendIn, 10) != nullptr);
    CHECK(handleOf(h, K::BendIn, 10)->time == Approx(0.25));
    CHECK(handleOf(h, K::BendIn, 10)->height == Approx(1.0f - fadeInGain(0.5, 0.4f)));
    CHECK(handleOf(h, K::FadeOut, 10)->time == Approx(2.0)); // no fade: at the corner
    CHECK(handleOf(h, K::BendOut, 10) == nullptr);
    // Its gain, at the top in its middle (audio-editor.md §10).
    CHECK(handleOf(h, K::Gain, 10)->time == Approx(1.0));
    CHECK(handleOf(h, K::Gain, 10)->height == 0.0f);

    // A crossfade at the join: its grips replace the two fades' handles there.
    t.clips[0].crossfadeOut = Crossfade{.length = 0.1, .from = -0.05};
    h = fadeHandles(t);
    CHECK(handleOf(h, K::FadeOut, 10) == nullptr);
    CHECK(handleOf(h, K::FadeIn, 11) == nullptr);
    CHECK(handleOf(h, K::CrossStart, 10)->time == Approx(1.95));
    CHECK(handleOf(h, K::CrossEnd, 10)->time == Approx(2.05));
    CHECK(handleOf(h, K::FadeOut, 11) != nullptr);
}

TEST_CASE("fade editing: dragging fades, across a join into a crossfade, and a crossfade's grips", "[fast]")
{
    using K = FadeHandle::Kind;
    TrackInfo t{.id = 1};
    t.clips = {audioClip(10, 0.0, 2.0, 0.0, "a.wav"), audioClip(11, 2.0, 2.0, 5.0, "b.wav")};
    const auto handles = fadeHandles(t);

    // A fade-in handle dragged in sets the fade; never past the fade-out, keeping its bend.
    t.clips[1].fadeOut = {.length = 0.5};
    auto e = dragFade(t, *handleOf(fadeHandles(t), K::FadeIn, 11), 2.3);
    CHECK(e.clip == 11);
    REQUIRE(e.in);
    CHECK(e.in->length == Approx(0.3));
    e = dragFade(t, *handleOf(fadeHandles(t), K::FadeIn, 11), 3.9);
    CHECK(e.in->length == Approx(1.5));
    e = dragFade(t, *handleOf(handles, K::FadeOut, 10), 1.2);
    CHECK(e.out->length == Approx(0.8));

    // Past the join into the clip that meets it: a crossfade, twice as long, centred.
    e = dragFade(t, *handleOf(handles, K::FadeOut, 10), 2.04);
    CHECK(e.crossClip == 10);
    REQUIRE(e.crossfade);
    CHECK(e.crossfade->length == Approx(0.08));
    CHECK(e.crossfade->from == Approx(-0.04));
    e = dragFade(t, *handleOf(handles, K::FadeIn, 11), 1.97);
    CHECK(e.crossClip == 10);
    CHECK(e.crossfade->length == Approx(0.06));
    // Not into empty space, a seamless neighbour or one without spare audio.
    TrackInfo apart{.id = 2, .clips = {audioClip(20, 0.0, 2.0, 0.0, "a.wav"), audioClip(21, 3.0, 1.0, 0.0, "b.wav")}};
    e = dragFade(apart, *handleOf(fadeHandles(apart), K::FadeOut, 20), 2.5);
    CHECK(e.crossClip == 0);
    CHECK(e.out->length == 0.0);
    TrackInfo split{.id = 3, .clips = {audioClip(30, 0.0, 2.0, 0.0, "a.wav"), audioClip(31, 2.0, 2.0, 2.0, "a.wav")}};
    CHECK(dragFade(split, *handleOf(fadeHandles(split), K::FadeOut, 30), 2.05).crossClip == 0);

    // A crossfade's start keeps its end; its end keeps its start.
    t.clips[0].crossfadeOut = Crossfade{.length = 0.1, .from = -0.05};
    const auto x = fadeHandles(t);
    e = dragFade(t, *handleOf(x, K::CrossStart, 10), 1.9);
    REQUIRE(e.crossfade);
    CHECK(e.crossfade->from == Approx(-0.1));
    CHECK(e.crossfade->length == Approx(0.15));
    e = dragFade(t, *handleOf(x, K::CrossEnd, 10), 2.0);
    CHECK(e.crossfade->from == Approx(-0.05));
    CHECK(e.crossfade->length == Approx(0.05));

    // Previewing an edit: the clips as they'll play.
    const auto clips = withEdit(fadeClips(t), {.clip = 11, .in = Fade{0.2, 0.0f}});
    CHECK(clips[1].in.length == Approx(0.2));
    const auto crossed = withEdit(fadeClips(t), {.crossClip = 10});
    CHECK_FALSE(crossed[0].crossOut); // removed
}

TEST_CASE("fade editing: bends follow the dot like an automation segment's", "[fast]")
{
    TrackInfo t{.id = 1, .clips = {audioClip(10, 0.0, 2.0, 1.0, "a.wav")}};
    t.clips[0].fadeIn = {.length = 0.5};
    t.clips[0].fadeOut = {.length = 0.5};
    const auto h = fadeHandles(t);
    const auto e = bendFade(t, *handleOf(h, FadeHandle::Kind::BendIn, 10), 0.7f);
    REQUIRE(e.in);
    CHECK(e.in->bend == Approx(0.7f));
    CHECK(e.in->length == Approx(0.5));
    // Up bows the line up: a rising fade-in bends one way, a falling fade-out the other.
    CHECK(bendFor(0.0f, 30.0f, 60.0f, true) == Approx(-0.5f));
    CHECK(bendFor(0.0f, 30.0f, 60.0f, false) == Approx(0.5f));
    CHECK(bendFor(0.9f, -60.0f, 60.0f, true) == 1.0f);
    CHECK(fadeInGain(0.5, bendFor(0.0f, 30.0f, 60.0f, true)) > 0.5f); // bowed up
}

TEST_CASE("fade editing: a clip with takes fades at its outer edges; a previewed edit plays as it will", "[fast]")
{
    using K = FadeHandle::Kind;
    TrackInfo t{.id = 1, .clips = {audioClip(10, 0.0, 1.0, 1.0, "a.wav"), audioClip(11, 1.0, 1.0, 3.0, "b.wav")}};
    t.clips[0].stack = t.clips[1].stack = 100; // a comp of two takes
    const auto h = fadeHandles(t);
    CHECK(handleOf(h, K::FadeIn, 10) != nullptr);
    CHECK(handleOf(h, K::FadeOut, 10) == nullptr); // the join between its parts
    CHECK(handleOf(h, K::FadeIn, 11) == nullptr);
    CHECK(handleOf(h, K::FadeOut, 11) != nullptr);

    // One gain handle for the clip with takes, in the middle of its parts (audio-editor.md §10).
    CHECK(handleOf(h, K::Gain, 10)->time == Approx(1.0));
    CHECK(handleOf(h, K::Gain, 11) == nullptr);

    const auto previewed = withFadeEdit(t, {.crossClip = 10, .crossfade = Crossfade{.length = 0.02, .from = -0.01}}, true);
    REQUIRE(previewed.clips[0].crossfadeOut);
    CHECK(previewed.clips[0].playback.out.kind == Ramp::Kind::Cross);
    CHECK(previewed.clips[1].playback.extendStart == Approx(0.01));
}

TEST_CASE("fades: a crossfade's placement, and what it sounds like", "[fast]")
{
    // Placement: before, centred on or after the join, where the spare audio allows it whole.
    const auto a = clip(1, 0.0, 5.0, 0.0), b = clip(2, 5.0, 5.0, 5.0, "b.wav");
    for (const auto p : {CrossfadePlacement::Before, CrossfadePlacement::Centred, CrossfadePlacement::After})
    {
        const auto x = placeCrossfadeAt(a, b, 0.02, p, CrossfadeShape::EqualPower);
        REQUIRE(x);
        CHECK(placementOf(*x) == p);
        CHECK(x->from == Approx(placementFrom(p, 0.02)));
    }
    CHECK_FALSE(placementOf(Crossfade{.length = 0.02, .from = -0.003}));
    // The second clip starts its file: nothing before the join, so only "after" fits.
    const auto fresh = clip(3, 5.0, 5.0, 0.0, "c.wav");
    CHECK_FALSE(placeCrossfadeAt(a, fresh, 0.02, CrossfadePlacement::Before, CrossfadeShape::EqualPower));
    CHECK_FALSE(placeCrossfadeAt(a, fresh, 0.02, CrossfadePlacement::Centred, CrossfadeShape::EqualPower));
    CHECK(placeCrossfadeAt(a, fresh, 0.02, CrossfadePlacement::After, CrossfadeShape::EqualPower));

    // The sum: the same material through equal gain comes back as it was; equal power from
    // silence to a tone follows the incoming gain.
    std::vector<float> tone(2 * 101), silence(2 * 101, 0.0f);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = std::sin(static_cast<float>(i) * 0.3f);
    const auto same = crossfadeSum(tone, tone, 2, CrossfadeShape::EqualGain);
    REQUIRE(same.size() == tone.size());
    for (size_t i = 0; i < tone.size(); ++i)
        CHECK(same[i] == Approx(tone[i]).margin(1e-6));
    std::vector<float> ones(2 * 101, 1.0f);
    const auto rising = crossfadeSum(silence, ones, 2, CrossfadeShape::EqualPower);
    for (size_t f = 0; f <= 100; f += 25)
        CHECK(rising[2 * f] == Approx(crossfadeGains(static_cast<double>(f) / 100.0, CrossfadeShape::EqualPower).in).margin(1e-6));
}


TEST_CASE("fades: a clip's two crossfades share it and never overlap", "[fast]")
{
    // A comp: A, a short B (0.4 s) and C, from three files with spare audio all round.
    std::vector<FadeClip> clips{clip(1, 0.0, 2.0, 2.0, "a.wav"), clip(2, 2.0, 0.4, 4.0, "b.wav"), clip(3, 2.4, 2.0, 4.4, "c.wav")};
    clips[0].crossOut = Crossfade{.length = 0.6, .from = -0.3};
    clips[1].crossOut = Crossfade{.length = 0.6, .from = -0.3};
    // As they play (and are drawn): each reaches 0.3 s into B, which has 0.4: they share it.
    const auto ab = crossfadeOut(clips, 0), bc = crossfadeOut(clips, 1);
    REQUIRE(ab);
    REQUIRE(bc);
    CHECK(crossfadeReachBefore(*ab) == Approx(0.3));
    CHECK(crossfadeReachAfter(*ab) == Approx(0.2));
    CHECK(crossfadeReachBefore(*bc) == Approx(0.2));
    CHECK(crossfadeReachAfter(*bc) == Approx(0.3));
    CHECK(crossfadeReachAfter(*ab) + crossfadeReachBefore(*bc) <= 0.4 + 1e-9);
    const auto b = clipPlayback(clips, 1, true);
    CHECK(b.extendStart == Approx(0.3));
    CHECK(b.extendEnd == Approx(0.3));
    CHECK(b.in.length + b.out.length <= 0.4 + 0.3 + 0.3 + 1e-9);

    // Placing one: it stops where the other begins, keeping its own side.
    clips[1].crossOut.reset();
    const auto placed = placeCrossfadeOn(clips, 1, 0.6, -0.3);
    REQUIRE(placed);
    CHECK(crossfadeReachBefore(*placed) == Approx(0.1)); // B's 0.4 less A|B's 0.3
    CHECK(crossfadeReachAfter(*placed) == Approx(0.3));
    // With room, as asked.
    clips[0].crossOut.reset();
    const auto whole = placeCrossfadeOn(clips, 1, 0.6, -0.3);
    REQUIRE(whole);
    CHECK(whole->length == Approx(0.6));
    CHECK(whole->from == Approx(-0.3));
}

TEST_CASE("fade editing: a crossfade's end stops at the next crossfade; a fade stops at a crossfade", "[fast]")
{
    TrackInfo t{.id = 1,
                .clips = {audioClip(1, 0.0, 2.0, 2.0, "a.wav"), audioClip(2, 2.0, 0.4, 4.0, "b.wav"), audioClip(3, 2.4, 2.0, 4.4, "c.wav")}};
    t.clips[0].crossfadeOut = Crossfade{.length = 0.2, .from = -0.1};
    t.clips[1].crossfadeOut = Crossfade{.length = 0.2, .from = -0.1};
    // A|B's end dragged far into C: it stops where B|C begins (2.3).
    const auto end = handleOf(fadeHandles(t), FadeHandle::Kind::CrossEnd, 1);
    REQUIRE(end);
    auto e = dragFade(t, *end, 3.5);
    REQUIRE(e.crossfade);
    CHECK(2.0 + e.crossfade->from + e.crossfade->length == Approx(2.3));
    // B|C's start dragged back into A: it stops where A|B ends (2.1).
    const auto start = handleOf(fadeHandles(t), FadeHandle::Kind::CrossStart, 2);
    REQUIRE(start);
    e = dragFade(t, *start, 1.0);
    REQUIRE(e.crossfade);
    CHECK(2.4 + e.crossfade->from == Approx(2.1));

    // C's fade-out stops at the crossfade into it (0.1 s of it): C is 2 s, so at most 1.9.
    const auto fadeOut = handleOf(fadeHandles(t), FadeHandle::Kind::FadeOut, 3);
    REQUIRE(fadeOut);
    e = dragFade(t, *fadeOut, 0.0);
    REQUIRE(e.out);
    CHECK(e.out->length == Approx(1.9));
}

TEST_CASE("fades: a looped clip's seams fade where they cut into its file; its spare audio is its next lap (ADR 0048)", "[fast]")
{
    // A 1 s loop from 0.5 s into a 10 s file, at 2 s for 3.5 s, starting 0.25 s into the loop.
    auto looped = clip(1, 2.0, 3.5, 0.25);
    looped.loop = {0.5, 1.0};
    const auto p = clipPlayback({looped}, 0, true);
    REQUIRE(p.seams.any());
    CHECK(p.seams.first == Approx(2.75));
    CHECK(p.seams.period == Approx(1.0));
    CHECK(p.seams.out == Approx(kEdgeFadeSeconds)); // the loop ends inside the file
    CHECK(p.seams.in == Approx(kEdgeFadeSeconds));  // and starts after its start
    CHECK(p.in.edge);                               // 0.75 s in: a cut start
    CHECK(p.out.edge);
    CHECK(playbackGain(looped, p, 2.75 - 0.002) == Approx(0.5f).margin(1e-3));
    CHECK(playbackGain(looped, p, 2.75 + 0.002) == Approx(0.5f).margin(1e-3));
    CHECK(playbackGain(looped, p, 3.25) == Approx(1.0f));
    CHECK(playbackGain(looped, p, 4.75 + 0.001) == Approx(0.25f).margin(1e-3)); // the third seam
    CHECK(seamRampBetween(p.seams, 3.74, 3.76));
    CHECK_FALSE(seamRampBetween(p.seams, 3.0, 3.5));
    CHECK_FALSE(seamRampBetween(p.seams, 5.4, 5.5)); // past the last seam

    // A loop of the whole file wraps without a fade, as a file's own ends aren't faded.
    auto whole = clip(1, 0.0, 30.0, 0.0);
    whole.loop = {0.0, 10.0};
    const auto w = clipPlayback({whole}, 0, true);
    CHECK_FALSE(w.any());
    // Edge fades off: none at seams either.
    CHECK_FALSE(clipPlayback({looped}, 0, false).seams.any());

    // Split inside a lap: seamless, no fades at the join. Split on a seam: the seam's own fades.
    auto left = clip(1, 2.0, 1.5, 0.25), right = clip(2, 3.5, 2.0, 0.25 + 1.5);
    left.loop = right.loop = looped.loop;
    CHECK(seamless(left, right));
    right.offset = 0.75; // wrapped: the same
    CHECK(seamless(left, right));
    auto atSeamLeft = clip(1, 2.0, 0.75, 0.25), atSeamRight = clip(2, 2.75, 2.75, 0.0);
    atSeamLeft.loop = atSeamRight.loop = looped.loop;
    CHECK_FALSE(seamless(atSeamLeft, atSeamRight));
    const auto parts = std::vector<FadeClip>{atSeamLeft, atSeamRight};
    CHECK(clipPlayback(parts, 0, true).out.edge);
    CHECK(clipPlayback(parts, 1, true).in.edge);

    // A part that starts just after a seam (split beside it, on the grid): the seam's fade-in
    // reaches into it, so the two parts fade as the whole did; one that ends just after a seam keeps
    // that seam's fades too.
    auto justAfter = clip(3, 2.75 + 2e-5, 1.0, 2e-5);
    justAfter.loop = looped.loop;
    CHECK(clipPlayback({justAfter}, 0, true).seams.first == Approx(2.75));
    auto endsJustAfter = clip(4, 2.0, 0.75 + 2e-5, 0.25);
    endsJustAfter.loop = looped.loop;
    const auto e = clipPlayback({endsJustAfter}, 0, true).seams;
    REQUIRE(e.any());
    CHECK(e.first == Approx(2.75));
    CHECK(e.until > 2.75);

    // The spare audio past a looped clip's edges is a lap of its loop.
    CHECK(spareBefore(looped) == Approx(1.0));
    CHECK(spareAfter(looped) == Approx(1.0));
}
