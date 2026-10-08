// engine::Session (ADR 0018): commands are undoable and snapshots mirror the Edit.

#include "engine/Session.h"
#include "model/Editing.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("session snapshots mirror tracks and clips; edits undo", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);

    const auto first = session.snapshot();
    REQUIRE(first->tracks.size() == 4);
    CHECK(first->tempo.bpm == Approx(120.0));
    CHECK(first->tempo.numerator == 4);
    CHECK(first->contentEnd() == 0.0);
    CHECK_FALSE(session.canUndo()); // a new project starts with a clean history

    int published = 0;
    session.onSnapshotChanged = [&] { ++published; };

    juce::TemporaryFile file(".wav");
    REQUIRE(fd::testkit::writeWav(file.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(440.0, 48000.0, 96000, 0.5f, 2), 48000.0));
    const auto trackId = first->tracks[1].id;
    const auto clipId = session.insertAudioFile(trackId, file.getFile(), 3.0);
    REQUIRE(clipId != 0);
    CHECK(session.insertAudioFile(trackId, juce::File("/nonexistent.wav"), 0.0) == 0);

    // Snapshots are rebuilt asynchronously after edits (coalesced).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(published >= 1);
    auto s = session.snapshot();
    CHECK(s->version > first->version);
    REQUIRE(s->tracks[1].clips.size() == 1);
    const auto& clip = s->tracks[1].clips[0];
    CHECK(clip.id == clipId);
    CHECK(clip.start == Approx(3.0));
    CHECK(clip.length == Approx(2.0));
    CHECK(clip.sourcePath == file.getFile().getFullPathName().toStdString());
    CHECK(s->contentEnd() == Approx(5.0));
    CHECK(first->tracks[1].clips.empty()); // published snapshots never change

    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[1].clips.empty());
    session.redo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[1].clips.size() == 1);

    // Transport position doesn't produce snapshots (after pending updates have settled).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    const auto version = session.snapshot()->version;
    session.setPosition(1.5);
    CHECK(session.position() == Approx(1.5));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(session.snapshot()->version == version);
}

TEST_CASE("session clip edits: move, trim, duplicate, split, delete; each one undo step", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);
    juce::TemporaryFile file(".wav");
    REQUIRE(fd::testkit::writeWav(file.getFile().getFullPathName().toStdString(),
                                  fd::testkit::sine(440.0, 48000.0, 4 * 48000, 0.5f, 2), 48000.0));
    const auto tracks = session.snapshot()->tracks;
    const auto a = session.insertAudioFile(tracks[0].id, file.getFile(), 1.0);
    session.updateSnapshotNow();
    auto clipOf = [&](std::uint64_t id) { return fd::model::findClip(*session.snapshot(), id); };
    REQUIRE(clipOf(a));
    CHECK(clipOf(a)->first->sourceLength == Approx(4.0));

    // Move to track 2 at 2 s, trimmed to start 0.5 s into the file.
    session.applyClipEdits({{.clip = a, .track = tracks[2].id, .start = 2.0, .length = 3.0, .offset = 0.5}});
    session.updateSnapshotNow();
    auto moved = clipOf(a);
    REQUIRE(moved);
    CHECK(moved->second == 2);
    CHECK(moved->first->start == Approx(2.0));
    CHECK(moved->first->length == Approx(3.0));
    CHECK(moved->first->offset == Approx(0.5));

    // Split at 3 s: two clips that join seamlessly.
    const auto right = session.splitClips({a}, 3.0);
    session.updateSnapshotNow();
    REQUIRE(right.size() == 1);
    CHECK(clipOf(a)->first->length == Approx(1.0));
    CHECK(clipOf(right[0])->first->start == Approx(3.0));
    CHECK(clipOf(right[0])->first->offset == Approx(1.5));
    CHECK(session.splitClips({a}, 10.0).empty()); // outside the clip: nothing

    // Duplicate both parts after themselves.
    const auto copies = session.duplicateClips(fd::model::duplicatePlacements(*session.snapshot(), {a, right[0]}));
    session.updateSnapshotNow();
    REQUIRE(copies.size() == 2);
    CHECK(session.snapshot()->tracks[2].clips.size() == 4);

    session.deleteClips(copies);
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 2);

    // Undo walks back one gesture at a time: delete, duplicate, split, move.
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 4);
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 2);
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tracks[2].clips.size() == 1);
    session.undo();
    session.updateSnapshotNow();
    CHECK(clipOf(a)->second == 0);
    CHECK(clipOf(a)->first->start == Approx(1.0));
}

TEST_CASE("session transport: loop, metronome, tempo and signature", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    tracktion::Engine engine{"fd-integration-tests"};
    fd::engine::Session session(engine);

    session.setLoopRange(8.0, 4.0); // reversed ends are fine
    CHECK(session.loopRange().first == Approx(4.0));
    CHECK(session.loopRange().second == Approx(8.0));
    session.setLooping(true);
    CHECK(session.isLooping());
    session.setMetronome(true);
    CHECK(session.isMetronomeOn());

    session.setTempo(96.5);
    session.setTimeSignature(7, 8);
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tempo.bpm == Approx(96.5));
    CHECK(session.snapshot()->tempo.numerator == 7);
    CHECK(session.snapshot()->tempo.denominator == 8);
    session.undo();
    session.undo();
    session.updateSnapshotNow();
    CHECK(session.snapshot()->tempo.bpm == Approx(120.0));
    CHECK(session.snapshot()->tempo.numerator == 4);
}
