#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
/// What the inspector's MODULE section says about a module in a chain (selection.md §8.2), from
/// the snapshot: what moves it, what automates it, and a modulator's own routes. Its type and the
/// delay it adds come from the engine (Session::moduleType, moduleLatencyMs).
struct ModuleFacts
{
    std::uint64_t id = 0, channel = 0;
    std::string name;
    DeviceSlot::Kind kind = DeviceSlot::Kind::Native;
    int chains = 0; // a rack's; 0 for anything else

    /// A modulator moving it: which, and what it moves of it ("Freq", "Mix").
    struct Mover
    {
        std::uint64_t modulator = 0;
        std::string name;
        std::vector<std::string> params;
    };
    std::vector<Mover> movedBy;
    /// A parameter of it with an automation lane: the lane, and the parameter's name.
    struct Automated
    {
        std::uint64_t lane = 0;
        std::string param;
    };
    std::vector<Automated> automated;
    /// A modulator's routes (ROUTES), as its route list has them.
    std::vector<ModulatorRouteInfo> routes;

    [[nodiscard]] bool isModulator() const noexcept { return kind == DeviceSlot::Kind::Modulator; }
};

/// The facts about module `module` in `channel`'s chain (in a rack's chains too), or none if it
/// isn't there. `lanes` are the project's automation lanes.
[[nodiscard]] std::optional<ModuleFacts>
moduleFacts(const TrackInfo& channel, const std::vector<AutomationLaneInfo>& lanes, std::uint64_t module);
} // namespace fd::model
