#pragma once

#include <array>
#include <atomic>

namespace fd
{
/// Lock-free channel from the audio thread to displays (meters, gain reduction, playheads).
/// The audio thread publishes; the UI takes. All operations are wait-free.
class AnalysisBus
{
public:
    static constexpr int numSlots = 16;

    /// Audio thread: raise slot to at least `value` (peak-hold until taken).
    void publishPeak(int slot, float value) noexcept
    {
        auto& a = slots_[static_cast<size_t>(slot)];
        float current = a.load(std::memory_order_relaxed);
        while (value > current && !a.compare_exchange_weak(current, value, std::memory_order_relaxed))
        {
        }
    }

    /// Audio thread: overwrite slot.
    void publish(int slot, float value) noexcept { slots_[static_cast<size_t>(slot)].store(value, std::memory_order_relaxed); }

    /// UI: read and reset a peak slot.
    float takePeak(int slot) noexcept { return slots_[static_cast<size_t>(slot)].exchange(0.0f, std::memory_order_relaxed); }

    /// UI: read without resetting.
    [[nodiscard]] float read(int slot) const noexcept { return slots_[static_cast<size_t>(slot)].load(std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>, numSlots> slots_{};
};
} // namespace fd
