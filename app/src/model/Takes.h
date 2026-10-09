#pragma once

#include "model/Snapshot.h"

#include <optional>
#include <utility>
#include <vector>

namespace fd::model
{
// Take lanes (M2 step C5b, ADR 0031, docs/design/recording.md §6): which parts of each take the
// main lane plays, and choosing among them.

/// The time ranges (seconds) of a take clip that the main lane plays: where a main-lane clip was
/// made from it.
[[nodiscard]] std::vector<std::pair<double, double>> inUse(const ClipInfo& take, const std::vector<ClipInfo>& main);

/// Whether the main lane plays any part of a take lane.
[[nodiscard]] bool isUsed(const TakeInfo& take, const std::vector<ClipInfo>& main);

/// The take lane whose clip the main lane plays at `seconds`, if any.
[[nodiscard]] std::optional<size_t> takeAt(const std::vector<TakeInfo>& takes, const std::vector<ClipInfo>& main,
                                           double seconds);

/// The next take lane (`direction` +1 or -1, wrapping) after `current` that has a clip covering
/// [from, to]; nullopt if none does.
[[nodiscard]] std::optional<size_t> stepTake(const std::vector<TakeInfo>& takes, std::optional<size_t> current,
                                             double from, double to, int direction);

/// The number for a new take: one more than the highest "Take N".
[[nodiscard]] int nextTakeNumber(const std::vector<TakeInfo>& takes);
} // namespace fd::model
