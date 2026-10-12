// A loop brace dragged with the mouse (daw-shell.md "Loop brace", clip-loops.md §6): the transport's
// and a clip's, the same gestures.
#include "model/BraceDrag.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace fd::model;
using Catch::Approx;

TEST_CASE("loop brace: its edges, its body, an empty strip; drawn, moved, resized; a click toggles", "[fast]")
{
    // A brace over pixels [100, 200], edges grabbed within 6 px.
    CHECK(braceZoneAt(103.0, 100.0, 200.0, 6.0) == BraceZone::Start);
    CHECK(braceZoneAt(196.0, 100.0, 200.0, 6.0) == BraceZone::End);
    CHECK(braceZoneAt(150.0, 100.0, 200.0, 6.0) == BraceZone::Body);
    CHECK(braceZoneAt(50.0, 100.0, 200.0, 6.0) == BraceZone::None);
    CHECK(braceZoneAt(101.0, 100.0, 109.0, 6.0) == BraceZone::Start); // a third of a short brace
    CHECK(braceZoneAt(104.5, 100.0, 109.0, 6.0) == BraceZone::Body);
    CHECK(braceZoneAt(150.0, 100.0, 100.0, 6.0) == BraceZone::None); // none drawn

    const auto snap = [](double t) { return std::round(t); }; // a grid of whole seconds
    const BraceRange at{2.0, 4.0};
    CHECK(dragBrace(BraceZone::None, at, 5.2, 7.7, snap, 0.01) == BraceRange{5.0, 8.0});
    CHECK(dragBrace(BraceZone::None, at, 7.7, 5.2, snap, 0.01) == BraceRange{5.0, 8.0}); // either way
    CHECK(dragBrace(BraceZone::Body, at, 3.0, 4.4, snap, 0.01) == BraceRange{3.0, 5.0});
    CHECK(dragBrace(BraceZone::Body, at, 3.0, -5.0, snap, 0.01) == BraceRange{0.0, 2.0}); // not before zero
    CHECK(dragBrace(BraceZone::Start, at, 2.0, 0.6, snap, 0.01) == BraceRange{1.0, 4.0});
    CHECK(dragBrace(BraceZone::Start, at, 2.0, 9.0, snap, 0.01).first == Approx(3.99)); // not past the end
    CHECK(dragBrace(BraceZone::End, at, 4.0, 6.3, snap, 0.01) == BraceRange{2.0, 6.0});

    // Letting go: a new brace is set and switched on; a moved one set; a click on it toggles; a
    // click on an empty part does nothing.
    const auto drawn = releaseBrace(BraceZone::None, true, BraceRange{5.0, 8.0});
    CHECK(drawn.range == BraceRange{5.0, 8.0});
    CHECK(drawn.switchOn);
    const auto moved = releaseBrace(BraceZone::Body, true, BraceRange{3.0, 5.0});
    CHECK(moved.range);
    CHECK_FALSE(moved.switchOn);
    CHECK(releaseBrace(BraceZone::Body, false, std::nullopt).toggle);
    const auto nothing = releaseBrace(BraceZone::None, false, std::nullopt);
    CHECK_FALSE(nothing.range);
    CHECK_FALSE(nothing.toggle);
}
