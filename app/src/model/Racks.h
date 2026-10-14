#pragma once

#include "model/Snapshot.h"

#include <algorithm>
#include <functional>

namespace fd::model
{
/// Every module of a chain in order, a rack's modules after it, chain by chain (racks.md §11).
inline void forEachDevice(const std::vector<DeviceSlot>& chain,
                          const std::function<void(const DeviceSlot&)>& visit)
{
    for (const auto& d : chain)
    {
        visit(d);
        if (d.rack != nullptr)
            for (const auto& c : d.rack->chains)
                forEachDevice(c.devices, visit);
    }
}

/// A module in a chain, in a rack of it too; null if it isn't there.
[[nodiscard]] inline const DeviceSlot* findDevice(const std::vector<DeviceSlot>& chain, std::uint64_t id)
{
    for (const auto& d : chain)
    {
        if (d.id == id)
            return &d;
        if (d.rack != nullptr)
            for (const auto& c : d.rack->chains)
                if (const auto* found = findDevice(c.devices, id))
                    return found;
    }
    return nullptr;
}

/// The rack holding a module in a chain (null: none, or it's not there).
[[nodiscard]] inline const DeviceSlot* rackHolding(const std::vector<DeviceSlot>& chain, std::uint64_t id)
{
    for (const auto& d : chain)
        if (d.rack != nullptr)
            for (const auto& c : d.rack->chains)
                if (findDevice(c.devices, id) != nullptr)
                    return &d;
    return nullptr;
}

/// The macro that sets a module's parameter in its rack, and how (null: none).
[[nodiscard]] inline const MacroMappingInfo* mappingOf(const RackInfo& rack, std::uint64_t module,
                                                       const std::string& param, int* macro = nullptr)
{
    for (size_t i = 0; i < rack.macros.size(); ++i)
        for (const auto& m : rack.macros[i].mappings)
            if (m.module == module && m.param == param)
            {
                if (macro != nullptr)
                    *macro = static_cast<int>(i);
                return &m;
            }
    return nullptr;
}

/// Where a macro's mapping takes its parameter (normalised) with the macro at `x` (0 ... 1)
/// (docs/design/racks.md §4): from `from` to `to`, straight, or bent by `curve` (-1 ... +1: up,
/// it starts slowly and comes in late; down, it comes in early). The engine's own curve
/// (tracktion's macro assignment, a quadratic Bézier whose curve is half ours), so the macro
/// editor draws what's heard (racks.md §12).
[[nodiscard]] inline float macroMapped(float x, float from, float to, float curve) noexcept
{
    x = std::clamp(x, 0.0f, 1.0f);
    const float k = std::clamp(curve, -1.0f, 1.0f);
    return from + (to - from) * (x * (1.0f - x) * (1.0f - k) + x * x);
}
} // namespace fd::model
