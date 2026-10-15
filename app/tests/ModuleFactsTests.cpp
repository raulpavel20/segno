// The inspector's MODULE section's facts (selection.md §8.2): what moves a module, what automates
// it, a modulator's routes; a module inside a rack found too.
#include "model/Modulators.h"
#include "model/ModuleFacts.h"
#include "model/Racks.h"

#include <catch2/catch_test_macros.hpp>

using namespace fd::model;

TEST_CASE("module facts: moved by, automated, a modulator's routes, in a rack too", "[fast]")
{
    TrackInfo track;
    track.id = 7;
    DeviceSlot filter{.id = 10, .kind = DeviceSlot::Kind::Native, .name = "Filter", .deviceId = "filter"};
    auto lfoInfo = std::make_shared<ModulatorInfo>();
    lfoInfo->routes = {{.channel = 7, .key = "m10/freq", .module = "Filter", .param = "Freq", .amount = 0.5f},
                       {.channel = 7, .key = "m10/res", .module = "Filter", .param = "Res", .amount = 0.2f},
                       {.channel = 7, .key = "m30/mix", .module = "Chorus", .param = "Mix", .amount = 0.5f}};
    DeviceSlot lfo{.id = 20, .kind = DeviceSlot::Kind::Modulator, .name = "LFO"};
    lfo.modulator = lfoInfo;
    // A Chorus inside a rack, moved by a Random beside it.
    auto rack = std::make_shared<RackInfo>();
    DeviceSlot chorus{.id = 30, .kind = DeviceSlot::Kind::Native, .name = "Chorus", .deviceId = "chorus"};
    auto randomInfo = std::make_shared<ModulatorInfo>();
    randomInfo->routes = {{.channel = 7, .key = "m30/depth", .module = "Chorus", .param = "Depth"}};
    DeviceSlot random{.id = 40, .kind = DeviceSlot::Kind::Modulator, .name = "Random"};
    random.modulator = randomInfo;
    rack->chains = {RackChain{.name = "Chain 1", .devices = {random, chorus}}, RackChain{.name = "Chain 2"}};
    DeviceSlot rackSlot{.id = 50, .kind = DeviceSlot::Kind::Native, .name = "Rack"};
    rackSlot.rack = rack;
    track.devices = {lfo, filter, rackSlot};

    AutomationLaneInfo lane;
    lane.id = 99;
    lane.assignments = {{.channel = 7, .key = "m10/freq", .name = "Freq"},
                        {.channel = 8, .key = "m10/res", .name = "Res"}};

    const auto f = moduleFacts(track, {lane}, 10);
    REQUIRE(f.has_value());
    CHECK(f->name == "Filter");
    REQUIRE(f->movedBy.size() == 1);
    CHECK(f->movedBy[0].modulator == 20);
    CHECK(f->movedBy[0].params == std::vector<std::string>{"Freq", "Res"});
    REQUIRE(f->automated.size() == 1); // the other assignment is another channel's
    CHECK(f->automated[0].lane == 99);
    CHECK(f->automated[0].param == "Freq");
    CHECK(f->routes.empty());

    const auto m = moduleFacts(track, {}, 20);
    REQUIRE(m.has_value());
    CHECK(m->isModulator());
    CHECK(m->routes.size() == 3);

    const auto inRack = moduleFacts(track, {}, 30);
    REQUIRE(inRack.has_value());
    REQUIRE(inRack->movedBy.size() == 2); // the LFO outside, and the Random beside it
    CHECK(inRack->movedBy[0].name == "LFO");
    CHECK(inRack->movedBy[1].params == std::vector<std::string>{"Depth"});

    const auto r = moduleFacts(track, {}, 50);
    REQUIRE(r.has_value());
    CHECK(r->chains == 2);
    CHECK_FALSE(moduleFacts(track, {}, 1234).has_value());
}
