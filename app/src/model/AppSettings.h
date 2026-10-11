#pragma once

#include "model/Export.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// Settings (docs/design/settings.md, ADR 0045): what you choose once, for every project, kept per
// user in one settings.json. JUCE-free; the shell, the engine and the assistants' server share one
// SettingsStore. What the engine keeps itself (the audio device it opened, each input's count-in
// and offsets, the plugin list) stays in its own file; Settings change it through the Session.

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

/// A recording's file: the formats and bit depths export writes (bitDepthsFor).
using RecordingFormat = ExportFormat;

/// The interface's sizes offered, in percent (0: automatic, the system's).
inline constexpr int kInterfaceSizes[] = {75, 100, 125, 150, 175, 200};

struct AppSettings
{
    RecentProjects recent;

    struct Display
    {
        int interfaceSize = 0;       // percent; 0: automatic
        bool systemTitleBar = false; // Linux and Windows: the system's title bar instead of ours
        bool operator==(const Display&) const = default;
    } display;

    struct Midi
    {
        bool computerKeyboard = false; // the computer keyboard as MIDI from launch
        bool operator==(const Midi&) const = default;
    } midi;

    struct Recording
    {
        RecordingFormat format = RecordingFormat::Wav;
        int bitDepth = 24; // one of bitDepthsFor(format): 16, 24, or 32 (float, WAV only)
        bool operator==(const Recording&) const = default;
    } recording;

    struct Library
    {
        std::string folder;      // the Segno folder; empty: Documents/Segno
        std::string newProjects; // where Save as… starts; empty: the Segno folder
        bool operator==(const Library&) const = default;
    } library;

    struct Plugins
    {
        std::vector<std::string> folders; // scanned besides the system's
        bool operator==(const Plugins&) const = default;
    } plugins;

    /// What a new project's export starts from: the last export's settings (its folder and name
    /// left out), or the defaults.
    ExportSettings exportDefaults;

    /// Shortcuts you've changed: an action's id to its keys (as `KeyPress::getTextDescription`
    /// writes them; empty: none). Actions not here keep their defaults.
    std::map<std::string, std::vector<std::string>> keys;

    struct Assistants
    {
        bool enabled = false;
        bool readOnly = false;
        int port = 7377;
        bool operator==(const Assistants&) const = default;
    } assistants;

    /// Keys this build doesn't know, kept as they were so an older Segno doesn't drop what a newer
    /// one wrote.
    nlohmann::json others = nlohmann::json::object();

    bool operator==(const AppSettings&) const = default;
};

[[nodiscard]] std::string toJson(const AppSettings& settings);
/// Settings from `json`; what's missing or malformed keeps its default.
[[nodiscard]] AppSettings appSettingsFromJson(const std::string& json);
/// The settings before ADR 0045, from app.json and assistants.json (either may be empty).
[[nodiscard]] AppSettings settingsFromOlderFiles(const std::string& appJson,
                                                 const std::string& assistantsJson);

/// The interface's size as a scale (automatic: 1).
[[nodiscard]] double interfaceScale(const AppSettings& settings);

/// settings.json, in memory: every change is saved at once and told to the listeners. Message
/// thread only.
class SettingsStore
{
public:
    /// Reads `folder`/settings.json; without it, moves app.json and assistants.json there. An
    /// empty folder keeps nothing (tests).
    explicit SettingsStore(std::filesystem::path folder);

    [[nodiscard]] const AppSettings& get() const noexcept { return settings_; }
    [[nodiscard]] std::filesystem::path file() const { return folder_ / "settings.json"; }
    /// Applies `edit`, saves, and tells the listeners (if anything changed).
    void change(const std::function<void(AppSettings&)>& edit);

    using Listener = std::function<void(const AppSettings&)>;
    int addListener(Listener listener);
    void removeListener(int id);

private:
    void save() const;

    std::filesystem::path folder_;
    AppSettings settings_;
    std::map<int, Listener> listeners_;
    int nextListener_ = 1;
};
} // namespace fd::model
