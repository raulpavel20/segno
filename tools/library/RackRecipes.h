#pragma once

// The factory racks (docs/design/racks.md §18.8): `.segnorack` files made from recipes, as the
// factory presets are, written into the Library's Racks folder by fd-library. JUCE-free (ADR 0006).
//
//   {"segnoRackRecipes": 1, "author": "Segno",
//    "racks": [{"name": "Riser", "folder": "Transitions", "tags": [...], "description": "...",
//               "chains": [{"name": "Main", "volumeDb": 0, "pan": 0,
//                           "modules": [{"device": "filter", "preset": "A factory preset",
//                                        "params": {"type": "High Pass", "freq": "20 Hz"}}]}],
//               "macros": [{"name": "Rise", "value": 0,
//                           "mappings": [{"chain": 1, "module": 1, "param": "freq",
//                                         "from": "20 Hz", "to": "2 kHz", "curve": 30}]}],
//               "snapshots": [{"name": "Verse", "macros": [0, 20]}],
//               "morph": {"on": false, "a": 1, "b": 2, "position": 0}}]}
//
// A module starts from a factory preset (or its defaults) and takes `params` over it, in the
// parameter's unit or as text it reads. A mapping names its module by chain and place, from 1,
// From and To in the parameter's unit (From defaults to where it is, To to its top), Curve in
// % (-100 .. 100). A macro's value and a snapshot's positions are in %, macro 1 first. Morph's A
// and B are snapshots, from 1.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fd::library_build
{
struct FactoryRack
{
    std::string folder, name; // Racks/<folder>/<name>.segnorack
    std::string text;         // the file, as the DAW saves one
};

struct RackRecipesResult
{
    std::vector<FactoryRack> racks;
    std::vector<std::string> problems; // a rack named in each
};

/// Every rack the recipes make. Problems: a name a file can't hold, an unknown module, preset,
/// parameter or value, a mapping that names no module or a parameter a macro can't move, two
/// macros on one parameter, more chains, macros or snapshots than a rack has.
[[nodiscard]] RackRecipesResult racksFromRecipes(const nlohmann::json& recipes);

inline constexpr const char* rackFileExtension = ".segnorack";
} // namespace fd::library_build
