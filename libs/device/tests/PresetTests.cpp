// Presets (docs/design/presets.md, ADR 0044): the file, the library of factory presets and yours,
// and the controller every host uses.
#include "fd/device/Presets.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>

using namespace fd;
namespace fs = std::filesystem;

namespace
{
constexpr std::array kParams{
    ParamSpec{.id = "gain", .name = "Gain", .min = -24.0f, .max = 24.0f, .def = 0.0f, .unit = Unit::Decibels},
    ParamSpec{.id = "mix", .name = "Mix", .min = 0.0f, .max = 100.0f, .def = 100.0f, .unit = Unit::Percent},
};
constexpr std::array<std::string_view, 1> kSession{"snapshot"};
constexpr DeviceInfo kInfo{.id = "test-device",
                           .name = "Test Module",
                           .params = kParams,
                           .stateVersion = 2,
                           .sessionExtraKeys = kSession};
constexpr DeviceInfo kOther{.id = "other-device", .name = "Other", .params = kParams, .stateVersion = 1};

DeviceState stateOf(float gain, float mix = 100.0f, nlohmann::json extra = nlohmann::json::object(),
                    std::string_view device = "test-device")
{
    DeviceState s;
    s.deviceId = std::string(device);
    s.stateVersion = 2;
    s.params = {{"gain", gain}, {"mix", mix}};
    s.extra = std::move(extra);
    return s;
}

std::string file(std::vector<std::string> tags, const DeviceState& s, std::string description = {})
{
    Preset p;
    p.tags = std::move(tags);
    p.description = std::move(description);
    p.author = "Segno";
    p.state = s;
    return presetFileText(p);
}

/// A presets folder of its own, removed afterwards.
struct Folder
{
    fs::path root = fs::temp_directory_path() / ("fd-presets-" + std::to_string(std::random_device{}()));
    ~Folder()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void write(const fs::path& relative, const std::string& text) const
    {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative) << text;
    }
};

/// A host holding a state and a memo, as the plugin and the DAW do.
class FakeHost final : public PresetHost
{
public:
    explicit FakeHost(PresetLibrary& library) : library_(library) {}
    PresetLibrary& library() override { return library_; }
    DeviceState state() const override { return current; }
    PresetMemo memo() const override { return memo_; }
    void apply(const DeviceState& s, const PresetMemo& m, const std::string& undoName) override
    {
        // As the hosts do: the parameters, and the extra state by patch (session keys stay).
        current.params = s.params;
        current.extra.merge_patch(extraPatch(kInfo, current.extra, s.extra));
        memo_ = m;
        edits.push_back(undoName);
    }
    void setMemo(const PresetMemo& m) override { memo_ = m; }
    bool trash(const fs::path& f) override { return fs::remove(f); }

    DeviceState current = stateOf(0.0f);
    PresetMemo memo_;
    std::vector<std::string> edits;

private:
    PresetLibrary& library_;
};

const std::array kFactoryTexts{
    file({"Bus", "Subtle"}, stateOf(-3.0f, 80.0f), "Gentle glue"),
    file({"Drums", "Heavy"}, stateOf(6.0f)),
    file({"Bus"}, stateOf(1.0f, 50.0f, nlohmann::json::object(), "other-device")),
};
const std::array kFactory{
    FactoryPresetFile{"Glue", kFactoryTexts[0]},
    FactoryPresetFile{"Punchy drums", kFactoryTexts[1]},
    FactoryPresetFile{"Elsewhere", kFactoryTexts[2]},
};

std::vector<std::string> names(const std::vector<const Preset*>& list)
{
    std::vector<std::string> out;
    for (const auto* p : list)
        out.push_back(p->name);
    return out;
}
} // namespace

TEST_CASE("presets: a file round-trips byte for byte; anything else isn't one", "[fast]")
{
    const auto text =
        file({"Bus", "Subtle"}, stateOf(-3.0f, 80.0f, {{"sample.path", "/x.wav"}}), "Gentle glue");
    const auto p = parsePresetFile(text, "Glue");
    REQUIRE(p);
    CHECK(p->name == "Glue");
    CHECK(p->tags == std::vector<std::string>{"Bus", "Subtle"});
    CHECK(p->description == "Gentle glue");
    CHECK(p->state.params.at("gain") == -3.0f);
    CHECK(presetFileText(*p) == text);

    CHECK_FALSE(parsePresetFile("{\"device\":\"x\"}", "x")); // a bare state isn't a preset file
    CHECK_FALSE(parsePresetFile("not json", "x"));
    // A newer build's: listed, never loaded.
    auto newer = nlohmann::json::parse(text);
    newer["segnoPreset"] = presetFormat + 1;
    const auto n = parsePresetFile(newer.dump(), "Newer");
    REQUIRE(n);
    CHECK(presetTooNew(kInfo, *n));
    auto later = *p;
    later.state.stateVersion = 3;
    CHECK(presetTooNew(kInfo, later));
    CHECK_FALSE(presetTooNew(kInfo, *p));
}

TEST_CASE("presets: a preset is the sound, not the session; sameSound and the extra patch", "[fast]")
{
    const auto s = stateOf(1.0f, 100.0f, {{"sample.path", "/a.wav"}, {"snapshot", {1, 2, 3}}});
    const auto kept = presetState(kInfo, s);
    CHECK(kept.extra.contains("sample.path"));
    CHECK_FALSE(kept.extra.contains("snapshot"));

    CHECK(sameSound(kInfo, s, kept)); // session keys don't count
    CHECK(sameSound(kInfo, stateOf(1.0f), stateOf(1.0f + 1e-4f)));
    CHECK_FALSE(sameSound(kInfo, stateOf(1.0f), stateOf(1.1f)));
    auto missing = stateOf(0.0f);
    missing.params.erase("mix"); // a missing parameter is its default
    CHECK(sameSound(kInfo, missing, stateOf(0.0f, 100.0f)));
    CHECK_FALSE(sameSound(kInfo, stateOf(0.0f, 100.0f, {{"sample.path", "/a.wav"}}), stateOf(0.0f)));

    // Loading turns the extra state into the preset's, and leaves the session's alone.
    const auto patch = extraPatch(kInfo, {{"sample.path", "/a.wav"}, {"snapshot", 1}, {"loop", true}},
                                  {{"sample.path", "/b.wav"}});
    CHECK(patch == nlohmann::json{{"sample.path", "/b.wav"}, {"loop", nullptr}});
}

TEST_CASE("presets: names a file can hold", "[fast]")
{
    CHECK(presetNameProblem("Glue").empty());
    CHECK(presetNameProblem("Vocal presence – wide").empty());
    CHECK_FALSE(presetNameProblem("").empty());
    CHECK_FALSE(presetNameProblem(" Glue").empty());
    CHECK_FALSE(presetNameProblem("Glue.").empty());
    CHECK_FALSE(presetNameProblem("A/B").empty());
    CHECK_FALSE(presetNameProblem("What?").empty());
    CHECK_FALSE(presetNameProblem("default SETTINGS").empty());
    CHECK_FALSE(presetNameProblem("COM1").empty());
    CHECK_FALSE(presetNameProblem(std::string(101, 'a')).empty());
    CHECK(presetNameProblem(std::string(100, 'a')).empty());
}

TEST_CASE("presets: the library lists Default settings, the factory set, then yours, found by what's in them",
          "[fast]")
{
    Folder f;
    f.write("Test Module/Mine.segnopreset", file({"Vocals"}, stateOf(2.0f)));
    f.write("Test Module/Drums/Kick.segnopreset", file({"Drums"}, stateOf(4.0f)));
    f.write("Moved here/Stray.segnopreset", file({}, stateOf(5.0f))); // any folder: found by content
    f.write("Other/Not mine.segnopreset",
            file({}, stateOf(1.0f, 50.0f, nlohmann::json::object(), "other-device")));
    f.write("Defaults/Test Module.segnopreset", file({}, stateOf(-1.0f)));
    f.write("Test Module/Broken.segnopreset", "{");
    PresetLibrary lib(kFactory, f.root);

    const auto list = lib.presetsFor(kInfo);
    CHECK(names(list) ==
          std::vector<std::string>{"Default settings", "Glue", "Punchy drums", "Stray", "Mine", "Kick"});
    CHECK(list[0]->source == Preset::Source::Builtin);
    CHECK(list[0]->state.params.at("mix") == 100.0f);
    CHECK(list[1]->source == Preset::Source::Factory);
    CHECK(list[1]->key() == "factory/Glue");
    CHECK(list[5]->key() == "user/Test Module/Drums/Kick");
    CHECK(lib.find(kInfo, "user/Test Module/Mine")->state.params.at("gain") == 2.0f);
    CHECK(lib.find(kInfo, "factory/Elsewhere") == nullptr); // another module's
    REQUIRE(lib.defaultFor(kInfo) != nullptr);
    CHECK(lib.find(kInfo, "default") == lib.defaultFor(kInfo));
    CHECK(lib.defaultFor(kOther) == nullptr);
    CHECK(names(lib.presetsFor(kOther)) ==
          std::vector<std::string>{"Default settings", "Elsewhere", "Not mine"});
}

TEST_CASE("presets: search ranks the name first; tags and favourites narrow the list", "[fast]")
{
    Folder f;
    f.write("Test Module/Snare glue.segnopreset", file({"Drums"}, stateOf(2.0f), "for a snare"));
    f.write("Test Module/Wide.segnopreset", file({"Bus"}, stateOf(1.0f), "glue for the mix"));
    PresetLibrary lib(kFactory, f.root);
    const auto all = lib.presetsFor(kInfo);

    CHECK(
        names(lib.filter(all, kInfo.id, "glue", {}, false)) ==
        std::vector<std::string>{"Glue", "Snare glue", "Wide"}); // name start, a word in it, the description
    CHECK(names(lib.filter(all, kInfo.id, "glue drums", {}, false)) ==
          std::vector<std::string>{"Snare glue"});
    CHECK(lib.filter(all, kInfo.id, "nothing", {}, false).empty());
    CHECK(names(lib.filter(all, kInfo.id, "", {"bus"}, false)) == std::vector<std::string>{"Glue", "Wide"});
    CHECK(names(lib.filter(all, kInfo.id, "", {"Bus", "Subtle"}, false)) == std::vector<std::string>{"Glue"});
    CHECK(PresetLibrary::tagsIn(all) == std::vector<std::string>{"Bus", "Drums", "Heavy", "Subtle"});

    REQUIRE(lib.setFavourite(kInfo.id, "user/Test Module/Wide", true));
    CHECK(names(lib.filter(all, kInfo.id, "", {}, true)) == std::vector<std::string>{"Wide"});
    // Kept in the folder, shared: another library reads it.
    PresetLibrary again(kFactory, f.root);
    CHECK(again.isFavourite(kInfo.id, "user/Test Module/Wide"));
    CHECK_FALSE(again.isFavourite(kInfo.id, "factory/Glue"));
}

TEST_CASE("presets: saving yours, replacing, renaming and removing", "[fast]")
{
    Folder f;
    PresetLibrary lib(kFactory, f.root);
    auto saved =
        lib.save(kInfo, stateOf(3.0f, 100.0f, {{"snapshot", 1}}), "My glue", {"Bus"}, "mine", "Me", false);
    REQUIRE(saved.preset != nullptr);
    CHECK(saved.preset->key() == "user/Test Module/My glue");
    CHECK(fs::exists(f.root / "Test Module" / "My glue.segnopreset"));
    CHECK_FALSE(saved.preset->state.extra.contains("snapshot")); // the session stays out

    // The same name: refused, unless replacing.
    auto again = lib.save(kInfo, stateOf(4.0f), "my GLUE", {}, {}, {}, false);
    CHECK(again.preset == nullptr);
    CHECK(again.exists);
    again = lib.save(kInfo, stateOf(4.0f), "My glue", {}, {}, {}, true);
    REQUIRE(again.preset != nullptr);
    CHECK(again.preset->state.params.at("gain") == 4.0f);
    CHECK(lib.save(kInfo, stateOf(4.0f), "a/b", {}, {}, {}, false).problem.find("can't hold") !=
          std::string::npos);

    // Overwriting keeps what's said about it; factory presets can't be.
    auto over = lib.overwrite(kInfo, *again.preset, stateOf(5.0f));
    REQUIRE(over.preset != nullptr);
    CHECK(over.preset->state.params.at("gain") == 5.0f);
    CHECK(lib.overwrite(kInfo, *lib.find(kInfo, "factory/Glue"), stateOf(5.0f)).preset == nullptr);

    // Renaming: the star follows it.
    lib.setFavourite(kInfo.id, over.preset->key(), true);
    auto renamed = lib.rename(kInfo, *over.preset, "Glue two");
    REQUIRE(renamed.preset != nullptr);
    CHECK(renamed.preset->key() == "user/Test Module/Glue two");
    CHECK(lib.isFavourite(kInfo.id, "user/Test Module/Glue two"));
    CHECK_FALSE(lib.isFavourite(kInfo.id, "user/Test Module/My glue"));
    CHECK_FALSE(fs::exists(f.root / "Test Module" / "My glue.segnopreset"));

    // Removing goes through the host (to the trash).
    fs::path trashed;
    CHECK(lib.remove(kInfo, *renamed.preset,
                     [&](const fs::path& p)
                     {
                         trashed = p;
                         return fs::remove(p);
                     }));
    CHECK(trashed.filename() == "Glue two.segnopreset");
    CHECK(lib.find(kInfo, "user/Test Module/Glue two") == nullptr);
    CHECK_FALSE(lib.isFavourite(kInfo.id, "user/Test Module/Glue two"));
}

TEST_CASE("presets: the controller loads, steps, tells what's modified, reverts and saves", "[fast]")
{
    Folder f;
    PresetLibrary lib(kFactory, f.root);
    FakeHost host(lib);
    host.current = stateOf(0.0f, 100.0f, {{"snapshot", 7}});
    PresetController c(kInfo, host);

    CHECK(c.loadedName() == "Default settings");
    CHECK_FALSE(c.modified());
    host.current.params["gain"] = 2.0f;
    CHECK(c.modified());

    REQUIRE(c.load(*lib.find(kInfo, "factory/Glue")));
    CHECK(host.edits.back() == "Load preset Glue");
    CHECK(host.memo_.preset == "factory/Glue");
    CHECK(c.loadedName() == "Glue");
    CHECK(host.current.params.at("gain") == -3.0f);
    CHECK(host.current.extra.at("snapshot") == 7); // the session's stays
    CHECK_FALSE(c.modified());

    host.current.params["mix"] = 20.0f;
    CHECK(c.modified());
    CHECK(c.revert());
    CHECK(host.edits.back() == "Revert to preset");
    CHECK(host.current.params.at("mix") == 80.0f);
    CHECK_FALSE(c.revert()); // nothing to revert

    // Stepping wraps, in the order given.
    const auto list = c.list();
    REQUIRE(c.step(list, +1));
    CHECK(c.loadedName() == "Punchy drums");
    REQUIRE(c.step(list, +1));
    CHECK(c.loadedName() == "Default settings");
    REQUIRE(c.step(list, -1));
    CHECK(c.loadedName() == "Punchy drums");

    // Saving: a factory preset can't be written over; Save as makes one of yours, which it then is.
    CHECK_FALSE(c.canSave());
    host.current.params["gain"] = 9.0f;
    auto saved = c.saveAs("Loud", {"Heavy"}, {}, {}, false);
    REQUIRE(saved.preset != nullptr);
    CHECK(host.memo_.preset == "user/Test Module/Loud");
    CHECK_FALSE(c.modified());
    CHECK(c.canSave());
    host.current.params["gain"] = 10.0f;
    CHECK(c.modified());
    REQUIRE(c.save().preset != nullptr);
    CHECK_FALSE(c.modified());

    // Renaming the loaded one: the module follows it.
    REQUIRE(c.rename(*c.loaded(), "Louder").preset != nullptr);
    CHECK(host.memo_.preset == "user/Test Module/Louder");
    CHECK(c.loadedName() == "Louder");

    // A loaded preset whose file goes keeps its name, without a dot.
    REQUIRE(c.remove(*c.loaded()));
    CHECK_FALSE(fs::exists(f.root / "Test Module" / "Louder.segnopreset"));
    CHECK(c.loadedName() == "Louder");
    CHECK_FALSE(c.modified());
}

TEST_CASE("presets: A/B starts the same, switches as one edit each, and copies", "[fast]")
{
    Folder f;
    PresetLibrary lib(kFactory, f.root);
    FakeHost host(lib);
    PresetController c(kInfo, host);
    c.load(*lib.find(kInfo, "factory/Glue"));

    c.switchCompare(); // B starts as A
    CHECK(c.onB());
    CHECK(host.edits.back() == "Compare B");
    CHECK(host.current.params.at("gain") == -3.0f);
    c.load(*lib.find(kInfo, "factory/Punchy drums"));
    CHECK(host.current.params.at("gain") == 6.0f);

    c.switchCompare(); // back to A: Glue
    CHECK_FALSE(c.onB());
    CHECK(host.current.params.at("gain") == -3.0f);
    CHECK(c.loadedName() == "Glue");
    c.switchCompare();
    CHECK(c.loadedName() == "Punchy drums");
    CHECK(host.current.params.at("gain") == 6.0f);

    c.copyCompare(); // A becomes B
    CHECK(host.edits.back() == "Copy B to A");
    c.switchCompare();
    CHECK(host.current.params.at("gain") == 6.0f);
    CHECK(c.loadedName() == "Punchy drums");

    // Kept as JSON beside the state.
    const auto memo = PresetMemo::fromJson(host.memo_.toJson());
    CHECK(memo.preset == host.memo_.preset);
    CHECK(memo.onB == host.memo_.onB);
    REQUIRE(memo.other);
    CHECK(memo.other->params == host.memo_.other->params);
    CHECK(PresetMemo::fromJson(PresetMemo{}.toJson()).empty());
}

TEST_CASE("presets: your default for new modules", "[fast]")
{
    Folder f;
    PresetLibrary lib(kFactory, f.root);
    FakeHost host(lib);
    PresetController c(kInfo, host);
    CHECK_FALSE(c.hasDefault());
    CHECK_FALSE(PresetController::startingPoint(kInfo, lib));

    host.current = stateOf(7.0f, 30.0f, {{"snapshot", 1}});
    REQUIRE(c.useAsDefault());
    CHECK(c.hasDefault());
    CHECK(fs::exists(f.root / "Defaults" / "Test Module.segnopreset"));
    const auto start = PresetController::startingPoint(kInfo, lib);
    REQUIRE(start);
    CHECK(start->first.params.at("gain") == 7.0f);
    CHECK_FALSE(start->first.extra.contains("snapshot"));
    CHECK(start->second.preset == "default");
    // A module started from it reads "Default", and isn't modified.
    host.memo_ = start->second;
    host.current = start->first;
    CHECK(c.loadedName() == "Default");
    CHECK_FALSE(c.modified());
    // Not one of yours in the list.
    CHECK(names(c.list()) == std::vector<std::string>{"Default settings", "Glue", "Punchy drums"});

    REQUIRE(c.clearDefault());
    CHECK_FALSE(c.hasDefault());
    CHECK_FALSE(PresetController::startingPoint(kInfo, lib));
}
