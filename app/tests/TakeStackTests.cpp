// Takes on clips (ADR 0032, recording.md §6): the badge, the strips, a clip's parts as one, and
// what clip commands do to the takes.
#include "model/Editing.h"
#include "model/Selection.h"
#include "model/StackEdits.h"
#include "model/Takes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
ClipInfo clip(std::uint64_t id, double start, double length, std::uint64_t stack = 0, std::uint64_t fromTake = 0)
{
    return {.id = id, .start = start, .length = length, .fromTake = fromTake, .stack = stack};
}

/// A track with one clip (id 10) playing take 2 of a stack of three over 4..6 s.
TrackInfo trackWithTakes(int playing = 1)
{
    TrackInfo t{.id = 1, .role = TrackInfo::Role::Track};
    TakeStackInfo s{.id = 100, .start = 4.0, .end = 6.0};
    for (std::uint64_t k = 0; k < 3; ++k)
        s.takes.push_back({.id = 200 + k, .name = "Take " + std::to_string(k + 1), .clips = {clip(300 + k, 4.0, 2.0)}});
    s.takes[1].star = true;
    s.parts = {10};
    t.stacks.push_back(s);
    t.clips.push_back(clip(10, 4.0, 2.0, 100, 300 + static_cast<std::uint64_t>(playing)));
    return t;
}
} // namespace

TEST_CASE("take stacks: the badge shows the take a clip plays from two takes on", "[fast]")
{
    auto t = trackWithTakes();
    auto b = badge(t.stacks[0], t.clips, false);
    REQUIRE(b);
    CHECK(b->text == "2/3");
    CHECK(b->star);
    CHECK_FALSE(b->comp);
    CHECK(playingTake(t.stacks[0], t.clips) == 1u);

    // Narrow: just the chevron. One take: nothing at all.
    CHECK(badge(t.stacks[0], t.clips, true)->text.empty());
    auto single = t;
    single.stacks[0].takes.resize(1);
    single.clips[0].fromTake = 300;
    CHECK_FALSE(badge(single.stacks[0], single.clips, false));
    CHECK_FALSE(hasTakesToShow(single.stacks[0]));

    // Parts playing different takes: a comp.
    t.clips = {clip(10, 4.0, 1.0, 100, 301), clip(11, 5.0, 1.0, 100, 302)};
    t.stacks[0].parts = {10, 11};
    b = badge(t.stacks[0], t.clips, false);
    CHECK(b->text == "Comp");
    CHECK(b->comp);
    CHECK_FALSE(playingTake(t.stacks[0], t.clips));
}

TEST_CASE("take stacks: open takes grow the row; strips are newest on top", "[fast]")
{
    auto t = trackWithTakes();
    CHECK(takeStripsHeight(t, 24.0) == 0.0);
    t.stacks[0].open = true;
    CHECK(takeStripsHeight(t, 24.0) == 72.0);

    auto hit = hitStrip(t, 5.0, 2.0, 24.0); // the top strip
    REQUIRE(hit);
    CHECK(hit->take == 202); // Take 3, the newest
    CHECK(hit->onClip);
    CHECK(hitStrip(t, 5.0, 50.0, 24.0)->take == 200);
    CHECK_FALSE(hitStrip(t, 5.0, 80.0, 24.0)); // below the strips
    CHECK_FALSE(hitStrip(t, 7.0, 2.0, 24.0));  // outside the clip's range

    // A take with a gap: the strip is there, its clip isn't.
    t.stacks[0].takes[2].clips = {clip(302, 4.0, 0.5)};
    hit = hitStrip(t, 5.0, 2.0, 24.0);
    REQUIRE(hit);
    CHECK_FALSE(hit->onClip);
}

TEST_CASE("take stacks: a clip's parts are one clip; their inner edges aren't trim handles", "[fast]")
{
    auto t = trackWithTakes();
    t.clips = {clip(9, 0.0, 2.0), clip(10, 4.0, 1.0, 100, 301), clip(11, 5.0, 1.0, 100, 302)};
    const auto groups = clipGroups(t);
    REQUIRE(groups.size() == 2);
    CHECK(groups[1].parts.size() == 2);
    CHECK(groups[1].start() == 4.0);
    CHECK(groups[1].end() == 6.0);
    CHECK(groups[1].stack->id == 100);

    CHECK_FALSE(isInnerEdge(t, t.clips[1], true));
    CHECK(isInnerEdge(t, t.clips[1], false));
    CHECK(isInnerEdge(t, t.clips[2], true));
    CHECK_FALSE(isInnerEdge(t, t.clips[2], false));
    CHECK_FALSE(isInnerEdge(t, t.clips[0], true));

    Snapshot s;
    s.tracks.push_back(t);
    CHECK(clipGroup(s, 11) == std::vector<std::uint64_t>{10, 11});
    CHECK(withGroups(s, {9, 10}) == std::set<std::uint64_t>{9, 10, 11});

    // The selection takes whole groups; the panels count them as one clip.
    const auto sel = withClips(s, {11}, std::nullopt, std::nullopt);
    CHECK(sel.clips == std::set<std::uint64_t>{10, 11});
    const auto ctx = panelContext(sel, s, CentreView::Arrange);
    CHECK(ctx.subject == Subject::Clip);
    CHECK(ctx.clipCount == 1);
    CHECK(ctx.clip->id == 10);
    CHECK(pruned(Selection{.channel = 1, .clips = {10}}, s).clips == std::set<std::uint64_t>{10, 11});
}

TEST_CASE("take stacks: a replacement for a deleted take; overlapping lanes group", "[fast]")
{
    auto t = trackWithTakes();
    CHECK(replacementTake(t.stacks[0], {201}, 4.0, 6.0) == 2u); // the newest other
    CHECK(replacementTake(t.stacks[0], {201, 202}, 4.0, 6.0) == 0u);
    CHECK_FALSE(replacementTake(t.stacks[0], {200, 201, 202}, 4.0, 6.0));
    t.stacks[0].takes[2].clips = {clip(302, 5.5, 2.0)};
    CHECK(replacementTake(t.stacks[0], {201}, 4.0, 6.0) == 0u); // take 3 doesn't cover it
    CHECK(replacementTake(t.stacks[0], {200, 201}, 4.0, 6.0) == 2u); // but overlaps

    const auto groups = overlapGroups({{4.0, 6.0}, {10.0, 12.0}, {5.0, 7.0}, {6.5, 8.0}, {20.0, 21.0}});
    REQUIRE(groups.size() == 3);
    CHECK(groups[0] == std::vector<size_t>{0, 2, 3});
    CHECK(groups[1] == std::vector<size_t>{1});
    CHECK(groups[2] == std::vector<size_t>{4});
}

TEST_CASE("stack edits: moving a part moves its stack; outer trims trim its range", "[fast]")
{
    // Track 1: plain clip 9, then a stack's two parts 10 and 11 (4..5, 5..6).
    const std::vector<ClipPlace> clips{{9, 1, 0, 0.0, 2.0, 0.0}, {10, 1, 100, 4.0, 1.0, 0.0}, {11, 1, 100, 5.0, 1.0, 1.0}};
    // A lone part moved by 2 s to track 2: its stack and its other part follow.
    auto g = groupEdits(clips, {{10, 2, 6.0, 1.0, 0.0}});
    REQUIRE(g.moves.size() == 1);
    CHECK(g.moves[0].stack == 100);
    CHECK(g.moves[0].toTrack == 2);
    CHECK(g.moves[0].delta == Approx(2.0));
    REQUIRE(g.edits.size() == 2);
    CHECK(std::any_of(g.edits.begin(), g.edits.end(), [](const auto& e) { return e.clip == 11 && e.track == 2 && e.start == 7.0; }));

    // Both parts asked to move differently: the first move wins.
    g = groupEdits(clips, {{10, 1, 5.0, 1.0, 0.0}, {11, 1, 9.0, 1.0, 1.0}});
    REQUIRE(g.edits.size() == 2);
    CHECK(g.edits[1].start == Approx(6.0));

    // Trims: the first part's start and the last part's end set the range; the inner edge doesn't.
    g = groupEdits(clips, {{10, 1, 3.5, 1.5, -0.5}});
    REQUIRE(g.trims.size() == 1);
    CHECK(g.trims[0].start == 3.5);
    CHECK_FALSE(g.trims[0].end);
    g = groupEdits(clips, {{11, 1, 5.0, 1.5, 1.0}});
    REQUIRE(g.trims.size() == 1);
    CHECK(g.trims[0].end == 6.5);
    g = groupEdits(clips, {{10, 1, 4.0, 0.5, 0.0}}); // the inner edge: comping, later
    CHECK(g.trims.empty());
    CHECK(g.edits.size() == 1);

    // A plain clip passes through; a move that goes nowhere moves no stack.
    g = groupEdits(clips, {{9, 1, 1.0, 2.0, 0.0}, {10, 1, 4.0, 1.0, 0.0}});
    CHECK(g.moves.empty());
    CHECK(g.edits.size() == 3);
}

TEST_CASE("stack edits: duplicates copy the stack once; splits; settling", "[fast]")
{
    const std::vector<ClipPlace> clips{{9, 1, 0, 0.0, 2.0, 0.0}, {10, 1, 100, 4.0, 1.0, 0.0}, {11, 1, 100, 5.0, 1.0, 1.0}};
    auto [edits, copies] = groupPlacements(clips, {{10, 1, 8.0, 1.0, 0.0}});
    REQUIRE(copies.size() == 1);
    CHECK(copies[0].delta == Approx(4.0));
    REQUIRE(edits.size() == 2);
    CHECK(edits[1].clip == 11);
    CHECK(edits[1].start == Approx(9.0));
    std::tie(edits, copies) = groupPlacements(clips, {{10, 1, 8.0, 1.0, 0.0}, {11, 1, 9.0, 1.0, 1.0}, {9, 1, 2.0, 2.0, 0.0}});
    CHECK(copies.size() == 1);
    CHECK(edits.size() == 3);

    CHECK(withParts(clips, {11}) == std::vector<std::uint64_t>{11, 10});

    // A split: inside a part cuts it and the stack; at a part boundary only the stack.
    const std::vector<StackRange> stacks{{100, 4.0, 6.0}};
    auto plan = splitPlan(clips, stacks, {10}, 5.5);
    CHECK(plan.clips == std::vector<std::uint64_t>{11});
    CHECK(plan.stacks == std::vector<std::uint64_t>{100});
    plan = splitPlan(clips, stacks, {11}, 5.0);
    CHECK(plan.clips.empty());
    CHECK(plan.stacks == std::vector<std::uint64_t>{100});
    plan = splitPlan(clips, stacks, {9}, 1.0);
    CHECK(plan.clips == std::vector<std::uint64_t>{9});
    CHECK(plan.stacks.empty());

    // Settling: nothing to do; then a foreign clip between the parts; a stack without parts; a
    // dangling stack; a part past the range.
    auto settle = settleStacks(clips, stacks);
    CHECK(settle.plain.empty());
    CHECK(settle.remove.empty());
    CHECK(settle.ranges.empty());
    settle = settleStacks({{10, 1, 100, 4.0, 0.5, 0.0}, {12, 1, 0, 4.5, 0.5, 0.0}, {11, 1, 100, 5.0, 1.0, 1.0}}, stacks);
    CHECK(settle.plain == std::vector<std::uint64_t>{11});
    settle = settleStacks({{9, 1, 0, 0.0, 2.0, 0.0}}, stacks);
    CHECK(settle.remove == std::vector<std::uint64_t>{100});
    settle = settleStacks({{9, 1, 555, 0.0, 2.0, 0.0}}, {});
    CHECK(settle.plain == std::vector<std::uint64_t>{9});
    settle = settleStacks({{10, 1, 100, 4.0, 3.0, 0.0}}, stacks);
    REQUIRE(settle.ranges.size() == 1);
    CHECK(settle.ranges[0].end == Approx(7.0));

    const auto trimmed = trimPlace({10, 1, 100, 4.0, 2.0, 1.0}, 4.5, 5.0);
    CHECK(trimmed.start == 4.5);
    CHECK(trimmed.length == Approx(0.5));
    CHECK(trimmed.offset == Approx(1.5));
}

TEST_CASE("take stacks: rows grow by their extra height; clips aren't hit in it", "[fast]")
{
    TrackLayout layout;
    layout.setTrackCount(2);
    layout.setExtra(0, 72.0);
    CHECK(layout.height(0) == 64.0 + 72.0);
    CHECK(layout.baseHeight(0) == 64.0);
    CHECK(layout.top(1) == Approx(136.0));
    layout.setHeight(0, 100.0); // the user's height stays the base
    CHECK(layout.height(0) == 172.0);
    CHECK(layout.hit(150.0)->track == 0);

    Snapshot s;
    s.tracks.push_back(trackWithTakes());
    s.tracks[0].stacks[0].open = true;
    s.tracks.push_back({.id = 2});
    Timeline tl;
    tl.setWidth(1000.0);
    tl.setView(0.0, 100.0);
    CHECK(hitClip(s, layout, tl, 500.0, 50.0));         // in the clip
    CHECK_FALSE(hitClip(s, layout, tl, 500.0, 120.0)); // in its open takes
}
