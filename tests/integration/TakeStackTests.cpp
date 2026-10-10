// Takes on clips (M2 step C5b revised, ADR 0032, docs/design/recording.md §6): a clip and its
// takes are recorded, edited and deleted as one, through tracktion's hosted test device.

#include "RecordingFixture.h"
#include "model/Takes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>

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

/// One pass on `track` from `from` for `seconds`, armed for it.
void pass(Recording& r, std::uint64_t track, double from, double seconds)
{
    r.session->setArmed(track, true, true);
    r.session->setPosition(from);
    r.recordFor(seconds);
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

TEST_CASE("take stacks: a pass makes its own clip, cutting what it covers in part and keeping it as \"Before take\"",
          "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t);
    // A second pass over 5..7 (ADR 0037): a clip of its own; the first is cut to 4..5 and keeps its
    // takes, its range with it; what played over 5..6 is the new clip's "Before take 4".
    pass(r, t, 5.0, 2.0);
    auto info = r.info(t);
    REQUIRE(info.clips.size() == 2);
    REQUIRE(info.stacks.size() == 2);
    CHECK(fd::model::clipGroups(info).size() == 2);
    const auto& old = info.stacks[0];
    CHECK(old.takes.size() == 3);
    CHECK(old.end == Approx(info.clips[0].end()));
    CHECK(info.clips[0].end() == Approx(5.0).margin(0.02));
    const auto& made = info.stacks[1];
    REQUIRE(made.takes.size() == 2);
    CHECK(made.takes[0].name == "Before take 4");
    CHECK(made.takes[1].name == "Take 4");
    REQUIRE(made.takes[0].clips.size() == 1);
    CHECK(made.takes[0].clips[0].start == Approx(5.0).margin(0.02));
    CHECK(made.takes[0].clips[0].end() == Approx(6.0).margin(0.02));
    CHECK(made.parts == std::vector{info.clips[1].id});
    CHECK(info.clips[1].fromTake == made.takes[1].clips.at(0).id);
    CHECK(fd::model::badge(made, info.clips, false)->text == "2/2");
    CHECK(r.session->undoDescription() == "Record");
    r.session->undo();
    info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.stacks.at(0).end == Approx(6.0).margin(0.02));

    // Over a plain clip: the same; what it covered is kept, cut to the pass.
    const auto u = r.track(1);
    REQUIRE(r.session->createMidiClip(u, 0.0, 2.0) != 0);
    r.session->setAudioInput(u, r.session->audioInputs().front().id);
    pass(r, u, 1.0, 2.0);
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

    // A punch inside a clip: the clip is split around it, and choosing "Before take" plays what
    // was there.
    const auto w = r.track(2);
    r.session->setAudioInput(w, r.session->audioInputs().front().id);
    pass(r, w, 0.0, 4.0);
    pass(r, w, 1.0, 1.0);
    info = r.info(w);
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[0].end() == Approx(1.0).margin(0.02));
    CHECK(info.clips[2].stack == 0); // the tail: a plain clip (ADR 0032)
    REQUIRE(info.stacks.size() == 2);
    const auto punch = info.stacks[1];
    REQUIRE(punch.takes.size() == 2);
    CHECK(punch.takes[0].name == "Before take 2");
    REQUIRE(r.session->useTake(w, punch.takes[0].id));
    info = r.info(w);
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[1].fromTake == punch.takes[0].clips.at(0).id);
    CHECK(info.clips[1].sourcePath == info.clips[0].sourcePath);
    CHECK(info.clips[1].offset == Approx(info.clips[0].offset + (info.clips[1].start - info.clips[0].start)).margin(1e-6));
}

TEST_CASE("take stacks: a pass over a whole clip adds to its takes; choosing a take plays it alone, in any order",
          "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    r.input = sine;
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    // A take, a punch inside it, then a pass over everything (the product owner's case, ADR 0037).
    pass(r, t, 2.0, 4.0);
    pass(r, t, 3.0, 1.0);
    pass(r, t, 1.0, 6.0);
    auto info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    REQUIRE(info.stacks.size() == 1);
    const auto stack = info.stacks[0];
    CHECK(stack.start == Approx(1.0).margin(0.02));
    CHECK(stack.end == Approx(7.0).margin(0.02));
    std::vector<std::string> names;
    for (const auto& k : stack.takes)
        names.push_back(k.name);
    CHECK(names == std::vector<std::string>{"Take 1", "Before take 2", "Take 2", "Before take 3", "Take 3"});
    CHECK(info.clips[0].fromTake == stack.takes.back().clips.at(0).id);
    // "Before take 3" is what played before the pass: take 1, the punch, take 1 again.
    REQUIRE(stack.takes[3].clips.size() == 3);

    // Each take, chosen in two orders: the clip is that take's clips over the range, every time.
    struct Part
    {
        std::uint64_t fromTake;
        double start, end, offset;
    };
    auto parts = [&] {
        std::vector<Part> out;
        for (const auto& c : r.info(t).clips)
            out.push_back({c.fromTake, c.start, c.end(), c.offset});
        return out;
    };
    std::map<size_t, std::vector<Part>> first;
    for (const auto& order : {std::vector<size_t>{0, 1, 2, 3, 4}, std::vector<size_t>{4, 2, 0, 3, 1, 0}})
        for (const auto k : order)
        {
            REQUIRE(r.session->useTake(t, stack.takes[k].id));
            const auto now = parts();
            REQUIRE(now.size() == stack.takes[k].clips.size());
            for (size_t i = 0; i < now.size(); ++i)
            {
                const auto& c = stack.takes[k].clips[i];
                CHECK(now[i].fromTake == c.id);
                CHECK(now[i].start == Approx(std::max(c.start, stack.start)));
                CHECK(now[i].end == Approx(std::min(c.end(), stack.end)));
            }
            if (const auto [it, fresh] = first.try_emplace(k, now); !fresh)
                for (size_t i = 0; i < now.size(); ++i)
                {
                    CHECK(now[i].start == it->second[i].start);
                    CHECK(now[i].end == it->second[i].end);
                    CHECK(now[i].offset == it->second[i].offset);
                }
        }

    // Another loop pass over a loop pass's clip: its takes add up on the clip, with no "Before take"
    // (it would only repeat the take playing).
    const auto u = r.track(1);
    threeTakes(r, u);
    threeTakes(r, u);
    info = r.info(u);
    REQUIRE(info.clips.size() == 1);
    REQUIRE(info.stacks.size() == 1);
    names.clear();
    for (const auto& k : info.stacks[0].takes)
        names.push_back(k.name);
    CHECK(names == std::vector<std::string>{"Take 1", "Take 2", "Take 3", "Take 4", "Take 5", "Take 6"});
    CHECK(info.clips[0].fromTake == info.stacks[0].takes[4].clips.at(0).id); // the newest complete lap

    // The clip's fades are its own: choosing another take keeps them.
    r.session->setFades({info.clips[0].id}, fd::model::Fade{0.2, 0.5f}, fd::model::Fade{0.1, -0.25f});
    REQUIRE(r.session->useTake(u, info.stacks[0].takes[0].id));
    info = r.info(u);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].fromTake == info.stacks[0].takes[0].clips.at(0).id);
    CHECK(info.clips[0].fadeIn.length == Approx(0.2));
    CHECK(info.clips[0].fadeIn.bend == Approx(0.5f));
    CHECK(info.clips[0].fadeOut.length == Approx(0.1));
    CHECK(info.clips[0].fadeOut.bend == Approx(-0.25f));
}

TEST_CASE("take stacks: comping uses a stretch of a take, crossfaded at its joins", "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t); // over 4..6: two whole laps and a short one (4..5); it plays the second
    auto info = r.info(t);
    const auto stack = info.stacks.at(0);
    const auto first = stack.takes[0].id, second = stack.takes[1].id, shortLap = stack.takes[2].id;
    r.session->setFades({info.clips.at(0).id}, fd::model::Fade{0.05, 0.0f}, fd::model::Fade{0.1, 0.0f});
    auto parts = [&] {
        std::vector<fd::model::ClipInfo> out;
        for (const auto& c : r.info(t).clips)
            out.push_back(c);
        return out;
    };
    auto takeOf = [&](const fd::model::ClipInfo& part) {
        const auto now = r.info(t);
        for (const auto& k : now.stacks.at(0).takes)
            for (const auto& c : k.clips)
                if (c.id == part.fromTake)
                    return k.id;
        return std::uint64_t{0};
    };

    // The first lap over 4.5..5: three parts, one clip, "Comp"; 10 ms crossfades at the two joins;
    // the clip's own fades stay at its ends; one undo step.
    REQUIRE(r.session->compTake(t, first, 4.5, 5.0));
    CHECK(r.session->undoDescription() == "Comp");
    auto p = parts();
    REQUIRE(p.size() == 3);
    CHECK(takeOf(p[0]) == second);
    CHECK(takeOf(p[1]) == first);
    CHECK(takeOf(p[2]) == second);
    CHECK(p[1].start == Approx(4.5));
    CHECK(p[1].end() == Approx(5.0));
    const auto lap1 = r.info(t).stacks.at(0).takes[0].clips.at(0);
    CHECK(p[1].offset == Approx(lap1.offset + (4.5 - lap1.start)).margin(1e-6));
    CHECK(fd::model::badge(r.info(t).stacks.at(0), p, false)->comp);
    CHECK(fd::model::clipGroups(r.info(t)).size() == 1);
    for (size_t i : {0u, 1u})
    {
        REQUIRE(p[i].crossfadeOut);
        CHECK(p[i].crossfadeOut->length == Approx(fd::model::kCrossfadeSeconds));
        CHECK(p[i].crossfadeOut->shape == fd::model::CrossfadeShape::EqualPower);
    }
    CHECK(p[0].fadeIn.length == Approx(0.05));
    CHECK(p[0].fadeOut.length == 0.0);
    CHECK(p[1].fadeIn.length == 0.0);
    CHECK(p[1].fadeOut.length == 0.0);
    CHECK(p[2].fadeIn.length == 0.0);
    CHECK(p[2].fadeOut.length == Approx(0.1));

    // The second lap back over it (swiped backwards): one part again, no lines, no crossfades.
    REQUIRE(r.session->compTake(t, second, 5.2, 4.4));
    p = parts();
    REQUIRE(p.size() == 1);
    CHECK(takeOf(p[0]) == second);
    CHECK(p[0].start == Approx(4.0).margin(0.02));
    CHECK(p[0].end() == Approx(6.0).margin(0.02));
    CHECK_FALSE(p[0].crossfadeOut);
    CHECK(p[0].fadeIn.length == Approx(0.05));
    CHECK(p[0].fadeOut.length == Approx(0.1));
    CHECK(fd::model::badge(r.info(t).stacks.at(0), p, false)->text == "2/3");

    // The short lap over 4.8..5.5: it has audio only up to 5, so only 4.8..5 changes.
    REQUIRE(r.session->compTake(t, shortLap, 4.8, 5.5));
    p = parts();
    REQUIRE(p.size() == 3);
    CHECK(takeOf(p[1]) == shortLap);
    CHECK(p[1].start == Approx(4.8));
    CHECK(p[1].end() == Approx(5.0).margin(0.02));
    // Before the clip's range: only what's inside it; all of it outside: nothing.
    REQUIRE(r.session->compTake(t, first, 3.0, 4.2));
    p = parts();
    CHECK(takeOf(p[0]) == first);
    CHECK(p[0].start == Approx(4.0).margin(0.02));
    CHECK(p[0].end() == Approx(4.2));
    CHECK_FALSE(r.session->compTake(t, first, 1.0, 2.0));

    // Undo takes back one swipe at a time.
    r.session->undo();
    p = parts();
    REQUIRE(p.size() == 3);
    CHECK(takeOf(p[0]) == second);
    r.session->undo();
    r.session->undo();
    r.session->undo();
    p = parts();
    REQUIRE(p.size() == 1);
    CHECK(takeOf(p[0]) == second);
    CHECK_FALSE(p[0].crossfadeOut);
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

TEST_CASE("take stacks: a clip placed over one cuts its range, its takes stay whole; deleting, keeping and stepping takes",
          "[integration]")
{
    Recording r;
    const auto t = r.track(0);
    threeTakes(r, t);
    auto info = r.info(t);
    const auto stack = info.stacks.at(0).id;

    // A MIDI clip over the middle: the head keeps the takes and its range shrinks to it (ADR 0037),
    // the tail is a plain clip; using a take changes the head only, never the MIDI clip.
    const auto midi = r.session->createMidiClip(t, 4.8, 0.4);
    REQUIRE(midi != 0);
    info = r.info(t);
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[0].stack == stack);
    CHECK(info.clips[2].stack == 0);
    CHECK(info.clips[2].fromTake == 0);
    CHECK(info.stacks.at(0).start == Approx(4.0).margin(0.02));
    CHECK(info.stacks.at(0).end == Approx(4.8));
    REQUIRE(r.session->useTake(t, info.stacks[0].takes[0].id));
    info = r.info(t);
    REQUIRE(info.clips.size() == 3);
    CHECK(info.clips[0].stack == stack);
    CHECK(info.clips[0].fromTake == info.stacks[0].takes[0].clips.at(0).id);
    CHECK(info.clips[0].end() == Approx(4.8));
    CHECK(info.clips[1].id == midi);
    // Its takes stayed whole: trimmed back out (over the others, which give way), it plays them.
    const auto head = info.clips[0];
    r.session->applyClipEdits({{head.id, t, head.start, 6.0 - head.start, head.offset}});
    info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].end() == Approx(6.0).margin(0.02));
    CHECK(info.stacks.at(0).end == Approx(6.0).margin(0.02));
    r.session->undo();
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
