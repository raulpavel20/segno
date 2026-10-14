#pragma once

#include "model/Snapshot.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

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

// ---- snapshots and Morph (racks.md §18) ------------------------------------------------------

/// Whether recall, Morph and Vary leave a macro alone: it's locked, or it follows its lane
/// (racks.md §18.4).
[[nodiscard]] inline bool macroHeld(const RackInfo& rack, int macro) noexcept
{
    if (macro < 0 || macro >= RackInfo::macroCount)
        return true;
    const auto& m = rack.macros[static_cast<size_t>(macro)];
    return m.locked || m.automated;
}

/// Whether Morph is on between two snapshots there are (racks.md §18.3).
[[nodiscard]] inline bool morphing(const RackInfo& rack) noexcept
{
    const int n = static_cast<int>(rack.snapshots.size());
    return rack.morph.on && rack.morph.a >= 0 && rack.morph.a < n && rack.morph.b >= 0 && rack.morph.b < n;
}

/// Whether Morph sets a macro now: it's morphing, and the macro isn't held.
[[nodiscard]] inline bool morphSets(const RackInfo& rack, int macro) noexcept
{
    return morphing(rack) && !macroHeld(rack, macro);
}

/// Where Morph puts a macro with Morph at `position` (0 ... 1): straight from where snapshot A has
/// it to where B has it.
[[nodiscard]] inline float morphPosition(const RackInfo& rack, int macro, float position) noexcept
{
    const auto i = static_cast<size_t>(std::clamp(macro, 0, RackInfo::macroCount - 1));
    const float a = rack.snapshots[static_cast<size_t>(rack.morph.a)].macros[i];
    const float b = rack.snapshots[static_cast<size_t>(rack.morph.b)].macros[i];
    return a + (b - a) * std::clamp(position, 0.0f, 1.0f);
}

/// Whether the macros have moved from the current snapshot (its changed mark, §18.2): a macro a
/// recall would move isn't where the snapshot has it. Never while there's no current one.
[[nodiscard]] inline bool snapshotChanged(const RackInfo& rack) noexcept
{
    if (rack.currentSnapshot < 0 || rack.currentSnapshot >= static_cast<int>(rack.snapshots.size()))
        return false;
    const auto& s = rack.snapshots[static_cast<size_t>(rack.currentSnapshot)];
    for (int i = 0; i < RackInfo::macroCount; ++i)
        if (!macroHeld(rack, i) &&
            std::abs(rack.macros[static_cast<size_t>(i)].value - s.macros[static_cast<size_t>(i)]) > 0.0005f)
            return true;
    return false;
}

/// The name a new snapshot gets: "Snapshot n", the first n no snapshot is called.
[[nodiscard]] inline std::string newSnapshotName(const RackInfo& rack)
{
    for (int n = 1;; ++n)
    {
        auto name = "Snapshot " + std::to_string(n);
        if (std::none_of(rack.snapshots.begin(), rack.snapshots.end(),
                         [&](const RackInfo::Snapshot& s) { return s.name == name; }))
            return name;
    }
}
} // namespace fd::model
