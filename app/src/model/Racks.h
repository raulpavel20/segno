#pragma once

#include <algorithm>

namespace fd::model
{
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
