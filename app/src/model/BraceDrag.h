#pragma once

#include <functional>
#include <optional>
#include <utility>

namespace fd::model
{
// A loop brace dragged with the mouse (daw-shell.md "Loop brace", clip-loops.md §6): the
// transport's in the arrangement, and a clip's in the clip views, the same gestures. Times are the
// view's, in seconds. Pure.

enum class BraceZone { None, Body, Start, End };
using BraceRange = std::pair<double, double>;

/// Where a press at `x` (pixels) falls on a brace drawn over [x0, x1]: its edges, within
/// `edgePixels` (a third of the brace at most), its body, or none (an empty part of the strip).
[[nodiscard]] BraceZone braceZoneAt(double x, double x0, double x1, double edgePixels) noexcept;

/// The brace while dragging from `then` to `now`: drawn new over them (grabbed on an empty part),
/// moved (the body: its length kept, never before time zero), or an edge moved (never past the
/// other, less `shortest`). `snap` maps a time to the grid.
[[nodiscard]] BraceRange dragBrace(BraceZone grabbed, BraceRange atPress, double then, double now,
                                   const std::function<double(double)>& snap, double shortest);

/// What letting go does: the brace set to `range` (and switched on, for one drawn new), switched
/// on or off (a click on it), or nothing (a click on an empty part).
struct BraceRelease
{
    std::optional<BraceRange> range;
    bool switchOn = false, toggle = false;
};
[[nodiscard]] BraceRelease releaseBrace(BraceZone grabbed, bool moved, std::optional<BraceRange> dragged) noexcept;
} // namespace fd::model
