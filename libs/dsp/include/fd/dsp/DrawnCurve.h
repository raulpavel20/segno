#pragma once

#include "fd/dsp/Wavetable.h" // CurveTable

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fd::dsp
{
/// A drawn curve from 0..1 to 0..1: an LFO's shape (the Wavetable's, an LFO modulator's), a Remap
/// warp's. A named shape, or points (x, y, bend), each segment bent as an envelope's curve
/// (CurvedEnvelope::shape). Its table is what plays it and what draws it.
struct DrawnCurve
{
    std::string shape = "sine"; // when there are no points: one of namedCurveShapes()
    std::vector<std::array<float, 3>> points;
    [[nodiscard]] CurveTable table() const;
    [[nodiscard]] bool operator==(const DrawnCurve&) const = default;
};

/// The named shapes, in a menu's order: sine, triangle, saw-up, saw-down, square, steps.
[[nodiscard]] std::span<const std::string_view> namedCurveShapes() noexcept;

/// A curve's points to start editing from: its own, or a named shape's (the bends near its own
/// roundness).
[[nodiscard]] std::vector<std::array<float, 3>> editablePoints(const DrawnCurve& curve);
} // namespace fd::dsp
