#pragma once

#include "fd/dsp/Wavetable.h"

#include <memory>
#include <span>
#include <string_view>

namespace fd::devices::wavetable
{
/// A factory table: its permanent ID (stored in presets and projects: never renamed), its name
/// and its category (devices/wavetable/SPEC.md, "Tables and their files").
struct FactoryTableInfo
{
    std::string_view id, name, category;
};

/// Every factory table, by category.
[[nodiscard]] std::span<const FactoryTableInfo> factoryTables();

/// A factory table, built the first time it's asked for (additive and formula recipes, made
/// in-house), then shared. Null for an unknown ID. Not real-time: call it off the audio thread.
[[nodiscard]] std::shared_ptr<const dsp::Wavetable> factoryTable(std::string_view id);

/// The table a new oscillator plays.
inline constexpr std::string_view defaultTableId = "basic/shapes";
} // namespace fd::devices::wavetable
