// The Drum Kit's view model (docs/design/drum-kit.md §5, §8, §14): the 16 pads shown, the pads
// with sounds, and what the kit's panel and frame say.
#include "model/DrumKit.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;

namespace
{
RackInfo kitOf(std::initializer_list<std::pair<int, std::string>> pads)
{
    RackInfo kit;
    kit.kit = true;
    for (const auto& [note, name] : pads)
        kit.chains.push_back({.name = name, .note = note});
    return kit;
}
} // namespace

TEST_CASE("drum kit: the 16 pads shown start a row of 4, all of them pads", "[fast]")
{
    CHECK(kit::shownFrom(36) == 36); // C1, a new kit's
    CHECK(kit::shownFrom(38) == 36); // within its row: the row's start
    CHECK(kit::shownFrom(40) == 40);
    CHECK(kit::shownFrom(0) == 0);
    CHECK(kit::shownFrom(-3) == 0);
    CHECK(kit::shownFrom(112) == 112); // the last 16: 112 … 127
    CHECK(kit::shownFrom(120) == 112);
    CHECK(kit::shownFrom(500) == 112);
}

TEST_CASE("drum kit: the pads with sounds, and what a folded kit says it holds", "[fast]")
{
    const auto kit = kitOf({{36, "Kick 1"}, {38, "Snare"}, {42, "Hat closed"}});
    CHECK(kit::padOn(kit, 38)->name == "Snare");
    CHECK(kit::padOn(kit, 37) == nullptr);
    const auto sounds = kit::padsWithSounds(kit);
    CHECK(sounds.count() == 3);
    CHECK(sounds.test(42));
    CHECK(kit::contents(kit) == "3 pads");
    CHECK(kit::contents(kitOf({{36, "Kick"}})) == "1 pad");
    CHECK(kit::contents(kitOf({})) == "No pads");
}

TEST_CASE("drum kit: a pad's note, its choke group, and the frame's label", "[fast]")
{
    CHECK(kit::noteName(36) == "C1");
    CHECK(kit::noteName(60) == "C3");
    CHECK(kit::noteName(39) == std::string("D\xe2\x99\xaf") + "1"); // D♯1
    CHECK(kit::chokeName(0) == "None");
    CHECK(kit::chokeName(1) == "1");
    CHECK(kit::chokeName(16) == "16");

    auto kit = kitOf({{36, "Kick 1"}, {38, "D1"}});
    kit.selectedPad = 36;
    CHECK(kit::frameLabel(kit) == "Pad C1 \xc2\xb7 Kick 1");
    kit.selectedPad = 38; // named as its note: said once
    CHECK(kit::frameLabel(kit) == "Pad D1");
    kit.selectedPad = 40; // empty
    CHECK(kit::frameLabel(kit) == "Pad E1");
}

TEST_CASE("drum kit: a track's kit is its instrument in its chain", "[fast]")
{
    auto kit = std::make_shared<RackInfo>(kitOf({{36, "Kick"}}));
    auto rack = std::make_shared<RackInfo>();
    std::vector<DeviceSlot> devices(3);
    devices[0].rack = rack; // a rack of effects: not a kit
    CHECK(kit::trackKit(devices) == nullptr);
    devices[1].id = 7;
    devices[1].rack = kit;
    REQUIRE(kit::trackKit(devices) != nullptr);
    CHECK(kit::trackKit(devices)->id == 7);
    CHECK(kit::trackKit({}) == nullptr);
}
