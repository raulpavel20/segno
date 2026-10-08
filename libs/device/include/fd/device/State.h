#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fd
{
class Device;

/// Saved state of one device instance; also the preset format. Shared by the plugin and the
/// DAW, so presets move between them. Parameters are stored as *plain* values, so range
/// changes never corrupt saved state.
///
///   { "device": "utility", "stateVersion": 1, "params": { "gain": -6.0 }, "extra": {} }
struct DeviceState
{
    std::string deviceId;
    std::uint32_t stateVersion = 0;
    std::map<std::string, float> params;
    nlohmann::json extra = nlohmann::json::object();
};

[[nodiscard]] nlohmann::json toJson(const DeviceState& state);
[[nodiscard]] std::optional<DeviceState> stateFromJson(const nlohmann::json& json);

[[nodiscard]] DeviceState captureState(const Device& device);

enum class ApplyResult
{
    Ok,
    WrongDevice,
    NewerVersion, // saved by a newer build; not applied
    Invalid
};

/// Migrates (if older), then sets known parameters (missing ones go to their default,
/// unknown ones are ignored) and restores extra state. Not real-time.
ApplyResult applyState(Device& device, DeviceState state);

[[nodiscard]] std::string serializeState(const Device& device);
ApplyResult deserializeState(Device& device, std::string_view json);
} // namespace fd
