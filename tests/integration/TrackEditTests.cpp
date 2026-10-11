// Managing tracks and clips (M2 step C11, ADR 0041, docs/design/tracks-and-clips.md): renaming,
// colours, deleting with what goes along, moving with lanes, duplicating, the clip clipboard.
// Through engine::Session, each step undone.

#include "RecordingFixture.h"
#include "fd/testkit/WavIO.h"
#include "model/ArrangementRows.h"
#include "model/Takes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using Catch::Approx;
using namespace recording_test;
using ParamTarget = fd::engine::Session::ParamTarget;

namespace
{
/// A sine of `seconds` at `hz` as a clip at `start` on track `t`.
std::uint64_t tone(Recording& r, std::uint64_t t, double start, double seconds, double hz = 220.0)
{
    const double rate = 48000.0;
    fd::testkit::AudioBuffer sine(2, static_cast<int>(seconds * rate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < sine.numSamples(); ++i)
            sine.at(c, i) =
                0.3f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate));
    REQUIRE((r.root.isDirectory() || r.root.createDirectory()));
    const auto file = r.root.getChildFile("tone-" + juce::Uuid().toString() + ".wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, rate));
    const auto clip = r.session->insertAudioFile(t, file, start);
    REQUIRE(clip != 0);
    return clip;
}

std::vector<std::uint64_t> trackIds(Recording& r)
{
    r.session->updateSnapshotNow();
    std::vector<std::uint64_t> out;
    for (const auto& t : r.session->snapshot()->tracks)
        out.push_back(t.id);
    return out;
}

const fd::model::AutomationLaneInfo* laneOf(Recording& r, std::uint64_t id)
{
    r.session->updateSnapshotNow();
    for (const auto& l : r.session->snapshot()->automationLanes)
        if (l.id == id)
            return &l;
    return nullptr;
}

/// The mix of [start, end), rendered offline: its samples, interleaved by channel.
std::vector<std::vector<float>> render(Recording& r, double start, double end)
{
    juce::TemporaryFile out(".wav");
    bool done = false;
    auto job = r.session->render({start, end, std::nullopt, r.session->sampleRate()}, out.getFile(),
                                 [&](const std::string&) { done = true; });
    REQUIRE(job != nullptr);
    for (int i = 0; i < 1000 && !done; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(done);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out.getFile()));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    std::vector<std::vector<float>> channels;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        channels.emplace_back(buffer.getReadPointer(c), buffer.getReadPointer(c) + buffer.getNumSamples());
    return channels;
}

float peakDifference(const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b)
{
    float worst = 0.0f;
    for (size_t c = 0; c < std::min(a.size(), b.size()); ++c)
        for (size_t i = 0; i < std::min(a[c].size(), b[c].size()); ++i)
            worst = std::max(worst, std::abs(a[c][i] - b[c][i]));
    return worst;
}
} // namespace

TEST_CASE("tracks: renamed and coloured, clips too; each one undo step", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0);
    CHECK(s.renameTrack(t, "  Bass  "));
    CHECK(r.info(t).name == "Bass");
    CHECK(s.undoDescription() == "Rename track");
    CHECK_FALSE(s.renameTrack(t, "   ")); // a track always has a name
    CHECK_FALSE(s.renameTrack(t, "Bass"));
    CHECK(s.setTrackColour(t, 7));
    CHECK(r.info(t).colour == 7);
    CHECK_FALSE(s.setTrackColour(t, 16));
    s.undo();
    s.undo();
    CHECK(r.info(t).name != "Bass");

    // A return's name is its bus's too.
    const auto ret = s.addReturnTrack();
    CHECK(s.renameTrack(ret, "Verb"));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->channel(ret)->name == "Verb");

    // Clips: one name for all; a colour each (ADR 0043).
    const auto a = tone(r, t, 0.0, 1.0), b = tone(r, t, 2.0, 1.0);
    s.renameClips({a, b}, "Riff");
    auto info = r.info(t);
    CHECK((info.clips[0].name == "Riff" && info.clips[1].name == "Riff"));
    CHECK(s.undoDescription() == "Rename clips");
    s.setClipColour({a, b}, 3);
    info = r.info(t);
    CHECK((info.clips[0].colour == 3 && info.clips[1].colour == 3));
    CHECK(s.undoDescription() == "Clip colour");
    s.setClipColour({a}, 16); // not a palette colour
    CHECK(r.info(t).clips[0].colour == 3);
}

TEST_CASE("clips: a new clip takes its track's colour; after that, clip and track colours are independent "
          "(ADR 0043)",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0), u = r.track(1);
    REQUIRE(s.setTrackColour(t, 4));
    REQUIRE(s.setTrackColour(u, 9));
    const auto clip = tone(r, t, 0.0, 1.0);
    CHECK(r.info(t).clips[0].colour == 4); // its track's, when made

    // Recolouring the track leaves the clip; recolouring the clip leaves the track.
    s.setTrackColour(t, 6);
    CHECK(r.info(t).clips[0].colour == 4);
    s.setClipColour({clip}, 12);
    CHECK(r.info(t).colour == 6);
    CHECK(r.info(t).clips[0].colour == 12);
    s.undo();
    CHECK(r.info(t).clips[0].colour == 4);

    // Moved to another track and back, it keeps its colour; the tracks keep theirs.
    s.applyClipEdits({{.clip = clip, .track = u, .start = 0.0, .length = 1.0}});
    REQUIRE(r.info(u).clips.size() == 1);
    CHECK(r.info(u).clips[0].colour == 4);
    s.applyClipEdits({{.clip = clip, .track = t, .start = 2.0, .length = 1.0}});
    CHECK(r.info(t).clips[0].colour == 4);
    CHECK(r.info(t).colour == 6);
    CHECK(r.info(u).colour == 9);
}

TEST_CASE("clips: every way of making one stores its colour, takes too; switching takes keeps it (ADR 0043)",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0), u = r.track(1);
    REQUIRE(s.setTrackColour(t, 4));
    REQUIRE(s.setTrackColour(u, 9));
    // Whether every clip and take clip on a track stores a colour of its own.
    auto allColoured = [&](std::uint64_t track)
    {
        auto* tr = s.impl().track(track);
        REQUIRE(tr != nullptr);
        bool all = true;
        for (auto* c : tr->getClips())
            all = all && c->state.hasProperty("fdColour");
        for (auto stack : tr->state.getChildWithName("FDTAKES"))
            for (auto take : stack)
                for (auto c : take)
                    all = all && c.hasProperty("fdColour");
        return all;
    };

    // A loop pass: one clip with three takes, all in the track's colour.
    r.input = sine;
    s.setAudioInput(t, s.audioInputs().front().id);
    s.setArmed(t, true, true);
    s.setLoopRange(4.0, 6.0);
    s.setLooping(true);
    s.setPosition(4.0);
    r.recordFor(5.0);
    s.setLooping(false);
    s.setArmed(t, false, false);
    auto info = r.info(t);
    REQUIRE(info.stacks.size() == 1);
    REQUIRE(info.stacks[0].takes.size() == 3);
    CHECK(info.clips[0].colour == 4);
    for (const auto& take : info.stacks[0].takes)
        for (const auto& c : take.clips)
            CHECK(c.colour == 4);
    CHECK(allColoured(t));

    // Recoloured, it keeps its colour whichever take it plays.
    s.setClipColour({info.clips[0].id}, 12);
    REQUIRE(s.useTake(t, info.stacks[0].takes[0].id));
    info = r.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].colour == 12);

    // Made, inserted, split, duplicated and pasted: each stores a colour, a copy its source's.
    const auto midi = s.createMidiClip(u, 0.0, 2.0);
    const auto audio = tone(r, u, 3.0, 1.0);
    CHECK(s.splitClips({midi}, 1.0).size() == 1);
    CHECK(s.duplicateClips({{.clip = audio, .track = u, .start = 5.0, .length = 1.0}}).size() == 1);
    CHECK(s.pasteClips(s.copyClips({info.clips[0].id}), u, 10.0).clips.size() == 1);
    CHECK(allColoured(u));
    info = r.info(u);
    REQUIRE(info.clips.size() == 5);
    for (size_t i = 0; i < 4; ++i)
        CHECK(info.clips[i].colour == 9);
    CHECK(info.clips[4].colour == 12); // the pasted clip, and its takes
    REQUIRE(info.stacks.size() == 1);
    for (const auto& take : info.stacks[0].takes)
        for (const auto& c : take.clips)
            CHECK(c.colour == 12);
}

TEST_CASE("clips: an older project's clips get colours as it opens, takes their clip's, and it stays saved "
          "(ADR 0043)",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0);
    REQUIRE(s.setTrackColour(t, 4));
    r.input = sine;
    s.setAudioInput(t, s.audioInputs().front().id);
    s.setArmed(t, true, true);
    s.setLoopRange(4.0, 6.0);
    s.setLooping(true);
    s.setPosition(4.0);
    r.recordFor(5.0);
    s.setLooping(false);
    s.setArmed(t, false, false);
    const auto stacked = r.info(t).clips[0].id;
    s.setClipColour({stacked}, 12);
    tone(r, t, 0.0, 1.0);
    const auto project = r.root.getChildFile("Old").getChildFile("Old.segno");
    REQUIRE((project.getParentDirectory().isDirectory() || project.getParentDirectory().createDirectory()));
    REQUIRE(s.saveAs(project));
    // As written before every clip stored a colour: the plain clip and the takes have none; the
    // clip with takes was given one by hand.
    auto xml = juce::parseXML(project);
    REQUIRE(xml != nullptr);
    std::function<void(juce::XmlElement&)> strip = [&](juce::XmlElement& e)
    {
        if ((e.hasTagName("AUDIOCLIP") || e.hasTagName("MIDICLIP")) &&
            e.getStringAttribute("id") != juce::String(static_cast<juce::int64>(stacked)))
            e.removeAttribute("fdColour");
        for (auto* child : e.getChildIterator())
            strip(*child);
    };
    strip(*xml);
    REQUIRE(xml->writeTo(project));

    REQUIRE(s.open(project));
    const auto opened = trackIds(r).front();
    const auto info = r.info(opened);
    REQUIRE(info.clips.size() == 2);
    CHECK(info.clips[0].colour == 4); // the plain clip: its track's
    CHECK(info.clips[1].colour == 12);
    REQUIRE(info.stacks.size() == 1);
    for (const auto& take : info.stacks[0].takes)
        for (const auto& c : take.clips)
            CHECK(c.colour == 12); // its clip's
    CHECK_FALSE(s.hasUnsavedChanges());
    CHECK(s.undoDescription().empty());
    s.setTrackColour(opened, 7); // and now they're its own
    CHECK(r.info(opened).clips[0].colour == 4);
}

TEST_CASE("tracks: deleting one takes its clips, the lanes only it drives, its assignments, sidechains; undo "
          "brings all back",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto ids = trackIds(r);
    REQUIRE(ids.size() >= 3);
    const auto t1 = ids[0], t2 = ids[1], t3 = ids[2];
    tone(r, t2, 0.0, 2.0);
    // T3's compressor listens to T2 (ADR 0025).
    const auto comp = s.addDevice(t3, "fd:compressor");
    REQUIRE(comp != 0);
    REQUIRE(s.setSidechainSource(comp, t2));
    // A lane only for T2's volume; a shared one (T2's pan and T1's volume), right after T2.
    const auto only = s.addAutomationLaneAfter(t2, ParamTarget{t2, "volume"});
    const auto shared = s.addAutomationLaneAfter(t2, ParamTarget{t2, "pan"});
    REQUIRE(s.assign(shared, ParamTarget{t1, "volume"}));
    s.updateSnapshotNow();
    const auto before = s.snapshot()->tracks.size();

    std::string why;
    REQUIRE(s.deleteTrack(t2, &why));
    s.updateSnapshotNow();
    auto snap = s.snapshot();
    CHECK(snap->tracks.size() == before - 1);
    CHECK(snap->channel(t2) == nullptr);
    CHECK(laneOf(r, only) == nullptr);
    REQUIRE(laneOf(r, shared) != nullptr);
    CHECK(laneOf(r, shared)->assignments.size() == 1);
    CHECK(laneOf(r, shared)->assignments[0].channel == t1);
    CHECK(laneOf(r, shared)->after == t1); // after the track above now
    s.updateSnapshotNow();
    CHECK_FALSE(s.snapshot()->channel(t3)->devices.front().sidechainSource.has_value()); // Off
    CHECK(s.undoDescription() == "Delete track");

    s.undo();
    s.updateSnapshotNow();
    snap = s.snapshot();
    REQUIRE(snap->channel(t2) != nullptr);
    CHECK(snap->channel(t2)->clips.size() == 1);
    CHECK(laneOf(r, only) != nullptr);
    CHECK(laneOf(r, shared)->assignments.size() == 2);
    CHECK(laneOf(r, shared)->after == t2);
    CHECK(s.snapshot()->channel(t3)->devices.front().sidechainSource == t2);

    // A return: the sends to it, and a lane of only those sends, go with it.
    const auto ret = s.addReturnTrack();
    REQUIRE(s.setSend(t1, ret, -6.0f, std::nullopt));
    const auto sendLane = s.addAutomationLane(ParamTarget{t1, "send/" + std::to_string(ret)});
    REQUIRE(sendLane != 0);
    REQUIRE(s.deleteTrack(ret));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->returns.empty());
    CHECK(s.snapshot()->channel(t1)->mix.sends.empty());
    CHECK(laneOf(r, sendLane) == nullptr);
    CHECK(s.undoDescription() == "Delete return track");
    s.undo();
    s.updateSnapshotNow();
    REQUIRE(s.snapshot()->returns.size() == 1);
    CHECK(s.snapshot()->channel(t1)->mix.sends.at(0).levelDb == Approx(-6.0f).margin(0.1f));

    // Not the master.
    CHECK_FALSE(s.deleteTrack(s.snapshot()->master.id));
}

TEST_CASE("tracks: a moved track or lane moves alone, every other row staying where it was (ADR 0042); "
          "returns among the returns",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    auto ids = trackIds(r);
    REQUIRE(ids.size() >= 3);
    const auto t1 = ids[0], t2 = ids[1], t3 = ids[2];
    const auto lane = s.addAutomationLaneAfter(t1, ParamTarget{t1, "volume"});
    // The rows as the arrangement shows them: tracks by their ids, lanes by theirs.
    auto order = [&s] {
        s.updateSnapshotNow();
        std::vector<std::uint64_t> out;
        for (const auto& row : fd::model::arrangementRows(*s.snapshot(), false))
            out.push_back(row.isLane() ? row.lane : row.channel);
        return out;
    };
    auto rest = std::vector<std::uint64_t>(ids.begin() + 3, ids.end());
    auto with = [](std::vector<std::uint64_t> head, const std::vector<std::uint64_t>& tail) {
        head.insert(head.end(), tail.begin(), tail.end());
        return head;
    };
    REQUIRE(order() == with({t1, lane, t2, t3}, rest));

    // T1 after T3: its lane stays where it was, now after T2.
    REQUIRE(s.moveTrack(t1, ids.size() > 3 ? ids[3] : 0));
    CHECK(order() == with({lane, t2, t3, t1}, rest));
    CHECK(laneOf(r, lane)->after == 0); // at the top now
    CHECK(s.undoDescription() == "Move track");
    CHECK_FALSE(s.moveTrack(t1, ids.size() > 3 ? ids[3] : 0)); // already there
    s.undo();
    CHECK(order() == with({t1, lane, t2, t3}, rest));
    CHECK(laneOf(r, lane)->after == t1);

    // T3 between T1 and its lane: the lane now sits after T3, still where it was.
    REQUIRE(s.moveTrack(t3, lane));
    CHECK(order() == with({t1, t3, lane, t2}, rest));
    CHECK(laneOf(r, lane)->after == t3);
    s.undo();

    // A lane alone: above T3, then to the top, then to the end.
    REQUIRE(s.moveLane(lane, t3));
    CHECK(order() == with({t1, t2, lane, t3}, rest));
    CHECK(laneOf(r, lane)->after == t2);
    CHECK(s.undoDescription() == "Move automation lane");
    REQUIRE(s.moveLane(lane, t1));
    CHECK(order() == with({lane, t1, t2, t3}, rest));
    CHECK(laneOf(r, lane)->after == 0);
    REQUIRE(s.moveLane(lane, 0));
    CHECK(order().back() == lane);
    CHECK_FALSE(s.moveLane(lane, 0)); // already there
    CHECK_FALSE(s.moveLane(t1, 0));   // not a lane

    // Returns among the returns.
    const auto a = s.addReturnTrack(), b = s.addReturnTrack();
    REQUIRE(s.moveTrack(b, a));
    s.updateSnapshotNow();
    CHECK(s.snapshot()->returns[0].id == b);
    CHECK(s.snapshot()->returns[1].id == a);
    CHECK(trackIds(r).size() == ids.size()); // the tracks unchanged
}

TEST_CASE("tracks: a duplicate sounds like the original, with its takes, modules, sends and lanes",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0);
    // Takes on it (a loop pass), a module, a send, a lane of its own.
    r.input = sine;
    s.setAudioInput(t, s.audioInputs().front().id);
    s.setArmed(t, true, true);
    s.setLoopRange(4.0, 6.0);
    s.setLooping(true);
    s.setPosition(4.0);
    r.recordFor(5.0);
    s.setLooping(false);
    s.setArmed(t, false, false);
    tone(r, t, 0.0, 2.0, 330.0);
    const auto sat = s.addDevice(t, "fd:saturator");
    REQUIRE(sat != 0);
    const auto ret = s.addReturnTrack();
    REQUIRE(s.setSend(t, ret, -12.0f, std::nullopt));
    const auto lane = s.addAutomationLaneAfter(t, ParamTarget{t, "volume"});
    REQUIRE(s.setLanePoints(lane, {{.time = 0.0, .value = 0.8f}, {.time = 8.0, .value = 0.2f}}));
    REQUIRE(s.renameTrack(t, "Bass"));
    s.setArmed(t, true, true);

    const auto copy = s.duplicateTrack(t);
    REQUIRE(copy != 0);
    CHECK(s.undoDescription() == "Duplicate track");
    const auto original = r.info(t), dup = r.info(copy);
    CHECK(dup.name == "Bass 2");
    CHECK(trackIds(r)[1] == copy); // right after it
    CHECK(dup.clips.size() == original.clips.size());
    REQUIRE(dup.stacks.size() == 1);
    CHECK(dup.stacks[0].takes.size() == original.stacks[0].takes.size());
    CHECK(dup.stacks[0].id != original.stacks[0].id);
    CHECK(fd::model::playingTake(dup.stacks[0], dup.clips) ==
          fd::model::playingTake(original.stacks[0], original.clips));
    REQUIRE(dup.devices.size() == 1);
    CHECK(dup.devices[0].id != sat);
    CHECK(dup.mix.sends.at(0).levelDb == Approx(-12.0f).margin(0.1f));
    CHECK_FALSE(dup.input.armed);
    CHECK(dup.colour == original.colour);
    // Its lane: a copy after it, driving it.
    s.updateSnapshotNow();
    const fd::model::AutomationLaneInfo* copied = nullptr;
    for (const auto& l : s.snapshot()->automationLanes)
        if (l.id != lane && l.after == copy)
            copied = &l;
    REQUIRE(copied != nullptr);
    REQUIRE(copied->assignments.size() == 1);
    CHECK(copied->assignments[0].channel == copy);
    CHECK(copied->points.size() == 2);
    CHECK(copied->name == "Automation 2"); // the next number: names never repeat (§18)

    // It sounds the same: the original alone, then the copy alone.
    s.setArmed(t, false, false);
    s.setMute(copy, true);
    const auto a = render(r, 0.0, 6.5);
    s.setMute(copy, false);
    s.setMute(t, true);
    const auto b = render(r, 0.0, 6.5);
    CHECK(peakDifference(a, b) < 1e-5f);

    // A return's copy listens to a bus of its own: the sends to the original stay its.
    const auto retCopy = s.duplicateTrack(ret);
    REQUIRE(retCopy != 0);
    s.updateSnapshotNow();
    CHECK(s.snapshot()->returns.size() == 2);
    CHECK(s.snapshot()->channel(t)->mix.sends.at(0).returnId == ret);

    // Undone (past the mutes and the return's copy), the copy is gone.
    for (int i = 0; i < 8 && s.snapshot()->channel(copy) != nullptr; ++i)
    {
        s.undo();
        s.updateSnapshotNow();
    }
    CHECK(s.snapshot()->channel(copy) == nullptr);
    CHECK(s.snapshot()->channel(t) != nullptr);
}

TEST_CASE(
    "clipboard: clips paste at a time on a track, rows kept, cutting what they land on; a cut still pastes",
    "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto ids = trackIds(r);
    REQUIRE(ids.size() >= 3);
    const auto a = tone(r, ids[0], 1.0, 1.0);
    const auto b = tone(r, ids[1], 1.5, 1.0);
    const auto under = tone(r, ids[1], 10.0, 4.0); // pasted clips cut it

    auto clipboard = s.copyClips({a, b});
    REQUIRE_FALSE(clipboard.empty());
    CHECK(clipboard.rows() == 2);
    auto pasted = s.pasteClips(clipboard, ids[0], 11.0);
    REQUIRE(pasted.clips.size() == 2);
    CHECK(pasted.rowsLeftOut == 0);
    CHECK(s.undoDescription() == "Paste");
    auto t0 = r.info(ids[0]), t1 = r.info(ids[1]);
    REQUIRE(t0.clips.size() == 2);
    CHECK(t0.clips[1].start == Approx(11.0));
    CHECK(t1.clips.back().start >= 11.0); // the cut clip's right part, after the paste
    const auto copyOnT1 = std::find_if(t1.clips.begin(), t1.clips.end(),
                                       [](const auto& c) { return std::abs(c.start - 11.5) < 1e-6; });
    CHECK(copyOnT1 != t1.clips.end());
    // What it cut: the clip under it now stops where the paste starts.
    CHECK(std::any_of(t1.clips.begin(), t1.clips.end(),
                      [&](const auto& c) { return c.id == under && c.end() <= 11.5 + 1e-6; }));
    s.undo(); // one step
    CHECK(r.info(ids[0]).clips.size() == 1);

    // Pasted on the last track, the second row has no track: left out.
    pasted = s.pasteClips(clipboard, ids.back(), 20.0);
    CHECK(pasted.clips.size() == 1);
    CHECK(pasted.rowsLeftOut == 1);

    // A cut, then the clipboard pastes what went.
    clipboard = s.copyClips({a});
    s.deleteClips({a});
    CHECK(r.info(ids[0]).clips.empty());
    pasted = s.pasteClips(clipboard, ids[2], 3.0);
    REQUIRE(pasted.clips.size() == 1);
    CHECK(r.info(ids[2]).clips.front().start == Approx(3.0));
    CHECK(r.info(ids[2]).clips.front().length == Approx(1.0));
}

TEST_CASE("clipboard: a clip with takes pastes with them, playing the same take", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = r.track(0), u = r.track(1);
    r.input = sine;
    s.setAudioInput(t, s.audioInputs().front().id);
    s.setArmed(t, true, true);
    s.setLoopRange(4.0, 6.0);
    s.setLooping(true);
    s.setPosition(4.0);
    r.recordFor(5.0);
    s.setLooping(false);
    s.setArmed(t, false, false);
    const auto original = r.info(t);
    REQUIRE(original.stacks.size() == 1);
    const auto pasted = s.pasteClips(s.copyClips({original.clips[0].id}), u, 10.0);
    REQUIRE(pasted.clips.size() == 1);
    const auto info = r.info(u);
    REQUIRE(info.stacks.size() == 1);
    CHECK(info.stacks[0].takes.size() == original.stacks[0].takes.size());
    CHECK(info.stacks[0].start == Approx(10.0).margin(0.02));
    CHECK(fd::model::playingTake(info.stacks[0], info.clips) ==
          fd::model::playingTake(original.stacks[0], original.clips));
}

TEST_CASE("tracks: new tracks, returns and lanes are named after the highest in use, never a deleted one's "
          "again (C11 review)",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    auto nameOf = [&s](std::uint64_t id) {
        s.updateSnapshotNow();
        return s.snapshot()->channel(id)->name;
    };
    // A project starts with numbered tracks; with the first deleted, the next is one past the
    // highest, not a second of the last.
    const auto ids = trackIds(r);
    REQUIRE(nameOf(ids.front()) == "1 Track");
    REQUIRE(nameOf(ids.back()) == std::to_string(ids.size()) + " Track");
    REQUIRE(s.deleteTrack(ids.front()));
    const auto added = s.addTrack();
    CHECK(nameOf(added) == std::to_string(ids.size() + 1) + " Track");
    // A name given is kept as given, one in use too.
    CHECK(nameOf(s.addTrack("2 Track")) == "2 Track");

    // Returns: A and B, A deleted: C.
    const auto a = s.addReturnTrack(), b = s.addReturnTrack();
    CHECK(nameOf(a) == "A Return");
    CHECK(nameOf(b) == "B Return");
    REQUIRE(s.deleteTrack(a));
    CHECK(nameOf(s.addReturnTrack()) == "C Return");

    // Lanes: numbered when made, whatever they drive; the first deleted, the next is 3.
    const auto first = s.addAutomationLane();
    const auto second = s.addAutomationLane(ParamTarget{added, "volume"});
    CHECK(laneOf(r, first)->name == "Automation 1");
    CHECK(laneOf(r, second)->name == "Automation 2");
    s.removeAutomationLane(first);
    CHECK(s.undoDescription() == "Delete automation lane");
    CHECK(laneOf(r, s.addAutomationLane())->name == "Automation 3");
    // A lane's name is never empty.
    CHECK_FALSE(s.renameAutomationLane(second, "  "));
    CHECK(s.renameAutomationLane(second, " Filter "));
    CHECK(laneOf(r, second)->name == "Filter");
}

TEST_CASE("lanes: an older project's lanes, named after what they drove, are numbered as it opens, and it "
          "stays saved (C11 review)",
          "[integration]")
{
    Recording r;
    auto& s = *r.session;
    const auto t = trackIds(r).front();
    s.addAutomationLane(ParamTarget{t, "volume"});
    s.addAutomationLane(ParamTarget{t, "pan"});
    const auto project = r.root.getChildFile("Old").getChildFile("Old.segno");
    REQUIRE((project.getParentDirectory().isDirectory() || project.getParentDirectory().createDirectory()));
    REQUIRE(s.saveAs(project));
    // As written before lanes were numbered: no names.
    auto xml = juce::parseXML(project);
    REQUIRE(xml != nullptr);
    auto* lanes = xml->getChildByName("FDAUTOMATION");
    REQUIRE(lanes != nullptr);
    for (auto* lane : lanes->getChildWithTagNameIterator("LANE"))
        lane->removeAttribute("name");
    REQUIRE(xml->writeTo(project));

    REQUIRE(s.open(project));
    s.updateSnapshotNow();
    const auto& opened = s.snapshot()->automationLanes;
    REQUIRE(opened.size() == 2);
    CHECK(opened[0].name == "Automation 1"); // top to bottom
    CHECK(opened[1].name == "Automation 2");
    CHECK_FALSE(s.hasUnsavedChanges());
    CHECK(s.undoDescription().empty());
}
