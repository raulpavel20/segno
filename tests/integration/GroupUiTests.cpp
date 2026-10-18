// Groups, step 0 drawn (docs/design/groups.md §17, ADR 0072): the real arrangement, mixer and
// inspector showing a stub of what the engine will hold, in every state: tracks selected to group,
// a group open and folded with a lane among its members, three levels at the header's width, a
// header dragged between members, onto a group's header and to a group's end at both depths, a
// muted group and a soloed member, a time selection on a folded group and a selected member while
// folded, the mixer with one group open and one folded, the inspector for a group, a member and
// several tracks, at standard and compact widths. Drawn for review with FD_DUMP_DIR set; every
// state's headers and strips are checked for nothing touching.

#include "ShellTestKit.h"
#include "fd/testkit/WavIO.h"
#include "model/Groups.h"
#include "model/Selection.h"
#include "model/TrackHeaderLayout.h"
#include "model/TrackLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace fd::testing;
using fd::model::Snapshot;
using fd::model::TrackInfo;

namespace
{
constexpr std::uint64_t kDrums = 900001, kBass = 900002, kRhythm = 900003, kAll = 900004;

/// Kick, Snare, Hats, Bass, Sub and Lead, each a few audio clips of a tone, a lane after the
/// Snare: the session's real tracks, which a stub then puts in groups.
struct GroupFixture : ShellFixture
{
    juce::File root;
    std::uint64_t kick = 0, snare = 0, hats = 0, bass = 0, sub = 0, lead = 0, lane = 0;

    explicit GroupFixture(int width = 1800, int height = 1000)
    {
        shell->setSize(width, height);
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("fd-groups-ui-" + juce::Uuid().toString());
        REQUIRE(root.createDirectory());
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        session->updateSnapshotNow();
        while (session->snapshot()->tracks.size() < 6)
        {
            session->addTrack();
            session->updateSnapshotNow();
        }
        while (session->snapshot()->tracks.size() > 6)
        {
            REQUIRE(session->deleteTrack(session->snapshot()->tracks.back().id));
            session->updateSnapshotNow();
        }
        const auto tracks = session->snapshot()->tracks;
        std::uint64_t* ids[] = {&kick, &snare, &hats, &bass, &sub, &lead};
        const char* names[] = {"Kick", "Snare", "Hats", "Bass", "Sub", "Lead"};
        for (size_t i = 0; i < 6; ++i)
        {
            *ids[i] = tracks[i].id;
            session->renameTrack(tracks[i].id, names[i]);
        }
        // A pattern each: where it plays, and how long each hit is.
        tone(kick, {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0}, 0.25);
        tone(snare, {0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5}, 0.25);
        tone(hats, {0.0}, 8.0);
        tone(bass, {0.0, 4.0}, 3.5);
        tone(sub, {2.0, 6.0}, 1.5);
        tone(lead, {1.0, 5.0}, 2.5);
        lane = session->addAutomationLane(fd::engine::Session::ParamTarget{snare, "volume"});
        REQUIRE(lane != 0);
        REQUIRE(session->setLanePoints(lane, {{0.0, 0.3f, 0.0f}, {4.0, 0.8f, 0.0f}, {8.0, 0.4f, 0.0f}}));
        session->renameAutomationLane(lane, "Snare verb");
        session->updateSnapshotNow();
        shell->arrangement().setSnapshot(session->snapshot());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        shell->finishAnimations();
        shell->arrangement().setView(0.0, (width - 600.0) / 9.0);
    }

    ~GroupFixture()
    {
        shell.reset();
        session.reset();
        root.deleteRecursively();
    }

    void tone(std::uint64_t t, std::vector<double> starts, double seconds)
    {
        const double rate = 48000.0;
        fd::testkit::AudioBuffer b(2, static_cast<int>(seconds * rate));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < b.numSamples(); ++i)
                b.at(c, i) =
                    0.3f * std::exp(-static_cast<float>(i) / 24000.0f) *
                    static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * i / rate));
        const auto file = root.getChildFile("tone-" + juce::String(t) + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), b, rate));
        for (const double s : starts)
            REQUIRE(session->insertAudioFile(t, file, s) != 0);
    }

    [[nodiscard]] TrackInfo track(std::uint64_t id, std::uint64_t group) const
    {
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        auto copy = *t;
        copy.group = group;
        return copy;
    }

    /// A group as the engine will report it: its own colour, a module, sends like a track's.
    [[nodiscard]] TrackInfo group(std::uint64_t id, const char* name, int colour, std::uint64_t parent) const
    {
        TrackInfo g{.id = id, .role = TrackInfo::Role::Group, .name = name, .colour = colour};
        g.group = parent;
        g.devices.push_back({.id = id + 100, .name = "Compressor", .deviceId = "compressor"});
        g.mix.sends = session->snapshot()->tracks.front().mix.sends;
        return g;
    }

    /// The session's snapshot with `tracks` in place of its own.
    [[nodiscard]] std::shared_ptr<const Snapshot> stub(std::vector<TrackInfo> tracks) const
    {
        auto s = std::make_shared<Snapshot>(*session->snapshot());
        s->tracks = std::move(tracks);
        for (auto& l : s->automationLanes) // the Snare's lane, among the drums
            if (l.id == lane)
                l.after = snare;
        return s;
    }

    /// Drums (Kick, Snare and its lane, Hats), Bass (Bass, Sub), Lead in none.
    [[nodiscard]] std::shared_ptr<const Snapshot> twoGroups() const
    {
        return stub({group(kDrums, "Drums", 0, 0), track(kick, kDrums), track(snare, kDrums),
                     track(hats, kDrums), group(kBass, "Bass", 6, 0), track(bass, kBass), track(sub, kBass),
                     track(lead, 0)});
    }

    /// All ▸ Rhythm ▸ Drums and Bass, three levels deep, Lead in none.
    [[nodiscard]] std::shared_ptr<const Snapshot> nested() const
    {
        return stub({group(kAll, "All", 10, 0), group(kRhythm, "Rhythm", 3, kAll),
                     group(kDrums, "Drums", 0, kRhythm), track(kick, kDrums), track(snare, kDrums),
                     track(hats, kDrums), group(kBass, "Bass", 6, kRhythm), track(bass, kBass),
                     track(sub, kBass), track(lead, 0)});
    }

    void show(const std::shared_ptr<const Snapshot>& s)
    {
        shell->arrangement().setSnapshot(s);
        shell->mixer().setSnapshot(s);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        shell->arrangement().frame(1.0 / 60.0, 0.0);
    }

    /// The inspector for `selection` on `s` (the shell's own reads the session's snapshot).
    void inspect(const std::shared_ptr<const Snapshot>& s, const fd::model::Selection& selection)
    {
        shell->arrangement().setSelection(selection);
        shell->inspector().setContext(fd::model::panelContext(selection, *s, fd::model::CentreView::Arrange),
                                      s->tempo, s->returns);
    }

    void dump(const juce::String& name, juce::Component& c, juce::Rectangle<int> area = {})
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            dumpPng(dir, name + "@2x.png",
                    c.createComponentSnapshot(area.isEmpty() ? c.getLocalBounds() : area, true, 2.0f));
    }

    /// The arrangement's headers and lanes, from the ruler down.
    [[nodiscard]] juce::Rectangle<int> rowsArea() const
    {
        auto& view = shell->arrangement();
        return shell->getLocalArea(&view, view.getLocalBounds());
    }
};

/// Every channel row's header slots, at its depth: the bars, the fold triangle, the name, the
/// buttons and the meter inside the header and apart; the name wide enough for a few letters.
void checkHeaders(fd::app::ArrangementView& view, const Snapshot& s)
{
    const auto width = static_cast<double>(fd::app::ArrangementView::headerWidth());
    for (const auto& row : view.rows())
    {
        if (row.isLane() || row.kind == fd::model::ArrangementRow::Kind::Return)
            continue;
        const auto* channel = s.channel(row.channel);
        REQUIRE(channel != nullptr);
        const int buttons = channel->holdsClips() ? 3 : 2;
        const auto slots = fd::model::layoutTrackHeader(width, fd::model::TrackLayout::defaultHeight, buttons,
                                                        true, {}, row.depth, channel->isGroup());
        INFO(channel->name << " at depth " << row.depth);
        std::vector<fd::model::HeaderRect> parts = slots.groupBars;
        parts.push_back(slots.colour);
        if (channel->isGroup())
            parts.push_back(slots.fold);
        parts.push_back(slots.name);
        parts.insert(parts.end(), slots.buttons.begin(), slots.buttons.end());
        parts.push_back(slots.meter);
        CHECK(slots.name.w >= 24.0); // room for a few letters, at depth 4 too
        for (size_t i = 0; i < parts.size(); ++i)
        {
            CHECK(parts[i].x >= 0.0);
            CHECK(parts[i].right() <= width);
            for (size_t j = i + 1; j < parts.size(); ++j)
            {
                const bool apart = parts[i].right() <= parts[j].x || parts[j].right() <= parts[i].x ||
                                   parts[i].bottom() <= parts[j].y || parts[j].bottom() <= parts[i].y;
                CHECK(apart);
            }
        }
    }
}
} // namespace

TEST_CASE(
    "groups drawn: tracks selected to group, a group open with a lane among its members, another folded",
    "[integration]")
{
    GroupFixture f;
    auto& view = f.shell->arrangement();

    // Kick, Snare and Hats selected, Ctrl+G about to group them: their headers lit, TRACKS in the
    // inspector.
    const auto flat = f.session->snapshot();
    f.show(flat);
    fd::model::Selection several{.channel = f.hats, .rowTracks = {f.kick, f.snare, f.hats}};
    f.inspect(flat, several);
    f.show(flat);
    f.dump("groups-selected-tracks", *f.shell);

    // Drums open, its lane among its members; Bass folded; Lead in none.
    const auto groups = f.twoGroups();
    view.setFoldedGroups({kBass});
    f.show(groups);
    CHECK(view.rows().size() == 7); // Drums, Kick, Snare, its lane, Hats, Bass, Lead
    CHECK(view.rows()[0].isGroup());
    CHECK(view.rows()[3].isLane());
    CHECK(view.rows()[3].depth == 1);
    checkHeaders(view, *groups);
    f.inspect(groups, {.channel = kDrums});
    f.show(groups);
    f.dump("groups-open-and-folded", *f.shell);
    f.dump("groups-open-and-folded-rows", *f.shell, f.rowsArea().withHeight(560));
}

TEST_CASE("groups drawn: three levels at the header's width; headers dragged between members, onto a group's "
          "header and to a group's end, at both depths",
          "[integration]")
{
    GroupFixture f;
    auto& view = f.shell->arrangement();
    const auto nested = f.nested();
    f.show(nested);
    checkHeaders(view, *nested);
    f.dump("groups-nested", *f.shell, f.rowsArea().withHeight(760));
    // Deeper than the indent goes: the bars come closer, nothing touches.
    {
        auto deep = std::make_shared<Snapshot>(*nested);
        auto tom = f.track(f.lead, 900005);
        tom.name = "Tom";
        tom.id = 900006;
        deep->tracks.insert(deep->tracks.begin() + 6,
                            f.group(900005, "Toms", 8, kDrums)); // after Hats, in Drums
        deep->tracks.insert(deep->tracks.begin() + 7, tom);
        f.show(deep);
        checkHeaders(view, *deep);
        f.dump("groups-depth-four", *f.shell, f.rowsArea().withHeight(820));
    }

    const auto groups = f.twoGroups();
    f.show(groups);
    // Rows: 0 Drums, 1 Kick, 2 Snare, 3 lane, 4 Hats, 5 Bass, 6 Bass track, 7 Sub, 8 Lead.
    const auto drop = [&](const juce::String& name, fd::model::RowDrop d)
    {
        view.previewRowDrop(8, d); // Lead dragged
        f.dump(name, *f.shell, f.rowsArea().withHeight(720));
    };
    drop("groups-drop-between-members",
         {.boundary = 2, .moves = true, .before = f.snare, .depth = 1, .group = kDrums});
    drop("groups-drop-onto-header",
         {.boundary = 8, .moves = true, .before = f.lead, .depth = 1, .group = kBass, .onHeader = true});
    drop("groups-drop-end-inside",
         {.boundary = 5, .moves = true, .before = kBass, .depth = 1, .group = kDrums});
    drop("groups-drop-end-outside", {.boundary = 5, .moves = true, .before = kBass, .depth = 0});
    view.previewRowDrop(8, std::nullopt);
}

TEST_CASE("groups drawn: a muted group, a soloed member and its group's solo half lit; a time selection on a "
          "folded group; a selected member while folded",
          "[integration]")
{
    GroupFixture f;
    auto& view = f.shell->arrangement();
    auto tracks = f.twoGroups()->tracks;
    for (auto& t : tracks)
    {
        if (t.id == kDrums)
            t.mix.mute = true; // its members' mute half lit
        if (t.id == f.sub)
            t.mix.solo = true; // Bass's solo half lit
    }
    const auto s = f.stub(tracks);
    f.show(s);
    CHECK(fd::model::holdsSolo(*s, *s->channel(kBass)));
    CHECK(fd::model::inMutedGroup(*s, *s->channel(f.kick)));
    f.dump("groups-mute-and-solo", *f.shell, f.rowsArea().withHeight(720));

    // Bars 2 to 4 on the folded Bass group: the stretch over its row, on every row in it.
    view.setFoldedGroups({kBass});
    const auto plain = f.twoGroups();
    f.show(plain);
    const auto time = fd::model::withTime(*plain, {2.0, 6.0}, {kBass}, {});
    CHECK(time.rowTracks == std::set<std::uint64_t>{kBass, f.bass, f.sub});
    view.setSelection(time);
    f.show(plain);
    f.dump("groups-time-on-folded", *f.shell, f.rowsArea().withHeight(560));
    // The Sub selected while Bass is folded: its line in the selection's colour.
    view.setSelection({.channel = f.sub});
    f.show(plain);
    f.dump("groups-selected-member-folded", *f.shell, f.rowsArea().withHeight(560));
}

TEST_CASE(
    "groups drawn: the mixer with one group open and one folded; the inspector for a group, a member and "
    "several tracks",
    "[integration]")
{
    GroupFixture f;
    const auto groups = f.twoGroups();
    REQUIRE(f.shell->actions().perform("view.mixer"));
    f.shell->mixer().setFoldedGroups({kBass});
    f.show(groups);
    f.dump("groups-mixer", *f.shell);
    f.shell->mixer().setFoldedGroups({});
    f.show(groups);
    f.dump("groups-mixer-open", *f.shell);
    REQUIRE(f.shell->actions().perform("view.mixer"));

    f.show(groups);
    auto& inspector = f.shell->inspector();
    f.inspect(groups, {.channel = kDrums});
    f.dump("groups-inspector-group", inspector);
    f.inspect(groups, {.channel = f.snare});
    f.dump("groups-inspector-member", inspector);
    f.inspect(groups, {.channel = f.hats, .rowTracks = {f.kick, f.hats, f.lead}});
    f.dump("groups-inspector-several", inspector);
}

TEST_CASE("groups drawn: at a compact width", "[integration]")
{
    GroupFixture f(1100, 760);
    f.shell->arrangement().setFoldedGroups({kBass});
    const auto groups = f.twoGroups();
    f.show(groups);
    f.inspect(groups, {.channel = kDrums});
    f.show(groups);
    f.dump("groups-compact", *f.shell);
    REQUIRE(f.shell->actions().perform("view.mixer"));
    f.show(groups);
    f.dump("groups-mixer-compact", *f.shell);
}
