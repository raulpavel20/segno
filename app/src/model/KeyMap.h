#pragma once

#include <fd/music/Key.h>
#include <optional>

namespace fd::model
{
/// The song's key along the song (docs/design/key-and-scale.md, ADR 0056): one key for the whole
/// song today, or none. Changes of key later replace this type without touching its users, as
/// tempo changes will the TempoMap; so ask for the key at a time.
struct KeyMap
{
    std::optional<music::Key> key;

    /// The key at a song beat (quarter notes).
    [[nodiscard]] const std::optional<music::Key>& keyAt(double /*beat*/) const noexcept { return key; }
    /// The key at the song's start: the one the top bar shows until changes of key come.
    [[nodiscard]] const std::optional<music::Key>& first() const noexcept { return key; }

    friend bool operator==(const KeyMap&, const KeyMap&) = default;
};
} // namespace fd::model
