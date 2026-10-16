// The factory racks (docs/design/racks.md §18.8): every recipe makes its rack, a file as the DAW
// saves one, the same every time; recipes with mistakes say what's wrong.
#include "RackRecipes.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>

namespace lb = fd::library_build;

namespace
{
nlohmann::json recipes()
{
    std::ifstream in(FD_RACK_RECIPES);
    return nlohmann::json::parse(in, nullptr, false);
}
} // namespace

TEST_CASE("factory racks: every recipe makes its rack, the same every time", "[fast]")
{
    const auto made = lb::racksFromRecipes(recipes());
    for (const auto& p : made.problems)
        INFO(p);
    REQUIRE(made.problems.empty());
    CHECK(made.racks.size() == 22); // and Granular's ten with a space (ADR 0069)
    std::set<std::string> folders, names;
    for (const auto& rack : made.racks)
    {
        INFO(rack.name);
        CHECK(names.insert(rack.name).second);
        folders.insert(rack.folder);
        const auto json = nlohmann::json::parse(rack.text);
        CHECK(json["segnoRack"] == 1);
        CHECK(json["name"] == rack.name);
        CHECK(json["author"] == "Segno");
        CHECK_FALSE(json["description"].get<std::string>().empty());
        CHECK_FALSE(json["tags"].empty());
        REQUIRE(json["macros"].size() == 8);
        // Each rack's first macro moves something, within its travel.
        REQUIRE_FALSE(json["macros"][0]["mappings"].empty());
        for (const auto& macro : json["macros"])
            for (const auto& m : macro["mappings"])
            {
                CHECK(m["from"].get<float>() >= 0.0f);
                CHECK(m["to"].get<float>() <= 1.0f);
                CHECK(m["from"] != m["to"]);
            }
        CHECK(json["kind"] == (rack.folder == "Instruments" ? "instrument" : "effect"));
    }
    CHECK(folders == std::set<std::string>{"Transitions", "Parallel", "Instruments"});
    // Made again: the same files.
    const auto again = lb::racksFromRecipes(recipes());
    REQUIRE(again.racks.size() == made.racks.size());
    for (size_t i = 0; i < made.racks.size(); ++i)
        CHECK(again.racks[i].text == made.racks[i].text);
}

TEST_CASE("factory racks: Verse to drop keeps its snapshots and Morph", "[fast]")
{
    const auto made = lb::racksFromRecipes(recipes());
    const auto it = std::find_if(made.racks.begin(), made.racks.end(),
                                 [](const auto& r) { return r.name == "Verse to drop"; });
    REQUIRE(it != made.racks.end());
    const auto json = nlohmann::json::parse(it->text);
    REQUIRE(json["snapshots"].size() == 2);
    CHECK(json["snapshots"][0]["name"] == "Verse");
    CHECK(json["snapshots"][1]["macros"][3].get<float>() == 0.9f);
    CHECK(json["morph"]["a"] == 0);
    CHECK(json["morph"]["b"] == 1);
    CHECK(json["morph"]["on"] == false);
}

TEST_CASE("factory racks: a recipe's mistakes are said, by rack", "[fast]")
{
    const auto bad = nlohmann::json::parse(R"({"segnoRackRecipes": 1, "racks": [
        {"name": "Bad", "folder": "Transitions", "chains": [{"modules": [
            {"device": "no-such"}, {"device": "filter", "preset": "No such preset", "params": {"nope": 1, "freq": "a lot"}}]}],
         "macros": [{"mappings": [{"chain": 1, "module": 9, "param": "freq"}, {"chain": 1, "module": 1, "param": "freq"},
                                  {"chain": 1, "module": 1, "param": "freq"}]}]},
        {"name": "Bad/name", "folder": "Transitions", "chains": [{"modules": []}]}]})");
    const auto made = lb::racksFromRecipes(bad);
    CHECK(made.racks.empty());
    const auto said = [&](const std::string& what)
    {
        return std::any_of(made.problems.begin(), made.problems.end(),
                           [&](const std::string& p) { return p.find(what) != std::string::npos; });
    };
    CHECK(said("Bad: no module no-such"));
    CHECK(said("no factory preset No such preset"));
    CHECK(said("no parameter nope"));
    CHECK(said("freq = \"a lot\""));
    CHECK(said("names no module"));
    CHECK(said("two macros map freq"));
    CHECK(said("Bad/name"));
}
