// Time selection, step 0 drawn (docs/design/time-selection.md §7, §14, §21, ADR 0073): a stretch on
// chosen rows in the arrangement (its fill, the ruler's band, the song row's tint), a stretch being
// moved, TIME in the inspector, the piano roll's stretch and the audio editor's selection, at
// standard and compact widths. Drawn for review with FD_DUMP_DIR set.

#include "ShellTestKit.h"
#include "fd/testkit/WavIO.h"
#include "model/Selection.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace fd::testing;

namespace
{
/// Drums (two audio clips of a beat), Bass (two MIDI clips), Keys (one long MIDI clip) and a
/// Cutoff lane on the Bass's volume, eight seconds (four bars at 120 BPM) long.
struct TimeFixture : ShellFixture
{
    juce::File root;
    std::uint64_t drums = 0, bass = 0, keys = 0, lane = 0, drumClip = 0, keysClip = 0;

    explicit TimeFixture(int width = 1800, int height = 1000)
    {
        shell->setSize(width, height);
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("fd-time-selection-ui-" + juce::Uuid().toString());
        REQUIRE(root.createDirectory());
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        // A new project's tracks, three of them named, the rest gone.
        session->updateSnapshotNow();
        const auto tracks = session->snapshot()->tracks;
        REQUIRE(tracks.size() >= 3);
        drums = tracks[0].id;
        bass = tracks[1].id;
        keys = tracks[2].id;
        for (std::size_t i = 3; i < tracks.size(); ++i)
            REQUIRE(session->deleteTrack(tracks[i].id));
        session->renameTrack(drums, "Drums");
        session->renameTrack(bass, "Bass");
        session->renameTrack(keys, "Keys");

        // A beat: a decaying hit on every half second.
        fd::testkit::AudioBuffer beat(2, 4 * 48000);
        std::uint32_t noise = 12345;
        for (int i = 0; i < beat.numSamples(); ++i)
        {
            noise = noise * 1664525u + 1013904223u;
            const float t = static_cast<float>(i) / 48000.0f, hit = std::fmod(t, 0.5f);
            const float x =
                std::exp(-hit / 0.08f) * (0.6f * std::sin(2.0f * 3.14159265f * 110.0f * t) +
                                          0.4f * (static_cast<float>(noise >> 8) / 8388608.0f - 1.0f));
            beat.at(0, i) = 0.6f * x;
            beat.at(1, i) = 0.5f * x;
        }
        const auto file = root.getChildFile("Beat.wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), beat, 48000.0));
        drumClip = session->insertAudioFile(drums, file, 0.0);
        REQUIRE(drumClip != 0);
        REQUIRE(session->insertAudioFile(drums, file, 4.0) != 0);

        phrase(bass, 0.0, 3.0, 36);
        phrase(bass, 4.5, 3.5, 38);
        keysClip = phrase(keys, 0.0, 8.0, 60);

        lane = session->addAutomationLane(fd::engine::Session::ParamTarget{bass, "volume"});
        REQUIRE(lane != 0);
        REQUIRE(session->setLanePoints(
            lane, {{0.0, 0.5f, 0.0f}, {3.0, 0.8f, 0.0f}, {5.0, 0.3f, 0.0f}, {8.0, 0.6f, 0.0f}}));
        session->renameAutomationLane(lane, "Cutoff");
        settle();
        shell->arrangement().setView(0.0, (width - 600.0) / 9.0); // the four bars, and a little more
        settle();
    }

    ~TimeFixture()
    {
        shell.reset();
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t phrase(std::uint64_t t, double start, double length, int pitch)
    {
        const auto clip = session->createMidiClip(t, start, length);
        REQUIRE(clip != 0);
        std::vector<fd::model::Note> notes;
        for (double b = 0.0; b + 0.5 <= length * 2.0; b += 1.0)
            notes.push_back(
                {.pitch = pitch + static_cast<int>(b) % 5 * 2, .start = b, .length = 0.75, .velocity = 0.8f});
        session->setNotes(clip, notes);
        return clip;
    }

    void settle()
    {
        session->updateSnapshotNow();
        shell->arrangement().setSnapshot(session->snapshot());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        shell->finishAnimations();
        shell->arrangement().frame(1.0 / 60.0, 0.0);
    }

    void dump(const juce::String& name, juce::Component& c, juce::Rectangle<int> area = {})
    {
        settle();
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            dumpPng(dir, name + "@2x.png",
                    c.createComponentSnapshot(area.isEmpty() ? c.getLocalBounds() : area, true, 2.0f));
    }
};
} // namespace

TEST_CASE("time selection drawn: a stretch on two tracks and a lane, a track between them left out; the "
          "ruler's band and TIME in the inspector (time-selection.md §7)",
          "[integration]")
{
    for (const auto [width, height] : {std::pair{1800, 1000}, std::pair{1280, 800}})
    {
        TimeFixture f(width, height);
        auto& view = f.shell->arrangement();
        view.selectTime({2.0, 6.0}, {f.drums, f.keys}, {f.lane}, f.drums);
        f.settle();

        const auto& sel = view.selection();
        CHECK(sel.hasTime());
        CHECK(sel.rowTracks == std::set<std::uint64_t>{f.drums, f.keys});
        CHECK(sel.rowLanes == std::set<std::uint64_t>{f.lane});
        CHECK(sel.channel == f.drums);
        int inTime = 0;
        for (const auto& row : view.rows())
            inTime += view.rowInTime(row) ? 1 : 0;
        CHECK(inTime == 3); // Drums, Keys and the lane; not Bass

        // TIME over the Drums' sections: Start, End, Length and Rows.
        auto& inspector = f.shell->inspector();
        REQUIRE(!inspector.sections().empty());
        CHECK(inspector.sections().front() == fd::model::InspectorSection::Time);
        const auto rows = inspector.properties(fd::model::InspectorSection::Time);
        REQUIRE(rows.size() == 4);
        CHECK(rows[3].value == "2 tracks, 1 lane");
        CHECK(inspector.timeField(0).text() == "2.1.1");
        CHECK(inspector.timeField(1).text() == "4.1.1");

        // Typing a new length moves the selection's end, on the same rows.
        inspector.timeField(2).setValue(8.0, juce::sendNotification); // two bars
        f.settle();
        REQUIRE(view.selection().time);
        CHECK(view.selection().time->end == 6.0);
        CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.drums, f.keys});

        const auto tag = width == 1800 ? juce::String() : juce::String("-compact");
        f.dump("time-selection" + tag, *f.shell);
        f.dump("time-selection-arrangement" + tag, view);
        f.dump("time-selection-inspector" + tag, inspector);
    }
}

TEST_CASE("time selection drawn: a stretch being moved by a title strip inside it, and copied a track down "
          "(time-selection.md §10)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.selectTime({2.0, 4.0}, {f.drums, f.bass}, {f.lane}, f.drums);
    view.previewStretchMove(fd::app::ArrangementView::StretchMove{.seconds = 4.0});
    f.dump("time-selection-moving", view);
    view.previewStretchMove(fd::app::ArrangementView::StretchMove{.seconds = 2.0, .tracks = 1, .copy = true});
    f.dump("time-selection-copying-down", view);
    view.previewStretchMove(std::nullopt);
}

TEST_CASE(
    "time selection drawn: every track and lane, the song row's overview tinted too (time-selection.md §7)",
    "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.selectTime({4.0, 8.0}, {f.drums, f.bass, f.keys}, {f.lane});
    f.dump("time-selection-every-row", view);
    // One track fewer: the song row isn't tinted (drawn for comparison).
    view.selectTime({4.0, 8.0}, {f.drums, f.keys}, {f.lane});
    f.dump("time-selection-not-every-row", view);
}

TEST_CASE("time selection drawn: inside one clip it opens the clip view (§14.3); the piano roll's stretch "
          "under a box of notes (§14.1)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.selectTime({2.0, 4.0}, {f.keys}, {}, f.keys);
    f.settle();
    auto& detail = f.shell->detail();
    CHECK(detail.showsPianoRoll());
    auto& roll = detail.pianoRoll();
    REQUIRE(roll.clip());
    CHECK(roll.clip()->id == f.keysClip);
    // The box's notes (beats 4 .. 8 of the clip) and its stretch.
    std::set<std::size_t> boxed;
    for (std::size_t i = 0; i < roll.notes().size(); ++i)
        if (roll.notes()[i].start >= 4.0 && roll.notes()[i].start < 8.0)
            boxed.insert(i);
    roll.setSelection(boxed);
    roll.setTimeSelection(fd::model::TimeSpan{2.0, 4.0});
    f.dump("time-selection-piano-roll", detail);
    f.dump("time-selection-with-clip-view", *f.shell);
}

TEST_CASE("time selection drawn: the audio editor's selection, looking as every time selection does, its "
          "commands renamed (§14.2)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.setSelection(fd::model::withClips(*f.session->snapshot(), {f.drumClip}, f.drums, f.drums));
    f.settle();
    auto& detail = f.shell->detail();
    auto& audio = detail.audioClipView();
    audio.waitForWaveform();
    audio.setRange(std::pair{1.0, 2.5});
    f.settle();
    const auto items = itemsOf(detail.audioMenu());
    CHECK(items.contains("Crop to selection"));
    CHECK(items.contains("Fade to selection"));
    CHECK(items.contains("Clip loop to selection"));
    CHECK_FALSE(items.contains("Crop to range"));
    f.dump("time-selection-audio-editor", detail);
}
