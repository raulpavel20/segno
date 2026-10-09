#pragma once

namespace fd::model
{
// The volume taper (docs/design/mixer.md §3): every volume fader and bar, and every send, maps
// its travel the same way. At position p (0 at the bottom or left, 1 at the top or right) the
// gain is (p / p0)^n, with n set so a volume's top is exactly +6 dB.

/// Volume: 0 dB at 80 % of the travel, -6 dB at 64 %, +6 dB at the top, -inf at the bottom.
[[nodiscard]] double volumeDbAt(double position) noexcept;
[[nodiscard]] double volumePosition(double db) noexcept;
/// Sends: 0 dB at the top, -6 dB at 80 %.
[[nodiscard]] double sendDbAt(double position) noexcept;
[[nodiscard]] double sendPosition(double db) noexcept;
} // namespace fd::model
