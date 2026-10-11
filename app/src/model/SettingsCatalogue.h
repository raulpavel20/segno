#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fd::model
{
// Every setting the Settings page shows (docs/design/settings.md §7): its section, name and what
// it does, for the page, its search and the command palette. JUCE-free.

enum class SettingsSection
{
    Audio,
    Midi,
    Recording,
    Library,
    Plugins,
    Export,
    Keys,
    Display,
    Assistants
};
inline constexpr SettingsSection kSettingsSections[] = {
    SettingsSection::Audio,   SettingsSection::Midi,    SettingsSection::Recording,
    SettingsSection::Library, SettingsSection::Plugins, SettingsSection::Export,
    SettingsSection::Keys,    SettingsSection::Display, SettingsSection::Assistants};

[[nodiscard]] std::string sectionName(SettingsSection section);

struct SettingInfo
{
    std::string id; // "audio.buffer"
    SettingsSection section = SettingsSection::Audio;
    std::string name;        // "Buffer size"
    std::string description; // one sentence, under the row
    std::string words;       // more words it's found by ("latency block")
};

[[nodiscard]] const std::vector<SettingInfo>& settingsCatalogue();
[[nodiscard]] const SettingInfo* findSetting(std::string_view id);

/// Whether every word of `query` is in the setting's name, description, section or words
/// (ignoring case). An empty query matches everything.
[[nodiscard]] bool settingMatches(const SettingInfo& setting, std::string_view query);
/// Whether every word of `query` is in `text` (ignoring case), for rows the catalogue doesn't
/// list one by one (an action and its keys).
[[nodiscard]] bool textMatches(std::string_view text, std::string_view query);
/// Where the words of `query` are in `text`: (start, length) pairs, in order, not overlapping,
/// for marking them.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> matchRanges(std::string_view text,
                                                                           std::string_view query);
} // namespace fd::model
