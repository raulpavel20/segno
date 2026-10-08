#pragma once

#include "fd/device/Device.h"

namespace fd
{
/// Runs fn(ctx) inside RealtimeSanitizer's real-time context in rtsan builds (FD_RTSAN), so any
/// allocation, lock or blocking call reached from it is reported. A plain call otherwise.
/// Every host adapter and test harness calls Device::process() through this.
void runRealtime(void (*fn)(void*), void* ctx) noexcept;

template <typename F>
void runRealtime(F& f) noexcept
{
    runRealtime([](void* p) { (*static_cast<F*>(p))(); }, &f);
}

inline void processRealtime(Device& device, ProcessContext& context) noexcept
{
    auto call = [&] { device.process(context); };
    runRealtime(call);
}
} // namespace fd
