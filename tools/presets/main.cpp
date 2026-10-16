// fd-presets: makes a module's factory presets from its recipes (docs/design/presets.md §13).
// JUCE-free (ADR 0006).
//
//   fd-presets describe <device>   its parameters: ids, ranges, defaults, units, choices
//   fd-presets make <recipes.json> one .segnopreset per recipe, beside the recipes
//   fd-presets defaults <device>   its state with every parameter at its default, as a preset holds
//                                  it (JSON): what the factory kits' pads start from
//                                  (tools/library/make_kits.py)
//
// The recipes (devices/<name>/presets/recipes.json) are the source; the conformance suite checks
// that the preset files are what they make (fd::presetsFromRecipes).

#include "fd/device/Presets.h"
#include "fd/device/Registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace fd;
namespace fs = std::filesystem;

namespace
{
const char* kindName(ParamKind kind)
{
    switch (kind)
    {
    case ParamKind::Continuous:
        return "continuous";
    case ParamKind::Toggle:
        return "toggle";
    case ParamKind::Choice:
        return "choice";
    case ParamKind::Discrete:
        return "discrete";
    }
    return "?";
}

int describe(const std::string& id)
{
    const auto* factory = findDevice(id);
    if (factory == nullptr)
    {
        std::cerr << "no device " << id << "\n";
        return 1;
    }
    const auto& info = factory->info();
    std::cout << info.name << " (" << info.id << ", state version " << info.stateVersion << ")\n";
    for (const auto& spec : info.params)
    {
        std::cout << "  " << spec.id << " [" << kindName(spec.kind) << "] " << spec.min << " .. " << spec.max
                  << ", default " << spec.toText(spec.def);
        if (!spec.choices.empty())
        {
            std::cout << ", choices:";
            for (size_t i = 0; i < spec.choices.size(); ++i)
                std::cout << " " << i << "=" << spec.choices[i];
        }
        if (spec.hasFlag(ParamFlags::Hidden))
            std::cout << " (hidden)";
        std::cout << "\n      " << spec.description << "\n";
    }
    return 0;
}

int defaults(const std::string& id)
{
    const auto* factory = findDevice(id);
    if (factory == nullptr)
    {
        std::cerr << "no device " << id << "\n";
        return 1;
    }
    std::cout << toJson(defaultState(factory->info())).dump(1) << "\n";
    return 0;
}

int make(const fs::path& recipesFile)
{
    std::ifstream in(recipesFile);
    std::stringstream text;
    text << in.rdbuf();
    const auto recipes = nlohmann::json::parse(text.str(), nullptr, false);
    const auto* factory = recipes.is_object() && recipes.contains("device")
                              ? findDevice(recipes["device"].get<std::string>())
                              : nullptr;
    if (factory == nullptr)
    {
        std::cerr << recipesFile << ": not recipes for a device of ours\n";
        return 1;
    }
    const auto made = presetsFromRecipes(*factory, recipes);
    for (const auto& problem : made.problems)
        std::cerr << problem << "\n";
    if (!made.problems.empty())
        return 1;
    const auto folder = recipesFile.parent_path();
    // Presets no recipe makes any more go.
    for (const auto& entry : fs::directory_iterator(folder))
        if (entry.path().extension() == presetExtension)
            fs::remove(entry.path());
    for (const auto& p : made.presets)
    {
        std::ofstream file(folder / (p.name + std::string(presetExtension)),
                           std::ios::binary | std::ios::trunc);
        file << presetFileText(p);
    }
    std::cout << made.presets.size() << " presets for the " << factory->info().name << " in " << folder
              << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "describe")
        return describe(args[1]);
    if (args.size() == 2 && args[0] == "make")
        return make(args[1]);
    if (args.size() == 2 && args[0] == "defaults")
        return defaults(args[1]);
    std::cerr << "usage: fd-presets describe <device>\n       fd-presets make "
                 "<devices/<name>/presets/recipes.json>\n       fd-presets defaults <device>\n";
    return 2;
}
