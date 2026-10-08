// engine::Session (ADR 0018): commands are undoable and snapshots mirror the Edit.

#include "engine/Session.h"
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
