// fd-presets: makes a module's factory presets from its recipes (docs/design/presets.md §13).
// JUCE-free (ADR 0006).
//
//   fd-presets describe <device>   its parameters: ids, ranges, defaults, units, choices
//   fd-presets check <recipes.json> [name] how each instrument preset plays across the keyboard
//                                  (one preset's every note and chord, with its name)
//   fd-presets level <recipes.json> <param>
//                                  moves each instrument preset's output level (`param`, in dB)
//                                  to the set's level: about -24 dB at C3, its loudest take at
//                                  -3 dBFS at most; rewrites the recipes
//   fd-presets make <recipes.json> one .segnopreset per recipe, beside the recipes; an
//                                  instrument's only when each passes the check (ADR 0069)
//   fd-presets defaults <device>   its state with every parameter at its default, as a preset holds
//                                  it (JSON): what the factory kits' pads start from
//                                  (tools/library/make_kits.py)
//
// The recipes (devices/<name>/presets/recipes.json) are the source; the conformance suite checks
// that the preset files are what they make (fd::presetsFromRecipes).

#include "fd/device/PresetCheck.h"
#include "fd/device/Presets.h"
#include "fd/device/Registry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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
        if (spec.kind == ParamKind::Continuous && spec.skewCentre > spec.min && spec.skewCentre < spec.max)
            std::cout << ", centre " << spec.skewCentre; // its knob's middle, for macros set about it
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

struct Made
{
    const DeviceFactory* factory = nullptr;
    RecipeResult result;
    /// How far apart its notes may be across the keyboard: 12 dB, unless the recipes say
    /// otherwise ("keyboardSpread": the Sampler's, whose multisamples keep their recordings'
    /// balance between registers until the Library levels them, B97).
    double spreadLimit = 12.0;
    /// Each preset's range for that (its recipe's "keys": [low, high]; else 36 to 84).
    std::map<std::string, std::pair<int, int>> ranges;
    [[nodiscard]] std::pair<int, int> rangeOf(const std::string& name) const
    {
        const auto it = ranges.find(name);
        return it != ranges.end() ? it->second : std::pair{36, 84};
    }
};

Made fromRecipes(const fs::path& recipesFile)
{
    std::ifstream in(recipesFile);
    std::stringstream text;
    text << in.rdbuf();
    const auto recipes = nlohmann::json::parse(text.str(), nullptr, false);
    Made m;
    m.factory = recipes.is_object() && recipes.contains("device")
                    ? findDevice(recipes["device"].get<std::string>())
                    : nullptr;
    if (m.factory == nullptr)
    {
        std::cerr << recipesFile << ": not recipes for a device of ours\n";
        return m;
    }
    m.result = presetsFromRecipes(*m.factory, recipes);
    if (recipes.contains("keyboardSpread"))
        m.spreadLimit = recipes["keyboardSpread"].get<double>();
    for (const auto& r : recipes["presets"])
        if (r.contains("keys") && r["keys"].is_array() && r["keys"].size() == 2)
            m.ranges[r["name"].get<std::string>()] = {r["keys"][0].get<int>(), r["keys"][1].get<int>()};
    for (const auto& problem : m.result.problems)
        std::cerr << problem << "\n";
    return m;
}

/// The keyboard check (ADR 0069) of every instrument preset: a line each, then what fails.
/// Returns how many fail.
int checkAll(const Made& m, const std::string& only = {})
{
    if (m.factory->info().kind != DeviceKind::Instrument)
        return 0;
    int failing = 0;
    std::cout << std::fixed << std::setprecision(1);
    for (const auto& p : m.result.presets)
    {
        if (!only.empty() && p.name != only)
            continue;
        const auto c = checkKeyboard(*m.factory, p.state);
        if (!only.empty())
            for (const auto* takes : {&c.notes, &c.chords, &c.extremes})
                for (const auto& t : *takes)
                    std::cout << (takes == &c.notes    ? "  note  "
                                  : takes == &c.chords ? "  chord "
                                                       : "  extreme ")
                              << t.key << (t.played.empty() ? "" : " (" + t.played + ")") << ": peak "
                              << t.peakDb << " dBFS, level " << t.rmsDb << " dB\n";
        double c3 = -200.0;
        for (const auto& t : c.notes)
            if (t.key == 60)
                c3 = t.rmsDb;
        const auto [low, high] = m.rangeOf(p.name);
        const auto problems = c.problems(-1.0, m.spreadLimit, low, high);
        std::cout << std::left << std::setw(26) << p.name << std::right << " peak " << std::setw(6)
                  << c.peakDb() << " dBFS  C3 " << std::setw(6) << c3 << " dB  spread " << std::setw(5)
                  << c.spreadDb(low, high) << " dB" << (problems.empty() ? "" : "  FAILS") << "\n";
        for (const auto& problem : problems)
            std::cout << "    " << problem << "\n";
        failing += problems.empty() ? 0 : 1;
    }
    return failing;
}

int check(const fs::path& recipesFile, const std::string& only)
{
    const auto m = fromRecipes(recipesFile);
    if (m.factory == nullptr || !m.result.problems.empty())
        return 1;
    const int failing = checkAll(m, only);
    std::cout << failing << " of " << m.result.presets.size() << " fail\n";
    return failing == 0 ? 0 : 1;
}

/// Moves each preset's `param` until its C3 is near -24 dB and its loudest take under -3 dBFS
/// (ADR 0069), rewriting the recipes; a few passes, as levels aren't quite linear (drive, crush).
int level(const fs::path& recipesFile, const std::string& param)
{
    constexpr double target = -24.0, ceiling = -3.0;
    for (int pass = 0; pass < 4; ++pass)
    {
        std::ifstream in(recipesFile);
        std::stringstream text;
        text << in.rdbuf();
        auto recipes = nlohmann::ordered_json::parse(text.str());
        // Written back as it was laid out: its own indent.
        const auto body = text.str();
        const auto line = body.find('\n');
        const int indent = line == std::string::npos
                               ? 4
                               : static_cast<int>(body.find_first_not_of(' ', line + 1) - line - 1);
        const auto m = fromRecipes(recipesFile);
        if (m.factory == nullptr || !m.result.problems.empty())
            return 1;
        const auto& info = m.factory->info();
        const auto spec = std::find_if(info.params.begin(), info.params.end(),
                                       [&](const ParamSpec& s) { return s.id == param; });
        if (spec == info.params.end())
        {
            std::cerr << info.id << " has no " << param << "\n";
            return 1;
        }
        double furthest = 0.0;
        for (auto& recipe : recipes["presets"])
        {
            const auto name = recipe["name"].get<std::string>();
            const auto made = std::find_if(m.result.presets.begin(), m.result.presets.end(),
                                           [&](const Preset& p) { return p.name == name; });
            const auto c = checkKeyboard(*m.factory, made->state);
            double c3 = -200.0;
            for (const auto& t : c.notes)
                if (t.key == 60)
                    c3 = t.rmsDb;
            const double delta = std::min(target - c3, ceiling - c.peakDb());
            auto& params = recipe["params"];
            const double now = params.contains(param) ? params[param].get<double>() : spec->def;
            const double next = std::clamp(std::round((now + delta) * 10.0) / 10.0,
                                           static_cast<double>(spec->min), static_cast<double>(spec->max));
            furthest = std::max(furthest, std::abs(next - now));
            if (std::abs(next - spec->def) < 0.05)
                params.erase(param);
            else
                params[param] = next;
            std::cout << std::fixed << std::setprecision(1) << std::left << std::setw(26) << name
                      << std::right << " C3 " << std::setw(6) << c3 << " dB, peak " << std::setw(6)
                      << c.peakDb() << " dBFS: " << param << " " << now << " -> " << next << "\n";
        }
        std::ofstream out(recipesFile, std::ios::binary | std::ios::trunc);
        out << recipes.dump(std::max(1, indent)) << "\n";
        std::cout << "pass " << pass + 1 << ": the largest move " << furthest << " dB\n";
        if (furthest < 0.3)
            break;
    }
    return 0;
}

int make(const fs::path& recipesFile)
{
    const auto m = fromRecipes(recipesFile);
    if (m.factory == nullptr || !m.result.problems.empty())
        return 1;
    const auto& made = m.result;
    const auto* factory = m.factory;
    // An instrument's set is made only when every preset plays cleanly across the keyboard.
    if (const int failing = checkAll(m); failing > 0)
    {
        std::cerr << failing << " presets fail the keyboard check (ADR 0069): nothing written\n";
        return 1;
    }
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
    if ((args.size() == 2 || args.size() == 3) && args[0] == "check")
        return check(args[1], args.size() == 3 ? args[2] : std::string());
    if (args.size() == 3 && args[0] == "level")
        return level(args[1], args[2]);
    if (args.size() == 2 && args[0] == "make")
        return make(args[1]);
    if (args.size() == 2 && args[0] == "defaults")
        return defaults(args[1]);
    std::cerr << "usage: fd-presets describe <device>\n       fd-presets check "
                 "<devices/<name>/presets/recipes.json> [name]\n       fd-presets level "
                 "<devices/<name>/presets/recipes.json> <param>\n       fd-presets make "
                 "<devices/<name>/presets/recipes.json>\n       fd-presets defaults <device>\n";
    return 2;
}
