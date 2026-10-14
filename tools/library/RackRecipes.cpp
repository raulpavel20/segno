#include "RackRecipes.h"

#include "fd/device/Presets.h"
#include "fd/device/Registry.h"
#include "fd/device/State.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>

namespace fd::library_build
{
namespace
{
constexpr int kMacros = 8, kChains = 4, kSnapshots = 8; // racks.md §4, §16, §18.2

/// A factory preset of `device` by name.
std::optional<DeviceState> factoryPreset(std::string_view device, const std::string& name)
{
    for (const auto& file : factoryPresetFiles())
        if (file.name == name)
            if (const auto preset = parsePresetFile(file.text, std::string(file.name));
                preset && preset->state.deviceId == device)
                return preset->state;
    return std::nullopt;
}

/// A value for `spec` as a recipe gives it: a number in its unit, a boolean, or text it reads.
std::optional<float> plainOf(const ParamSpec& spec, const nlohmann::json& value)
{
    std::optional<float> plain;
    if (value.is_number())
        plain = value.get<float>();
    else if (value.is_boolean())
        plain = value.get<bool>() ? spec.max : spec.min;
    else if (value.is_string())
        plain = spec.fromText(value.get<std::string>());
    if (!plain || spec.sanitize(*plain) != *plain)
        return std::nullopt;
    return plain;
}

/// A rack's module, made: its state, as a preset holds it.
struct Made
{
    const DeviceInfo* info = nullptr;
    std::unique_ptr<Device> device;
};

float percent(const nlohmann::json& value)
{
    return std::clamp(value.get<float>() / 100.0f, 0.0f, 1.0f);
}
} // namespace

RackRecipesResult racksFromRecipes(const nlohmann::json& recipes)
{
    RackRecipesResult out;
    if (!recipes.is_object() || recipes.value("segnoRackRecipes", 0) != 1 || !recipes.contains("racks"))
    {
        out.problems.push_back("not rack recipes");
        return out;
    }
    const auto author = recipes.value("author", std::string("Segno"));
    std::set<std::string> names;
    for (const auto& r : recipes["racks"])
    {
        const auto name = r.value("name", std::string());
        const auto folder = r.value("folder", std::string());
        auto problem = [&out, &name](const std::string& why) { out.problems.push_back(name + ": " + why); };
        if (const auto why = presetNameProblem(name); !why.empty())
        {
            problem(why);
            continue;
        }
        if (const auto why = presetNameProblem(folder); !why.empty())
        {
            problem("folder: " + why);
            continue;
        }
        if (!names.insert(name).second)
            problem("two racks of that name");
        const size_t problemsBefore = out.problems.size();

        // The chains: each module made from its preset and what the recipe sets.
        const auto chainsIn = r.value("chains", nlohmann::json::array());
        if (chainsIn.empty() || chainsIn.size() > static_cast<size_t>(kChains))
            problem("a rack has 1 to 4 chains");
        std::vector<std::vector<Made>> made;
        auto chains = nlohmann::json::array();
        bool instrument = false;
        for (size_t c = 0; c < chainsIn.size(); ++c)
        {
            const auto& chainIn = chainsIn[c];
            auto& madeChain = made.emplace_back();
            auto modules = nlohmann::json::array();
            for (const auto& m : chainIn.value("modules", nlohmann::json::array()))
            {
                const auto id = m.value("device", std::string());
                const auto* factory = findDevice(id);
                if (factory == nullptr)
                {
                    problem("no module " + id);
                    continue;
                }
                const auto& info = factory->info();
                auto device = factory->create();
                if (m.contains("preset"))
                {
                    const auto preset = m["preset"].get<std::string>();
                    const auto state = factoryPreset(info.id, preset);
                    if (!state || applyState(*device, *state) != ApplyResult::Ok)
                        problem("the " + std::string(info.name) + " has no factory preset " + preset);
                }
                const auto params = m.value("params", nlohmann::json::object()); // items() of it, kept
                for (const auto& [key, value] : params.items())
                {
                    const auto i = device->params().indexOf(key);
                    if (!i)
                    {
                        problem("the " + std::string(info.name) + " has no parameter " + key);
                        continue;
                    }
                    if (const auto plain = plainOf(info.params[*i], value))
                        device->params().set(*i, *plain);
                    else
                        problem(key + " = " + value.dump() + " isn't a value it takes");
                }
                if (madeChain.empty() && info.kind == DeviceKind::Instrument)
                    instrument = true; // an instrument starts a chain: an instrument rack (§3)
                modules.push_back({{"name", std::string(info.name)},
                                   {"on", m.value("on", true)},
                                   {"device", std::string(info.id)},
                                   {"state", toJson(presetState(info, captureState(*device)))}});
                madeChain.push_back({&info, std::move(device)});
            }
            chains.push_back({{"name", chainIn.value("name", "Chain " + std::to_string(c + 1))},
                              {"on", chainIn.value("on", true)},
                              {"volumeDb", chainIn.value("volumeDb", 0.0f)},
                              {"pan", chainIn.value("pan", 0.0f)},
                              {"modules", std::move(modules)}});
        }

        // The macros: names, values, mappings onto the modules made.
        const auto macrosIn = r.value("macros", nlohmann::json::array());
        if (macrosIn.size() > static_cast<size_t>(kMacros))
            problem("a rack has 8 macros");
        auto macros = nlohmann::json::array();
        std::set<std::pair<std::pair<int, int>, std::string>> mapped;
        for (int i = 0; i < kMacros; ++i)
        {
            const auto macroIn = i < static_cast<int>(macrosIn.size()) ? macrosIn[static_cast<size_t>(i)]
                                                                       : nlohmann::json::object();
            auto mappings = nlohmann::json::array();
            for (const auto& mIn : macroIn.value("mappings", nlohmann::json::array()))
            {
                const int c = mIn.value("chain", 1) - 1, at = mIn.value("module", 1) - 1;
                if (c < 0 || c >= static_cast<int>(made.size()) || at < 0 ||
                    at >= static_cast<int>(made[static_cast<size_t>(c)].size()))
                {
                    problem("a mapping names no module (chain " + std::to_string(c + 1) + ", module " +
                            std::to_string(at + 1) + ")");
                    continue;
                }
                const auto& module = made[static_cast<size_t>(c)][static_cast<size_t>(at)];
                const auto param = mIn.value("param", std::string());
                const auto index = module.device->params().indexOf(param);
                if (!index)
                {
                    problem("the " + std::string(module.info->name) + " has no parameter " + param);
                    continue;
                }
                const auto& spec = module.info->params[*index];
                if (!spec.hasFlag(ParamFlags::Automatable) || spec.hasFlag(ParamFlags::Hidden) ||
                    spec.hasFlag(ParamFlags::ChangesLatency))
                {
                    problem(param + " can't be moved by a macro");
                    continue;
                }
                if (!mapped.insert({{c, at}, param}).second)
                    problem("two macros map " + param);
                const float now = spec.toNormalized(module.device->params().get(*index));
                float from = now, to = now < 0.999f ? 1.0f : 0.0f;
                for (const auto& [key, at01] : {std::pair{"from", &from}, std::pair{"to", &to}})
                    if (mIn.contains(key))
                    {
                        if (const auto plain = plainOf(spec, mIn[key]))
                            *at01 = spec.toNormalized(*plain);
                        else
                            problem(param + " " + key + " " + mIn[key].dump() + " isn't a value it takes");
                    }
                mappings.push_back({{"chain", c},
                                    {"module", at},
                                    {"param", param},
                                    {"from", from},
                                    {"to", to},
                                    {"curve", std::clamp(mIn.value("curve", 0.0f) / 100.0f, -1.0f, 1.0f)}});
            }
            nlohmann::json macro{{"name", macroIn.value("name", "Macro " + std::to_string(i + 1))},
                                 {"value", percent(macroIn.value("value", 0.0f))},
                                 {"mappings", std::move(mappings)}};
            if (macroIn.value("locked", false))
                macro["locked"] = true;
            macros.push_back(std::move(macro));
        }

        nlohmann::json rack{{"segnoRack", 1},
                            {"name", name},
                            {"kind", instrument ? "instrument" : "effect"},
                            {"tags", r.value("tags", std::vector<std::string>{})},
                            {"description", r.value("description", std::string())},
                            {"author", author},
                            {"chains", std::move(chains)},
                            {"macros", std::move(macros)}};

        // Its snapshots and Morph (racks.md §18).
        const auto snapshotsIn = r.value("snapshots", nlohmann::json::array());
        if (snapshotsIn.size() > static_cast<size_t>(kSnapshots))
            problem("a rack has 8 snapshots");
        if (!snapshotsIn.empty())
        {
            auto snapshots = nlohmann::json::array();
            for (const auto& s : snapshotsIn)
            {
                std::array<float, kMacros> positions{};
                const auto given = s.value("macros", nlohmann::json::array());
                for (size_t i = 0; i < positions.size() && i < given.size(); ++i)
                    positions[i] = percent(given[i]);
                snapshots.push_back({{"name", s.value("name", std::string())}, {"macros", positions}});
            }
            const auto morph = r.value("morph", nlohmann::json::object());
            const int count = static_cast<int>(snapshotsIn.size());
            const int a = morph.value("a", count >= 2 ? 1 : 0) - 1,
                      b = morph.value("b", count >= 2 ? 2 : 0) - 1;
            if (a < -1 || a >= count || b < -1 || b >= count)
                problem("Morph's A and B are its snapshots, from 1");
            rack["snapshots"] = std::move(snapshots);
            rack["morph"] = {{"on", morph.value("on", false)},
                             {"a", a},
                             {"b", b},
                             {"position", percent(morph.value("position", 0.0f))}};
        }
        if (out.problems.size() == problemsBefore)
            out.racks.push_back({folder, name, rack.dump(2) + "\n"});
    }
    return out;
}
} // namespace fd::library_build
