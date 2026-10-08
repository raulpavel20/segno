#pragma once

#include <algorithm>
#include <cstdint>

namespace fd::dsp
{
/// Splits audio blocks into segments at control-rate "ticks" aligned to an absolute
/// sample counter (counted from the last reset), not to host block boundaries.
///
/// Control-rate work (reading parameters, updating coefficients) done on ticks therefore
/// happens at the same absolute sample whatever block sizes the host uses. That makes
/// block-size invariance exactly testable (docs/architecture.md, "Device contract").
class ControlGrid
{
public:
    explicit ControlGrid(int interval = 32) noexcept : interval_(std::max(1, interval)) {}

    void reset() noexcept { position_ = 0; }

    /// Calls fn(offset, length, isTick) for consecutive segments covering `numSamples`.
    /// `isTick` is true when the segment starts exactly on a tick.
    template <typename Fn>
    void forEachSegment(int numSamples, Fn&& fn) noexcept(noexcept(fn(0, 0, false)))
    {
        int offset = 0;
        while (offset < numSamples)
        {
            const int phase = static_cast<int>(position_ % interval_);
            const int length = std::min(interval_ - phase, numSamples - offset);
            fn(offset, length, phase == 0);
            offset += length;
            position_ += length;
        }
    }

    [[nodiscard]] std::int64_t position() const noexcept { return position_; }
    [[nodiscard]] int interval() const noexcept { return interval_; }

private:
    int interval_;
    std::int64_t position_ = 0;
};
} // namespace fd::dsp
