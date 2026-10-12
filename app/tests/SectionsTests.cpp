// The song's sections (M3 step C3, ADR 0049, docs/design/arranger.md): names, colours, where a
// section lands, and each command's plan on clips, automation and sections.

#include "model/Sections.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
/// A B C D, two seconds each from 0 (a bar at 120 BPM).
std::vector<SectionInfo> abcd()
{
    return {{1, "A", 0, 0.0, 2.0}, {2, "B", 1, 2.0, 4.0}, {3, "C", 2, 4.0, 6.0}, {4, "D", 3, 6.0, 8.0}};
}

/// One clip in each section, on one track (already split at the edges).
SongContent content()
{
    SongContent c;
    c.sections = abcd();
    for (std::uint64_t i = 0; i < 4; ++i)
        c.clips.push_back({.id = 10 + i,
                           .track = 1,
                           .start = 2.0 * static_cast<double>(i),
                           .end = 2.0 * static_cast<double>(i) + 2.0});
    return c;
}

double startOf(const PlanResult& r, std::uint64_t clip, double otherwise)
{
    for (const auto& m : r.moves)
        if (m.clip == clip)
            return m.start;
    return otherwise;
}

std::string order(const std::vector<SectionInfo>& sections)
{
    std::string out;
    for (const auto& s : sections)
        out += (out.empty() ? "" : " ") + s.name;
    return out;
}
} // namespace

TEST_CASE("sections: fitting, finding, names and colours", "[fast]")
{
    const auto s = abcd();
    CHECK_FALSE(sectionFits(s, 1.0, 3.0));
    CHECK(sectionFits(s, 8.0, 10.0));
    CHECK(sectionFits(s, 2.0, 4.0, 2));    // B itself
    CHECK_FALSE(sectionFits(s, 9.0, 9.0)); // nothing long
    CHECK(sectionAt(s, 2.0)->name == "B");
    CHECK(sectionAt(s, 3.999)->name == "B");
    CHECK(sectionAt(s, 8.5) == nullptr);

    CHECK(newSectionName({}) == "A");
    CHECK(newSectionName(s) == "E");
    CHECK(copyName(s, "B") == "B 2");
    auto withCopies = s;
    withCopies.push_back({5, "B 2", 1, 8.0, 10.0});
    CHECK(copyName(withCopies, "B") == "B 3");
    CHECK(copyName(withCopies, "B 2") == "B 3");
    CHECK(newSectionColour({}, 12) == 0);
    CHECK(newSectionColour(s, 12) == 4);
    CHECK(newSectionColour(s, 4) == 0);
}

TEST_CASE("sections: where one lands, and finding your way", "[fast]")
{
    auto s = abcd();
    s.push_back({5, "E", 4, 10.0, 12.0}); // after a gap
    CHECK(sectionBoundaries(s) == std::vector<double>{0.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0});
    CHECK(*nearestBoundary(s, 4.9) == Approx(4.0));
    CHECK(*nearestBoundary(s, 9.2) == Approx(10.0));
    CHECK(*previousSectionStart(s, 5.0) == Approx(4.0));
    CHECK(*previousSectionStart(s, 4.0) == Approx(2.0)); // at a start: the one before
    CHECK_FALSE(previousSectionStart(s, 0.0));
    CHECK(*nextSectionStart(s, 4.0) == Approx(6.0));
    CHECK(*nextSectionStart(s, 8.5) == Approx(10.0));
    CHECK_FALSE(nextSectionStart(s, 10.0));
    const auto span = spanOf(s, {2, 3});
    CHECK(span->first == Approx(2.0));
    CHECK(span->second == Approx(6.0));
}

TEST_CASE("sections: moving one takes its music, the rest making room", "[fast]")
{
    SECTION("B after C: A C B D")
    {
        const auto plan = planMove(2.0, 4.0, 6.0);
        CHECK(cutPoints(plan) == std::vector<double>{2.0, 4.0, 6.0});
        const auto r = applyPlan(content(), plan);
        CHECK(order(r.sections) == "A C B D");
        CHECK(startOf(r, 11, 2.0) == Approx(4.0)); // B's clip
        CHECK(startOf(r, 12, 4.0) == Approx(2.0)); // C's clip
        CHECK(startOf(r, 10, 0.0) == Approx(0.0)); // A's stays
        CHECK(startOf(r, 13, 6.0) == Approx(6.0)); // D's stays: the song keeps its length
        CHECK(r.copies.empty());
        CHECK(r.deletes.empty());
        CHECK(r.sections[2].id == 2); // B keeps its id
    }
    SECTION("D before A: D A B C")
    {
        const auto r = applyPlan(content(), planMove(6.0, 8.0, 0.0));
        CHECK(order(r.sections) == "D A B C");
        CHECK(startOf(r, 13, 6.0) == Approx(0.0));
        CHECK(startOf(r, 10, 0.0) == Approx(2.0));
    }
    SECTION("onto its own edges: no move")
    {
        CHECK(planMove(2.0, 4.0, 2.0).empty());
        CHECK(planMove(2.0, 4.0, 4.0).empty());
    }
}

TEST_CASE("sections: copying, deleting and inserting", "[fast]")
{
    SECTION("copy B after C: A B C B 2 D, the song a section longer")
    {
        const auto r = applyPlan(content(), planCopy(2.0, 4.0, 6.0));
        CHECK(order(r.sections) == "A B C B 2 D");
        REQUIRE(r.copies.size() == 1);
        CHECK(r.copies[0].clip == 11);
        CHECK(r.copies[0].start == Approx(6.0));
        CHECK(startOf(r, 13, 6.0) == Approx(8.0)); // D moves later
        CHECK(r.sections[3].id == 0);
        CHECK(r.sections[3].colour == 1); // a copy keeps its colour
    }
    SECTION("copy B before A: the original stays B, the copy is B 2")
    {
        const auto r = applyPlan(content(), planCopy(2.0, 4.0, 0.0));
        CHECK(order(r.sections) == "B 2 A B C D");
        CHECK(r.sections[0].id == 0);
        CHECK(r.sections[2].id == 2);
    }
    SECTION("delete B: A C D, the gap closed")
    {
        const auto r = applyPlan(content(), planDelete(2.0, 4.0));
        CHECK(order(r.sections) == "A C D");
        CHECK(r.deletes == std::vector<std::uint64_t>{11});
        CHECK(startOf(r, 12, 4.0) == Approx(2.0));
        CHECK(startOf(r, 13, 6.0) == Approx(4.0));
    }
    SECTION("insert after B: A B E C D")
    {
        SectionInfo fresh{.name = "E", .colour = 4};
        const auto r = applyPlan(content(), planInsert(4.0, 2.0), fresh);
        CHECK(order(r.sections) == "A B E C D");
        CHECK(r.sections[2].start == Approx(4.0));
        CHECK(r.sections[2].end == Approx(6.0));
        CHECK(startOf(r, 12, 4.0) == Approx(6.0));
        CHECK(startOf(r, 10, 0.0) == Approx(0.0));
    }
}

TEST_CASE("sections: automation keeps its values at the edges", "[fast]")
{
    // A ramp from 0 at 0 s to 1 at 8 s: B (2-4 s) runs from 0.25 to 0.5.
    auto c = content();
    c.lanes.push_back({.id = 7, .points = {{0.0, 0.0f, 0.0f}, {8.0, 1.0f, 0.0f}}});
    SECTION("moved, B's stretch keeps its values, and so does everything outside it")
    {
        const auto r = applyPlan(c, planMove(2.0, 4.0, 6.0));
        REQUIRE(r.lanes.size() == 1);
        const auto& p = r.lanes[0].points;
        // A, unchanged up to its end.
        CHECK(automationValueAt(p, 1.0) == Approx(0.125f));
        // C where B was: 0.5 to 0.75.
        CHECK(automationValueAt(p, 2.0) == Approx(0.5f));
        CHECK(automationValueAt(p, 3.0) == Approx(0.625f));
        // B where C was: 0.25 to 0.5.
        CHECK(automationValueAt(p, 4.0) == Approx(0.25f));
        CHECK(automationValueAt(p, 5.0) == Approx(0.375f));
        // D, unchanged.
        CHECK(automationValueAt(p, 7.0) == Approx(0.875f));
    }
    SECTION("an empty section holds the value where it was put in")
    {
        const auto r = applyPlan(c, planInsert(4.0, 2.0));
        REQUIRE(r.lanes.size() == 1);
        const auto& p = r.lanes[0].points;
        CHECK(automationValueAt(p, 4.5) == Approx(0.5f));
        CHECK(automationValueAt(p, 5.5) == Approx(0.5f));
        CHECK(automationValueAt(p, 7.0) == Approx(0.625f)); // C, two seconds later
    }
    SECTION("a step lane steps on")
    {
        c.lanes[0].points = {{0.0, 0.2f, kStepCurve}, {3.0, 0.8f, kStepCurve}};
        const auto r = applyPlan(c, planMove(2.0, 4.0, 8.0)); // B to the end
        REQUIRE(r.lanes.size() == 1);
        const auto& p = r.lanes[0].points;
        CHECK(automationValueAt(p, 2.5) == Approx(0.8f)); // C: after the step
        CHECK(automationValueAt(p, 6.5) == Approx(0.2f)); // B's first second, before its step
        CHECK(automationValueAt(p, 7.5) == Approx(0.8f)); // and after it
    }
    SECTION("a lane that ends before the first cut doesn't change")
    {
        c.lanes[0].points = {{0.0, 0.1f, 0.0f}, {1.0, 0.9f, 0.0f}};
        CHECK(applyPlan(c, planMove(2.0, 4.0, 6.0)).lanes.empty());
    }
}

TEST_CASE("sections: a dragged run lands between sections, empty time included", "[fast]")
{
    const auto s = abcd(); // A 0-2, B 2-4, C 4-6, D 6-8
    // B dragged to start at 4: after C (it cuts at its new end, 6).
    CHECK(landRun(s, {2.0, 4.0}, 4.0, false) == Landing{6.0, 4.0});
    // To start at 4.6: its end would cut D (6.6), so it lands where D starts... at C's end.
    CHECK(landRun(s, {2.0, 4.0}, 4.6, false) == Landing{6.0, 4.0});
    // Past the last section, into empty time: anywhere.
    CHECK(landRun(s, {2.0, 4.0}, 9.0, false) == Landing{11.0, 9.0});
    // Earlier: D to start at 0, before A.
    CHECK(landRun(s, {6.0, 8.0}, 0.0, false) == Landing{0.0, 0.0});
    // Where it is, or a move inside itself: none.
    CHECK_FALSE(landRun(s, {2.0, 4.0}, 2.0, false));
    // A lone section moves into empty time.
    const std::vector<SectionInfo> one{{1, "A", 0, 0.0, 2.0}};
    CHECK(landRun(one, {0.0, 2.0}, 8.0, false) == Landing{10.0, 8.0});
    // A copy lands at its start, never inside itself.
    CHECK(landRun(s, {2.0, 4.0}, 8.0, true) == Landing{8.0, 8.0});
    CHECK(landRun(s, {2.0, 4.0}, 4.0, true) == Landing{4.0, 4.0}); // right after itself
    CHECK_FALSE(landRun(s, {2.0, 4.0}, 3.0, true));
}

TEST_CASE("sections: a copied piece pastes with its automation, keeping its name after a cut", "[fast]")
{
    auto c = content();
    c.lanes.push_back({.id = 7, .points = {{0.0, 0.0f, 0.0f}, {8.0, 1.0f, 0.0f}}});
    const auto piece = pieceOf(c, 2.0, 4.0); // B: 0.25 to 0.5
    CHECK(piece.length == Approx(2.0));
    REQUIRE(piece.sections.size() == 1);
    CHECK(piece.sections[0].name == "B");
    CHECK(piece.sections[0].start == Approx(0.0));
    CHECK(piece.sections[0].end == Approx(2.0));
    REQUIRE(piece.lanes.size() == 1);
    CHECK(automationValueAt(piece.lanes[0].points, 0.0) == Approx(0.25f));
    CHECK(automationValueAt(piece.lanes[0].points, 2.0) == Approx(0.5f));

    SECTION("pasted after C: A B C B 2 D, the song a section longer, B's curve there")
    {
        const auto r = applyPlan(c, planPaste(6.0, piece.length), std::nullopt, &piece);
        CHECK(order(r.sections) == "A B C B 2 D");
        CHECK(r.sections[3].id == 0);
        CHECK(r.sections[3].colour == 1);
        CHECK(startOf(r, 13, 6.0) == Approx(8.0)); // D's clip later
        CHECK(r.copies.empty());                   // its clips are the engine's
        REQUIRE(r.lanes.size() == 1);
        const auto& p = r.lanes[0].points;
        CHECK(automationValueAt(p, 5.0) == Approx(0.625f));  // C, as it was
        CHECK(automationValueAt(p, 6.5) == Approx(0.3125f)); // B's curve
        CHECK(automationValueAt(p, 9.0) == Approx(0.875f));  // D, two seconds later
    }
    SECTION("pasted after B was cut: it's B again")
    {
        auto cut = c;
        cut.sections.erase(cut.sections.begin() + 1);
        const auto r = applyPlan(cut, planPaste(0.0, piece.length), std::nullopt, &piece);
        CHECK(order(r.sections) == "B A C D");
    }
    SECTION("a lane the piece has no points for holds its value there")
    {
        c.lanes.push_back({.id = 8, .points = {{0.0, 0.2f, 0.0f}, {8.0, 0.6f, 0.0f}}});
        const auto r = applyPlan(c, planPaste(4.0, piece.length), std::nullopt, &piece);
        for (const auto& lane : r.lanes)
            if (lane.id == 8)
                CHECK(automationValueAt(lane.points, 5.0) == Approx(0.4f));
    }
    // Pasted at the playhead: there, or inside a section at its nearer edge.
    CHECK(pasteLanding(c.sections, 0.5) == Approx(0.0));
    CHECK(pasteLanding(c.sections, 1.5) == Approx(2.0));
    CHECK(pasteLanding(c.sections, 4.0) == Approx(4.0));
    CHECK(pasteLanding(c.sections, 9.3) == Approx(9.3));
    CHECK(pasteLanding(c.sections, -1.0) == Approx(0.0));
}
