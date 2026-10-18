// Time selection, step 0 drawn (docs/design/time-selection.md §7, §14, §21, ADR 0073): a stretch on
// chosen rows in the arrangement (its fill, the ruler's band, the song row's tint), a stretch being
// moved, TIME in the inspector, the piano roll's stretch and the audio editor's selection, at
// standard and compact widths. Drawn for review with FD_DUMP_DIR set.

#include "ShellTestKit.h"
#include "fd/testkit/WavIO.h"
#include "model/Selection.h"

#include <catch2/catch_approx.hpp>
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

namespace
{
/// A point in the arrangement: `seconds` on row `row`, `y` down from the row's top.
juce::Point<float> at(fd::app::ArrangementView& view, double seconds, int row, float y)
{
    const auto& rows = view.rows();
    REQUIRE(row < static_cast<int>(rows.size()));
    float top = static_cast<float>(fd::app::ArrangementView::defaultRowsTop());
    for (int i = 0; i < row; ++i)
        top += 64.0f;
    return {static_cast<float>(fd::app::ArrangementView::headerWidth()) +
                static_cast<float>(view.timeline().xForTime(seconds)),
            top + y};
}
} // namespace

TEST_CASE(
    "time selection: a drag on a clip's body or empty space selects time on the rows it crosses; a click "
    "selects the clip; its title strip moves it (time-selection.md §5)",
    "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.setAllTrackHeights(64.0);
    f.settle();
    int told = 0;
    const auto previous = view.onSelectionChanged;
    view.onSelectionChanged = [&]
    {
        ++told;
        if (previous)
            previous();
    };

    // A click on the Keys clip's body selects it.
    click(view, at(view, 1.0, 2, 40.0f));
    CHECK(view.selection().clips == std::set<std::uint64_t>{f.keysClip});
    CHECK_FALSE(view.selection().hasTime());

    // A drag on its body down to the Bass row: a time selection on Bass and Keys, from where it
    // started, snapped (the grid at this zoom is a sixteenth or coarser).
    told = 0;
    drag(view, at(view, 1.0, 2, 40.0f), at(view, 3.0, 1, 40.0f));
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.bass, f.keys});
    CHECK(view.selection().time->start == Catch::Approx(1.0).margin(0.13));
    CHECK(view.selection().time->end == Catch::Approx(3.0).margin(0.13));
    CHECK(view.selection().channel == f.keys);
    CHECK(view.selection().clips.empty());
    CHECK(told == 1);
    // Stopped, the playhead goes to its start (§12).
    CHECK(f.session->position() == Catch::Approx(view.selection().time->start));

    // From empty space, through the lane: the lane's row too.
    drag(view, at(view, 4.0, 1, 40.0f), at(view, 4.5, 3, 30.0f));
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().rowLanes == std::set<std::uint64_t>{f.lane});
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.bass, f.keys});

    // A clip's title strip still moves the clip; hovering it shows the hand that moves things, its
    // body the plain pointer (a drag there selects time).
    const auto hover = [&](juce::Point<float> p)
    {
        view.mouseMove(mouseEvent(view, p, p, false, {}));
        return view.getMouseCursor();
    };
    const fd::ui::HandCursor hand;
    CHECK(hover(at(view, 0.5, 0, 5.0f)) == hand.get());
    CHECK(hover(at(view, 0.5, 0, 40.0f)) == juce::MouseCursor::NormalCursor);
    // The hand is a system cursor's size, as the loop cursor is (JUCE's own is a 16 px picture a
    // 2x screen leaves tiny).
    const auto picture = fd::ui::cursorImage(fd::ui::Icon::Hand);
    CHECK(picture.getImage().getWidth() / picture.getScale() >= 32.0);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "hand-cursor@2x.png", picture.getImage());
    const auto drum = view.selection();
    drag(view, at(view, 0.5, 0, 5.0f), at(view, 1.5, 0, 5.0f));
    f.session->updateSnapshotNow();
    CHECK(fd::model::findClip(*f.session->snapshot(), f.drumClip)->first->start > 0.5);
    CHECK(f.session->undoDescription() == "Move clips");
    (void)drum;
}

TEST_CASE("time selection: Shift extends it, Ctrl+click takes a row out, Ctrl+A widens it, Escape clears it "
          "(time-selection.md §6)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.setAllTrackHeights(64.0);
    f.settle();
    const auto& actions = f.shell->actions();
    drag(view, at(view, 2.0, 0, 40.0f), at(view, 4.0, 0, 40.0f));
    REQUIRE(view.selection().rowTracks == std::set<std::uint64_t>{f.drums});

    // Shift+click on the Keys row, later: to the click, the rows between taken in.
    click(view, at(view, 6.0, 2, 40.0f), 1, juce::ModifierKeys::shiftModifier);
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().time->end == Catch::Approx(6.0).margin(0.13));
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.drums, f.bass, f.keys});
    // Ctrl+click inside it on Bass: Bass taken out.
    click(view, at(view, 3.0, 1, 40.0f), 1, juce::ModifierKeys::commandModifier);
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.drums, f.keys});
    // Ctrl+A: every track and lane.
    REQUIRE(actions.perform("edit.selectAll"));
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.drums, f.bass, f.keys});
    CHECK(view.selection().rowLanes == std::set<std::uint64_t>{f.lane});
    // Shift+→ moves its end a grid step later.
    const double end = view.selection().time->end;
    REQUIRE(
        actions.handleKey(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK(view.selection().time->end > end);
    // Escape: none.
    REQUIRE(actions.handleKey(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(view.selection().hasTime());
}

TEST_CASE("time selection: the keys act on it: Delete, Duplicate, Copy and Paste, Split, Loop and Zoom say "
          "so; its right-click menu; the song-wide commands (time-selection.md §8, §9, §12, §17)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.setAllTrackHeights(64.0);
    f.settle();
    const auto& actions = f.shell->actions();
    auto spans = [&](std::uint64_t track)
    {
        f.session->updateSnapshotNow();
        std::vector<std::pair<double, double>> out;
        for (const auto& c : f.session->snapshot()->channel(track)->clips)
            out.emplace_back(c.start, c.end());
        return out;
    };
    view.selectTime({2.0, 3.0}, {f.keys}, {}, f.keys);
    f.settle();
    CHECK(actions.label("edit.split") == "Split at selection");
    CHECK(actions.label("transport.loop") == "Loop selection");
    CHECK(actions.label("view.zoomFit") == "Zoom to selection");
    CHECK(actions.label("clip.crossfade") == "Fade to selection");

    // Its right-click menu lists its commands.
    juce::PopupMenu menu;
    REQUIRE(view.onTimeMenu);
    view.onTimeMenu(menu);
    const auto items = itemsOf(menu);
    for (const auto* item :
         {"Cut", "Copy", "Paste", "Delete", "Duplicate", "Split at selection", "Crop to selection",
          "Fade to selection", "Loop selection", "Insert time", "Delete time"})
        CHECK(items.contains(item));

    // Delete: the stretch goes, the gap stays.
    REQUIRE(actions.perform("edit.delete"));
    CHECK(f.session->undoDescription() == "Delete");
    CHECK(spans(f.keys) == std::vector<std::pair<double, double>>{{0.0, 2.0}, {3.0, 8.0}});
    f.session->undo();
    f.settle();

    // Ctrl+D: a copy after it, and the selection moves onto it.
    view.selectTime({2.0, 3.0}, {f.keys}, {}, f.keys);
    REQUIRE(actions.perform("edit.duplicate"));
    CHECK(f.session->undoDescription() == "Duplicate");
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().time->start == Catch::Approx(3.0));

    // Copy, then paste at the playhead (at 6 s).
    view.selectTime({0.0, 1.0}, {f.keys}, {}, f.keys);
    REQUIRE(actions.perform("edit.copy"));
    f.session->setPosition(6.0);
    REQUIRE(actions.perform("edit.paste"));
    CHECK(f.session->undoDescription() == "Paste");
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().time->start == Catch::Approx(6.0));

    // Ctrl+L: the loop brace takes it, looping on.
    view.selectTime({4.0, 6.0}, {f.keys}, {}, f.keys);
    REQUIRE(actions.perform("transport.loop"));
    CHECK(f.session->isLooping());
    CHECK(f.session->loopRange().first == Catch::Approx(4.0));
    CHECK(f.session->loopRange().second == Catch::Approx(6.0));

    // Song-wide: Insert time moves every track; Delete time closes it again.
    view.selectTime({2.0, 4.0}, {f.keys}, {}, f.keys);
    REQUIRE(actions.perform("song.insertTime"));
    CHECK(f.session->undoDescription() == "Insert time");
    CHECK(spans(f.drums).back().second == Catch::Approx(10.0));
    view.selectTime({2.0, 4.0}, {f.keys}, {}, f.keys);
    REQUIRE(actions.perform("song.deleteTime"));
    CHECK(spans(f.drums).back().second == Catch::Approx(8.0));
}

TEST_CASE("time selection: a title strip inside it drags the stretch, later and a track down; Ctrl copies it "
          "(time-selection.md §10); the song row's overview selects every row (§5)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    view.setAllTrackHeights(64.0);
    f.settle();
    view.selectTime({2.0, 4.0}, {f.drums}, {}, f.drums);
    f.settle();
    // Grab the Drums clip's title strip at 3 s, drop two seconds later on the Bass row.
    drag(view, at(view, 3.0, 0, 5.0f), at(view, 5.0, 1, 5.0f));
    CHECK(f.session->undoDescription() == "Move");
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.bass});
    CHECK(view.selection().time->start == Catch::Approx(4.0).margin(0.13));

    // The song row's overview: a drag selects time on every track and lane.
    const auto lane = view.songLaneArea().toFloat();
    const float overview = lane.getBottom() - 6.0f;
    const auto x = [&](double s) { return lane.getX() + static_cast<float>(view.timeline().xForTime(s)); };
    // A section's title band shows the hand that moves it; the overview under it, the plain pointer.
    REQUIRE(f.session->addSection(6.0, 10.0, "Verse") != 0);
    f.settle();
    const auto hover = [&](juce::Point<float> p)
    {
        view.mouseMove(mouseEvent(view, p, p, false, {}));
        return view.getMouseCursor();
    };
    CHECK(hover({x(8.0), lane.getY() + 4.0f}) == fd::ui::HandCursor{}.get());
    CHECK(hover({x(8.0), overview}) == juce::MouseCursor::NormalCursor);
    drag(view, {x(1.0), overview}, {x(3.0), overview});
    REQUIRE(view.selection().hasTime());
    CHECK(view.selection().rowTracks == std::set<std::uint64_t>{f.drums, f.bass, f.keys});
    CHECK(view.selection().rowLanes == std::set<std::uint64_t>{f.lane});
}

TEST_CASE("time selection: the export panel's Selection range is chosen when it opens with one, exports "
          "exactly the "
          "stretch, and the selection's tracks as stems; with none, the song",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    auto& panel = f.shell->exportPanel();
    const auto& actions = f.shell->actions();
    const auto range = [&] { return f.session->exportSettings().range; };
    const auto waitForExport = [&]
    {
        for (int i = 0; i < 3000 && panel.progress(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE_FALSE(panel.progress());
    };
    const auto frames = [](const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        REQUIRE(reader != nullptr);
        return reader->lengthInSamples;
    };

    // Bars 2 to 3 on the drums and bass (and the lane): the range is the selection, named for its bars.
    view.selectTime({2.0, 6.0}, {f.drums, f.bass}, {f.lane}, f.drums);
    f.settle();
    REQUIRE(actions.perform("project.export"));
    REQUIRE(panel.isOpen());
    CHECK(range() == fd::model::ExportRange::Selection);
    f.dump("export-selection", panel);
    REQUIRE(panel.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    waitForExport();
    const auto mix = f.session->exportsFolder().getChildFile("Untitled - Bars 2-4.wav");
    REQUIRE(mix.existsAsFile());
    // Exactly the stretch, no tail; the whole mix, whatever the rows.
    CHECK(frames(mix) == std::llround(4.0 * f.session->sampleRate()));

    // Stems: the selection's tracks are the selected tracks.
    auto settings = f.session->exportSettings();
    settings.what = fd::model::ExportWhat::SelectedTracks;
    f.session->setExportSettings(settings);
    panel.close();
    REQUIRE(actions.perform("project.export"));
    REQUIRE(panel.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    waitForExport();
    CHECK(f.session->exportsFolder().getChildFile("Untitled - Drums - Bars 2-4.wav").existsAsFile());
    CHECK(f.session->exportsFolder().getChildFile("Untitled - Bass - Bars 2-4.wav").existsAsFile());
    CHECK_FALSE(f.session->exportsFolder().getChildFile("Untitled - Keys - Bars 2-4.wav").existsAsFile());
    panel.close();

    // With none, the remembered Selection is the song, and the panel can't choose it.
    REQUIRE(actions.perform("edit.selectNone"));
    REQUIRE_FALSE(view.selection().hasTime());
    REQUIRE(actions.perform("project.export"));
    CHECK(range() == fd::model::ExportRange::Song);
    panel.close();
}

TEST_CASE(
    "time selection: Bounce in place bounces the stretch on its tracks, cut at its edges, and selects the "
    "bounce; a track it can't bounce says why",
    "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    const auto& actions = f.shell->actions();

    // Bars 2 to 3 of the drums: the end of the first beat clip and the start of the second.
    view.selectTime({2.0, 6.0}, {f.drums}, {}, f.drums);
    f.settle();
    REQUIRE(actions.isEnabled("clip.bounce"));
    REQUIRE(actions.perform("clip.bounce"));
    for (int i = 0; i < 2000 && f.session->undoDescription() != "Bounce in place"; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(f.session->undoDescription() == "Bounce in place");
    f.settle();
    const auto* drums = f.session->snapshot()->channel(f.drums);
    REQUIRE(drums != nullptr);
    REQUIRE(drums->clips.size() == 3);
    CHECK(drums->clips[0].end() == Catch::Approx(2.0));
    CHECK(drums->clips[1].start == Catch::Approx(2.0));
    CHECK(drums->clips[1].end() == Catch::Approx(6.0));
    CHECK(drums->clips[1].stack != 0); // its Before bounce take holds both parts
    CHECK(drums->clips[2].start == Catch::Approx(6.0));
    CHECK(view.selection().clips == std::set<std::uint64_t>{drums->clips[1].id});

    // The bass has no instrument: refused, and the menu says why.
    view.selectTime({0.0, 2.0}, {f.bass}, {}, f.bass);
    f.settle();
    CHECK_FALSE(actions.isEnabled("clip.bounce"));
    const auto* bounce = actions.find("clip.bounce");
    REQUIRE(bounce != nullptr);
    REQUIRE(bounce->whyNot);
    CHECK(bounce->whyNot().contains("instrument"));
}

namespace
{
/// The Keys clip in the clip view, the clip view used last.
fd::app::PianoRoll& keysInClipView(TimeFixture& f)
{
    f.shell->arrangement().setSelection(
        fd::model::withClips(*f.session->snapshot(), {f.keysClip}, f.keys, f.keys));
    f.settle();
    auto& roll = f.shell->detail().pianoRoll();
    REQUIRE(f.shell->detail().showsPianoRoll());
    REQUIRE(roll.clip());
    REQUIRE(roll.clip()->id == f.keysClip);
    click(roll, roll.pointFor(0.9, 72)); // the clip view used last: an empty spot, above the notes
    return roll;
}

const fd::model::ClipInfo& keysClipIn(TimeFixture& f)
{
    f.settle();
    const auto found = fd::model::findClip(*f.session->snapshot(), f.keysClip);
    REQUIRE(found);
    return *found->first;
}
} // namespace

TEST_CASE(
    "time selection in the piano roll: a box selects its notes and its stretch, a click clears both; Ctrl+D "
    "copies after the stretch, Ctrl+E cuts at its edges, Escape clears it (§14.1)",
    "[integration]")
{
    TimeFixture f;
    auto& roll = keysInClipView(f);
    const auto& actions = f.shell->actions();
    REQUIRE_FALSE(roll.timeSelection());

    // A box from just before beat 4 to just after beat 8, across the notes' pitches (60 to 68).
    drag(roll, roll.pointFor(3.98, 72), roll.pointFor(8.02, 58));
    REQUIRE(roll.timeSelection());
    CHECK(roll.timeSelection()->start == Catch::Approx(2.0)); // beat 4 at 120 BPM
    CHECK(roll.timeSelection()->end == Catch::Approx(4.0));
    const auto boxed = roll.selection().size();
    CHECK(boxed > 0);
    f.dump("time-selection-piano-roll-box", f.shell->detail());

    // Ctrl+D: copies of the notes right after the stretch, which moves along.
    const auto before = roll.notes().size();
    REQUIRE(actions.handleKey(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
    f.settle();
    CHECK(roll.notes().size() == before + boxed);
    REQUIRE(roll.timeSelection());
    CHECK(roll.timeSelection()->start == Catch::Approx(4.0));
    CHECK(roll.timeSelection()->end == Catch::Approx(6.0));
    f.session->undo();
    f.settle();

    // A click on an empty spot clears it.
    click(roll, roll.pointFor(0.9, 72));
    CHECK_FALSE(roll.timeSelection());

    // Ctrl+E on a stretch inside the note at beat 2 (2 .. 2.75): it's cut at both edges.
    roll.setSelection({});
    roll.setTimeSelection(fd::model::TimeSpan{1.125, 1.25}); // beats 2.25 .. 2.5
    CHECK(actions.label("edit.split") == "Split at selection");
    const auto notes = roll.notes().size();
    REQUIRE(actions.perform("edit.split"));
    f.settle();
    CHECK(roll.notes().size() == notes + 2);
    f.session->undo();
    f.settle();

    // Escape: the clip view's selection and stretch cleared; the clip stays in it.
    roll.setSelection({0});
    roll.setTimeSelection(fd::model::TimeSpan{1.0, 2.0});
    REQUIRE(actions.perform("edit.selectNone"));
    CHECK_FALSE(roll.timeSelection());
    CHECK_FALSE(roll.hasSelection());
    CHECK(f.shell->arrangement().selection().clips == std::set<std::uint64_t>{f.keysClip});
}

TEST_CASE("time selection in the piano roll: the Song menu's keys insert, delete and duplicate time inside "
          "the clip, "
          "its end following; Crop and Clip loop take the stretch (§14.1, §17)",
          "[integration]")
{
    TimeFixture f;
    auto& roll = keysInClipView(f);
    const auto& actions = f.shell->actions();
    const auto notesAfter = [&](double beat)
    {
        std::size_t n = 0;
        for (const auto& note : keysClipIn(f).notes)
            n += note.start >= beat - 1e-6 ? 1 : 0;
        return n;
    };
    // Beats 4 .. 6 (2 .. 3 s): Notes ▸ Time ▸ Insert time, on the Song menu's key.
    roll.setTimeSelection(fd::model::TimeSpan{2.0, 3.0});
    CHECK(actions.shortcut("notes.insertTime") == actions.shortcut("song.insertTime"));
    REQUIRE(actions.isEnabled("notes.insertTime"));
    const auto later = notesAfter(4.0);
    REQUIRE(actions.handleKey(juce::KeyPress('i', juce::ModifierKeys::commandModifier, 0)));
    CHECK(f.session->undoDescription() == "Insert time");
    CHECK(keysClipIn(f).length == Catch::Approx(9.0)); // a second longer
    CHECK(notesAfter(6.0) == later);                   // the notes from beat 4 now from beat 6
    CHECK(notesAfter(4.0) == later);                   // nothing in the time put in
    REQUIRE(roll.timeSelection());                     // the time put in stays selected
    // The song itself didn't move: the bass's second clip is where it was.
    CHECK(f.session->snapshot()->channel(f.bass)->clips[1].start == Catch::Approx(4.5));

    // Delete time takes it out again: the clip as it was.
    REQUIRE(actions.handleKey(
        juce::KeyPress(juce::KeyPress::deleteKey,
                       juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK(f.session->undoDescription() == "Delete time");
    CHECK(keysClipIn(f).length == Catch::Approx(8.0));
    CHECK_FALSE(roll.timeSelection());

    // Duplicate time: a copy of the stretch after it, the clip that much longer, the copy selected.
    roll.setTimeSelection(fd::model::TimeSpan{2.0, 3.0});
    REQUIRE(actions.handleKey(
        juce::KeyPress('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK(f.session->undoDescription() == "Duplicate time");
    CHECK(keysClipIn(f).length == Catch::Approx(9.0));
    REQUIRE(roll.timeSelection());
    CHECK(roll.timeSelection()->start == Catch::Approx(3.0));
    f.session->undo();
    f.settle();

    // Crop clip to selection: the clip is the stretch, selected, still in the clip view.
    roll.setTimeSelection(fd::model::TimeSpan{2.0, 3.0});
    REQUIRE(actions.perform("notes.cropClip"));
    f.settle();
    const auto& keys = f.session->snapshot()->channel(f.keys)->clips;
    REQUIRE(keys.size() == 1);
    CHECK(keys[0].start == Catch::Approx(2.0));
    CHECK(keys[0].length == Catch::Approx(1.0));
    REQUIRE(roll.clip());
    CHECK(roll.clip()->id == keys[0].id);
    f.session->undo();
    keysInClipView(f);
    // Clip loop to selection: the clip loops it.
    roll.setTimeSelection(fd::model::TimeSpan{2.0, 3.0});
    REQUIRE(actions.perform("notes.loopClip"));
    const auto& looped = keysClipIn(f);
    CHECK(looped.loop.on());
    CHECK(looped.loop.length == Catch::Approx(1.0));
    // None selected: they say why.
    roll.setTimeSelection(std::nullopt);
    CHECK_FALSE(actions.isEnabled("notes.insertTime"));
    CHECK(actions.find("notes.cropClip")->whyNot().contains("Select a stretch"));
}

TEST_CASE(
    "time selection: inside one clip, the clip view selects the same stretch, once; after that the two are "
    "separate (§14.3)",
    "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    auto& detail = f.shell->detail();
    view.selectTime({2.0, 4.0}, {f.keys}, {}, f.keys);
    f.settle();
    REQUIRE(detail.showsPianoRoll());
    auto& roll = detail.pianoRoll();
    REQUIRE(roll.timeSelection());
    CHECK(roll.timeSelection()->start == Catch::Approx(2.0));
    CHECK(roll.timeSelection()->end == Catch::Approx(4.0));
    // The clip view's own stretch changes nothing in the arrangement, and a snapshot doesn't undo it.
    roll.setTimeSelection(fd::model::TimeSpan{1.0, 1.5});
    f.settle();
    CHECK(view.selection().time == fd::model::TimeSpan{2.0, 4.0});
    CHECK(roll.timeSelection() == fd::model::TimeSpan{1.0, 1.5});
    // A new one in the arrangement: shown again.
    view.selectTime({3.0, 5.0}, {f.keys}, {}, f.keys);
    f.settle();
    CHECK(roll.timeSelection() == fd::model::TimeSpan{3.0, 5.0});

    // An audio clip's: its range.
    view.selectTime({1.0, 2.0}, {f.drums}, {}, f.drums);
    f.settle();
    REQUIRE(detail.showsAudioClip());
    REQUIRE(detail.audioClipView().range());
    CHECK(detail.audioClipView().range()->first == Catch::Approx(1.0));
    CHECK(detail.audioClipView().range()->second == Catch::Approx(2.0));
}

TEST_CASE("time selection in the piano roll: a drag from a controller lane's empty spot selects time and the "
          "lane's "
          "points in it (§14.1)",
          "[integration]")
{
    TimeFixture f;
    namespace cc = fd::model::controllers;
    f.session->setControllers(
        f.keysClip,
        {{.controller = cc::modWheel,
          .points = {{0.0, 0.1f, 0.0f}, {2.0, 0.4f, 0.0f}, {5.0, 0.6f, 0.0f}, {9.0, 0.9f, 0.0f}}}});
    auto& roll = keysInClipView(f);
    roll.setLane({.kind = fd::model::BottomLane::Kind::Controller, .controller = cc::modWheel});
    f.settle();
    // From beat 1 to beat 6, low in the lane (away from the curve).
    drag(roll, roll.lanePointFor(0.98, 0.95f), roll.lanePointFor(6.02, 0.95f));
    REQUIRE(roll.timeSelection());
    CHECK(roll.timeSelection()->start == Catch::Approx(0.5));
    CHECK(roll.timeSelection()->end == Catch::Approx(3.0));
    CHECK(roll.pointSelection() == std::set<size_t>{1, 2});
    f.dump("time-selection-controller-lane", f.shell->detail());
}

TEST_CASE("time selection: Song ▸ Add section makes a section of exactly the selection (§13)",
          "[integration]")
{
    TimeFixture f;
    auto& view = f.shell->arrangement();
    const auto& actions = f.shell->actions();
    view.selectTime({2.0, 6.0}, {f.drums, f.bass}, {}, f.drums);
    f.settle();
    CHECK(actions.label("song.addSection") == "Add section from selection");
    REQUIRE(actions.perform("song.addSection"));
    f.settle();
    const auto& sections = f.session->snapshot()->sections;
    REQUIRE(sections.size() == 1);
    CHECK(sections[0].start == Catch::Approx(2.0));
    CHECK(sections[0].end == Catch::Approx(6.0));
    CHECK(view.selection().sections == std::set<std::uint64_t>{sections[0].id});
    // Overlapping it, another is refused, saying why.
    view.selectTime({4.0, 7.0}, {f.drums}, {}, f.drums);
    f.settle();
    REQUIRE(actions.perform("song.addSection"));
    CHECK(f.session->snapshot()->sections.size() == 1);
    CHECK(f.shell->lastWarning().contains("overlap"));
}
