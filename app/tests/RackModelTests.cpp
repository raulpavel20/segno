// A rack's snapshots, Morph and locks (docs/design/racks.md §18): the rules the panel, the
// engine and the assistants share.
#include "model/Racks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
/// Two snapshots, Verse and Drop, of three macros; the macros where Verse has them.
RackInfo verseAndDrop()
{
    RackInfo rack;
    rack.snapshots = {{.name = "Verse", .macros = {0.2f, 0.5f, 0.0f}},
                      {.name = "Drop", .macros = {0.8f, 0.1f, 1.0f}}};
    for (int i = 0; i < 3; ++i)
        rack.macros[static_cast<size_t>(i)].value = rack.snapshots[0].macros[static_cast<size_t>(i)];
    rack.currentSnapshot = 0;
    rack.morph = {.on = false, .a = 0, .b = 1};
    return rack;
}
} // namespace

TEST_CASE("racks model: a locked or automated macro is held; Morph sets the rest while it's on",
          "[fast][racks]")
{
    auto rack = verseAndDrop();
    CHECK_FALSE(macroHeld(rack, 0));
    rack.macros[1].locked = true;
    rack.macros[2].automated = true;
    CHECK(macroHeld(rack, 1));
    CHECK(macroHeld(rack, 2));
    CHECK(macroHeld(rack, -1));
    CHECK(macroHeld(rack, RackInfo::macroCount));

    CHECK_FALSE(morphing(rack));
    CHECK_FALSE(morphSets(rack, 0));
    rack.morph.on = true;
    CHECK(morphing(rack));
    CHECK(morphSets(rack, 0));
    CHECK_FALSE(morphSets(rack, 1)); // locked
    CHECK_FALSE(morphSets(rack, 2)); // automated

    // On, but without two snapshots to go between, it sets nothing.
    rack.morph.b = 2;
    CHECK_FALSE(morphing(rack));
    rack.morph.b = -1;
    CHECK_FALSE(morphSets(rack, 0));
}

TEST_CASE("racks model: Morph takes a macro straight from A to B", "[fast][racks]")
{
    auto rack = verseAndDrop();
    rack.morph.on = true;
    CHECK(morphPosition(rack, 0, 0.0f) == Approx(0.2f));
    CHECK(morphPosition(rack, 0, 1.0f) == Approx(0.8f));
    CHECK(morphPosition(rack, 0, 0.5f) == Approx(0.5f));
    CHECK(morphPosition(rack, 1, 0.25f) == Approx(0.4f)); // down, from 0.5 to 0.1
    CHECK(morphPosition(rack, 0, 2.0f) == Approx(0.8f));  // never past B
    // A and B swapped: the other way.
    std::swap(rack.morph.a, rack.morph.b);
    CHECK(morphPosition(rack, 0, 0.0f) == Approx(0.8f));
}

TEST_CASE("racks model: the current snapshot is changed once a macro a recall would move has moved",
          "[fast][racks]")
{
    auto rack = verseAndDrop();
    CHECK_FALSE(snapshotChanged(rack));
    rack.macros[0].value = 0.3f;
    CHECK(snapshotChanged(rack));
    // A locked macro, or one following its lane, doesn't count: a recall wouldn't move it.
    rack.macros[0].locked = true;
    CHECK_FALSE(snapshotChanged(rack));
    rack.macros[0].locked = false;
    rack.macros[0].automated = true;
    CHECK_FALSE(snapshotChanged(rack));
    rack.macros[0].automated = false;
    rack.macros[0].value = 0.2f;
    CHECK_FALSE(snapshotChanged(rack));
    // No current snapshot: nothing to be changed from.
    rack.macros[0].value = 0.9f;
    rack.currentSnapshot = -1;
    CHECK_FALSE(snapshotChanged(rack));
    rack.currentSnapshot = 5;
    CHECK_FALSE(snapshotChanged(rack));
}

TEST_CASE("racks model: a new snapshot is named after the first number no snapshot has", "[fast][racks]")
{
    RackInfo rack;
    CHECK(newSnapshotName(rack) == "Snapshot 1");
    rack.snapshots = {{.name = "Snapshot 1"}, {.name = "Drop"}};
    CHECK(newSnapshotName(rack) == "Snapshot 2");
    rack.snapshots.push_back({.name = "Snapshot 2"});
    rack.snapshots.push_back({.name = "Snapshot 4"});
    CHECK(newSnapshotName(rack) == "Snapshot 3");
}
