// Groups through the Session (ADR 0072, docs/design/groups.md §4, §5, §7, §11, step 2): grouping,
// ungrouping and leaving a group, each one undo step; moves into and out of groups with the lanes
// keeping their places; a group deleted and duplicated with what's in it, never left empty; a
// group's sound, mix, sends, modules and sidechains; a track added in a group; saved and opened.

#include "RecordingFixture.h"
#include "fd/testkit/WavIO.h"
#include "model/ArrangementRows.h"
#include "model/Groups.h"
#include "model/Selection.h"
#include "model/TimeEdits.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using Catch::Approx;
using namespace recording_test;
using ParamTarget = fd::engine::Session::ParamTarget;
using Rows = std::vector<std::string>;

namespace
{
/// Kick, Snare, Bass and Lead.
struct Groups : Recording
{
    std::uint64_t kick = 0, snare = 0, bass = 0, lead = 0;

    Groups()
    {
        session->updateSnapshotNow();
        while (session->snapshot()->tracks.size() < 4)
        {
            session->addTrack();
            session->updateSnapshotNow();
        }
        while (session->snapshot()->tracks.size() > 4)
        {
            REQUIRE(session->deleteTrack(session->snapshot()->tracks.back().id));
            session->updateSnapshotNow();
        }
        std::uint64_t* ids[] = {&kick, &snare, &bass, &lead};
        const char* names[] = {"Kick", "Snare", "Bass", "Lead"};
        for (size_t i = 0; i < 4; ++i)
        {
            *ids[i] = track(i);
            session->renameTrack(*ids[i], names[i]);
        }
    }

    std::shared_ptr<const fd::model::Snapshot> snap()
    {
        session->updateSnapshotNow();
        return session->snapshot();
    }

    bool exists(std::uint64_t channel) { return snap()->channel(channel) != nullptr; }

    /// Each row's name as the arrangement shows it, a dot for each group it's in.
    Rows rows()
    {
        const auto shot = snap();
        Rows out;
        for (const auto& r : fd::model::arrangementRows(*shot, false))
        {
            const auto* lane = r.isLane() ? shot->automationLane(r.lane) : nullptr;
            const auto name = lane != nullptr ? lane->name : shot->channel(r.channel)->name;
            out.push_back(std::string(static_cast<size_t>(r.depth), '.') + name);
        }
        return out;
    }

    /// A sine at 220 Hz as a clip of [0, 1) s on `t`.
    void tone(std::uint64_t t)
    {
        fd::testkit::AudioBuffer sine(2, static_cast<int>(kRate));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < sine.numSamples(); ++i)
                sine.at(c, i) =
                    0.3f *
                    static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / kRate));
        REQUIRE((root.isDirectory() || root.createDirectory()));
        const auto file = root.getChildFile("tone-" + juce::Uuid().toString() + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, kRate));
        REQUIRE(session->insertAudioFile(t, file, 0.0) != 0);
    }

    /// The mix's peak over [0.1, 0.9) s, rendered offline.
    float peak()
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        auto job = session->render({0.1, 0.9, std::nullopt, session->sampleRate()}, out.getFile(),
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
        return buffer.getMagnitude(0, 0, buffer.getNumSamples());
    }
};
} // namespace

TEST_CASE(
    "groups: grouping gathers the tracks at the topmost one's place, one undo step; ungroup puts them back",
    "[integration]")
{
    Groups g;
    auto& s = *g.session;
    const auto kickColour = g.info(g.kick).colour;
    // Kick and Bass, not next to each other: gathered at Kick's place; Snare stays where it was.
    const auto drums = s.groupTracks({g.bass, g.kick});
    REQUIRE(drums != 0);
    CHECK(s.undoDescription() == "Group tracks");
    CHECK(g.rows() == Rows{"1 Group", ".Kick", ".Bass", "Snare", "Lead"});
    const auto group = g.info(drums);
    CHECK(group.isGroup());
    CHECK(group.colour == kickColour);
    CHECK(g.info(g.kick).group == drums);
    CHECK(fd::model::tracksIn(*g.snap(), drums) == 2);
    s.undo();
    CHECK(g.rows() == Rows{"Kick", "Snare", "Bass", "Lead"});
    s.redo();
    CHECK(g.rows() == Rows{"1 Group", ".Kick", ".Bass", "Snare", "Lead"});

    // Named, and nested: Drums and Lead into Rhythm.
    REQUIRE(s.renameTrack(drums, "Drums"));
    const auto rhythm = s.groupTracks({drums, g.lead}, "Rhythm");
    REQUIRE(rhythm != 0);
    CHECK(g.rows() == Rows{"Rhythm", ".Drums", "..Kick", "..Bass", ".Lead", "Snare"});
    // A track inside a group that's grouped too goes with it: nothing grouped twice.
    REQUIRE(s.groupTracks({drums, g.kick}) != 0);
    CHECK(g.rows() == Rows{"Rhythm", ".1 Group", "..Drums", "...Kick", "...Bass", ".Lead", "Snare"});
    s.undo();

    // Ungroup Drums: Kick and Bass take its place in Rhythm; it goes.
    CHECK(s.ungroupTracks(drums) == std::vector<std::uint64_t>{g.kick, g.bass});
    CHECK(s.undoDescription() == "Ungroup tracks");
    CHECK(g.rows() == Rows{"Rhythm", ".Kick", ".Bass", ".Lead", "Snare"});
    CHECK_FALSE(g.exists(drums));
    s.undo();
    CHECK(g.rows() == Rows{"Rhythm", ".Drums", "..Kick", "..Bass", ".Lead", "Snare"});
    // Nothing to group, or not a group to ungroup.
    CHECK(s.groupTracks({}) == 0);
    CHECK(s.ungroupTracks(g.kick).empty());
}

TEST_CASE("groups: a lane between the tracks grouped goes in with them; the others stay where they were",
          "[integration]")
{
    Groups g;
    auto& s = *g.session;
    const auto between = s.addAutomationLaneAfter(g.kick, ParamTarget{g.kick, "volume"});
    const auto after = s.addAutomationLaneAfter(g.snare, ParamTarget{g.snare, "volume"});
    REQUIRE(s.renameAutomationLane(between, "Kick vol"));
    REQUIRE(s.renameAutomationLane(after, "Snare vol"));
    REQUIRE(s.groupTracks({g.kick, g.snare}) != 0);
    CHECK(g.rows() == Rows{"1 Group", ".Kick", ".Kick vol", ".Snare", "Snare vol", "Bass", "Lead"});
    s.undo();
    CHECK(g.rows() == Rows{"Kick", "Kick vol", "Snare", "Snare vol", "Bass", "Lead"});
}

TEST_CASE(
    "groups: leaving a group, moving into and out of groups, a group emptied going in the same step; lanes "
    "keep their places",
    "[integration]")
{
    Groups g;
    auto& s = *g.session;
    const auto drums = s.groupTracks({g.kick, g.snare}, "Drums");
    const auto lane = s.addAutomationLaneAfter(g.kick, ParamTarget{g.kick, "volume"});
    REQUIRE(lane != 0);
    REQUIRE(s.renameAutomationLane(lane, "Kick vol"));
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Kick vol", ".Snare", "Bass", "Lead"});
    // A new lane and a new track go after the last row outside any group.
    const auto end = s.addAutomationLane();
    CHECK(g.rows().back() == g.snap()->automationLane(end)->name);
    s.undo();

    // Snare out of Drums: right after it.
    REQUIRE(s.removeFromGroup({g.snare}));
    CHECK(s.undoDescription() == "Remove from group");
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Kick vol", "Snare", "Bass", "Lead"});
    // Lead into Drums, right above Kick; then to Drums' end.
    REQUIRE(s.moveTrack(g.lead, g.kick));
    CHECK(g.rows() == Rows{"Drums", ".Lead", ".Kick", ".Kick vol", "Snare", "Bass"});
    REQUIRE(s.moveTrack(g.lead, 0, drums));
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Kick vol", ".Lead", "Snare", "Bass"});
    // Kick out, to the end: its lane stays in Drums, first there.
    REQUIRE(s.moveTrack(g.kick, 0, 0));
    CHECK(g.rows() == Rows{"Drums", ".Kick vol", ".Lead", "Snare", "Bass", "Kick"});
    // Lead out too: Drums is empty and goes in the same step; the lane stays where it was.
    REQUIRE(s.moveTrack(g.lead, 0, 0));
    CHECK(s.undoDescription() == "Move track");
    CHECK_FALSE(g.exists(drums));
    CHECK(g.rows() == Rows{"Kick vol", "Snare", "Bass", "Kick", "Lead"});
    s.undo();
    CHECK(g.rows() == Rows{"Drums", ".Kick vol", ".Lead", "Snare", "Bass", "Kick"});
    // Drums moved, with its rows; never inside itself.
    REQUIRE(s.moveTrack(drums, 0));
    CHECK(g.rows() == Rows{"Snare", "Bass", "Kick", "Drums", ".Kick vol", ".Lead"});
    CHECK_FALSE(s.moveTrack(drums, 0, drums));
    CHECK_FALSE(s.moveTrack(drums, g.lead));
    // The lane moved into Drums and out.
    REQUIRE(s.moveLane(lane, 0, 0));
    CHECK(g.rows() == Rows{"Snare", "Bass", "Kick", "Drums", ".Lead", "Kick vol"});
    REQUIRE(s.moveLane(lane, g.lead));
    CHECK(g.rows() == Rows{"Snare", "Bass", "Kick", "Drums", ".Kick vol", ".Lead"});
}

TEST_CASE(
    "groups: deleted with everything in it, one step; duplicated with copies of what's in it right after it",
    "[integration]")
{
    Groups g;
    auto& s = *g.session;
    const auto drums = s.groupTracks({g.kick, g.snare}, "Drums");
    const auto lane = s.addAutomationLaneAfter(g.kick, ParamTarget{g.kick, "volume"});
    REQUIRE(lane != 0);
    REQUIRE(s.renameAutomationLane(lane, "Kick vol"));

    // Duplicated: "Drums 2" right after its rows, holding "Kick 2" and "Snare 2"; the lane that
    // drives only Kick copied for Kick 2.
    const auto copy = s.duplicateTrack(drums);
    REQUIRE(copy != 0);
    CHECK(s.undoDescription() == "Duplicate track");
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Kick vol", ".Snare", "Drums 2", ".Kick 2", ".Kick vol 2",
                           ".Snare 2", "Bass", "Lead"});
    CHECK(g.info(copy).isGroup());
    CHECK(fd::model::tracksIn(*g.snap(), copy) == 2);
    s.undo();
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Kick vol", ".Snare", "Bass", "Lead"});

    // Deleted: it, its tracks and the lane that drove only Kick, in one step.
    REQUIRE(s.deleteTrack(drums));
    CHECK(g.rows() == Rows{"Bass", "Lead"});
    s.undo();
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Kick vol", ".Snare", "Bass", "Lead"});
    CHECK(g.info(g.kick).group == drums);

    // Its last track deleted: it goes too.
    REQUIRE(s.deleteTrack(g.kick));
    REQUIRE(s.deleteTrack(g.snare));
    CHECK_FALSE(g.exists(drums));
    CHECK(g.rows() == Rows{"Bass", "Lead"});
}

TEST_CASE("groups: a group sums what's in it through its own volume and mute", "[integration]")
{
    Groups g;
    auto& s = *g.session;
    g.tone(g.kick);
    const float alone = g.peak();
    REQUIRE(alone > 0.1f);
    const auto drums = s.groupTracks({g.kick});
    CHECK(s.levelReader(drums) != nullptr); // its meter, as every channel has
    CHECK(g.peak() == Approx(alone).margin(0.01f));
    s.setVolume(drums, -6.0f);
    CHECK(g.peak() == Approx(alone * std::pow(10.0f, -6.0f / 20.0f)).margin(0.01f));
    s.setMute(drums, true);
    CHECK(g.peak() < 0.001f);
}

TEST_CASE(
    "groups: a group's mix, sends and modules; no instrument on it; a group as a sidechain source, never "
    "for what's in it",
    "[integration]")
{
    Groups g;
    auto& s = *g.session;
    const auto ret = s.addReturnTrack();
    REQUIRE(ret != 0);
    const auto drums = s.groupTracks({g.kick, g.snare}, "Drums");
    s.setVolume(drums, -6.0f);
    s.setPan(drums, -0.5f);
    s.setMute(drums, true);
    REQUIRE(s.setSend(drums, ret, -12.0f, std::nullopt));
    const auto mix = g.info(drums).mix;
    CHECK(mix.volumeDb == Approx(-6.0f).margin(0.01f));
    CHECK(mix.pan == Approx(-0.5f));
    CHECK(mix.mute);
    REQUIRE(mix.sends.size() == 1);
    CHECK(mix.sends[0].levelDb == Approx(-12.0f).margin(0.05f));
    // Solo: a member's holds the group's; the group's, exclusive, clears the member's.
    s.setSolo(g.kick, true, true);
    CHECK(fd::model::holdsSolo(*g.snap(), g.info(drums)));
    s.setSolo(drums, true, true);
    CHECK(g.info(drums).mix.solo);
    CHECK_FALSE(g.info(g.kick).mix.solo);

    // Effects and modulators, not instruments or MIDI effects.
    CHECK_FALSE(s.playsNotes("fd:compressor"));
    CHECK(s.playsNotes("fd:drum-kit"));
    CHECK_FALSE(s.playsNotes("fd:pitch"));
    CHECK(s.usesNotes("fd:pitch"));
    CHECK_FALSE(s.usesNotes("fd:compressor"));
    CHECK(s.addDevice(drums, "fd:compressor") != 0);
    CHECK(s.addDevice(drums, "fd:drum-kit") == 0);
    CHECK(s.addDevice(drums, "fd:pitch") == 0);
    CHECK(s.addModulator(drums, fd::model::ModulatorKind::Lfo) != 0);

    // A compressor on Kick can't listen to Drums (it's in it); one on Bass can.
    const auto onKick = s.addDevice(g.kick, "fd:compressor");
    const auto onBass = s.addDevice(g.bass, "fd:compressor");
    REQUIRE(onKick != 0);
    REQUIRE(onBass != 0);
    const auto has = [](const std::vector<std::uint64_t>& v, std::uint64_t id)
    { return std::find(v.begin(), v.end(), id) != v.end(); };
    CHECK_FALSE(has(s.sidechainSources(onKick), drums));
    CHECK(has(s.sidechainSources(onBass), drums));
    CHECK_FALSE(s.setSidechainSource(onKick, drums));
    REQUIRE(s.setSidechainSource(onBass, drums));
    // One on Drums can listen to Kick, in it; not to Bass, which listens to Drums now.
    const auto onDrums = s.addDevice(drums, "fd:compressor");
    CHECK(has(s.sidechainSources(onDrums), g.kick));
    CHECK_FALSE(has(s.sidechainSources(onDrums), g.bass));
    CHECK(has(s.sidechainSources(onDrums), g.lead));
}

TEST_CASE(
    "groups: the tracks' colour from their group; a track added in a group at its end; saved and opened",
    "[integration]")
{
    Groups g;
    auto& s = *g.session;
    const auto drums = s.groupTracks({g.kick, g.snare}, "Drums");
    REQUIRE(s.setTrackColour(drums, 7));
    CHECK(g.info(g.snare).colour != 7);
    REQUIRE(s.colourMembers(drums));
    CHECK(s.undoDescription() == "Track colour");
    CHECK(g.info(g.kick).colour == 7);
    CHECK(g.info(g.snare).colour == 7);
    CHECK_FALSE(s.colourMembers(drums)); // nothing to change

    const auto hats = s.addTrack("Hats", drums);
    REQUIRE(hats != 0);
    CHECK(g.rows() == Rows{"Drums", ".Kick", ".Snare", ".Hats", "Bass", "Lead"});
    // A lane first in Drums, which only the group's property says.
    const auto lane = s.addAutomationLaneAfter(g.kick, ParamTarget{g.kick, "volume"});
    REQUIRE(s.renameAutomationLane(lane, "Kick vol"));
    REQUIRE(s.moveLane(lane, g.kick));
    REQUIRE(s.moveTrack(g.kick, g.bass, 0));
    const Rows before{"Drums", ".Kick vol", ".Snare", ".Hats", "Kick", "Bass", "Lead"};
    CHECK(g.rows() == before);

    const auto file = g.root.getChildFile("Groups").getChildFile("Groups.segno");
    REQUIRE(file.getParentDirectory().createDirectory());
    REQUIRE(s.saveAs(file));
    REQUIRE(s.open(file));
    CHECK(g.rows() == before);
    const auto shot = g.snap();
    const auto opened = std::find_if(shot->tracks.begin(), shot->tracks.end(),
                                     [](const fd::model::TrackInfo& t) { return t.name == "Drums"; });
    REQUIRE(opened != shot->tracks.end());
    CHECK(opened->isGroup());
    CHECK(fd::model::tracksIn(*shot, opened->id) == 2);
}

TEST_CASE("groups: a stretch selected on a group's row acts on every track in it", "[integration]")
{
    Groups g;
    auto& s = *g.session;
    REQUIRE(s.createMidiClip(g.kick, 0.0, 2.0) != 0);
    REQUIRE(s.createMidiClip(g.snare, 0.0, 2.0) != 0);
    const auto drums = s.groupTracks({g.kick, g.snare}, "Drums");
    // Bars on the group's row: the group and what it holds (groups.md §6.5).
    const auto sel = fd::model::withTime(*g.snap(), {0.0, 2.0}, {drums}, {});
    CHECK(sel.rowTracks == std::set<std::uint64_t>{drums, g.kick, g.snare});
    const fd::model::TimeSelection stretch{.span = *sel.time, .tracks = sel.rowTracks, .lanes = sel.rowLanes};
    CHECK(s.stretchRefusal(stretch, Session::StretchCommand::Duplicate).empty());
    REQUIRE(s.editStretch(stretch, Session::StretchCommand::Duplicate).changed);
    CHECK(g.info(g.kick).clips.size() == 2);
    CHECK(g.info(g.snare).clips.size() == 2);
    CHECK(g.info(g.bass).clips.empty());
    // Copied and pasted on Bass and Lead: two rows, the group skipped.
    const auto copied = s.copyStretch(stretch);
    REQUIRE(s.pasteStretch(copied, 4.0, g.bass).changed);
    CHECK(g.info(g.bass).clips.size() == 1);
    CHECK(g.info(g.lead).clips.size() == 1);
}
