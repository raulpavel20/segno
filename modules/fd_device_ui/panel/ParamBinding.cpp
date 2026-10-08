#include <nlohmann/json.hpp>

namespace fd::ui
{
void mergeExtraState(Device& device, const nlohmann::json& patch)
{
    nlohmann::json extra = nlohmann::json::object();
    device.writeExtraState(extra);
    extra.merge_patch(patch);
    device.readExtraState(extra);
}
} // namespace fd::ui
