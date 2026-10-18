// Groups (ADR 0072, docs/design/groups.md): what a group holds, read from the snapshot; the
// arrangement's rows with groups, their depth and folding (§6); the header's and the strip's slots
// at each depth (§6.1, §6.3, §9); a time selection on a group's row (§6.5); the inspector's
// sections for a group, a member and several tracks (§8, §10).

#include "model/ArrangementRows.h"
#include "model/ChannelStripLayout.h"
#include "model/Groups.h"
#include "model/Selection.h"
#include "model/TrackHeaderLayout.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;

namespace
{
TrackInfo track(std::uint64_t id, std::uint64_t group = 0)
{
    TrackInfo t{.id = id, .name = "T" + std::to_string(id)};
    t.group = group;
    return t;
}

TrackInfo group(std::uint64_t id, std::uint64_t parent = 0)
{
    TrackInfo g{.id = id, .role = TrackInfo::Role::Group, .name = "G" + std::to_string(id)};
    g.group = parent;
    return g;
}

/// All (100) ▸ Rhythm (101) ▸ Drums (102: tracks 1, 2), Bass track 3 in Rhythm; Lead 4 in none.
Snapshot nested()
{
    Snapshot s;
    s.tracks = {group(100),    group(101, 100), group(102, 101), track(1, 102),
                track(2, 102), track(3, 101),   track(4)};
    return s;
}

bool apart(const HeaderRect& a, const HeaderRect& b)
{
    return a.right() <= b.x || b.right() <= a.x || a.bottom() <= b.y || b.bottom() <= a.y;
}
} // namespace

TEST_CASE("groups: what a group holds, how deep a channel is, and its half-lit mute and solo", "[fast]")
{
    auto s = nested();
    const auto& kick = s.tracks[3];
    CHECK(parentGroup(s, kick)->id == 102);
    CHECK(groupDepth(s, kick) == 3);
    CHECK(groupDepth(s, s.tracks[6]) == 0);
    CHECK(isInGroup(s, kick, 100));
    CHECK_FALSE(isInGroup(s, s.tracks[6], 100));
    const auto above = groupsAbove(s, kick);
    REQUIRE(above.size() == 3);
    CHECK(above.front()->id == 100);             // outermost first
    CHECK(membersOf(s, 101, false).size() == 2); // Drums and the Bass track
    CHECK(membersOf(s, 101, true).size() == 4);
    CHECK(tracksIn(s, 100) == 3);
    CHECK(tracksText(3) == "3 tracks");
    CHECK(tracksText(1) == "1 track");
    CHECK(hiddenByFolding(s, kick, {101}));
    CHECK_FALSE(hiddenByFolding(s, s.tracks[1], {101})); // a folded group's own row stays
    CHECK_FALSE(hiddenByFolding(s, kick, {}));

    // A soloed member: its groups' solos half lit; a muted group: its members' mutes.
    s.tracks[4].mix.solo = true;
    CHECK(holdsSolo(s, s.tracks[0]));
    CHECK(holdsSolo(s, s.tracks[2]));
    CHECK_FALSE(holdsSolo(s, s.tracks[4])); // a track holds nothing
    s.tracks[1].mix.mute = true;
    CHECK(inMutedGroup(s, kick));
    CHECK_FALSE(inMutedGroup(s, s.tracks[1]));
    CHECK_FALSE(inMutedGroup(s, s.tracks[6]));
}

TEST_CASE(
    "groups: the arrangement's rows, each group before what it holds, at their depth; a folded group hides "
    "its rows; a lane after a group comes after them",
    "[fast]")
{
    auto s = nested();
    s.automationLanes.push_back({.id = 50, .after = 1});   // among the drums
    s.automationLanes.push_back({.id = 51, .after = 102}); // after the Drums group
    auto rows = arrangementRows(s, false);
    REQUIRE(rows.size() == 9);
    CHECK(rows[0].isGroup());
    CHECK(rows[0].depth == 0);
    CHECK(rows[2].depth == 2);
    CHECK(rows[3].channel == 1);
    CHECK(rows[3].depth == 3);
    CHECK(rows[4].lane == 50); // after its track, as deep
    CHECK(rows[4].depth == 3);
    CHECK(rows[5].channel == 2);
    CHECK(rows[6].lane == 51); // after the Drums' rows, at the Drums' depth
    CHECK(rows[6].depth == 2);
    CHECK(rows[7].channel == 3);
    CHECK(rows[8].channel == 4);

    // Drums folded: its tracks and the lane among them go; the lane after it stays.
    rows = arrangementRows(s, false, true, {102});
    REQUIRE(rows.size() == 6);
    CHECK(rows[2].channel == 102);
    CHECK(rows[3].lane == 51);
    // All folded: one row, and Lead.
    rows = arrangementRows(s, false, true, {100});
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].channel == 100);
    CHECK(rows[1].channel == 4);
}

TEST_CASE(
    "groups: a header's colour bars a step apart, its fold triangle before its name, nothing touching at "
    "depth four in 180 px",
    "[fast]")
{
    const TrackHeaderMetrics m;
    auto s = layoutTrackHeader(180.0, 64.0, 3, true);
    CHECK(s.groupBars.empty());
    CHECK(s.colour.x == 0.0);
    CHECK(s.fold.isEmpty());

    // One group: its bar on the edge, its own a step in, the name after it.
    s = layoutTrackHeader(180.0, 64.0, 3, true, m, 1);
    REQUIRE(s.groupBars.size() == 1);
    CHECK(s.groupBars[0].x == 0.0);
    CHECK(s.colour.x == m.groupIndent);
    CHECK(s.name.x == m.groupIndent + m.paddingLeft);

    // A group: the triangle, then its name.
    s = layoutTrackHeader(180.0, 64.0, 2, true, m, 1, true);
    CHECK(s.fold.x == m.groupIndent + m.paddingLeft);
    CHECK(s.name.x == s.fold.right() + m.foldGap);
    CHECK(s.subtitle.x == s.name.x);

    // Four deep, a track and a group: everything inside, apart.
    for (const bool fold : {false, true})
    {
        s = layoutTrackHeader(180.0, 64.0, fold ? 2 : 3, true, m, 4, fold);
        REQUIRE(s.groupBars.size() == 4);
        std::vector<HeaderRect> parts = s.groupBars;
        parts.push_back(s.colour);
        if (fold)
            parts.push_back(s.fold);
        parts.push_back(s.name);
        parts.insert(parts.end(), s.buttons.begin(), s.buttons.end());
        parts.push_back(s.meter);
        CHECK(s.name.w >= 24.0);
        for (size_t i = 0; i < parts.size(); ++i)
            for (size_t j = i + 1; j < parts.size(); ++j)
                CHECK(apart(parts[i], parts[j]));
    }
    // Deeper: the indent stays, the bars come closer, still apart.
    s = layoutTrackHeader(180.0, 64.0, 3, true, m, 6);
    REQUIRE(s.groupBars.size() == 6);
    CHECK(s.colour.x == m.groupIndent * m.maxIndentDepth);
    for (size_t i = 0; i + 1 < s.groupBars.size(); ++i)
        CHECK(apart(s.groupBars[i], s.groupBars[i + 1]));
    CHECK(apart(s.groupBars.back(), s.colour));
}

TEST_CASE("groups: a strip keeps rows for the deepest group bars, so every fader starts at the same height",
          "[fast]")
{
    const ChannelStripOptions options{.groupRows = 2};
    const auto top = layoutChannelStrip(600.0, options);
    const auto member = layoutChannelStrip(600.0, options, {}, 2);
    const auto group = layoutChannelStrip(600.0, options, {}, 1, true);
    CHECK(top.groupBars.empty());
    REQUIRE(member.groupBars.size() == 2);
    CHECK(member.groupBars[0].y < member.groupBars[1].y);
    CHECK(member.groupBars[1].bottom() <= member.colour.y);
    CHECK(top.colour.y == member.colour.y);
    CHECK(top.fader.y == member.fader.y);
    CHECK(group.fader.y == top.fader.y);
    CHECK(group.fold.right() < group.name.x);
    // No groups shown: as before.
    CHECK(layoutChannelStrip(600.0, {}).colour.y == 0.0);
}

TEST_CASE("groups: a time selection on a group's row covers every row in it; the inspector shows a group, a "
          "member's group, and several tracks",
          "[fast]")
{
    const auto s = nested();
    const auto time = withTime(s, {1.0, 2.0}, {102}, {});
    CHECK(time.rowTracks == std::set<std::uint64_t>{102, 1, 2});

    auto ctx = panelContext({.channel = 102}, s, CentreView::Arrange);
    CHECK(ctx.inspector == std::vector<InspectorSection>{InspectorSection::Group, InspectorSection::Mix,
                                                         InspectorSection::Sends});
    CHECK(ctx.groupTracks == 2);
    ctx = panelContext({.channel = 1}, s, CentreView::Arrange);
    REQUIRE(ctx.group);
    CHECK(ctx.group->id == 102);
    ctx = panelContext({.channel = 4, .rowTracks = {1, 3, 4}}, s, CentreView::Arrange);
    CHECK(ctx.trackCount == 3);
    CHECK(ctx.inspector == std::vector<InspectorSection>{InspectorSection::Tracks, InspectorSection::Mix});
    // Several selected tracks stay through a new snapshot while they exist.
    CHECK(pruned({.channel = 4, .rowTracks = {1, 3, 4}}, s).rowTracks == std::set<std::uint64_t>{1, 3, 4});
}
