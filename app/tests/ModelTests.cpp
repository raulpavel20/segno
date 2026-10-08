// JUCE-free tests of the DAW's view models (app/src/model, ADR 0018).

#include "model/Snapshot.h"
#include "model/Timeline.h"
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
