// The piano roll's drum view (docs/design/drum-kit.md §23, ADR 0066): a Drum Kit's clip opens on its
// pads, a row for each pad with a sound, named; a note on a pad without one keeps a hatched row; Pads
// in In key's place; the chord lane, Draw's chords, Others and the tools drums don't need gone.
// Drawn for review with FD_DUMP_DIR set.

#include "ShellTestKit.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"
#include "model/Selection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::testing;

namespace
{
/// A shell with a Drum Kit on its first track, seven pads loaded, and a four-bar clip open in the
/// clip view.
struct DrumView : ShellFixture
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-drum-view-" + juce::Uuid().toString());
    std::uint64_t track = 0, kit = 0, clipId = 0;

    explicit DrumView(int width = 1800, int height = 1000, bool loaded = true)
    {
        shell->setSize(width, height);
        (void)root.createDirectory();
        track = session->snapshot()->tracks.front().id;
        kit = session->addDevice(track, "fd:drum-kit");
        REQUIRE(kit != 0);
        if (loaded)
            for (const auto& [note, name] :
                 {std::pair{36, "Kick"}, std::pair{37, "Rim"}, std::pair{38, "Snare"}, std::pair{39, "Clap"},
                  std::pair{42, "Closed hat"}, std::pair{46, "Open hat"}, std::pair{49, "Crash"}})
                REQUIRE(session->loadPads(kit, note, {sample(name)}).size() == 1);
        clipId = session->createMidiClip(track, 0.0, 8.0); // four bars at 120 BPM
        REQUIRE(clipId != 0);
        session->updateSnapshotNow();
        shell->arrangement().setSelection(fd::model::withClips(*session->snapshot(), {clipId}, track, track));
        shell->finishAnimations();
        refresh();
        REQUIRE(shell->detail().showsPianoRoll());
    }

    ~DrumView() { root.deleteRecursively(); }

    juce::File sample(const std::string& name)
    {
        const auto file = root.getChildFile(name + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(),
                                      fd::testkit::sine(80.0, 48000.0, 4800, 0.5f, 1), 48000.0));
        return file;
    }

    void refresh()
    {
        session->updateSnapshotNow();
        for (const auto& t : session->snapshot()->tracks)
            for (const auto& c : t.clips)
                if (c.id == clipId)
                    shell->detail().setClip(c, session->snapshot()->tempo);
    }

    /// A bar of a beat, four times: kick, snare, hats, the crash at the top, a note on E1 (no pad).
    void beat()
    {
        std::vector<fd::model::Note> notes;
        const auto hit = [&](int pitch, double start, float velocity = 0.8f)
        { notes.push_back({pitch, start, 0.25, velocity}); };
        for (const double bar : {0.0, 4.0, 8.0, 12.0})
        {
            for (const double k : {0.0, 1.5, 2.5})
                hit(36, bar + k, 1.0f);
            for (const double s : {1.0, 3.0})
                hit(38, bar + s, 0.9f);
            for (int h = 0; h < 8; ++h)
                hit(h == 7 ? 46 : 42, bar + h * 0.5, h % 2 == 0 ? 0.75f : 0.55f);
        }
        hit(49, 0.0, 1.0f);
        hit(40, 6.75, 0.6f); // E1: a pad without a sound
        std::sort(notes.begin(), notes.end(), fd::model::noteOrder);
        session->setNotes(clipId, notes);
        refresh();
    }

    fd::app::PianoRoll& roll() { return shell->detail().pianoRoll(); }

    void dump(const char* name)
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            auto& detail = shell->detail();
            detail.frame(0.0);
            dumpPng(dir, juce::String(name) + "@2x.png",
                    detail.createComponentSnapshot(detail.getLocalBounds(), true, 2.0f));
        }
    }
};
} // namespace

TEST_CASE("drum view: a kit's clip opens on its pads, named, a note without a pad hatched; Pads in In "
          "key's place; the chord lane, Draw and Others gone (drum-kit.md §23)",
          "[integration]")
{
    DrumView f;
    REQUIRE(f.session->setKey(fd::music::Key{9, "minor"})); // a key: still the pads, never In key
    f.beat();
    auto& roll = f.roll();
    auto& detail = f.shell->detail();
    CHECK(roll.kit() == f.kit);
    CHECK(roll.onPads());
    CHECK_FALSE(roll.inKey());
    CHECK(roll.shownRows() == std::vector<int>{36, 37, 38, 39, 40, 42, 46, 49});
    CHECK(roll.padOn(42)->name == "Closed hat");
    CHECK(roll.padOn(40) == nullptr);
    // The names' column as wide as they need, past the piano's.
    CHECK(roll.keysWidth() > fd::ui::Theme::current().shell.keyboardWidth);
    CHECK(roll.keysWidth() <= fd::ui::Theme::current().shell.padNamesWidth);
    // The rows fill the notes' height, the top one whole under the ruler.
    {
        const auto grid = roll.gridArea();
        const float half = static_cast<float>(roll.pointFor(0.0, 36).y - roll.pointFor(0.0, 37).y) * 0.5f;
        INFO("grid " << grid.toString() << ", crash " << roll.pointFor(0.0, 49).y << ", kick "
                     << roll.pointFor(0.0, 36).y << ", half row " << half);
        CHECK(roll.pointFor(0.0, 49).y - half == Catch::Approx(grid.getY()).margin(1.0));
        CHECK(roll.pointFor(0.0, 36).y + half <= static_cast<float>(grid.getBottom()) + 1.0f);
    }
    // Three hits at once aren't a chord.
    CHECK(roll.chordLaneArea().isEmpty());
    CHECK(detail.inKeyOption().isVisible());
    CHECK(detail.inKeyOption().getButtonText() == "Pads");
    CHECK(detail.inKeyOption().getToggleState());
    CHECK_FALSE(detail.othersOption().isVisible());
    CHECK_FALSE(detail.drawChoice().isVisible());
    CHECK(crowded(detail.header(), fd::ui::Theme::current().shell.itemGap).isEmpty());
    f.dump("drum-view");

    // ↑ moves a hit a row: the kick to the rim.
    roll.setSelection({0});
    const auto kick = roll.notes()[0];
    REQUIRE(kick.pitch == 36);
    roll.transposeSteps(1);
    f.refresh();
    CHECK(roll.notes()[*roll.selection().begin()].pitch == 37);

    // A pad that's off: its name faded.
    REQUIRE(f.session->setPad(f.kit, 46, {.on = false}));
    f.refresh();
    CHECK_FALSE(roll.padOn(46)->on);
    f.dump("drum-view-pad-off");

    // Pads off: the whole roll, In key's options back; remembered for the clip.
    detail.inKeyOption().onClick();
    CHECK_FALSE(roll.onPads());
    CHECK(roll.shownRows().empty());
    CHECK(detail.othersOption().isVisible());
    CHECK(roll.keysWidth() == fd::ui::Theme::current().shell.keyboardWidth);
    f.dump("drum-view-whole-roll");
    detail.inKeyOption().onClick();
    CHECK(roll.onPads());
}

TEST_CASE("drum view: an empty kit says what to do; the Tools pane shows the drum tools, Rhythm on a pad",
          "[integration]")
{
    {
        DrumView f(1800, 1000, false);
        CHECK(f.roll().onPads());
        CHECK(f.roll().shownRows().empty());
        f.dump("drum-view-empty-kit");
    }
    DrumView f;
    f.beat();
    auto& detail = f.shell->detail();
    auto& pane = detail.toolsPane();
    detail.transformButton().onClick();
    REQUIRE(pane.isVisible());
    pane.refresh();
    for (const auto* id : {"quantise", "humanize", "chop", "ornament", "velocity", "span"})
        CHECK(pane.tile(id)->isVisible());
    for (const auto* id : {"strum", "arpeggiate", "randomise", "makeChords"})
        CHECK_FALSE(pane.tile(id)->isVisible());
    CHECK(crowded(pane, fd::ui::Theme::current().shell.itemGap).isEmpty());
    pane.tile("ornament")->onClick();
    f.dump("drum-view-transform");

    detail.generateButton().onClick();
    pane.refresh();
    CHECK(pane.tile("rhythm")->isVisible());
    for (const auto* id : {"melody", "chords", "bassline"})
        CHECK_FALSE(pane.tile(id)->isVisible());
    // Rhythm on a pad, chosen by name: the selected notes' (the snare's).
    std::set<std::size_t> snares;
    for (std::size_t i = 0; i < f.roll().notes().size(); ++i)
        if (f.roll().notes()[i].pitch == 38)
            snares.insert(i);
    f.roll().setSelection(snares);
    pane.tile("rhythm")->onClick();
    auto* pad = dynamic_cast<fd::ui::ChoiceField*>(pane.form().control("pitch"));
    REQUIRE(pad != nullptr);
    CHECK(pad->value() == "Snare");
    CHECK(pane.form().control("on") == nullptr); // never the chords on the pads
    CHECK(crowded(pane.form(), fd::ui::Theme::current().shell.itemGap).isEmpty());
    f.dump("drum-view-rhythm");
}

TEST_CASE("drum view: the header at standard and compact widths", "[integration]")
{
    for (const int width : {1800, 1280, 1024, 820, 700})
    {
        DrumView f(width, 900);
        f.beat();
        auto& detail = f.shell->detail();
        auto& header = detail.header();
        detail.frame(0.0);
        INFO("width " << width);
        CHECK(crowded(header, fd::ui::Theme::current().shell.itemGap).isEmpty());
        CHECK_FALSE(header.isDropped(detail.inKeyOption()));
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            dumpPng(dir, "drum-view-header-" + juce::String(width) + "@2x.png",
                    detail.createComponentSnapshot(header.getBounds(), true, 2.0f));
            if (width == 1024)
                f.dump("drum-view-1024");
        }
    }
}
