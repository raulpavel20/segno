// The Granular in the session (devices/granular/SPEC.md, G9 and G22): a file, a multisample or a
// clip's part loaded into a source, the sources swapped, what a source holds, its roll kept.

#include "Granular.h"
#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"

#include <fd/device/Library.h>

namespace fd::engine
{
namespace
{
using G = devices::Granular;

const G* granularIn(DeviceHandle* handle)
{
    return handle != nullptr && handle->deviceId() == "granular"
               ? dynamic_cast<const G*>(handle->context().device)
               : nullptr;
}

void applySettings(ui::ParamBinding& params, const std::vector<std::pair<size_t, float>>& settings)
{
    for (const auto& [param, value] : settings)
    {
        params.beginGesture(param);
        params.setPlain(param, value);
        params.endGesture(param);
    }
}
} // namespace

std::optional<Session::GranularSource> Session::granularSource(std::uint64_t deviceId, int source)
{
    auto handle = openDevice(deviceId);
    const auto* g = granularIn(handle.get());
    if (g == nullptr)
        return std::nullopt;
    const int n = std::clamp(source, 0, G::numSources - 1);
    GranularSource out;
    out.error = g->loadError(n);
    out.partStart = handle->context().params.getPlain(G::source(n, G::SrcStart)) * 0.01;
    out.partEnd = handle->context().params.getPlain(G::source(n, G::SrcEnd)) * 0.01;
    nlohmann::json extra = nlohmann::json::object();
    g->writeExtraState(extra);
    const std::string key = "src" + std::to_string(n + 1) + ".";
    out.path = extra.value(key + "path", std::string());
    const auto set = g->sourceSet(n);
    const auto audio = set != nullptr ? set->shown() : nullptr;
    if (set == nullptr || audio == nullptr)
        return out;
    out.kind = set->multisample ? "multisample" : "file";
    out.name = set->name;
    out.seconds = audio->seconds();
    out.channels = audio->numChannels;
    out.sampleRate = audio->sampleRate;
    out.truncated = audio->truncated;
    out.zones = static_cast<int>(set->zones.size());
    if (const auto it = extra.find(key + "root"); it != extra.end() && it->is_number())
        out.root = it->get<double>();
    else
    {
        const auto* zone = set->zoneFor(60, 100);
        out.root = zone != nullptr ? std::optional<double>(zone->root) : audio->root;
        out.rootFound = out.root.has_value();
    }
    out.transients = audio->transients;
    out.beats = audio->beats;
    return out;
}

std::optional<Session::GranularSource>
Session::loadGranularSource(std::uint64_t deviceId, int source, const juce::File& file,
                            std::optional<std::pair<double, double>> part, std::string* error)
{
    auto handle = openDevice(deviceId);
    if (granularIn(handle.get()) == nullptr)
    {
        if (error != nullptr)
            *error = "isn't a Granular";
        return std::nullopt;
    }
    // Read before it's put in, so a file that can't play changes nothing; held until the module
    // has it, so it's read once (the sources' cache).
    const int n = std::clamp(source, 0, G::numSources - 1);
    const auto stored = library::reference(std::filesystem::path(file.getFullPathName().toStdString()));
    std::string why;
    const auto set = devices::GrainSourceSet::load(stored, &why);
    if (set == nullptr || set->zones.empty())
    {
        if (error != nullptr)
            *error = why.empty() ? "it has nothing Granular can play" : why;
        return std::nullopt;
    }
    {
        const EditGroup group(*this, "Load " + file.getFileNameWithoutExtension().toStdString() +
                                         " into Source " + std::to_string(n + 1));
        auto& params = handle->context().params;
        params.applyExtraState(G::sourcePatch(n, stored));
        auto settings = G::sourceSettings(n);
        if (part)
            for (auto& [param, value] : settings)
            {
                if (param == G::source(n, G::SrcStart))
                    value = static_cast<float>(std::clamp(part->first, 0.0, 1.0) * 100.0);
                else if (param == G::source(n, G::SrcEnd))
                    value = static_cast<float>(std::clamp(part->second, 0.0, 1.0) * 100.0);
            }
        applySettings(params, settings);
    }
    return granularSource(deviceId, n);
}

bool Session::swapGranularSources(std::uint64_t deviceId)
{
    auto handle = openDevice(deviceId);
    const auto* g = granularIn(handle.get());
    if (g == nullptr)
        return false;
    const EditGroup group(*this, "Swap sources");
    const auto settings = g->swappedSettings(); // read before either changes
    handle->context().params.applyExtraState(g->swappedPatch());
    applySettings(handle->context().params, settings);
    return true;
}

bool Session::keepModuleRoll(std::uint64_t deviceId, bool keep)
{
    auto handle = openDevice(deviceId);
    const auto* g = granularIn(handle.get());
    if (g == nullptr || g->rollKept() == keep)
        return false;
    const EditGroup group(*this, keep ? "Keep as played" : "Let it roll");
    handle->context().params.applyExtraState(g->rollPatch(keep));
    impl_->triggerAsyncUpdate(); // the snapshot says whether it keeps its roll
    return true;
}
} // namespace fd::engine
