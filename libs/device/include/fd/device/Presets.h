#pragma once

#include "fd/device/Device.h"
#include "fd/device/State.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fd
{
// Presets (docs/design/presets.md, ADR 0044): files of a module's state, factory ones built in,
// yours in one folder shared by the plugins and the DAW. JUCE-free; message thread only.

/// A factory preset built into the plugins and the DAW, generated at build time from
/// `devices/<name>/presets/*.segnopreset` (cmake/FdAddDevice.cmake).
struct FactoryPresetFile
{
    std::string_view name; // the file's name without its extension
    std::string_view text; // the file
};

/// Every built-in factory preset (link fd::registry, which generates it).
std::span<const FactoryPresetFile> factoryPresetFiles();

inline constexpr std::string_view presetExtension = ".segnopreset";
inline constexpr int presetFormat = 1;
/// The first entry of every module's list: its own parameter defaults.
inline constexpr std::string_view defaultSettingsName = "Default settings";
/// What a module started from your default shows (§9).
inline constexpr std::string_view yourDefaultName = "Default";

/// A preset: a module's state, with what's said about it.
struct Preset
{
    enum class Source
    {
        Builtin, // "Default settings": the module's own defaults
        Factory,
        User,
        Default // your default for new modules (§9)
    };
    Source source = Source::Factory;
    std::string name;
    /// Yours: its folder under the presets root ("Compressor", "Compressor/Drums"; "" at the top).
    std::string folder;
    /// Yours and your default: the file.
    std::filesystem::path file;
    std::vector<std::string> tags;
    std::string description, author;
    DeviceState state;
    /// The file's format is newer than this build reads.
    bool newerFormat = false;

    /// What identifies it among a module's presets, and what a module remembers it came from:
    /// "" (Default settings), "factory/<name>", "user/<folder>/<name>" (the folder may be empty),
    /// "default".
    [[nodiscard]] std::string key() const;
    [[nodiscard]] bool isYours() const noexcept { return source == Source::User; }
};

/// Whether a newer build saved it (a newer file format or module version): it's listed, dimmed,
/// and never loaded.
[[nodiscard]] bool presetTooNew(const DeviceInfo& info, const Preset& preset);

/// The file's text: JSON with the format's version, tags, description, author and the state.
[[nodiscard]] std::string presetFileText(const Preset& preset);
/// Reads a preset file; `name` is the file's name without its extension. Null if it isn't one.
[[nodiscard]] std::optional<Preset> parsePresetFile(std::string_view text, std::string name);

/// What a preset keeps of a state: everything but the module's session keys
/// (DeviceInfo::sessionExtraKeys).
[[nodiscard]] DeviceState presetState(const DeviceInfo& info, DeviceState state);
/// A module's own defaults: every parameter at its default, no extra state.
[[nodiscard]] DeviceState defaultState(const DeviceInfo& info);
/// Whether two states of `info` sound the same: every parameter within a ten-thousandth of its
/// range (a missing one counts as its default) and the same extra state, session keys apart.
[[nodiscard]] bool sameSound(const DeviceInfo& info, const DeviceState& a, const DeviceState& b);
/// The patch that turns extra state `from` into `to` (JSON merge patch: keys `to` lacks become
/// null), leaving session keys alone.
[[nodiscard]] nlohmann::json extraPatch(const DeviceInfo& info, const nlohmann::json& from,
                                        const nlohmann::json& to);

/// Why `name` can't name a preset of yours, or "" if it can: empty, longer than 100 characters,
/// starting or ending with a space or dot, a character file names can't hold, or "Default
/// settings".
[[nodiscard]] std::string presetNameProblem(std::string_view name);

/// Every preset a host offers: the factory set and yours, under `root` (Documents/Segno/Presets),
/// found by the module named inside them. Preset pointers stay valid until the next rescan() or
/// change of files.
class PresetLibrary
{
public:
    PresetLibrary(std::span<const FactoryPresetFile> factory, std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    /// Reads your presets, favourites and defaults again (the list does as it opens).
    void rescan();

    /// A module's presets in list order: Default settings, the factory set by name, then yours
    /// by folder and name.
    [[nodiscard]] std::vector<const Preset*> presetsFor(const DeviceInfo& info) const;
    /// One of them by key (a preset's key()); null if it's gone.
    [[nodiscard]] const Preset* find(const DeviceInfo& info, std::string_view key) const;
    /// The presets of `list` that match every word of `query` (in the name, tags, description or
    /// author; the name counts most) and carry every tag of `tags`; best first, otherwise in
    /// list order. With `favouritesOnly`, only the starred.
    [[nodiscard]] std::vector<const Preset*> filter(const std::vector<const Preset*>& list,
                                                    std::string_view device, std::string_view query,
                                                    const std::set<std::string>& tags,
                                                    bool favouritesOnly) const;
    /// The tags `list` uses, most used first.
    [[nodiscard]] static std::vector<std::string> tagsIn(const std::vector<const Preset*>& list);

    [[nodiscard]] bool isFavourite(std::string_view device, std::string_view key) const;
    /// Stars or unstars; written to Favourites.json at once. False if it couldn't be written.
    bool setFavourite(std::string_view device, std::string_view key, bool favourite);

    /// Your default for new modules of `info`, if you set one (§9).
    [[nodiscard]] const Preset* defaultFor(const DeviceInfo& info) const;
    bool saveDefault(const DeviceInfo& info, const DeviceState& state);
    bool clearDefault(const DeviceInfo& info);

    struct Saved
    {
        const Preset* preset = nullptr; // null: not saved
        std::string problem;            // why not, for people
        bool exists = false;            // refused because one of yours has the name
    };
    /// Saves `state` as a preset of yours in the module's folder (its display name). One of the
    /// same name is refused unless `replace`.
    Saved save(const DeviceInfo& info, const DeviceState& state, const std::string& name,
               std::vector<std::string> tags, std::string description, std::string author, bool replace);
    /// Writes `state` over one of yours, keeping what's said about it.
    Saved overwrite(const DeviceInfo& info, const Preset& preset, const DeviceState& state);
    /// Renames one of yours (in its folder); its favourite star follows it.
    Saved rename(const DeviceInfo& info, const Preset& preset, const std::string& name);
    /// Removes one of yours with `remove` (the host moves the file to the trash).
    bool remove(const DeviceInfo& info, const Preset& preset,
                const std::function<bool(const std::filesystem::path&)>& remove);

private:
    [[nodiscard]] const Preset& builtin(const DeviceInfo& info) const;
    void readFavourites();
    bool writeFavourites() const;
    [[nodiscard]] std::filesystem::path folderFor(const DeviceInfo& info) const;
    Saved writeYours(const DeviceInfo& info, Preset preset, const std::filesystem::path& file);

    std::filesystem::path root_;
    std::vector<Preset> factory_;
    std::vector<Preset> user_, defaults_;
    mutable std::deque<Preset> builtin_; // one per module asked for, made when first asked
    std::map<std::string, std::set<std::string>> favourites_; // device -> preset keys
};

/// What a module remembers about presets, kept by its host beside the device state (never in
/// it, never in a preset): which preset it came from, and its A/B settings (§8).
struct PresetMemo
{
    std::string preset; // the loaded preset's key ("": Default settings)
    bool onB = false;   // B shows
    /// The setting not shown, and the preset it came from (none yet: the same as shown).
    std::optional<DeviceState> other;
    std::string otherPreset;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static PresetMemo fromJson(const nlohmann::json& json);
    [[nodiscard]] bool empty() const noexcept { return preset.empty() && !onB && !other; }
};

/// A module's host, for presets: the plugin adapter and the DAW implement it.
class PresetHost
{
public:
    virtual ~PresetHost() = default;

    [[nodiscard]] virtual PresetLibrary& library() = 0;
    /// The module's whole state now (parameters as the host holds them, and extra state).
    [[nodiscard]] virtual DeviceState state() const = 0;
    /// `state` brought up to the module's version (its migrateState).
    [[nodiscard]] virtual DeviceState migrated(DeviceState state) const { return state; }
    [[nodiscard]] virtual PresetMemo memo() const = 0;
    /// Sets a whole state and the memo as one edit named `undoName` (one undo step in the DAW).
    virtual void apply(const DeviceState& state, const PresetMemo& memo, const std::string& undoName) = 0;
    /// Changes only the memo (after a save, the module came from the saved preset).
    virtual void setMemo(const PresetMemo& memo) = 0;

    /// The list opens: what loads while it's open is one edit. It closes: `keep` false goes back
    /// to what was there before it opened.
    virtual void beginBrowsing() {}
    virtual void endBrowsing(bool /*keep*/) {}

    /// Moves a file to the system's trash.
    virtual bool trash(const std::filesystem::path& file) = 0;
    /// Shows a folder in the file manager.
    virtual void reveal(const std::filesystem::path& /*folder*/) {}
};

/// What a module does with presets, the same in both hosts: the header, the list, the Module
/// menu and assistants all go through it. Cheap to make; holds no state of its own.
class PresetController
{
public:
    PresetController(const DeviceInfo& info, PresetHost& host) : info_(info), host_(host) {}

    [[nodiscard]] const DeviceInfo& info() const noexcept { return info_; }
    [[nodiscard]] PresetLibrary& library() const { return host_.library(); }
    /// The module's presets in list order (the library as last scanned).
    [[nodiscard]] std::vector<const Preset*> list() const;

    /// The preset the module came from; null if its file is gone.
    [[nodiscard]] const Preset* loaded() const;
    /// The name the header shows: the preset's, or "Default settings" / "Default".
    [[nodiscard]] std::string loadedName() const;
    /// Whether the module differs from the preset it came from (false if that's gone).
    [[nodiscard]] bool modified() const;

    /// Loads a preset into the setting shown; one edit, "Load preset <name>". False for a preset
    /// saved by a newer build.
    bool load(const Preset& preset);
    /// Loads the next (+1) or previous (-1) preset of `list` from the loaded one, wrapping.
    bool step(const std::vector<const Preset*>& list, int direction);
    /// Goes back to the loaded preset ("Revert to preset"); false if not modified.
    bool revert();

    /// Saves the module as a preset of yours, which it then came from.
    PresetLibrary::Saved saveAs(const std::string& name, std::vector<std::string> tags,
                                std::string description, std::string author, bool replace);
    /// Writes over the loaded preset if it's yours; otherwise nothing (the UI offers Save as).
    PresetLibrary::Saved save();
    [[nodiscard]] bool canSave() const;
    /// Renames one of yours; a module that came from it follows.
    PresetLibrary::Saved rename(const Preset& preset, const std::string& name);
    /// Moves one of yours to the trash (through the host). A module that came from it keeps its
    /// name, without the modified mark.
    bool remove(const Preset& preset);

    [[nodiscard]] bool onB() const { return host_.memo().onB; }
    /// Shows the other setting; one edit, "Compare A" / "Compare B".
    void switchCompare();
    /// Makes the setting not shown the same as the one shown; one edit.
    void copyCompare();

    /// Your default for new modules: the module's state now (§9).
    bool useAsDefault();
    bool clearDefault();
    [[nodiscard]] bool hasDefault() const;

    /// A new module's first state and memo: your default if you set one, otherwise nothing.
    [[nodiscard]] static std::optional<std::pair<DeviceState, PresetMemo>>
    startingPoint(const DeviceInfo& info, PresetLibrary& library);

private:
    [[nodiscard]] const Preset* byKey(std::string_view key) const;

    const DeviceInfo& info_;
    PresetHost& host_;
};
} // namespace fd
