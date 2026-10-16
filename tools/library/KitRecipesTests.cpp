// The factory kits' recipes (docs/design/drum-kit.md §27, ADR 0067, ADR 0068), without the Library:
// every kit's pieces are instruments of the Library's recipe and every piece is played by a kit;
// the names keep makers' and model names out; hats choke each other; and every kit's preset is
// there, its pads as its recipe has them.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
json read(const fs::path& path)
{
    std::ifstream in(path);
    return json::parse(in, nullptr, false);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// A name ADR 0067 keeps out: a maker's, or a model's with its prefix.
bool vendorName(const std::string& name)
{
    static const char* words[] = {
        "roland", "linn",    "oberheim",     "e-mu",     "emu ",      "sequential", "simmons",   "korg",
        "yamaha", "casio",   "boss",         "elektron", "behringer", "tr-",        "tr8",       "tr 8",
        "cr-78",  "lm-1",    "dmx",          "sp-1200",  "sp1200",    "drumulator", "sds",       "rx5",
        "dr-5",   "dr5",     "rd-6",         "rd6",      "karoryfer", "virtuosity", "big rusty", "swirly",
        "unruly", "gogodze", "frankensnare", "freepats", "sonic pi",  "lmms"};
    const auto n = lower(name);
    return std::any_of(std::begin(words), std::end(words),
                       [&](const char* w) { return n.find(w) != std::string::npos; });
}
} // namespace

TEST_CASE("factory kits: their recipes, the Library's pieces and the presets agree; names keep makers out",
          "[fast]")
{
    const auto kits = read(FD_KIT_RECIPES);
    const auto recipe = read(FD_LIBRARY_RECIPE);
    REQUIRE(kits.is_object());
    REQUIRE(recipe.is_object());
    std::set<std::string> drums;
    for (const auto& i : recipe["instruments"])
        if (const auto sfz = i["sfz"].get<std::string>(); sfz.starts_with("Samples/Drums/"))
        {
            drums.insert(sfz);
            INFO(sfz);
            CHECK_FALSE(vendorName(sfz));
            CHECK(i.value("normalize", 0.0) < 0.0); // levelled as one
            CHECK_FALSE(i.value("pitched", true));
        }

    std::set<std::string> played, names;
    for (const auto& kit : kits["kits"])
    {
        const auto name = kit["name"].get<std::string>();
        INFO(name);
        CHECK(names.insert(name).second);
        CHECK_FALSE(vendorName(name));
        CHECK_FALSE(vendorName(kit["description"].get<std::string>()));
        CHECK(kit["pads"].size() >= 6);
        CHECK(kit["pads"].size() <= 32);
        std::set<int> notes;
        std::set<int> hatGroups;
        bool hats = false;
        for (const auto& pad : kit["pads"])
        {
            INFO(pad["name"].get<std::string>());
            CHECK(notes.insert(pad["note"].get<int>()).second);
            CHECK_FALSE(vendorName(pad["name"].get<std::string>()));
            CHECK(drums.contains(pad["piece"].get<std::string>()));
            CHECK(kits["roles"].contains(pad["role"].get<std::string>()));
            played.insert(pad["piece"].get<std::string>());
            if (const auto role = pad["role"].get<std::string>();
                (role == "hat" || role == "open hat") &&
                lower(pad["name"].get<std::string>()).find("hat") != std::string::npos)
            {
                hats = true;
                hatGroups.insert(pad["choke"].get<int>());
            }
        }
        // Its family first: the browser's folder (drum-kit.md §27.2).
        const auto& tags = kit["tags"];
        REQUIRE_FALSE(tags.empty());
        CHECK((tags[0] == "Acoustic" || tags[0] == "Electronic" || tags[0] == "Percussion"));
        // A kit's hats choke each other: one group, not none.
        if (hats)
        {
            CHECK(hatGroups.size() == 1);
            CHECK_FALSE(hatGroups.contains(0));
        }

        // Its preset: the same pads, each a One-shot Sampler playing its piece from the Library.
        const auto preset = read(fs::path(FD_KIT_PRESETS) / (name + ".segnopreset"));
        REQUIRE(preset.is_object());
        CHECK(preset["state"]["device"] == "drum-kit");
        const auto& pads = preset["state"]["extra"]["pads"];
        REQUIRE(pads.size() == kit["pads"].size());
        std::map<int, json> byNote;
        for (const auto& pad : kit["pads"])
            byNote[pad["note"].get<int>()] = pad;
        for (const auto& pad : pads)
        {
            const auto& r = byNote[pad["note"].get<int>()];
            CHECK(pad["name"] == r["name"]);
            CHECK(pad["choke"] == r["choke"]);
            REQUIRE(pad["modules"].size() == 1);
            const auto& sampler = pad["modules"][0]["state"];
            CHECK(sampler["device"] == "sampler");
            CHECK(sampler["extra"]["sample.path"] == "library:" + r["piece"].get<std::string>());
            CHECK(sampler["params"]["mode"] == 1.0); // One-shot
        }
    }
    // Every piece the Library makes for drums is played by a kit.
    for (const auto& sfz : drums)
    {
        INFO(sfz);
        CHECK(played.contains(sfz));
    }
    // And no preset is left from a kit that's gone.
    for (const auto& entry : fs::directory_iterator(FD_KIT_PRESETS))
        if (entry.path().extension() == ".segnopreset")
        {
            INFO(entry.path().filename().string());
            CHECK(names.contains(entry.path().stem().string()));
        }
}
