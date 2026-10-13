#include "fd/device/State.h"

#include "fd/device/Device.h"

namespace fd
{
nlohmann::json toJson(const DeviceState& state)
{
    nlohmann::json params = nlohmann::json::object();
    for (const auto& [id, value] : state.params)
        params[id] = value;
    return {{"device", state.deviceId},
            {"stateVersion", state.stateVersion},
            {"params", params},
            {"extra", state.extra.is_object() ? state.extra : nlohmann::json::object()}};
}

std::optional<DeviceState> stateFromJson(const nlohmann::json& json)
{
    if (!json.is_object() || !json.contains("device") || !json["device"].is_string())
        return std::nullopt;
    DeviceState s;
    s.deviceId = json["device"].get<std::string>();
    s.stateVersion = json.value("stateVersion", 0u);
    if (json.contains("params") && json["params"].is_object())
        for (const auto& [id, value] : json["params"].items())
            if (value.is_number())
                s.params[id] = value.get<float>();
    if (json.contains("extra") && json["extra"].is_object())
        s.extra = json["extra"];
    return s;
}

DeviceState captureState(const Device& device)
{
    DeviceState s;
    s.deviceId = std::string(device.info().id);
    s.stateVersion = device.info().stateVersion;
    const auto& store = device.params();
    for (size_t i = 0; i < store.size(); ++i)
        if (!store.spec(i).hasFlag(ParamFlags::Transient)) // how you listen isn't kept
            s.params[std::string(store.spec(i).id)] = store.get(i);
    device.writeExtraState(s.extra);
    return s;
}

ApplyResult applyState(Device& device, DeviceState state)
{
    if (state.deviceId != device.info().id)
        return ApplyResult::WrongDevice;
    if (state.stateVersion > device.info().stateVersion)
        return ApplyResult::NewerVersion;
    if (state.stateVersion < device.info().stateVersion)
        device.migrateState(state);

    auto& store = device.params();
    for (size_t i = 0; i < store.size(); ++i)
    {
        const auto& spec = store.spec(i);
        const auto it = state.params.find(std::string(spec.id));
        const bool kept = it != state.params.end() && !spec.hasFlag(ParamFlags::Transient);
        store.set(i, kept ? it->second : spec.def);
    }
    device.readExtraState(state.extra);
    return ApplyResult::Ok;
}

std::string serializeState(const Device& device)
{
    return toJson(captureState(device)).dump(2);
}

ApplyResult deserializeState(Device& device, std::string_view json)
{
    const auto parsed = nlohmann::json::parse(json.begin(), json.end(), nullptr, /*allow_exceptions*/ false);
    if (parsed.is_discarded())
        return ApplyResult::Invalid;
    auto state = stateFromJson(parsed);
    if (!state)
        return ApplyResult::Invalid;
    return applyState(device, std::move(*state));
}
} // namespace fd
