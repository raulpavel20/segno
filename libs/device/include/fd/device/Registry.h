#pragma once

#include "fd/device/Device.h"

#include <memory>
#include <span>
#include <string_view>

namespace fd
{
struct DeviceFactory
{
    const DeviceInfo& (*info)();
    std::unique_ptr<Device> (*create)();
};

/// Every registered device. Generated at build time by fd_generate_device_registry()
/// (link fd::registry to use it).
std::span<const DeviceFactory* const> allDevices();

[[nodiscard]] inline const DeviceFactory* findDevice(std::string_view id)
{
    for (const auto* f : allDevices())
        if (f->info().id == id)
            return f;
    return nullptr;
}
} // namespace fd
