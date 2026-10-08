#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fd::model
{
/// Fuzzy match for the command palette (C1): `query`'s characters in order in `text`
/// (case-insensitive), scored so that word starts, consecutive letters and prefixes rank
/// higher. nullopt when it doesn't match. An empty query matches everything with score 0.
[[nodiscard]] std::optional<int> fuzzyScore(std::string_view query, std::string_view text);

struct PaletteItem
{
    std::string id;
    std::string name;
    std::string category;
};

/// Indices of `items` matching `query`, best first. Ties go to the more recently used
/// (`recent`, most recent first), then alphabetical. An empty query lists recent items first.
[[nodiscard]] std::vector<size_t> rankPalette(std::string_view query, const std::vector<PaletteItem>& items,
                                              const std::vector<std::string>& recent);
} // namespace fd::model
