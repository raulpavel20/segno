#pragma once

#include "fd/device/SampleFifo.h"

#include <array>
#include <atomic>

namespace fd
{
/// Lock-free channel from the audio thread to displays: value slots (meters, gain reduction)
/// and sample FIFOs (spectra, scopes). The audio thread publishes; the UI takes. Wait-free.
class AnalysisBus
{
public:
    static constexpr int numSlots = 16;

    /// Audio thread: raise slot to at least `value` (peak-hold until taken). Both readers see
    /// it: the panel (takePeak) and a probe (takeProbePeak).
    void publishPeak(int slot, float value) noexcept
    {
        raise(slots_[static_cast<size_t>(slot)], value);
        raise(probes_[static_cast<size_t>(slot)], value);
    }

    /// Audio thread: overwrite slot.
    void publish(int slot, float value) noexcept { slots_[static_cast<size_t>(slot)].store(value, std::memory_order_relaxed); }

    /// UI: read and reset a peak slot.
    float takePeak(int slot) noexcept { return slots_[static_cast<size_t>(slot)].exchange(0.0f, std::memory_order_relaxed); }

    /// A second reader of peak slots, for measurements (the MCP measure_module tool), so they
    /// never take the peaks a panel's meters show.
    float takeProbePeak(int slot) noexcept
    {
        return probes_[static_cast<size_t>(slot)].exchange(0.0f, std::memory_order_relaxed);
    }

    /// UI: read without resetting.
    [[nodiscard]] float read(int slot) const noexcept { return slots_[static_cast<size_t>(slot)].load(std::memory_order_relaxed); }

    /// Sample FIFOs (spectra, scopes): the device prepares them in Device::prepare(); the audio
    /// thread pushes, the UI pops.
    static constexpr int numFifos = 4;
    [[nodiscard]] SampleFifo& fifo(int index) noexcept { return fifos_[static_cast<size_t>(index)]; }

    /// Display commands, the one way back: the UI asks (e.g. "reset the loudness meter") and
    /// the audio thread acts at its next block. Only for what displays show, never for state:
    /// state changes go through the host binding. Wait-free; requests are never lost.
    static constexpr int numRequests = 4;
    /// UI: asks for `index`.
    void request(int index) noexcept { requests_[static_cast<size_t>(index)].fetch_add(1, std::memory_order_relaxed); }
    /// Audio thread: whether `index` was asked for since last taken.
    [[nodiscard]] bool takeRequest(int index) noexcept
    {
        auto& r = requests_[static_cast<size_t>(index)];
        return r.load(std::memory_order_relaxed) > 0 && r.exchange(0, std::memory_order_relaxed) > 0;
    }

private:
    static void raise(std::atomic<float>& a, float value) noexcept
    {
        float current = a.load(std::memory_order_relaxed);
        while (value > current && !a.compare_exchange_weak(current, value, std::memory_order_relaxed))
        {
        }
    }

    std::array<std::atomic<float>, numSlots> slots_{}, probes_{};
    std::array<SampleFifo, numFifos> fifos_{};
    std::array<std::atomic<int>, numRequests> requests_{};
};
} // namespace fd
