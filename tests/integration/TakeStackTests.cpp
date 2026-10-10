// Takes on clips (M2 step C5b revised, ADR 0032, docs/design/recording.md §6): a clip and its
// takes are recorded, edited and deleted as one, through tracktion's hosted test device.

#include "RecordingFixture.h"
#include "model/Takes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace recording_test;

namespace
{
/// A loop pass over [4, 6] of two laps and a half: one clip with three takes (the third short,
/// 4..5), playing the second, the last complete one. Disarms after.
void threeTakes(Recording& r, std::uint64_t track)
{
    r.input = sine;
    r.session->setAudioInput(track, r.session->audioInputs().front().id);
    r.session->setArmed(track, true, true);
    r.session->setLoopRange(4.0, 6.0);
    r.session->setLooping(true);
    r.session->setPosition(4.0);
    r.recordFor(5.0);
    r.session->setLooping(false);
    r.session->setArmed(track, false, false);
}
} // namespace

TEST_CASE("take stacks: a loop pass makes one clip with its takes, opened; one pass shows none", "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t);
    auto info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    REQUIRE(info.stacks.size() == 1);
    const auto& s = info.stacks[0];
    CHECK(s.takes.size() == 3);
    CHECK(s.open);
    CHECK(s.start == Approx(4.0).margin(0.02));
    CHECK(s.end == Approx(6.0).margin(0.02));
    CHECK(s.parts == std::vector{info.clips[0].id});
    CHECK(info.clips[0].stack == s.id);
    CHECK(fd::model::playingTake(s, info.clips) == 1u);
    CHECK(fd::model::badge(s, info.clips, false)->text == "2/3");
    CHECK(r.session->undoDescription() == "Record");

    // A single pass: its clip keeps its one take, and nothing shows.
    const auto u = r.track(1);
    r.session->setAudioInput(u, r.session->audioInputs().front().id);
    r.session->setArmed(u, true, true);
    r.session->setPosition(0.0);
    r.recordFor(1.0);
    info = r.info(u);
    REQUIRE(info.stacks.size() == 1);
    CHECK(info.stacks[0].takes.size() == 1);
    CHECK_FALSE(info.stacks[0].open);
    CHECK_FALSE(fd::model::badge(info.stacks[0], info.clips, false));
}

TEST_CASE("take stacks: a pass over part of a clip joins its takes, and keeps a plain clip it covers first",
          "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t);
    // A second pass over 5..7: one clip playing two takes, its range both.
    r.session->setArmed(t, true, true);
    r.session->setPosition(5.0);
    r.recordFor(2.0);
    auto info = r.info(t);
    REQUIRE(info.stacks.size() == 1);
    const auto& s = info.stacks[0];
    CHECK(s.takes.size() == 4);
    CHECK(s.takes.back().name == "Take 4");
    CHECK(s.start == Approx(4.0).margin(0.02));
    CHECK(s.end == Approx(7.0).margin(0.02));
    REQUIRE(info.clips.size() == 2);
    CHECK(s.parts.size() == 2);
    CHECK(info.clips[0].stack == s.id);
    CHECK(info.clips[1].stack == s.id);
    CHECK(fd::model::badge(s, info.clips, false)->comp);
    CHECK(fd::model::clipGroups(info).size() == 1); // one clip to the user

    // Over a plain clip: it's kept as a take first, cut to what the pass covers.
    const auto u = r.track(1);
    REQUIRE(r.session->createMidiClip(u, 0.0, 2.0) != 0);
    r.session->setAudioInput(u, r.session->audioInputs().front().id);
    r.session->setArmed(u, true, true);
    r.session->setPosition(1.0);
    r.recordFor(2.0);
    info = r.info(u);
    REQUIRE(info.stacks.size() == 1);
    REQUIRE(info.stacks[0].takes.size() == 2);
    CHECK(info.stacks[0].takes[0].name == "Before take 1");
    REQUIRE(info.stacks[0].takes[0].clips.size() == 1);
    CHECK(info.stacks[0].takes[0].clips[0].start == Approx(1.0).margin(0.02));
    CHECK(info.stacks[0].takes[0].clips[0].end() == Approx(2.0).margin(0.02));
    REQUIRE(info.clips.size() == 2); // the MIDI clip's head, then the recording
    CHECK(info.clips[0].stack == 0);
    CHECK(info.clips[1].stack == info.stacks[0].id);
}

TEST_CASE("take stacks: moving, trimming, splitting, duplicating and deleting a clip carry its takes", "[integration]")
{
    Recording r;
    const auto t = r.track(0), u = r.track(1);
    threeTakes(r, t);
    const auto clip = r.info(t).clips.at(0);
    auto firstTakeStart = [](const fd::model::TrackInfo& i) { return i.stacks.at(0).takes.at(0).clips.at(0).start; };

    // Later in time: the takes and the range follow, in one undo step.
    r.session->applyClipEdits({{clip.id, t, clip.start + 2.0, clip.length, clip.offset}});
    CHECK(r.session->undoDescription() == "Move clips");
    auto info = r.info(t);
    REQUIRE(info.stacks.size() == 1);
    CHECK(info.stacks[0].start == Approx(6.0).margin(0.02));
    CHECK(firstTakeStart(info) == Approx(6.0).margin(0.02));
    CHECK(info.clips.at(0).stack == info.stacks[0].id);
    r.session->undo();
    CHECK(r.info(t).stacks.at(0).start == Approx(4.0).margin(0.02));
    CHECK(firstTakeStart(r.info(t)) == Approx(4.0).margin(0.02));

    // To another track: its takes go along.
    r.session->applyClipEdits({{clip.id, u, clip.start, clip.length, clip.offset}});
    CHECK(r.info(t).stacks.empty());
    REQUIRE(r.info(u).stacks.size() == 1);
    CHECK(r.info(u).clips.at(0).stack == r.info(u).stacks[0].id);
    r.session->undo();
    CHECK(r.info(t).stacks.size() == 1);
    CHECK(r.info(u).stacks.empty());

    // Trimmed in and back out: the range follows; the takes stay whole.
    r.session->applyClipEdits({{clip.id, t, clip.start, 1.0, clip.offset}});
    info = r.info(t);
    CHECK(info.stacks.at(0).end == Approx(clip.start + 1.0).margin(1e-6));
    CHECK(info.stacks[0].takes[0].clips[0].length == Approx(2.0).margin(0.02));
    r.session->applyClipEdits({{clip.id, t, clip.start, clip.length, clip.offset}});
    CHECK(r.info(t).stacks.at(0).end == Approx(clip.end()).margin(1e-6));
    r.session->undo();
    r.session->undo();

    // Split at 5.5 s: two clips, each with its takes' halves (the short third lap stays left).
    const auto right = r.session->splitClips({clip.id}, 5.5);
    REQUIRE(right.size() == 1);
    CHECK(r.session->undoDescription() == "Split clips");
    info = r.info(t);
    REQUIRE(info.stacks.size() == 2);
    CHECK(info.stacks[0].end == Approx(5.5));
    CHECK(info.stacks[1].start == Approx(5.5));
    CHECK(info.stacks[0].takes.size() == 3);
    CHECK(info.stacks[1].takes.size() == 2);
    for (const auto& s : info.stacks)
    {
        CHECK(s.parts.size() == 1);
        for (const auto& k : s.takes)
            for (const auto& c : k.clips)
            {
                CHECK(c.start >= s.start - 1e-6);
                CHECK(c.end() <= s.end + 1e-6);
            }
    }
    REQUIRE(info.clips.size() == 2);
    CHECK(info.clips[0].stack == info.stacks[0].id);
    CHECK(info.clips[1].stack == info.stacks[1].id);
    r.session->undo();
    REQUIRE(r.info(t).stacks.size() == 1);
    CHECK(r.info(t).stacks[0].takes.size() == 3);

    // Duplicated: the copy has its own copy of the takes.
    const auto copies = r.session->duplicateClips({{clip.id, t, 8.0, clip.length, clip.offset}});
    REQUIRE(copies.size() == 1);
    info = r.info(t);
    REQUIRE(info.stacks.size() == 2);
    CHECK(info.stacks[1].start == Approx(8.0).margin(0.02));
    CHECK(info.stacks[1].parts == std::vector{copies[0]});
    CHECK(info.stacks[1].takes[0].clips[0].id != info.stacks[0].takes[0].clips[0].id);
    CHECK(info.stacks[1].takes[0].clips[0].start == Approx(8.0).margin(0.02));
    r.session->undo();

    // Deleted: its takes go with it; undo brings them back.
    r.session->deleteClips({clip.id});
    CHECK(r.info(t).stacks.empty());
    r.session->undo();
    REQUIRE(r.info(t).stacks.size() == 1);
    CHECK(r.info(t).clips.at(0).stack == r.info(t).stacks[0].id);
}

TEST_CASE("take stacks: a clip placed over one keeps its takes whole; deleting, keeping and stepping takes",
          "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t);
    auto info = r.info(t);
    const auto stack = info.stacks.at(0).id;

    // A MIDI clip over the middle: the head keeps the takes, the tail is a plain clip, the range
    // stays whole; using a take fills it again.
    REQUIRE(r.session->createMidiClip(t, 4.8, 0.4) != 0);
    info = r.info(t);
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[0].stack == stack);
    CHECK(info.clips[2].stack == 0);
    CHECK(info.clips[2].fromTake == 0);
    CHECK(info.stacks.at(0).start == Approx(4.0).margin(0.02));
    CHECK(info.stacks.at(0).end == Approx(6.0).margin(0.02));
    REQUIRE(r.session->useTake(t, info.stacks[0].takes[0].id));
    info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].stack == stack);
    r.session->undo();
    r.session->undo();

    // Deleting the take it plays: it plays another (the short lap doesn't cover it).
    info = r.info(t);
    r.session->deleteTakes(t, {info.stacks[0].takes[1].id});
    CHECK(r.session->undoDescription() == "Delete take");
    info = r.info(t);
    REQUIRE(info.stacks.size() == 1);
    CHECK(info.stacks[0].takes.size() == 2);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].fromTake == info.stacks[0].takes.at(0).clips.at(0).id);
    r.session->undo();

    // Stepping: Alt+Down plays the next take that covers the clip (skipping the short lap).
    info = r.info(t);
    REQUIRE(r.session->stepTake(info.clips.at(0).id, 1));
    info = r.info(t);
    CHECK(info.clips.at(0).fromTake == info.stacks[0].takes.at(0).clips.at(0).id);
    r.session->undo();

    // Keep only this take: one take left, playing.
    info = r.info(t);
    REQUIRE(r.session->keepOnlyTake(t, info.stacks[0].takes[0].id));
    CHECK(r.session->undoDescription() == "Keep only this take");
    info = r.info(t);
    REQUIRE(info.stacks.size() == 1);
    CHECK(info.stacks[0].takes.size() == 1);
    CHECK(info.clips.at(0).fromTake == info.stacks[0].takes[0].clips.at(0).id);
    r.session->undo();
    CHECK(r.info(t).stacks.at(0).takes.size() == 3);

    // Moving the takes off to a new track leaves a plain clip there and keeps this clip playing.
    info = r.info(t);
    const auto made = r.session->takesToTracks(t, {info.stacks[0].takes[0].id}, true);
    REQUIRE(made.size() == 1);
    CHECK(r.info(made[0]).stacks.empty());
    CHECK(r.info(made[0]).clips.size() == 1);
    CHECK(r.info(t).stacks.at(0).takes.size() == 2);
}

TEST_CASE("take stacks: a C5b project's take lanes become its clips' takes", "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t);
    // Back to C5b's shape: TAKEs right under FDTAKES, fdShowTakes on the track, format 3.
    auto& impl = r.session->impl();
    auto* track = impl.track(t);
    REQUIRE(track != nullptr);
    auto takes = track->state.getChildWithName(Session::Impl::takesId);
    auto stack = takes.getChild(0);
    while (stack.getNumChildren() > 0)
    {
        auto take = stack.getChild(0);
        stack.removeChild(take, nullptr);
        takes.appendChild(take, nullptr);
    }
    takes.removeChild(stack, nullptr);
    track->state.setProperty(Session::Impl::showTakesId, true, nullptr);
    impl.edit->state.setProperty("fdFormat", 3, nullptr);
    const auto file = r.root.getChildFile("Old").getChildFile("Old.segno");
    REQUIRE(r.session->saveAs(file));

    REQUIRE(r.session->open(file));
    const auto info = r.info(r.track(0));
    REQUIRE(info.stacks.size() == 1);
    CHECK(info.stacks[0].takes.size() == 3);
    CHECK(info.stacks[0].start == Approx(4.0).margin(0.02));
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].stack == info.stacks[0].id);
    CHECK_FALSE(r.session->canUndo());
    CHECK_FALSE(r.session->hasUnsavedChanges());
}
