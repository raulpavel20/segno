// Settings (docs/design/settings.md, ADR 0045): the file, the move from the older files, the
// catalogue and its search.
#include "model/AppSettings.h"
#include "model/SettingsCatalogue.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>
#include <set>

using namespace fd::model;
namespace fs = std::filesystem;

namespace
{
struct Folder
{
    fs::path path = fs::temp_directory_path() / ("fd-settings-" + std::to_string(std::random_device{}()));
    Folder() { fs::create_directories(path); }
    ~Folder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& name, const std::string& text) const { std::ofstream(path / name) << text; }
};
} // namespace

TEST_CASE("settings: every setting round-trips; what's malformed keeps its default; unknown keys stay",
          "[fast]")
{
    AppSettings s;
    s.recent.add("/p/a.segno");
    s.display = {.interfaceSize = 150, .systemTitleBar = true};
    s.midi.computerKeyboard = true;
    s.recording = {.format = RecordingFormat::Flac, .bitDepth = 16};
    s.library = {.folder = "/home/me/Music/Segno", .newProjects = "/home/me/Songs"};
    s.plugins.folders = {"/opt/vst3"};
    s.exportDefaults.format = ExportFormat::Flac;
    s.exportDefaults.bitDepth = 16;
    s.keys["edit.duplicate"] = {"ctrl + J"};
    s.keys["edit.split"] = {}; // no key
    s.assistants = {.enabled = true, .readOnly = true, .port = 7400};
    CHECK(appSettingsFromJson(toJson(s)) == s);

    // A newer Segno's keys survive this one's save.
    auto withNews = nlohmann::json::parse(toJson(s));
    withNews["theme"] = {{"name", "Light"}};
    const auto read = appSettingsFromJson(withNews.dump());
    CHECK(read.others.contains("theme"));
    CHECK(nlohmann::json::parse(toJson(read))["theme"]["name"] == "Light");

    // Bad values take their defaults.
    const auto bad = appSettingsFromJson(
        R"({"display": {"interfaceSize": 133}, "recording": {"format": "mp3", "bitDepth": 20}, "assistants": {"port": 80}})");
    CHECK(bad.display.interfaceSize == 0);
    CHECK(bad.recording == AppSettings::Recording{});
    // A bit depth the format can't write (FLAC has no 32-bit float) takes the default.
    CHECK(appSettingsFromJson(R"({"recording": {"format": "flac", "bitDepth": 32}})").recording.bitDepth ==
          24);
    CHECK(bad.assistants.port == 1024);
    CHECK(appSettingsFromJson("[]") == AppSettings{});

    // An export's folder and name are the project's, never a default.
    s.exportDefaults.folder = "/somewhere";
    s.exportDefaults.name = "Song";
    const auto again = appSettingsFromJson(toJson(s));
    CHECK(again.exportDefaults.folder.empty());
    CHECK(again.exportDefaults.name.empty());

    CHECK(interfaceScale(AppSettings{}) == 1.0);
    CHECK(interfaceScale(s) == 1.5);
}

TEST_CASE("settings: the store saves every change, tells its listeners, and moves the older files in",
          "[fast]")
{
    Folder f;
    f.write("app.json", R"({"recentProjects": ["/a.segno"], "systemTitleBar": true})");
    f.write("assistants.json", R"({"enabled": true, "readOnly": false, "port": 7390})");
    {
        SettingsStore store(f.path);
        CHECK(store.get().recent.paths == std::vector<std::string>{"/a.segno"});
        CHECK(store.get().display.systemTitleBar);
        CHECK(store.get().assistants.enabled);
        CHECK(store.get().assistants.port == 7390);
        CHECK(fs::exists(f.path / "settings.json"));
        CHECK_FALSE(fs::exists(f.path / "app.json"));
        CHECK_FALSE(fs::exists(f.path / "assistants.json"));

        int told = 0;
        const int id = store.addListener(
            [&](const AppSettings& s)
            {
                ++told;
                CHECK(s.recording.bitDepth == 16);
            });
        store.change([](AppSettings& s) { s.recording.bitDepth = 16; });
        CHECK(told == 1);
        store.change([](AppSettings& s) { s.recording.bitDepth = 16; }); // no change: no word
        CHECK(told == 1);
        store.removeListener(id);
        store.change([](AppSettings& s) { s.recording.bitDepth = 24; });
        CHECK(told == 1);
        store.change([](AppSettings& s) { s.recording.bitDepth = 16; });
    }
    SettingsStore again(f.path);
    CHECK(again.get().recording.bitDepth == 16);
    CHECK(again.get().recent.paths == std::vector<std::string>{"/a.segno"});

    Folder empty; // a first launch: the defaults, nothing written yet
    SettingsStore fresh(empty.path);
    CHECK(fresh.get() == AppSettings{});
}

TEST_CASE("settings: the catalogue names, describes and sections every setting; search finds them", "[fast]")
{
    std::set<std::string> ids;
    std::set<SettingsSection> sections;
    for (const auto& s : settingsCatalogue())
    {
        INFO(s.id);
        CHECK(ids.insert(s.id).second);
        sections.insert(s.section);
        CHECK_FALSE(s.name.empty());
        CHECK(s.description.ends_with("."));
        CHECK(s.description.size() < 140); // one line under the row
        CHECK(findSetting(s.id) == &s);
    }
    CHECK(sections.size() == std::size(kSettingsSections)); // every section has something

    auto found = [](std::string_view query)
    {
        std::vector<std::string> out;
        for (const auto& s : settingsCatalogue())
            if (settingMatches(s, query))
                out.push_back(s.id);
        return out;
    };
    CHECK(found("buffer") == std::vector<std::string>{"audio.buffer"});
    CHECK(found("LATENCY").size() >= 2); // the buffer and the offsets
    CHECK(found("flac") == std::vector<std::string>{"recording.format", "export.format"});
    CHECK(found("midi offset") == std::vector<std::string>{"recording.midiOffset"});
    CHECK(found("").size() == settingsCatalogue().size());
    CHECK(found("nothing like this").empty());
    CHECK(textMatches("Duplicate Ctrl+D", "ctrl+d"));

    const auto ranges = matchRanges("Buffer size", "buf size");
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0] == std::pair<std::size_t, std::size_t>{0, 3});
    CHECK(ranges[1] == std::pair<std::size_t, std::size_t>{7, 4});
    CHECK(matchRanges("aaaa", "aa aaa").size() == 1); // overlaps merge
}
