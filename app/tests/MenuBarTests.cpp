// The menu bar and the title bar (M2 step C10, ADR 0040, docs/design/menu-bar.md): the menus as
// data, what the app remembers, the title's place. JUCE-free.

#include "model/AppSettings.h"
#include "model/MenuBar.h"
#include "model/TopBarLayout.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fd::model;
using Catch::Approx;

namespace
{
bool contains(const std::vector<std::string>& ids, const std::string& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
} // namespace

TEST_CASE("menu bar: traditional menus, well made, each action once", "[fast]")
{
    for (const auto platform : {MenuPlatform::Windowed, MenuPlatform::Mac})
    {
        const auto menus = menuBar(platform);
        INFO((platform == MenuPlatform::Mac ? "macOS" : "Linux and Windows"));
        CHECK(menuProblems(menus).empty());
        for (const auto& problem : menuProblems(menus))
            UNSCOPED_INFO(problem);
        std::vector<std::string> titles;
        for (const auto& m : menus)
            titles.push_back(m.title);
        CHECK(titles == std::vector<std::string>{"File", "Edit", "Track", "Clip", "Song", "Module",
                                                 "Transport", "View", "Options", "Help"});
        // File holds the project; Edit starts with Undo and Redo.
        CHECK(menus[0].entries.front().id == "file.new");
        CHECK(contains(menuActions({menus[0]}), "project.export"));
        CHECK(menus[1].entries[0].id == "edit.undo");
        CHECK(menus[1].entries[1].id == "edit.redo");
    }
    // On macOS the application menu has About, Settings and Quit; no title bar option.
    const auto mac = menuActions(menuBar(MenuPlatform::Mac));
    CHECK_FALSE(contains(mac, "app.quit"));
    CHECK_FALSE(contains(mac, "app.about"));
    CHECK_FALSE(contains(mac, "app.settings"));
    CHECK_FALSE(contains(mac, "app.systemTitleBar"));
    CHECK(menuActions({{"Segno", 's', macAppMenu()}}) ==
          std::vector<std::string>{"app.about", "app.settings"});
    const auto windowed = menuActions(menuBar(MenuPlatform::Windowed));
    for (const auto* id : {"app.quit", "app.about", "app.settings", "app.systemTitleBar"})
        CHECK(contains(windowed, id));

    // Mnemonics avoid the Alt shortcuts in use (Alt+R, M, S, L).
    const auto menus = menuBar(MenuPlatform::Windowed);
    CHECK(menuForMnemonic(menus, 'F') == 0);
    CHECK(menuForMnemonic(menus, 'n') == 4); // Song (ADR 0049; Alt+S solos)
    CHECK(menuForMnemonic(menus, 'd') == 5); // Module (ADR 0044)
    CHECK(menuForMnemonic(menus, 'p') == 6); // Transport
    CHECK(menuForMnemonic(menus, 'h') == 9);
    for (const char taken : {'r', 'm', 's', 'l'})
        CHECK(menuForMnemonic(menus, taken) == -1);
}

TEST_CASE("menu bar: the checks find badly made menus", "[fast]")
{
    using K = MenuEntry::Kind;
    const MenuEntry sep{.kind = K::Separator};
    const MenuEntry a{.kind = K::Action, .id = "a"};
    const MenuEntry nested{.kind = K::Submenu, .title = "Inner", .entries = {a}};
    const std::vector<Menu> bad{{"File", 'f', {sep, a, sep, sep, a}},
                                {"Edit", 'x', {{.kind = K::Submenu, .title = "Outer", .entries = {nested}}}},
                                {"View", 'f', {}}};
    const auto problems = menuProblems(bad);
    auto has = [&](const std::string& text)
    {
        return std::any_of(problems.begin(), problems.end(),
                           [&](const auto& p) { return p.find(text) != std::string::npos; });
    };
    CHECK(has("starts with a separator"));
    CHECK(has("two separators in a row"));
    CHECK(has("submenu inside a submenu"));
    CHECK(has("View is empty"));
    CHECK(has("a is in the menus twice"));
    CHECK(has("Edit's mnemonic isn't in its title"));
    CHECK(has("View's mnemonic"));
}

TEST_CASE("recent projects: newest first, once each, ten at most; settings kept as JSON", "[fast]")
{
    RecentProjects r;
    for (int i = 0; i < 12; ++i)
        r.add("/p/" + std::to_string(i) + ".segno");
    REQUIRE(r.paths.size() == RecentProjects::kMax);
    CHECK(r.paths.front() == "/p/11.segno");
    CHECK(r.paths.back() == "/p/2.segno");
    r.add("/p/5.segno"); // again: to the front, not twice
    CHECK(r.paths.front() == "/p/5.segno");
    CHECK(std::count(r.paths.begin(), r.paths.end(), "/p/5.segno") == 1);
    r.add("");
    CHECK(r.paths.size() == RecentProjects::kMax);
    CHECK(r.existing([](const std::string& p) { return p != "/p/5.segno"; }).size() ==
          RecentProjects::kMax - 1);
    r.remove("/p/11.segno");
    CHECK(r.paths.size() == RecentProjects::kMax - 1);

    AppSettings s{.recent = r};
    s.display.systemTitleBar = true;
    CHECK(appSettingsFromJson(toJson(s)) == s);
    CHECK(appSettingsFromJson("not json") == AppSettings{});
    CHECK(
        appSettingsFromJson(R"({"display": {"systemTitleBar": "yes"}, "recentProjects": [1, "/a.segno"]})") ==
        AppSettings{.recent = {.paths = {"/a.segno"}}});
    r.clear();
    CHECK(r.paths.empty());
}

TEST_CASE("title bar: the project's name centred, moved in or narrowed when the sides need the room",
          "[fast]")
{
    // Room enough: centred on the bar.
    auto slot = placeTitle(1600.0, 500.0, 1480.0, 100.0, 24.0);
    REQUIRE(slot.shown);
    CHECK(slot.x == Approx(750.0));
    CHECK(slot.width == Approx(100.0));
    // A wide left side: moved right, keeping its gap.
    slot = placeTitle(1024.0, 520.0, 904.0, 100.0, 24.0);
    CHECK(slot.x == Approx(544.0));
    // Less room than text: as wide as the room (the caller elides).
    slot = placeTitle(1024.0, 520.0, 700.0, 300.0, 24.0);
    CHECK(slot.x == Approx(544.0));
    CHECK(slot.width == Approx(132.0));
    // No room: not shown.
    CHECK_FALSE(placeTitle(1024.0, 520.0, 560.0, 100.0, 24.0).shown);
}
