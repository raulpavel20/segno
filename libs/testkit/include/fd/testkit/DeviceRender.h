#pragma once

#include "fd/device/Device.h"
#include "fd/testkit/AudioBuffer.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace fd::testkit
{
/// An event at an absolute sample position (converted to block-relative offsets on render).
struct TimedEvent
{
    std::int64_t samplePosition = 0;
    Event event{};
};

struct RenderOptions
{
    double sampleRate = 48000.0;
    /// Block sizes, cycled. Empty = {512}.
    std::vector<int> blockPattern{};
    /// Process with input and output pointing at the same buffers (as most hosts do).
    bool inPlace = false;
    /// Extra silent samples appended after the input (tails).
    int tailSamples = 0;
    /// Called before each block with the absolute start sample (e.g. to change parameters).
    std::function<void(Device&, std::int64_t blockStart)> beforeBlock{};
    /// MIDI/note events, sorted by samplePosition.
    std::vector<TimedEvent> events{};
    /// Skip prepare() (the caller already prepared/reset the device).
    bool skipPrepare = false;
};

/// Offline render through Device::process (via fd::processRealtime). Prepares the device first
/// (which resets it) unless skipPrepare is set. Output has the device's output channel count.
AudioBuffer render(Device& device, const AudioBuffer& input, const RenderOptions& options = {});

/// Number of operator-new calls made inside Device::process during renders on this thread,
/// counted when allocation tracking is compiled in (see allocationTrackingEnabled()).
std::int64_t allocationsDuringProcess() noexcept;
bool allocationTrackingEnabled() noexcept;
} // namespace fd::testkit
