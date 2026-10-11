#pragma once

#include <cstdint>

namespace fd::testkit
{
/// Number of operator-new calls made inside Device::process during renders on this thread (and
/// inside CountAllocations scopes), counted when allocation tracking is compiled in (see
/// allocationTrackingEnabled()).
std::int64_t allocationsDuringProcess() noexcept;
bool allocationTrackingEnabled() noexcept;

/// Counts operator-new calls on this thread while it exists, as inside Device::process: a DSP
/// block's real-time code checked like a device's.
class CountAllocations
{
public:
    CountAllocations() noexcept;
    ~CountAllocations();
    CountAllocations(const CountAllocations&) = delete;
    CountAllocations& operator=(const CountAllocations&) = delete;

private:
    bool was_ = false;
};
} // namespace fd::testkit
