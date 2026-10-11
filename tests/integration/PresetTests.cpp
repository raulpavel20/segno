// Presets in the DAW (M3 A1, ADR 0044, docs/design/presets.md): undo, browsing, A/B kept with the
// project, your default for new modules, automation over a preset. Through engine::Session.

#include "RecordingFixture.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>

using Catch::Approx;
using namespace recording_test;

namespace
{
const fd::DeviceInfo& compressorInfo()
{
    return fd::findDevice("compressor")->info();
}

/// A session with presets of its own, and a Compressor on its first track.
struct Presets
{
    Recording r;
    std::uint64_t module = 0;
    std::unique_ptr<fd::PresetHost> host;

    Presets()
    {
        r.session->setPresetsFolder(r.root.getChildFile("Presets"));
        module = r.session->addDevice(r.track(0), "fd:compressor");
        REQUIRE(module != 0);
        host = r.session->presetHost(module);
        REQUIRE(host != nullptr);
    }

    fd::PresetController controller() { return {compressorInfo(), *host}; }

    /// The module's parameter now, as its panel reads it.
    float param(const std::string& id) { return host->state().params.at(id); }

    /// Saves one of yours with `threshold` (and the rest as the module is now).
    const fd::Preset& yours(const std::string& name, float threshold)
    {
        auto state = host->state();
        state.params["threshold"] = threshold;
        auto saved = r.session->presetLibrary().save(compressorInfo(), state, name, {"Bus"}, {}, {}, true);
        REQUIRE(saved.preset != nullptr);
        return *saved.preset;
    }
};
} // namespace

TEST_CASE("presets in the DAW: a load is one undo step; a browse is one, and going back undoes it",
          "[integration]")
{
    Presets p;
    auto& s = *p.r.session;
    p.yours("Soft", -10.0f);
    p.yours("Hard", -40.0f);
    auto c = p.controller();
    const float before = p.param("threshold");

    REQUIRE(c.load(*s.presetLibrary().find(compressorInfo(), "user/Compressor/Soft")));
    CHECK(p.param("threshold") == Approx(-10.0f).margin(0.01f));
    CHECK(s.undoDescription() == "Load preset Soft");
    CHECK(c.loadedName() == "Soft");
    s.undo();
    CHECK(p.param("threshold") == Approx(before).margin(0.01f));
    CHECK(c.loadedName() == "Default settings"); // the memo is undone with the state

    // Browsing: three loads are one step, named for the last.
    p.host->beginBrowsing();
    for (const auto* name : {"user/Compressor/Soft", "user/Compressor/Hard", "user/Compressor/Soft"})
        c.load(*s.presetLibrary().find(compressorInfo(), name));
    p.host->endBrowsing(true);
    CHECK(p.param("threshold") == Approx(-10.0f).margin(0.01f));
    CHECK(s.undoDescription() == "Load preset Soft");
    s.undo();
    CHECK(p.param("threshold") == Approx(before).margin(0.01f));

    // Escape: back to what was there before the list opened.
    p.host->beginBrowsing();
    c.load(*s.presetLibrary().find(compressorInfo(), "user/Compressor/Hard"));
    CHECK(p.param("threshold") == Approx(-40.0f).margin(0.01f));
    p.host->endBrowsing(false);
    CHECK(p.param("threshold") == Approx(before).margin(0.01f));
    CHECK(c.loadedName() == "Default settings");
    // Opening and closing without loading anything adds no step.
    const auto description = s.undoDescription();
    p.host->beginBrowsing();
    p.host->endBrowsing(false);
    CHECK(s.undoDescription() == description);
}

TEST_CASE("presets in the DAW: A/B switches as one undo step each and is kept with the project",
          "[integration]")
{
    Presets p;
    auto& s = *p.r.session;
    p.yours("Soft", -10.0f);
    p.yours("Hard", -40.0f);
    auto c = p.controller();
    c.load(*s.presetLibrary().find(compressorInfo(), "user/Compressor/Soft"));
    c.switchCompare();
    CHECK(s.undoDescription() == "Compare B");
    c.load(*s.presetLibrary().find(compressorInfo(), "user/Compressor/Hard"));
    c.switchCompare();
    CHECK(p.param("threshold") == Approx(-10.0f).margin(0.01f));
    s.undo(); // back on B
    CHECK(c.onB());
    CHECK(p.param("threshold") == Approx(-40.0f).margin(0.01f));
    CHECK(c.loadedName() == "Hard");

    const auto project = p.r.root.getChildFile("AB").getChildFile("AB.segno");
    REQUIRE((project.getParentDirectory().isDirectory() || project.getParentDirectory().createDirectory()));
    REQUIRE(s.saveAs(project));
    p.host.reset(); // as the app releases its modules before opening
    REQUIRE(s.open(project));
    const auto module = s.snapshot()->tracks[0].devices[0].id;
    auto host = s.presetHost(module);
    REQUIRE(host != nullptr);
    fd::PresetController again(compressorInfo(), *host);
    CHECK(again.onB());
    CHECK(again.loadedName() == "Hard");
    again.switchCompare();
    CHECK(again.loadedName() == "Soft");
    CHECK(host->state().params.at("threshold") == Approx(-10.0f).margin(0.01f));
}

TEST_CASE("presets in the DAW: a new module starts from your default, in the same undo step", "[integration]")
{
    Presets p;
    auto& s = *p.r.session;
    auto state = p.host->state();
    state.params["ratio"] = 8.0f;
    REQUIRE(s.presetLibrary().saveDefault(compressorInfo(), state));

    const auto second = s.addDevice(p.r.track(1), "fd:compressor");
    auto host = s.presetHost(second);
    REQUIRE(host != nullptr);
    fd::PresetController c(compressorInfo(), *host);
    CHECK(host->state().params.at("ratio") == Approx(8.0f).margin(0.01f));
    CHECK(c.loadedName() == "Default");
    CHECK_FALSE(c.modified());
    CHECK(s.undoDescription() == "Add module");

    // Another kind of module isn't touched; clearing the default goes back to its own.
    REQUIRE(c.clearDefault());
    const auto third = s.addDevice(p.r.track(2), "fd:compressor");
    CHECK(s.presetHost(third)->state().params.at("ratio") == Approx(4.0f).margin(0.01f));
}

TEST_CASE("presets in the DAW: an automated parameter keeps following its lane over a preset",
          "[integration]")
{
    Presets p;
    auto& s = *p.r.session;
    s.updateSnapshotNow();
    const auto target = s.moduleParameter(p.module, "threshold");
    REQUIRE(target);
    const auto lane = s.addAutomationLane(*target);
    REQUIRE(lane != 0);
    REQUIRE(s.setLanePoints(lane, {{.time = 0.0, .value = 0.25f}, {.time = 30.0, .value = 0.25f}}));
    const float automated = compressorInfo().params[0].fromNormalized(0.25f);
    auto handle = s.openDevice(p.module);
    auto& controls = handle->context().params; // what the panel shows: the lane's value
    s.setPosition(0.0);
    s.play();
    p.r.pump(0.2);
    CHECK(controls.getPlain(0) == Approx(automated).margin(0.05f));

    p.yours("Other", -3.0f);
    auto c = p.controller();
    c.load(*s.presetLibrary().find(compressorInfo(), "user/Compressor/Other"));
    p.r.pump(0.3);
    CHECK(controls.getPlain(0) == Approx(automated).margin(0.05f));    // the lane wins
    CHECK(controls.automationState(0) == fd::ui::AutomationState::On); // and still plays
}

TEST_CASE("presets in the DAW: loading leaves the session's own extra state alone", "[integration]")
{
    Recording r;
    auto& s = *r.session;
    s.setPresetsFolder(r.root.getChildFile("Presets"));
    const auto spectrum = s.addDevice(r.track(0), "fd:spectrum");
    auto host = s.presetHost(spectrum);
    REQUIRE(host != nullptr);
    const auto& info = fd::findDevice("spectrum")->info();
    // A snapshot of this mix (session data) and a reference curve (kept by presets).
    auto curve = nlohmann::json::array();
    for (int i = 0; i < 121; ++i)
        curve.push_back(-30.0);
    auto handle = s.openDevice(spectrum);
    handle->context().params.applyExtraState({{"snapshot.curve", curve}, {"snapshot.channel", 0}});
    auto saved = s.presetLibrary().save(info, host->state(), "Mine", {}, {}, {}, false);
    REQUIRE(saved.preset != nullptr);
    CHECK_FALSE(saved.preset->state.extra.contains("snapshot.curve"));

    fd::PresetController c(info, *host);
    REQUIRE(c.load(*s.presetLibrary().find(info, "")));
    CHECK(host->state().extra.contains("snapshot.curve")); // still there after Default settings
}
