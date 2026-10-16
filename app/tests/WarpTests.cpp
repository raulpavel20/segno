// Warping (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7): the map from a clip's beats to
// its file, its pieces, a reversed clip's map, where the piece player starts pieces, and every
// marker edit with its limits.

#include "model/Warp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace fd::model;

namespace
{
/// A clip at 120 BPM with a marker pinning 1.0 s of its file to beat 2.5 (the first piece slower
/// than the clip's tempo: 2.5 beats over 1 s, 150 BPM).
AudioWarp pinned()
{
    AudioWarp w;
    w.on = true;
    w.tempo = 120.0;
    w.markers = {{1.0, 2.5}};
    return w;
}

/// Every piece in order and within ×0.25 … ×4 of the clip's tempo.
bool withinLimits(const AudioWarp& w, double fileLength = 100.0)
{
    for (const auto& p : warpPieces(w, fileLength))
    {
        const double s = pieceStretch(w, p);
        if (!(p.beat1 > p.beat0) || s < kMinPieceStretch - 1e-6 || s > kMaxPieceStretch + 1e-6)
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("warp: modes and tails by name, both ways", "[fast][warp]")
{
    for (auto m : {WarpMode::Beats, WarpMode::Tones, WarpMode::Repitch})
        CHECK(warpModeFromName(warpModeName(m)) == m);
    CHECK(warpModeFromName("BEATS") == WarpMode::Beats);
    CHECK_FALSE(warpModeFromName("Complex"));
    CHECK(tailsFromName("loop") == WarpTails::Loop);
    CHECK(tailsFromName(tailsName(WarpTails::Fade)) == WarpTails::Fade);
}

TEST_CASE("warp: without markers the file plays at the clip's tempo", "[fast][warp]")
{
    AudioWarp w;
    w.tempo = 90.0; // 1.5 beats a second
    CHECK(fileAtBeat(w, 3.0) == Approx(2.0));
    CHECK(beatAtFile(w, 2.0) == Approx(3.0));
    CHECK(fileAtBeat(w, -1.5) == Approx(-1.0));
    const auto pieces = warpPieces(w, 4.0);
    REQUIRE(pieces.size() == 1);
    CHECK(pieces[0].beat1 == Approx(6.0));
    CHECK(pieces[0].tempo() == Approx(90.0));
    CHECK(pieceStretch(w, pieces[0]) == Approx(1.0));
}

TEST_CASE("warp: markers pin moments to beats; straight between, at the clip's tempo outside", "[fast][warp]")
{
    const auto w = pinned();
    CHECK(fileAtBeat(w, 1.25) == Approx(0.5));
    CHECK(fileAtBeat(w, 2.5) == Approx(1.0));
    CHECK(fileAtBeat(w, 4.5) == Approx(2.0)); // after it, 2 beats a second again
    for (double b = -1.0; b < 8.0; b += 0.37)
        CHECK(beatAtFile(w, fileAtBeat(w, b)) == Approx(b));
    const auto pieces = warpPieces(w, 3.0);
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0] == WarpPiece{0.0, 2.5, 0.0, 1.0});
    CHECK(pieces[0].tempo() == Approx(150.0));
    CHECK(pieceStretch(w, pieces[0]) == Approx(1.25)); // the tag's "× 1.25"
    CHECK(pieces[1] == WarpPiece{2.5, 6.5, 1.0, 3.0});
    // A marker past the file's end pins nothing in it.
    auto past = w;
    past.markers.push_back({5.0, 12.0});
    CHECK(warpPieces(past, 3.0).size() == 2);
}

TEST_CASE("warp: a reversed clip's map is the file's played backwards, and its own inverse", "[fast][warp]")
{
    AudioWarp w = pinned();
    w.markers.push_back({2.0, 4.0}); // 1.5 beats over 1 s: 90 BPM
    const double length = 3.0, end = beatAtFile(w, length);
    const auto r = reversedWarp(w, length);
    for (double b = 0.0; b <= end; b += 0.25)
        CHECK(fileAtBeat(r, b) == Approx(length - fileAtBeat(w, end - b)).margin(1e-9));
    const auto back = reversedWarp(r, length);
    CHECK(back.markers.size() == w.markers.size());
    for (size_t i = 0; i < w.markers.size(); ++i)
    {
        CHECK(back.markers[i].file == Approx(w.markers[i].file));
        CHECK(back.markers[i].beat == Approx(w.markers[i].beat));
    }
    // Without markers, nothing to mirror.
    CHECK(reversedWarp(AudioWarp{}, length).markers.empty());
}

TEST_CASE("warp: the piece player starts pieces at markers, and in Beats at transients too", "[fast][warp]")
{
    auto w = pinned();
    const std::vector<double> transients{0.0005, 0.5, 1.0004, 1.5, 3.5};
    auto starts = pieceStarts(w, transients, 3.0);
    REQUIRE(starts.size() == 2); // Tones: the start and the marker
    CHECK(starts[1] == WarpMarker{1.0, 2.5});
    w.mode = WarpMode::Beats;
    starts = pieceStarts(w, transients, 3.0);
    // The transients on the start and on the marker add none, the one past the end neither.
    REQUIRE(starts.size() == 4);
    CHECK(starts[1].file == Approx(0.5));
    CHECK(starts[1].beat == Approx(1.25));
    CHECK(starts[3].file == Approx(1.5));
    CHECK(starts[3].beat == Approx(3.5));
}

TEST_CASE("warp: adding a marker moves nothing; one near the start or another marker adds none",
          "[fast][warp]")
{
    const auto w = pinned();
    const auto [added, index] = withMarkerAt(w, 2.0);
    REQUIRE(index == 1u);
    REQUIRE(added.markers.size() == 2);
    for (double b = 0.0; b < 8.0; b += 0.5)
        CHECK(fileAtBeat(added, b) == Approx(fileAtBeat(w, b)));
    CHECK(withMarkerAt(w, 1.0).second == 0u);
    CHECK(withMarkerAt(w, 1.0).first == w);
    CHECK_FALSE(withMarkerAt(w, 0.0).second);
    CHECK(markerNear(added, 1.98, 0.05) == 1u);
    CHECK_FALSE(markerNear(added, 1.5, 0.05));
}

TEST_CASE("warp: dragging a marker stretches each side, within ×0.25 … ×4", "[fast][warp]")
{
    auto w = withMarkerAt(pinned(), 2.0).first; // markers at (1, 2.5), (2, 4.5)
    auto moved = withMarkerMoved(w, 0, 3.0);
    CHECK(moved.markers[0] == WarpMarker{1.0, 3.0});
    CHECK(moved.markers[1] == w.markers[1]); // its neighbour stays
    CHECK(fileAtBeat(moved, 1.5) == Approx(0.5));
    // Too far: the piece after it would be 0.5 beats over 1 s, a quarter of 2 beats a second is
    // the most it may shrink to.
    moved = withMarkerMoved(w, 0, 4.4);
    CHECK(moved.markers[0].beat == Approx(4.0));
    CHECK(withinLimits(moved));
    moved = withMarkerMoved(w, 0, -3.0); // before the start: the first piece at its shortest
    CHECK(moved.markers[0].beat == Approx(0.5));
    // The last marker has nothing after it to squeeze: only the piece before limits it.
    moved = withMarkerMoved(w, 1, 40.0);
    CHECK(moved.markers[1].beat == Approx(2.5 + 8.0));
    CHECK(withMarkerMoved(w, 7, 1.0) == w);
}

TEST_CASE("warp: Shift + drag slides the audio under a marker, which keeps its beat", "[fast][warp]")
{
    auto w = withMarkerAt(pinned(), 2.0).first;
    auto slid = withMarkerSlid(w, 0, 1.2);
    CHECK(slid.markers[0] == WarpMarker{1.2, 2.5});
    CHECK(fileAtBeat(slid, 2.5) == Approx(1.2));
    // Within its neighbours and the limits: the piece after it can't be shorter than 2 beats at
    // ×4 of the clip's tempo (0.25 s) or longer than at ×0.25 (4 s).
    slid = withMarkerSlid(w, 0, 1.99);
    CHECK(slid.markers[0].file == Approx(2.0 - 2.0 / 8.0));
    CHECK(withinLimits(slid));
    slid = withMarkerSlid(w, 0, 0.0);
    CHECK(slid.markers[0].file == Approx(2.5 / 8.0)); // the first piece at ×4
}

TEST_CASE("warp: markers moved together shrink the move to fit; removed ones leave fitting pieces",
          "[fast][warp]")
{
    auto w = withMarkerAt(withMarkerAt(pinned(), 2.0).first, 2.5).first; // (1,2.5) (2,4.5) (2.5,5.5)
    auto moved = withMarkersMoved(w, {1, 2}, 0.5);
    CHECK(moved.markers[1].beat == Approx(5.0));
    CHECK(moved.markers[2].beat == Approx(6.0));
    CHECK(moved.markers[0] == w.markers[0]);
    moved = withMarkersMoved(w, {1, 2}, -10.0); // the piece before them at its shortest
    CHECK(moved.markers[1].beat == Approx(2.5 + 0.5));
    CHECK(withinLimits(moved));
    auto removed = withoutMarkers(w, {1});
    REQUIRE(removed.markers.size() == 2);
    CHECK(removed.markers[1] == w.markers[2]);
    CHECK(withoutMarkers(w, {0, 1, 2, 9}).markers.empty());
    // Warp from here: one tempo from the first marker on.
    CHECK(warpedFrom(w, 0).markers == std::vector<WarpMarker>{w.markers[0]});
}

TEST_CASE("warp: markers at transients, in a range, at the beats they play", "[fast][warp]")
{
    const auto w = pinned();
    const auto out = withMarkersAtTransients(w, {0.25, 0.5, 1.0005, 1.5, 2.5}, 0.4, 2.0);
    REQUIRE(out.markers.size() == 3);
    CHECK(out.markers[0].file == Approx(0.5));
    CHECK(out.markers[2].file == Approx(1.5));
    for (double b = 0.0; b < 8.0; b += 0.5)
        CHECK(fileAtBeat(out, b) == Approx(fileAtBeat(w, b)));
}

TEST_CASE("warp: a new tempo scales every marker's beat, so the file plays at it", "[fast][warp]")
{
    const auto w = pinned();
    const auto doubled = withTempo(w, 240.0);
    CHECK(doubled.tempo == 240.0);
    CHECK(doubled.markers[0] == WarpMarker{1.0, 5.0});
    for (double b = 0.0; b < 8.0; b += 0.5)
        CHECK(fileAtBeat(doubled, 2.0 * b) == Approx(fileAtBeat(w, b)));
    CHECK(withTempo(w, 5000.0).tempo == kMaxClipTempo);
}

TEST_CASE("warp: a range's time handles stretch its audio, markers inside scaled", "[fast][warp]")
{
    AudioWarp w;
    w.tempo = 120.0;
    w.markers = {{0.75, 1.5}};
    // Beats 1 to 2 stretched to 1 to 2.5: a marker at each end, the one inside scaled.
    const auto out = withRangeStretched(w, 1.0, 2.0, 1.0, 2.5);
    REQUIRE(out.markers.size() == 3);
    CHECK(out.markers[0] == WarpMarker{0.5, 1.0});
    CHECK(out.markers[1].beat == Approx(1.75));
    CHECK(out.markers[2].file == Approx(1.0));
    CHECK(out.markers[2].beat == Approx(2.5));
    CHECK(fileAtBeat(out, 3.5) == Approx(1.5)); // after it, the clip's tempo again
    // Too far: it stops at the limit.
    const auto far = withRangeStretched(w, 1.0, 2.0, 1.0, 50.0);
    CHECK(withinLimits(far));
    CHECK(far.markers.back().beat == Approx(1.0 + 4.0));
}

TEST_CASE("warp: Quantise moves transients towards the song's grid by its strength", "[fast][warp]")
{
    AudioWarp w;
    w.tempo = 120.0;
    // Hits a little late and early of each half beat.
    const std::vector<double> hits{0.27, 0.49, 0.77, 1.02};
    auto out = quantised(w, hits, 0.0, 8.0, 0.0, {.gridBeats = 0.5, .strength = 1.0});
    REQUIRE(out.markers.size() == 4);
    for (size_t i = 0; i < hits.size(); ++i)
    {
        CHECK(out.markers[i].file == Approx(hits[i]));
        CHECK(out.markers[i].beat == Approx(0.5 * static_cast<double>(i + 1)));
    }
    // Half strength: halfway.
    out = quantised(w, hits, 0.0, 8.0, 0.0, {.gridBeats = 0.5, .strength = 0.5});
    CHECK(out.markers[0].beat == Approx((0.54 + 0.5) / 2.0));
    // The song's grid: with content beat 0 at song beat 0.25, the first hit (song beat 0.79)
    // goes to song beat 1.
    out = quantised(w, hits, 0.0, 8.0, 0.25, {.gridBeats = 0.5, .strength = 1.0});
    CHECK(out.markers[0].beat == Approx(0.75));
    // Swing pushes every second step later.
    out = quantised(w, hits, 0.0, 8.0, 0.0, {.gridBeats = 0.5, .strength = 1.0, .swing = 0.5});
    CHECK(out.markers[0].beat == Approx(0.5 + 0.125)); // step 1 is odd
    CHECK(out.markers[1].beat == Approx(1.0));
    // Only in the range asked.
    out = quantised(w, hits, 1.0, 1.8, 0.0, {.gridBeats = 0.5, .strength = 1.0});
    REQUIRE(out.markers.size() == 1);
    CHECK(out.markers[0].file == Approx(0.77));
    CHECK(withinLimits(out));
}
