// The selection and what the context panels show for it (M2 step A5, docs/design/selection.md).

#include "model/Selection.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Section = InspectorSection;

namespace
{
ClipInfo clip(std::uint64_t id, ClipInfo::Kind kind, double start)
{
    return {.id = id, .kind = kind, .name = "clip " + std::to_string(id), .start = start, .length = 2.0};
}

/// Tracks 1 (an instrument: clips 11 MIDI, 12 audio) and 2 (audio: clip 21), return 5, master 9.
Snapshot project()
{
    Snapshot s;
    s.tracks.push_back({.id = 1, .kind = TrackInfo::Kind::Instrument, .name = "Keys", .instrument = "Synth",
                        .clips = {clip(11, ClipInfo::Kind::Midi, 0.0), clip(12, ClipInfo::Kind::Audio, 4.0)}});
    s.tracks.push_back({.id = 2, .name = "Drums", .clips = {clip(21, ClipInfo::Kind::Audio, 0.0)}});
    s.returns.push_back({.id = 5, .role = TrackInfo::Role::Return, .name = "A Return"});
    s.master.id = 9;
    return s;
}
} // namespace

TEST_CASE("selection: the rule table for the arrangement", "[fast]")
{
    const auto s = project();
    auto in = [&](Selection sel) { return panelContext(sel, s, CentreView::Arrange); };

    SECTION("nothing")
    {
        const auto c = in({});
        CHECK(c.subject == Subject::Nothing);
        CHECK(!c.channel);
        CHECK(c.inspector.empty());
        CHECK(c.clipViewOffered);
        CHECK(!c.opensClipView);
        CHECK(!c.midiInputTarget);
    }
    SECTION("a track")
    {
        const auto c = in({.channel = 2});
        CHECK(c.subject == Subject::Channel);
        CHECK(c.channel->id == 2);
        CHECK(c.inspector == std::vector{Section::Track, Section::Input, Section::Mix, Section::Sends});
        CHECK(!c.opensClipView);
        CHECK(!c.midiInputTarget); // no instrument
    }
    SECTION("an instrument track takes live MIDI")
    {
        CHECK(in({.channel = 1}).midiInputTarget == 1u);
    }
    SECTION("a return and the master")
    {
        const auto r = in({.channel = 5});
        CHECK(r.subject == Subject::Channel);
        CHECK(r.inspector == std::vector{Section::Return, Section::Mix});
        CHECK(!r.midiInputTarget);
        const auto m = in({.channel = 9});
        CHECK(m.channel->role == TrackInfo::Role::Master);
        CHECK(m.inspector == std::vector{Section::Master, Section::Mix});
    }
    SECTION("one MIDI clip opens the clip view")
    {
        const auto c = in({.channel = 1, .clips = {11}});
        CHECK(c.subject == Subject::Clip);
        CHECK(c.clip->id == 11);
        CHECK(c.midiClip->id == 11);
        CHECK(c.opensClipView);
        CHECK(c.channel->id == 1);
        CHECK(c.midiInputTarget == 1u);
        CHECK(c.inspector == std::vector{Section::Clip, Section::Track, Section::Input, Section::Mix, Section::Sends});
    }
    SECTION("one audio clip shows the modules")
    {
        const auto c = in({.channel = 2, .clips = {21}});
        CHECK(c.subject == Subject::Clip);
        CHECK(!c.midiClip);
        CHECK(!c.opensClipView);
        CHECK(c.inspector == std::vector{Section::Clip, Section::Track, Section::Input, Section::Mix, Section::Sends});
    }
    SECTION("several clips")
    {
        const auto c = in({.channel = 1, .clips = {11, 21}});
        CHECK(c.subject == Subject::Clips);
        CHECK(c.clipCount == 2);
        CHECK(!c.clip);
        CHECK(!c.opensClipView);
        CHECK(c.channel->id == 1);
        CHECK(c.inspector == std::vector{Section::Clips, Section::Track, Section::Input, Section::Mix, Section::Sends});
    }
    SECTION("a clip not in the snapshot yet: its track, until it arrives")
    {
        const auto c = in({.channel = 1, .clips = {99}});
        CHECK(c.subject == Subject::Channel);
        CHECK(c.clipCount == 0);
        CHECK(c.channel->id == 1);
        CHECK(!c.opensClipView);
    }
}

TEST_CASE("selection: the mixer view shows modules only and no inspector", "[fast]")
{
    const auto s = project();
    for (const auto& sel : {Selection{}, Selection{.channel = 1}, Selection{.channel = 5}, Selection{.channel = 9},
                            Selection{.channel = 1, .clips = {11}}, Selection{.channel = 1, .clips = {11, 21}}})
    {
        const auto mix = panelContext(sel, s, CentreView::Mix);
        const auto arrange = panelContext(sel, s, CentreView::Arrange);
        CHECK(!mix.clipViewOffered);
        CHECK(mix.inspector.empty());
        // The same subject and channel in both views, so switching views changes nothing else.
        CHECK(mix.subject == arrange.subject);
        CHECK(mix.channel.has_value() == arrange.channel.has_value());
        if (mix.channel)
            CHECK(mix.channel->id == arrange.channel->id);
        CHECK(mix.key == arrange.key);
        CHECK(mix.midiInputTarget == arrange.midiInputTarget);
    }
}

TEST_CASE("selection: the key names the subject", "[fast]")
{
    auto s = project();
    const Selection one{.channel = 1, .clips = {11}};
    const auto before = panelContext(one, s, CentreView::Arrange).key;

    // Editing the clip (new notes, a new length) keeps the subject: Tab's choice stays.
    s.tracks[0].clips[0].length = 8.0;
    s.tracks[0].clips[0].notes.push_back({});
    CHECK(panelContext(one, s, CentreView::Arrange).key == before);

    // Another clip, or the track alone, is another subject.
    CHECK(panelContext({.channel = 1, .clips = {12}}, s, CentreView::Arrange).key != before);
    CHECK(panelContext({.channel = 1}, s, CentreView::Arrange).key != before);

    // A clip made just now counts as soon as the snapshot has it.
    const Selection made{.channel = 1, .clips = {99}};
    const auto waiting = panelContext(made, s, CentreView::Arrange);
    s.tracks[0].clips.push_back(clip(99, ClipInfo::Kind::Midi, 8.0));
    const auto arrived = panelContext(made, s, CentreView::Arrange);
    CHECK(waiting.key != arrived.key);
    CHECK(arrived.opensClipView);
}

TEST_CASE("selection: clips bring their channel", "[fast]")
{
    const auto s = project();
    SECTION("the clicked clip's track")
    {
        CHECK(withClips(s, {21}, 2, 2) == Selection{.channel = 2, .clips = {21}});
    }
    SECTION("a Shift-click adds a clip on another track: that track")
    {
        CHECK(withClips(s, {11, 21}, 2, 2).channel == 2u);
        CHECK(withClips(s, {11, 21}, 1, 1).channel == 1u);
    }
    SECTION("a Shift-click removed the clip: the topmost remaining clip's track")
    {
        CHECK(withClips(s, {21}, 1, 1).channel == 2u);
        CHECK(withClips(s, {11}, 2, 2).channel == 1u);
    }
    SECTION("a box: the topmost track holding one")
    {
        CHECK(withClips(s, {12, 21}, std::nullopt, 2).channel == 1u);
    }
    SECTION("no clips: the fallback")
    {
        CHECK(withClips(s, {}, std::nullopt, 2) == Selection{.channel = 2});
        CHECK(withClips(s, {}, 1, std::nullopt) == Selection{});
    }
    SECTION("clips not in the snapshot yet keep the fallback")
    {
        CHECK(withClips(s, {99}, 1, 1) == Selection{.channel = 1, .clips = {99}});
    }
}

TEST_CASE("selection: after a change to the project", "[fast]")
{
    auto s = project();
    SECTION("unchanged")
    {
        const Selection sel{.channel = 1, .clips = {11, 12}};
        CHECK(pruned(sel, s) == sel);
        CHECK(pruned({.channel = 5}, s) == Selection{.channel = 5});
    }
    SECTION("a deleted clip goes; its track stays")
    {
        s.tracks[0].clips.erase(s.tracks[0].clips.begin());
        CHECK(pruned({.channel = 1, .clips = {11}}, s) == Selection{.channel = 1});
    }
    SECTION("a gone channel goes")
    {
        s.returns.clear();
        CHECK(pruned({.channel = 5}, s) == Selection{});
    }
    SECTION("a clip moved to another track: the channel follows it")
    {
        s.tracks[1].clips.push_back(s.tracks[0].clips[0]);
        s.tracks[0].clips.erase(s.tracks[0].clips.begin());
        CHECK(pruned({.channel = 1, .clips = {11}}, s) == Selection{.channel = 2, .clips = {11}});
    }
}

TEST_CASE("selection: a lane and its points are the subject", "[fast]")
{
    auto s = project();
    s.automationLanes.push_back({.id = 40,
                                 .after = 1,
                                 .points = {{0.0, 0.5f, 0.0f}, {1.0, 0.7f, 0.0f}},
                                 .assignments = {{.channel = 2, .key = "volume", .name = "Volume", .module = "Mixer"},
                                                 {.channel = 1, .key = "pan", .name = "Pan", .module = "Mixer"}}});
    s.automationLanes.push_back({.id = 41});
    const auto lane = withLane(s, 40);
    CHECK(lane == Selection{.channel = 2, .lane = 40}); // its first assignment's channel
    CHECK(withLane(s, 41) == Selection{.lane = 41});   // none: no channel
    auto c = panelContext(lane, s, CentreView::Arrange);
    CHECK(c.subject == Subject::Lane);
    CHECK(c.lane->id == 40);
    CHECK(c.channel->id == 2);
    // A lane belongs to no track: its own sections only.
    CHECK(c.inspector == std::vector{Section::Lane, Section::Assignments});

    c = panelContext(withLane(s, 40, {1}), s, CentreView::Arrange);
    CHECK(c.pointCount == 1);
    CHECK(c.point->value == 0.7f);
    CHECK(c.inspector.front() == Section::Point);
    c = panelContext(withLane(s, 40, {0, 1}), s, CentreView::Arrange);
    CHECK(c.inspector.front() == Section::Points);
    CHECK_FALSE(c.point);
    CHECK(panelContext(lane, s, CentreView::Mix).inspector.empty());
    c = panelContext(withLane(s, 41), s, CentreView::Arrange);
    CHECK(c.subject == Subject::Lane);
    CHECK_FALSE(c.channel);

    // Pruning: points that are gone, the lane when it goes, and the channel follows the first
    // assignment.
    CHECK(pruned({.channel = 2, .lane = 40, .points = {1, 5}}, s) == Selection{.channel = 2, .lane = 40, .points = {1}});
    CHECK(pruned({.channel = 2, .lane = 99, .points = {0}}, s) == Selection{.channel = 2});
    auto reassigned = s;
    reassigned.automationLanes[0].assignments.erase(reassigned.automationLanes[0].assignments.begin());
    CHECK(pruned(lane, reassigned) == Selection{.channel = 1, .lane = 40});
    // Clips win over a lane (they never mix).
    CHECK(pruned({.channel = 1, .clips = {11}, .lane = 40}, s) == Selection{.channel = 1, .clips = {11}});
}
