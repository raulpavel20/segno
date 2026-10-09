// JUCE-free tests of the DAW's view models (app/src/model, ADR 0018).

#include "model/Snapshot.h"
#include "model/Taper.h"
#include "model/Timeline.h"
#include "model/TrackHeaderLayout.h"
#include "model/TrackLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace fd::model;
using Catch::Approx;

TEST_CASE("timeline maps time and pixels both ways", "[fast]")
{
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(10.0, 100.0);
    CHECK(tl.xForTime(10.0) == Approx(0.0));
    CHECK(tl.xForTime(12.5) == Approx(250.0));
    CHECK(tl.timeForX(250.0) == Approx(12.5));
    CHECK(tl.end() == Approx(20.0));
}

TEST_CASE("zoom keeps the time under the anchor in place", "[fast]")
{
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(10.0, 100.0);
    const double before = tl.timeForX(300.0);
    tl.zoomAround(300.0, 2.0);
    CHECK(tl.pixelsPerSecond() == Approx(200.0));
    CHECK(tl.timeForX(300.0) == Approx(before));
    tl.zoomAround(300.0, 0.25);
    CHECK(tl.timeForX(300.0) == Approx(before));

    // Limits: never before time zero, never beyond the zoom range.
    tl.setView(0.5, 100.0);
    tl.zoomAround(500.0, 0.001);
    CHECK(tl.start() == 0.0);
    CHECK(tl.pixelsPerSecond() == Timeline::minPixelsPerSecond);
    tl.zoomAround(500.0, 1e9);
    CHECK(tl.pixelsPerSecond() == Timeline::maxPixelsPerSecond);
}

TEST_CASE("animated zoom eases to the target and keeps its anchor", "[fast]")
{
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(4.0, 50.0);
    const double anchorTime = tl.timeForX(400.0);
    tl.animateZoomAround(400.0, 2.0);
    tl.animateZoomAround(400.0, 2.0); // compounds: target 200 px/s
    REQUIRE(tl.isAnimating());
    double last = tl.pixelsPerSecond();
    int frames = 0;
    while (tl.advance(1.0 / 60.0) && frames < 120)
    {
        CHECK(tl.pixelsPerSecond() > last); // monotonic
        CHECK(tl.timeForX(400.0) == Approx(anchorTime));
        last = tl.pixelsPerSecond();
        ++frames;
    }
    CHECK(frames < 40); // well under a second at 60 fps
    CHECK(tl.pixelsPerSecond() == Approx(200.0));
    CHECK_FALSE(tl.isAnimating());

    tl.animateZoomAround(400.0, 0.5);
    tl.scrollBy(10.0); // user input cancels the animation
    CHECK_FALSE(tl.isAnimating());
}

TEST_CASE("zoom to fit shows the range with a small margin", "[fast]")
{
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(0.0, 20.0);
    tl.animateZoomTo(30.0, 40.0);
    for (int i = 0; i < 200 && tl.advance(1.0 / 60.0); ++i)
    {
    }
    CHECK(tl.start() < 30.0);
    CHECK(tl.end() > 40.0);
    CHECK(tl.end() - tl.start() == Approx(10.4).epsilon(0.01));
}

TEST_CASE("adaptive grid picks the finest readable step", "[fast]")
{
    const TempoMap tempo; // 120 BPM, 4/4: a beat is 0.5 s, a bar 2 s
    Timeline tl;
    tl.setWidth(1000.0);

    tl.setView(0.0, 200.0); // a 1/32 note (1/16 s) is 12.5 px: finest step
    CHECK(tl.gridStepBeats(tempo) == Approx(0.125));
    tl.setView(0.0, 40.0); // 1/32 = 2.5 px, 1/16 = 5, 1/8 = 10 px
    CHECK(tl.gridStepBeats(tempo) == Approx(0.5));
    tl.setView(0.0, 8.0); // beat = 4 px, bar = 16 px
    CHECK(tl.gridStepBeats(tempo) == Approx(4.0));
    tl.setView(0.0, 1.0); // bar = 2 px: 8 bars = 16 px
    CHECK(tl.gridStepBeats(tempo) == Approx(32.0));

    // Line kinds and labels at 40 px/s: bars every 80 px, so every bar is labelled.
    tl.setView(0.0, 40.0);
    std::vector<Timeline::GridLine> lines;
    tl.gridLines(tempo, lines);
    REQUIRE(lines.size() > 8);
    CHECK(lines[0].kind == Timeline::LineKind::Bar);
    CHECK(lines[0].bar == 1);
    CHECK(lines[0].label);
    CHECK(lines[1].kind == Timeline::LineKind::Subdivision); // 1/8 note
    CHECK(lines[2].kind == Timeline::LineKind::Beat);
    CHECK(lines[8].kind == Timeline::LineKind::Bar);
    CHECK(lines[8].bar == 2);
    CHECK(lines[8].seconds == Approx(2.0));

    // Zoomed out, labels thin out to keep 56 px apart: bars are 20 px, so every 4th bar.
    tl.setView(0.0, 10.0);
    tl.gridLines(tempo, lines);
    for (const auto& l : lines)
        if (l.kind == Timeline::LineKind::Bar)
            CHECK(l.label == ((l.bar - 1) % 4 == 0));
}

TEST_CASE("grid follows the time signature", "[fast]")
{
    const TempoMap waltz{.bpm = 90.0, .numerator = 3, .denominator = 4};
    const TempoMap sevenEight{.bpm = 120.0, .numerator = 7, .denominator = 8};
    CHECK(waltz.beatsPerBar() == 3.0);
    CHECK(sevenEight.beatsPerBar() == 3.5);

    Timeline tl;
    tl.setWidth(2000.0);
    tl.setView(0.0, 100.0);
    std::vector<Timeline::GridLine> lines;
    tl.gridLines(sevenEight, lines);
    int bars = 0;
    for (const auto& l : lines)
        if (l.kind == Timeline::LineKind::Bar)
        {
            CHECK(l.seconds == Approx(bars * sevenEight.secondsForBeats(3.5)));
            ++bars;
        }
    CHECK(bars > 5);

    const auto pos = waltz.barsBeatsForSeconds(waltz.secondsForBeats(3.0 * 2 + 1.25)); // bar 3, beat 2, 2nd 16th
    CHECK(pos.bar == 3);
    CHECK(pos.beat == 2);
    CHECK(pos.sixteenth == 2);
}

TEST_CASE("snapping goes to the nearest line of the visible grid", "[fast]")
{
    const TempoMap tempo;
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(0.0, 40.0); // 1/8-note grid: 0.25 s
    CHECK(tl.snap(1.1, tempo) == Approx(1.0));
    CHECK(tl.snap(1.2, tempo) == Approx(1.25));
    tl.setView(0.0, 8.0); // bar grid: 2 s
    CHECK(tl.snap(2.9, tempo) == Approx(2.0));
    CHECK(tl.snap(3.1, tempo) == Approx(4.0));
    CHECK(tl.snap(-1.0, tempo) == 0.0);
}

TEST_CASE("track layout stacks, scrolls and hit-tests", "[fast]")
{
    TrackLayout layout;
    layout.setTrackCount(3);
    layout.setHeight(1, 100.0);
    layout.setViewportHeight(150.0);
    CHECK(layout.contentHeight() == Approx(64.0 + 100.0 + 64.0));
    CHECK(layout.top(2) == Approx(164.0));

    CHECK(layout.hit(10.0)->track == 0);
    CHECK(layout.hit(70.0)->track == 1);
    CHECK(layout.hit(70.0)->offset == Approx(6.0));
    CHECK_FALSE(layout.hit(300.0));

    layout.scrollBy(1000.0); // clamped: the last track can reach the middle of the view
    CHECK(layout.scroll() == Approx(228.0 - 75.0));
    CHECK(layout.hit(0.0)->track == 1);
    layout.scrollBy(-1000.0);
    CHECK(layout.scroll() == 0.0);

    layout.setHeight(0, 1.0); // clamped to the minimum
    CHECK(layout.height(0) == TrackLayout::minHeight);
    layout.setTrackCount(4); // keeps existing heights
    CHECK(layout.height(1) == 100.0);
    CHECK(layout.height(3) == TrackLayout::defaultHeight);
}

TEST_CASE("snapshot reports where the content ends", "[fast]")
{
    Snapshot s;
    CHECK(s.contentEnd() == 0.0);
    s.tracks.push_back({.id = 1, .clips = {{.id = 1, .start = 1.0, .length = 2.0}, {.id = 2, .start = 5.0, .length = 1.0}}});
    s.tracks.push_back({.id = 2, .clips = {{.id = 3, .start = 0.0, .length = 4.0}}});
    CHECK(s.contentEnd() == 6.0);
}

#include "model/Peaks.h"

#include "fd/audiofile/AudioFile.h"

#include <cmath>
#include <filesystem>

TEST_CASE("peaks hold the min and max at every resolution", "[fast]")
{
    // Stereo: a slow sine on the left, a single spike on the right.
    constexpr int frames = 100000;
    std::vector<std::vector<float>> ch(2, std::vector<float>(frames, 0.0f));
    for (int i = 0; i < frames; ++i)
        ch[0][static_cast<size_t>(i)] = 0.5f * std::sin(2.0f * 3.14159265f * static_cast<float>(i) / 1000.0f);
    ch[1][77777] = -0.9f;
    const auto path = (std::filesystem::temp_directory_path() / "fd-peaks-test.wav").string();
    REQUIRE(fd::audiofile::writeWav(path, ch, 48000.0));

    std::string error;
    const auto peaks = buildPeaks(path, &error);
    INFO(error);
    REQUIRE(peaks != nullptr);
    CHECK(peaks->frames == frames);
    CHECK(peaks->sampleRate == 48000.0);
    REQUIRE(peaks->levels.size() >= 6);
    CHECK(peaks->levels[0].binFrames == 16);
    CHECK(peaks->levels[1].binFrames == 64);
    CHECK(peaks->levels.back().min.size() == 1);

    // Any range, at any resolution, reports the true extremes it covers.
    for (std::int64_t span : {10, 100, 1000, 10000, 100000})
    {
        INFO("span " << span);
        const auto [lo, hi] = peaks->range(77777 - span / 2, 77777 + span / 2 + 1);
        CHECK(lo == Approx(-0.9f));
        CHECK(hi <= 0.5f + 1e-6f);
    }
    const auto [lo, hi] = peaks->range(0, 1000); // one full sine cycle
    CHECK(lo == Approx(-0.5f).margin(0.01));
    CHECK(hi == Approx(0.5f).margin(0.01));
    CHECK(peaks->range(frames, frames + 100) == std::pair{0.0f, 0.0f}); // past the end
    CHECK(peaks->bytes() < 100000 * sizeof(float)); // far smaller than the audio
    std::filesystem::remove(path);

    CHECK(buildPeaks("/nonexistent/fd.wav") == nullptr);
}

#include "model/Editing.h"

#include <set>

namespace
{
/// Two tracks: A has clips 1 [0, 2) and 2 [4, 5), B has clip 3 [1, 3) (offset 0.5 into a 4 s file).
Snapshot editingSnapshot()
{
    Snapshot s;
    s.tracks.push_back({.id = 10, .clips = {{.id = 1, .start = 0.0, .length = 2.0, .sourceLength = 2.0},
                                            {.id = 2, .start = 4.0, .length = 1.0, .sourceLength = 3.0}}});
    s.tracks.push_back({.id = 20, .clips = {{.id = 3, .start = 1.0, .length = 2.0, .offset = 0.5, .sourceLength = 4.0}}});
    return s;
}

const std::function<double(double)> noSnap = [](double t) { return t; };
} // namespace

TEST_CASE("clip hit testing finds the clip and its edges", "[fast]")
{
    const auto s = editingSnapshot();
    TrackLayout layout;
    layout.setTrackCount(2);
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(0.0, 100.0); // 100 px per second

    auto hit = hitClip(s, layout, tl, 100.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->clip == 1);
    CHECK(hit->zone == ClipHit::Zone::Body);
    CHECK(hitClip(s, layout, tl, 2.0, 10.0)->zone == ClipHit::Zone::StartEdge);
    CHECK(hitClip(s, layout, tl, 197.0, 10.0)->zone == ClipHit::Zone::EndEdge);
    CHECK(hitClip(s, layout, tl, 150.0, 70.0)->clip == 3); // second track
    CHECK_FALSE(hitClip(s, layout, tl, 350.0, 10.0));       // gap
    CHECK_FALSE(hitClip(s, layout, tl, 100.0, 500.0));      // below the tracks
}

TEST_CASE("moving clips snaps the grabbed clip and keeps the others relative", "[fast]")
{
    const auto s = editingSnapshot();
    const auto snapToHalf = [](double t) { return std::round(t * 2.0) / 2.0; };
    auto edits = dragClips(s, {1, 3}, {.kind = DragKind::Move, .anchor = 3, .deltaSeconds = 0.7}, snapToHalf);
    REQUIRE(edits.size() == 2);
    // Clip 3 starts at 1.7 -> snaps to 1.5: both move by 0.5.
    for (const auto& e : edits)
        CHECK(e.start == Approx(e.clip == 1 ? 0.5 : 1.5));

    // Never before zero; tracks clamp to the list.
    edits = dragClips(s, {1, 3}, {.kind = DragKind::Move, .anchor = 1, .deltaSeconds = -5.0, .deltaTracks = 3}, noSnap);
    for (const auto& e : edits)
    {
        CHECK(e.start >= 0.0);
        CHECK(e.track == (e.clip == 1 ? 10u : 20u)); // the pair can't move down: B is the last track
    }
    edits = dragClips(s, {1}, {.kind = DragKind::Move, .anchor = 1, .deltaTracks = 1}, noSnap);
    CHECK(edits[0].track == 20);
}

TEST_CASE("trimming stays inside the source and keeps a minimum length", "[fast]")
{
    const auto s = editingSnapshot();
    // Clip 3 starts 0.5 s into its file: its start can move back at most 0.5 s.
    auto e = dragClips(s, {3}, {.kind = DragKind::TrimStart, .anchor = 3, .deltaSeconds = -2.0}, noSnap);
    CHECK(e[0].start == Approx(0.5));
    CHECK(e[0].offset == Approx(0.0));
    CHECK(e[0].length == Approx(2.5));
    e = dragClips(s, {3}, {.kind = DragKind::TrimStart, .anchor = 3, .deltaSeconds = 5.0}, noSnap);
    CHECK(e[0].length == Approx(minClipLength));
    CHECK(e[0].offset == Approx(0.5 + 2.0 - minClipLength));

    // Clip 3's file has 4 s: from offset 0.5 the end can reach 1.0 + 3.5 = 4.5.
    e = dragClips(s, {3}, {.kind = DragKind::TrimEnd, .anchor = 3, .deltaSeconds = 10.0}, noSnap);
    CHECK(e[0].length == Approx(3.5));
    e = dragClips(s, {3}, {.kind = DragKind::TrimEnd, .anchor = 3, .deltaSeconds = -10.0}, noSnap);
    CHECK(e[0].length == Approx(minClipLength));
}

TEST_CASE("box selection and duplicate placement", "[fast]")
{
    const auto s = editingSnapshot();
    auto ids = clipsInBox(s, 2.5, 0.5, 1, 0); // reversed corners work too
    std::set<std::uint64_t> found(ids.begin(), ids.end());
    CHECK(found == std::set<std::uint64_t>{1, 3});
    CHECK(clipsInBox(s, 3.5, 3.9, 0, 1).empty());

    const auto copies = duplicatePlacements(s, {1, 3}); // span [0, 3): copies start 3 s later
    REQUIRE(copies.size() == 2);
    for (const auto& c : copies)
        CHECK(c.start == Approx(c.clip == 1 ? 3.0 : 4.0));
}

TEST_CASE("placing a clip cuts what it covers (no overlaps on a track)", "[fast]")
{
    // Clips on one track: A [0, 4), B [6, 8) offset 1, C [10, 20).
    const std::vector<ClipInfo> clips{{.id = 1, .start = 0.0, .length = 4.0},
                                      {.id = 2, .start = 6.0, .length = 2.0, .offset = 1.0},
                                      {.id = 3, .start = 10.0, .length = 10.0}};
    // Placing [3, 7): A loses its tail, B loses its head (and its offset moves on).
    auto cuts = clearRange(clips, 3.0, 7.0, {});
    REQUIRE(cuts.size() == 2);
    CHECK(cuts[0].clip == 1);
    CHECK(cuts[0].kind == ClipCut::Kind::Trim);
    CHECK(cuts[0].length == Approx(3.0));
    CHECK(cuts[1].clip == 2);
    CHECK(cuts[1].kind == ClipCut::Kind::Trim);
    CHECK(cuts[1].start == Approx(7.0));
    CHECK(cuts[1].length == Approx(1.0));
    CHECK(cuts[1].offset == Approx(2.0));

    // Covering B entirely removes it; landing inside C splits it around the range.
    cuts = clearRange(clips, 5.0, 9.0, {});
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].kind == ClipCut::Kind::Remove);
    cuts = clearRange(clips, 12.0, 14.0, {});
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].kind == ClipCut::Kind::SplitAround);
    CHECK(cuts[0].from == Approx(12.0));
    CHECK(cuts[0].to == Approx(14.0));

    // The clips being placed never cut themselves; touching edges are not overlaps.
    CHECK(clearRange(clips, 3.0, 7.0, {1, 2}).empty());
    CHECK(clearRange(clips, 4.0, 6.0, {}).empty());
}

#include "model/Fuzzy.h"

TEST_CASE("fuzzy matching finds subsequences and prefers word starts", "[fast]")
{
    CHECK(fuzzyScore("", "Anything") == 0);
    CHECK(fuzzyScore("ply", "Play / stop"));
    CHECK_FALSE(fuzzyScore("xyz", "Play / stop"));
    CHECK_FALSE(fuzzyScore("pa", "Ap")); // order matters
    // Word starts beat letters inside words; prefixes beat both.
    CHECK(*fuzzyScore("ps", "Play / stop") > *fuzzyScore("ps", "Upside"));
    CHECK(*fuzzyScore("zoom", "Zoom in") > *fuzzyScore("zoom", "Show zoomed"));
    CHECK(*fuzzyScore("SAVE", "save as") > 0); // case-insensitive
}

TEST_CASE("the palette ranks matches, then recent use", "[fast]")
{
    const std::vector<PaletteItem> items{{"file.save", "Save", "File"},
                                         {"file.saveAs", "Save as", "File"},
                                         {"view.snap", "Snap to grid", "View"},
                                         {"transport.playStop", "Play / stop", "Transport"}};
    auto names = [&](const std::vector<size_t>& order) {
        std::vector<std::string> out;
        for (auto i : order)
            out.push_back(items[i].name);
        return out;
    };
    CHECK(names(rankPalette("sa", items, {})).front() == "Save");
    // Recent use decides between equal matches.
    CHECK(names(rankPalette("save", items, {"file.saveAs"})).front() == "Save as");
    // The category helps: "transport play".
    CHECK(names(rankPalette("transport play", items, {})).front() == "Play / stop");
    // An empty query lists everything, recent first.
    const auto all = names(rankPalette("", items, {"view.snap"}));
    CHECK(all.size() == items.size());
    CHECK(all.front() == "Snap to grid");
}

TEST_CASE("a coarser line spacing gives a coarser grid", "[fast]")
{
    Timeline tl;
    tl.setWidth(1000.0);
    const TempoMap tempo; // 120 BPM: 0.5 s per beat
    tl.setView(0.0, 60.0); // 30 px per beat: 1/8 notes are 15 px apart
    CHECK(tl.gridStepBeats(tempo) == Approx(0.5));
    tl.setLineSpacing(24.0);
    CHECK(tl.gridStepBeats(tempo) == Approx(1.0));
    CHECK(tl.snap(0.3, tempo) == Approx(0.5)); // snapping follows
}

TEST_CASE("track header slots appear with height and never overlap", "[fast]")
{
    const TrackHeaderMetrics m;
    // The smallest track: just the colour and the name.
    auto s = layoutTrackHeader(180.0, 24.0, 3, false);
    CHECK_FALSE(s.colour.isEmpty());
    CHECK_FALSE(s.name.isEmpty());
    CHECK(s.buttons.empty());
    CHECK(s.subtitle.isEmpty());
    CHECK(s.controls.isEmpty());

    // Buttons from 32 px, right-aligned beside the name, left to right.
    s = layoutTrackHeader(180.0, 40.0, 3, false);
    REQUIRE(s.buttons.size() == 3);
    CHECK(s.buttons.back().right() == Approx(180.0 - m.paddingRight));
    CHECK(s.buttons[1].x == Approx(s.buttons[0].right() + m.buttonGap));
    CHECK(s.name.right() <= s.buttons.front().x - m.buttonGap + 1e-9);
    CHECK(s.subtitle.isEmpty());

    // The subtitle from 48 px, the controls from 80 px, each below the last.
    s = layoutTrackHeader(180.0, 64.0, 3, false);
    CHECK_FALSE(s.subtitle.isEmpty());
    CHECK(s.subtitle.y >= s.name.bottom());
    CHECK(s.controls.isEmpty());
    s = layoutTrackHeader(180.0, 96.0, 3, true);
    CHECK_FALSE(s.controls.isEmpty());
    CHECK(s.controls.y >= s.subtitle.bottom());
    CHECK(s.controls.bottom() <= 96.0 - m.paddingY + 1e-9);

    // The meter takes the right edge; everything else stays left of it.
    CHECK_FALSE(s.meter.isEmpty());
    CHECK(s.meter.right() == Approx(180.0 - m.paddingRight));
    for (const auto& r : {s.name, s.subtitle, s.controls})
        CHECK(r.right() <= s.meter.x + 1e-9);
    for (const auto& b : s.buttons)
        CHECK(b.right() <= s.meter.x + 1e-9);

    // A header too narrow for every button keeps the ones that fit.
    s = layoutTrackHeader(60.0, 40.0, 3, false);
    for (const auto& b : s.buttons)
        CHECK(b.x >= m.paddingLeft);
}

TEST_CASE("the volume taper puts 0 dB at 80 % and maps both ways", "[fast]")
{
    using namespace fd::model;
    CHECK(volumeDbAt(0.8) == Approx(0.0).margin(1e-9));
    CHECK(volumeDbAt(0.64) == Approx(-6.0).margin(1e-6));
    CHECK(volumeDbAt(1.0) == Approx(6.0).margin(1e-9));
    CHECK(volumeDbAt(0.0) == minusInfinityDb);
    CHECK(volumeDbAt(1.5) == Approx(6.0).margin(1e-9)); // clamped to the travel
    CHECK(sendDbAt(1.0) == Approx(0.0).margin(1e-9));
    CHECK(sendDbAt(0.8) == Approx(-6.0).margin(1e-6));
    for (double db : {-60.0, -20.0, -6.0, -0.5, 0.0, 3.0, 6.0})
        CHECK(volumeDbAt(volumePosition(db)) == Approx(db).margin(1e-9));
    for (double db : {-60.0, -12.0, 0.0})
        CHECK(sendDbAt(sendPosition(db)) == Approx(db).margin(1e-9));
    CHECK(volumePosition(minusInfinityDb) == 0.0);
    CHECK(volumePosition(12.0) == 1.0);
    // Monotonic: more travel is always louder (the first percent or two sit at -inf).
    double last = minusInfinityDb;
    for (int i = 1; i <= 100; ++i)
    {
        const double db = volumeDbAt(i / 100.0);
        CHECK((db > last || db == minusInfinityDb));
        last = db;
    }
}
