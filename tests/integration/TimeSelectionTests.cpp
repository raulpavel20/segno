// Time selection's commands (ADR 0073, docs/design/time-selection.md §8–§11), through
// engine::Session: a stretch on its rows deleted, split, cropped, muted, reversed, faded, given a
// MIDI clip, duplicated, copied, pasted and moved, each one undo step that sounds as the same edit
// made by hand; and the song-wide "… time" commands moving the whole song and its sections (§9).

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"
#include "model/Editing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using Command = fd::engine::Session::StretchCommand;
using fd::model::ClipInfo;
using fd::model::TimeSelection;
using fd::testkit::AudioBuffer;

namespace
{
/// Two tracks, each with an eight-second audio clip of noise in its own level, and a lane on the
/// first's volume with a ramp; at 120 BPM.
struct Stretches
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-bounce-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-time-commands-" + juce::Uuid().toString());
    std::uint64_t a = 0, b = 0, lane = 0;

    Stretches()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        REQUIRE(root.createDirectory());
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->updateSnapshotNow();
        a = session->snapshot()->tracks.at(0).id;
        b = session->snapshot()->tracks.at(1).id;
        REQUIRE(session->insertAudioFile(a, noise("A.wav", 0.5f, 1), 0.0) != 0);
        REQUIRE(session->insertAudioFile(b, noise("B.wav", 0.3f, 2), 0.0) != 0);
        lane = session->addAutomationLane(Session::ParamTarget{a, "volume"});
        REQUIRE(lane != 0);
        REQUIRE(session->setLanePoints(lane, {{0.0, 0.4f, 0.0f}, {8.0, 0.9f, 0.0f}}));
        session->updateSnapshotNow();
    }

    ~Stretches()
    {
        session.reset();
        root.deleteRecursively();
    }

    juce::File noise(const juce::String& name, float level, std::uint32_t seed)
    {
        // At the session's rate: a moved clip of a file at another rate resamples from another phase.
        const double rate = session->sampleRate();
        AudioBuffer buffer(2, static_cast<int>(8 * rate));
        for (int i = 0; i < buffer.numSamples(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float x = level * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
            buffer.at(0, i) = x;
            buffer.at(1, i) = -x;
        }
        const auto file = root.getChildFile(name);
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), buffer, rate));
        return file;
    }

    fd::model::TrackInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }

    std::vector<std::pair<double, double>> spans(std::uint64_t id)
    {
        std::vector<std::pair<double, double>> out;
        for (const auto& c : info(id).clips)
            out.emplace_back(c.start, c.end());
        return out;
    }

    fd::model::AutomationPoints points()
    {
        session->updateSnapshotNow();
        return session->snapshot()->automationLane(lane)->points;
    }

    AudioBuffer render(double start, double end, std::optional<std::uint64_t> channel = std::nullopt)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, channel, session->sampleRate()}, out.getFile(),
                                   [&](const std::string& e)
                                   {
                                       error = e;
                                       done = true;
                                   });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 1000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }

    /// The clips on `track` crossing `t`, split there by hand.
    void splitByHand(std::uint64_t track, double t)
    {
        std::vector<std::uint64_t> ids;
        for (const auto& c : info(track).clips)
            if (c.start < t - 1e-6 && c.end() > t + 1e-6)
                ids.push_back(c.id);
        session->splitClips(ids, t);
    }

    /// The clips on `track` inside [from, to].
    std::vector<std::uint64_t> inside(std::uint64_t track, double from, double to)
    {
        std::vector<std::uint64_t> ids;
        for (const auto& c : info(track).clips)
            if (c.start >= from - 1e-6 && c.end() <= to + 1e-6)
                ids.push_back(c.id);
        return ids;
    }
};

double differenceDb(const AudioBuffer& x, const AudioBuffer& y)
{
    REQUIRE(x.numChannels() == y.numChannels());
    REQUIRE(x.numSamples() == y.numSamples());
    float worst = 0.0f;
    for (int c = 0; c < x.numChannels(); ++c)
        for (int s = 0; s < x.numSamples(); ++s)
            worst = std::max(worst, std::abs(x.at(c, s) - y.at(c, s)));
    return worst > 0.0f ? 20.0 * std::log10(worst) : -200.0;
}
} // namespace

TEST_CASE("time selection commands: Delete clears a stretch inside a clip on its rows, the gap staying; the "
          "lane's curve outside kept; one undo step; it sounds as splitting and deleting by hand",
          "[integration]")
{
    Stretches f;
    const TimeSelection stretch{{2.0, 4.0}, {f.a}, {f.lane}};
    CHECK(f.session->stretchRefusal(stretch, Command::Delete).empty());
    const auto before = f.points();
    REQUIRE(f.session->editStretch(stretch, Command::Delete).changed);
    CHECK(f.session->undoDescription() == "Delete");
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 2.0}, {4.0, 8.0}});
    CHECK(f.spans(f.b) == std::vector<std::pair<double, double>>{{0.0, 8.0}}); // not in it
    // The lane: the ramp outside the stretch as it was.
    for (double t : {0.0, 1.0, 1.9, 4.1, 6.0, 8.0})
        CHECK(fd::model::automationValueAt(f.points(), t) ==
              Approx(fd::model::automationValueAt(before, t)).margin(1e-5));
    const auto byCommand = f.render(0.0, 8.0);

    f.session->undo();
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 8.0}});
    f.splitByHand(f.a, 2.0);
    f.splitByHand(f.a, 4.0);
    f.session->deleteClips(f.inside(f.a, 2.0, 4.0));
    f.session->setLanePoints(f.lane, fd::model::clearPoints(before, 2.0, 4.0));
    CHECK(differenceDb(f.render(0.0, 8.0), byCommand) < -120.0);
}

TEST_CASE("time selection commands: Split cuts at the edges and hands back the parts inside; Crop keeps only "
          "the stretch of the clips crossing it",
          "[integration]")
{
    Stretches f;
    const TimeSelection stretch{{2.0, 5.0}, {f.a, f.b}, {}};
    const auto split = f.session->editStretch(stretch, Command::Split);
    CHECK(f.session->undoDescription() == "Split");
    CHECK(split.clips.size() == 2);
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 2.0}, {2.0, 5.0}, {5.0, 8.0}});
    f.session->undo();

    REQUIRE(f.session->editStretch({{2.0, 5.0}, {f.a}, {}}, Command::Crop).changed);
    CHECK(f.session->undoDescription() == "Crop");
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{2.0, 5.0}});
    CHECK(f.spans(f.b) == std::vector<std::pair<double, double>>{{0.0, 8.0}});
    // Nothing crossing its edges: refused, saying why.
    CHECK_FALSE(f.session->stretchRefusal({{2.0, 5.0}, {f.a}, {}}, Command::Crop).empty());
}

TEST_CASE(
    "time selection commands: Duplicate lays a copy right after the stretch on its rows, overwriting; it "
    "sounds as the same edit made by hand, and the stretch plays twice",
    "[integration]")
{
    Stretches f;
    const auto originalB = f.render(0.0, 8.0, f.b);
    const auto lanePoints = f.points();
    const auto result = f.session->editStretch({{1.0, 3.0}, {f.a, f.b}, {f.lane}}, Command::Duplicate);
    REQUIRE(result.changed);
    CHECK(f.session->undoDescription() == "Duplicate");
    CHECK(result.span.start == Approx(3.0));
    CHECK(result.span.end == Approx(5.0));
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 3.0}, {3.0, 5.0}, {5.0, 8.0}});
    // The lane: the copy carries the ramp of 1 .. 3 into 3 .. 5; after 5 it's as it was.
    const auto lane = f.points();
    CHECK(fd::model::automationValueAt(lane, 4.0) == Approx(0.4f + 0.5f * 2.0f / 8.0f).margin(1e-4));
    CHECK(fd::model::automationValueAt(lane, 6.0) == Approx(0.4f + 0.5f * 6.0f / 8.0f).margin(1e-4));

    // On the track without automation, what plays from 3 to 5 is what played from 1 to 3, and the
    // rest as it was (a tenth of a second from the new edges' fades).
    const auto nowB = f.render(0.0, 8.0, f.b);
    const int rate = static_cast<int>(f.session->sampleRate());
    float worst = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int s = rate * 31 / 10; s < rate * 49 / 10; ++s)
            worst = std::max(worst, std::abs(nowB.at(c, s) - originalB.at(c, s - 2 * rate)));
    CHECK(20.0 * std::log10(std::max(worst, 1e-12f)) < -100.0);
    worst = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int s = rate * 51 / 10; s < rate * 79 / 10; ++s)
            worst = std::max(worst, std::abs(nowB.at(c, s) - originalB.at(c, s)));
    CHECK(20.0 * std::log10(std::max(worst, 1e-12f)) < -100.0);

    // The mix, against the same edit made by hand: split at the edges, the middle duplicated over
    // what was there, the lane's piece laid over 3 .. 5.
    const auto byCommand = f.render(0.0, 8.0);
    f.session->undo();
    for (const auto track : {f.a, f.b})
    {
        for (const double t : {1.0, 3.0, 5.0})
            f.splitByHand(track, t);
        const auto old = f.inside(track, 3.0, 5.0);
        f.session->deleteClips(old);
        const auto middle = f.inside(track, 1.0, 3.0);
        REQUIRE(middle.size() == 1);
        f.session->updateSnapshotNow();
        const auto* c = fd::model::findClip(*f.session->snapshot(), middle[0])->first;
        f.session->duplicateClips(
            {{.clip = c->id, .track = track, .start = 3.0, .length = c->length, .offset = c->offset}});
    }
    f.session->setLanePoints(
        f.lane, fd::model::placePoints(lanePoints, fd::model::piecePoints(lanePoints, 1.0, 3.0), 3.0, 2.0));
    CHECK(differenceDb(f.render(0.0, 8.0), byCommand) < -120.0);
}

TEST_CASE(
    "time selection commands: a copied stretch pastes at another time and another track, keeping its rows' "
    "places; the lane's curve back on its own lane; a cut still pastes",
    "[integration]")
{
    Stretches f;
    const auto third = f.session->addTrack("Third");
    const TimeSelection stretch{{1.0, 2.0}, {f.a}, {f.lane}};
    const auto clip = f.session->copyStretch(stretch);
    REQUIRE(!clip.empty());
    CHECK(clip.length() == Approx(1.0));
    // Onto the second track, at 6 s: only its 6 .. 7 is overwritten.
    const auto pasted = f.session->pasteStretch(clip, 6.0, f.b);
    REQUIRE(pasted.changed);
    CHECK(f.session->undoDescription() == "Paste");
    CHECK(f.spans(f.b) == std::vector<std::pair<double, double>>{{0.0, 6.0}, {6.0, 7.0}, {7.0, 8.0}});
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 8.0}});
    REQUIRE(pasted.clips.size() == 1);
    f.session->updateSnapshotNow();
    CHECK(fd::model::findClip(*f.session->snapshot(), pasted.clips[0])->first->offset ==
          Approx(1.0).margin(1e-6));
    // Onto the empty third track at 0: a clip of its own, 0 .. 1.
    f.session->pasteStretch(clip, 0.0, third);
    CHECK(f.spans(third) == std::vector<std::pair<double, double>>{{0.0, 1.0}});
    // Its lane's curve went back onto its own lane, at 6 .. 7 and 0 .. 1.
    CHECK(fd::model::automationValueAt(f.points(), 6.5) == Approx(0.4f + 0.5f * 1.5f / 8.0f).margin(1e-4));

    // Cut: copied, then cleared; it still pastes.
    const auto cut = f.session->copyStretch({{4.0, 5.0}, {f.a}, {}});
    f.session->editStretch({{4.0, 5.0}, {f.a}, {}}, Command::Delete);
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 4.0}, {5.0, 8.0}});
    f.session->pasteStretch(cut, 4.0, f.a);
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 4.0}, {4.0, 5.0}, {5.0, 8.0}});
}

TEST_CASE(
    "time selection commands: a stretch moved later and a track down leaves its place empty and overwrites "
    "where it lands; lanes stay on their rows; copied, the source stays",
    "[integration]")
{
    Stretches f;
    const auto result = f.session->moveStretch({{1.0, 2.0}, {f.a}, {f.lane}}, 4.0, 1, false);
    REQUIRE(result.changed);
    CHECK(f.session->undoDescription() == "Move");
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 1.0}, {2.0, 8.0}});
    CHECK(f.spans(f.b) == std::vector<std::pair<double, double>>{{0.0, 5.0}, {5.0, 6.0}, {6.0, 8.0}});
    // The lane: its 1 .. 2 now plays at 5 .. 6 on the same lane, and 1 .. 2 holds a line.
    CHECK(fd::model::automationValueAt(f.points(), 5.5) == Approx(0.4f + 0.5f * 1.5f / 8.0f).margin(1e-4));
    f.session->undo();
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 8.0}});

    REQUIRE(f.session->moveStretch({{1.0, 2.0}, {f.a}, {}}, 4.0, 1, true).changed);
    CHECK(f.session->undoDescription() == "Copy");
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 8.0}});
    CHECK(f.spans(f.b) == std::vector<std::pair<double, double>>{{0.0, 5.0}, {5.0, 6.0}, {6.0, 8.0}});
    // Past the last track: nothing.
    CHECK_FALSE(f.session->moveStretch({{1.0, 2.0}, {f.a}, {}}, 0.0, 9, false).changed);
}

TEST_CASE(
    "time selection commands: Mute and Reverse act on the parts inside; Fade fades in, out or crosses a "
    "join; New MIDI clip covers the stretch on each track",
    "[integration]")
{
    Stretches f;
    REQUIRE(f.session->editStretch({{2.0, 3.0}, {f.a}, {}}, Command::Mute).changed);
    CHECK(f.session->undoDescription() == "Mute");
    for (const auto& c : f.info(f.a).clips)
        CHECK(c.muted == (c.start == Approx(2.0).margin(1e-6)));
    f.session->editStretch({{2.0, 3.0}, {f.a}, {}}, Command::Mute); // all muted: unmuted
    for (const auto& c : f.info(f.a).clips)
        CHECK_FALSE(c.muted);

    REQUIRE(f.session->editStretch({{5.0, 6.0}, {f.a}, {}}, Command::Reverse).changed);
    for (const auto& c : f.info(f.a).clips)
        CHECK(c.sound.reversed == (c.start == Approx(5.0).margin(1e-6)));

    // A fade-in from the clip's start to 1 s; a crossfade across a join at 2 s, made by moving
    // 4 .. 6 back against it (a split alone plays on seamlessly and takes none: fades.md §4).
    REQUIRE(f.session->editStretch({{0.0, 1.0}, {f.b}, {}}, Command::Fade).changed);
    CHECK(f.session->undoDescription() == "Fade");
    CHECK(f.info(f.b).clips.front().fadeIn.length == Approx(1.0).margin(1e-6));
    // Across a split alone it would do nothing, so it's refused there, saying why.
    REQUIRE_FALSE(f.session->splitClips({f.info(f.b).clips.front().id}, 3.0).empty());
    CHECK_FALSE(f.session->stretchRefusal({{2.75, 3.25}, {f.b}, {}}, Command::Fade).empty());
    f.session->undo();
    REQUIRE(f.session->moveStretch({{4.0, 6.0}, {f.b}, {}}, -2.0, 0, false).changed);
    REQUIRE(f.session->editStretch({{1.75, 2.25}, {f.b}, {}}, Command::Fade).changed);
    bool crossed = false;
    for (const auto& c : f.info(f.b).clips)
        crossed = crossed || c.crossfadeOut.has_value();
    CHECK(crossed);

    const auto midi = f.session->editStretch({{1.0, 3.0}, {f.a, f.b}, {}}, Command::NewMidiClip);
    CHECK(midi.clips.size() == 2);
    CHECK(f.session->undoDescription() == "New MIDI clip");
    f.session->updateSnapshotNow();
    for (const auto id : midi.clips)
    {
        const auto found = fd::model::findClip(*f.session->snapshot(), id);
        REQUIRE(found);
        const auto* c = found->first;
        CHECK(c->kind == ClipInfo::Kind::Midi);
        CHECK(c->start == Approx(1.0));
        CHECK(c->end() == Approx(3.0));
    }
}

TEST_CASE(
    "time selection commands: song-wide Insert, Delete, Duplicate, Copy and Paste time move every track, "
    "lane and section mark",
    "[integration]")
{
    Stretches f;
    // Sections A 0 .. 4 and B 4 .. 8.
    REQUIRE(f.session->addSection(0.0, 4.0, "A") != 0);
    REQUIRE(f.session->addSection(4.0, 8.0, "B") != 0);
    auto sections = [&]
    {
        f.session->updateSnapshotNow();
        std::vector<std::tuple<std::string, double, double>> out;
        for (const auto& s : f.session->snapshot()->sections)
            out.emplace_back(s.name, s.start, s.end);
        return out;
    };

    REQUIRE(f.session->insertTime(2.0, 1.0));
    CHECK(f.session->undoDescription() == "Insert time");
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 2.0}, {3.0, 9.0}});
    CHECK(f.spans(f.b) == std::vector<std::pair<double, double>>{{0.0, 2.0}, {3.0, 9.0}});
    CHECK(sections() ==
          std::vector<std::tuple<std::string, double, double>>{{"A", 0.0, 5.0}, {"B", 5.0, 9.0}});
    f.session->undo();

    REQUIRE(f.session->deleteTime(3.0, 5.0));
    CHECK(f.session->undoDescription() == "Delete time");
    CHECK(f.spans(f.a) == std::vector<std::pair<double, double>>{{0.0, 3.0}, {3.0, 6.0}});
    CHECK(sections() ==
          std::vector<std::tuple<std::string, double, double>>{{"A", 0.0, 3.0}, {"B", 3.0, 6.0}});
    f.session->undo();

    REQUIRE(f.session->duplicateTime(4.0, 8.0));
    CHECK(f.session->undoDescription() == "Duplicate time");
    CHECK(sections().size() == 3); // B copied whole
    f.session->undo();

    const auto piece = f.session->copyTime(0.0, 4.0);
    REQUIRE(!piece.empty());
    f.session->pasteTime(piece, 8.0);
    CHECK(f.session->undoDescription() == "Paste time");
    CHECK(f.spans(f.a).back().second == Approx(12.0));
    CHECK(sections().size() == 3);
}
