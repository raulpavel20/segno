#include "model/ModuleFacts.h"

#include "model/Racks.h"

#include <functional>

namespace fd::model
{
namespace
{
/// Every module of a chain, those in its racks' chains too, in order.
void eachModule(const std::vector<DeviceSlot>& devices, const std::function<void(const DeviceSlot&)>& visit)
{
    for (const auto& d : devices)
    {
        visit(d);
        if (d.rack != nullptr)
            for (const auto& chain : d.rack->chains)
                eachModule(chain.devices, visit);
    }
}
} // namespace

std::optional<ModuleFacts> moduleFacts(const TrackInfo& channel, const std::vector<AutomationLaneInfo>& lanes,
                                       std::uint64_t module)
{
    std::optional<ModuleFacts> facts;
    eachModule(channel.devices,
               [&](const DeviceSlot& d)
               {
                   if (d.id != module || facts)
                       return;
                   facts.emplace();
                   facts->id = d.id;
                   facts->channel = channel.id;
                   facts->name = d.name;
                   facts->kind = d.kind;
                   facts->chains = d.rack != nullptr ? static_cast<int>(d.rack->chains.size()) : 0;
                   if (d.modulator != nullptr)
                       facts->routes = d.modulator->routes;
               });
    if (!facts)
        return std::nullopt;

    // What moves it: each modulator with routes to its parameters, in the chain's order.
    const auto prefix = "m" + std::to_string(module) + "/";
    eachModule(channel.devices,
               [&](const DeviceSlot& d)
               {
                   if (d.modulator == nullptr)
                       return;
                   ModuleFacts::Mover mover{.modulator = d.id, .name = d.name, .params = {}};
                   for (const auto& r : d.modulator->routes)
                       if (r.channel == channel.id && r.key.rfind(prefix, 0) == 0)
                           mover.params.push_back(r.param);
                   if (!mover.params.empty())
                       facts->movedBy.push_back(std::move(mover));
               });
    // What automates it: each lane assigned to one of its parameters.
    for (const auto& lane : lanes)
        for (const auto& a : lane.assignments)
            if (a.channel == channel.id && a.key.rfind(prefix, 0) == 0)
                facts->automated.push_back({lane.id, a.name});
    return facts;
}
} // namespace fd::model
