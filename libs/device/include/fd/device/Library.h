#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// The Library (ADR 0055, docs/design/factory-library.md): what Segno ships besides its modules,
/// as read-only files (sample instruments, the factory wavetables), found in the same places by
/// the DAW and the plugins. Presets and projects point into it by `library:` paths, resolved on
/// each machine. Any thread.
namespace fd::library
{
/// What a stored path into the Library starts with: "library:Samples/Strings/…".
inline constexpr std::string_view prefix = "library:";

/// Where it is: the folder the host set (the DAW's Settings > Library), else `SEGNO_LIBRARY`
/// (where there are no Settings: the plugins elsewhere, portable setups, tests), else the first
/// standard place that holds a Library (its `library.json`); empty when there's none.
[[nodiscard]] std::filesystem::path root();
/// The host's choice; empty: the standard places.
void setRoot(std::filesystem::path folder);
/// The standard places, in the order they're looked in: yours, then everyone's; a development
/// build looks last in the source tree's `.cache/library`.
[[nodiscard]] std::vector<std::filesystem::path> standardPlaces();

/// Whether a stored path is a Library reference.
[[nodiscard]] bool isReference(std::string_view stored) noexcept;
/// The file a stored path names: a reference resolved in the Library, anything else as it is.
/// Empty for a reference while there's no Library.
[[nodiscard]] std::filesystem::path resolve(std::string_view stored);
/// What to store for `file`: a reference when it's inside the Library, else its path.
[[nodiscard]] std::string reference(const std::filesystem::path& file);

/// The Library's version (its `library.json`); empty without one.
[[nodiscard]] std::string version();
/// What a missing reference says: where the sound would be and how to get the Library.
[[nodiscard]] std::string missingMessage(std::string_view stored);
} // namespace fd::library
