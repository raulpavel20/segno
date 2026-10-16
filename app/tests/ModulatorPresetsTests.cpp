// The modulators' factory presets (docs/design/modulators.md §14, ADR 0044): about eight each, each a
// preset file of this build, naming only the modulator's own parameters, within their ranges, and
// a Steps pattern of the right length.
#include "model/FactoryPresets.h"
#include "model/Modulators.h"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>

using namespace fd::model;

TEST_CASE("modulator presets: about eight each, of this build, their own parameters in range",
          "[fast][modulators]")
{
    std::map<std::string, int> count;
    std::set<std::string> names;
    for (const auto& file : dawFactoryPresets())
    {
        INFO(file.name);
        const auto preset = fd::parsePresetFile(file.text, std::string(file.name));
        REQUIRE(preset.has_value());
        const auto kind = modulatorKind(preset->state.deviceId);
        if (!kind.has_value())
        {
            CHECK(preset->state.deviceId == "drum-kit"); // the only other module with presets here
            continue;
        }
        const auto& info = modulatorInfo(*kind);
        CHECK_FALSE(fd::presetTooNew(info, *preset));
        CHECK_FALSE(preset->description.empty());
        CHECK_FALSE(preset->tags.empty());
        CHECK(names.insert(preset->state.deviceId + "/" + preset->name).second);
        ++count[preset->state.deviceId];
        for (const auto& [id, value] : preset->state.params)
        {
            INFO(id);
            const auto spec = std::find_if(info.params.begin(), info.params.end(),
                                           [&](const auto& p) { return p.id == id; });
            REQUIRE(spec != info.params.end());
            CHECK(value >= spec->min);
            CHECK(value <= spec->max);
        }
        CHECK(preset->state.params.size() == info.params.size());
        if (*kind == ModulatorKind::Steps)
        {
            REQUIRE(preset->state.extra.contains("steps"));
            CHECK(preset->state.extra["steps"].size() == static_cast<size_t>(modulator::Steps::maxSteps));
            CHECK(preset->state.extra["chances"].size() == static_cast<size_t>(modulator::Steps::maxSteps));
        }
        else
            CHECK(preset->state.extra.empty());
    }
    for (const auto kind : allModulators)
        CHECK(count[std::string(modulatorInfo(kind).id)] == 8);
}
