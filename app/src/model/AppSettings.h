#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fd::model
{
// What the app remembers between launches, apart from projects and the assistants' settings
// (docs/design/menu-bar.md §9): the projects opened last and the title bar option. JUCE-free; the
// shell reads and writes it as JSON in the app's data folder.

/// File > Open recent: the last projects opened or saved, newest first.
struct RecentProjects
{
    static constexpr std::size_t kMax = 10;
    std::vector<std::string> paths;

    /// `path` to the front, once; the oldest past kMax drop off.
    void add(const std::string& path);
    void remove(const std::string& path);
    void clear() { paths.clear(); }
    /// The paths that `exists` still finds (the menu leaves the rest out).
    [[nodiscard]] std::vector<std::string>
    existing(const std::function<bool(const std::string&)>& exists) const;

    bool operator==(const RecentProjects&) const = default;
};

struct AppSettings
{
    RecentProjects recent;
    /// Linux and Windows: the system's title bar instead of ours (the menus keep their row).
    bool systemTitleBar = false;

    bool operator==(const AppSettings&) const = default;
};

[[nodiscard]] std::string toJson(const AppSettings& settings);
/// Settings from `json`; what's missing or malformed keeps its default.
[[nodiscard]] AppSettings appSettingsFromJson(const std::string& json);
} // namespace fd::model
