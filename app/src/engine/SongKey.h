#pragma once

#include <cstdint>
#include <fd/device/Device.h>
#include <fd/music/Key.h>
#include <optional>
#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

/// The song's key in an Edit (docs/design/key-and-scale.md §15, ADR 0057). It lives in tracktion's
/// pitch sequence: the first setting's pitch holds the root, and our scale ID is a property of
/// ours beside it ("segnoScale"); without that property the song has no key. tracktion's own scale
/// and sharps fields follow, nearest match, so its features (and ARA's key signatures) agree.
/// Reads the state alone, so it works while the Edit loads.
[[nodiscard]] std::optional<music::Key> songKeyOf(const te::Edit& edit);

/// Writes the song's key, or removes it, in the Edit's undo manager's current step.
void writeSongKey(te::Edit& edit, const std::optional<music::Key>& key);

/// What modules see (TransportInfo::key).
[[nodiscard]] KeyInfo keyInfoOf(const std::optional<music::Key>& key) noexcept;

/// A KeyInfo in 32 bits, for an atomic read on the audio thread: the notes in bits 0–11, the root
/// plus one in bits 12–15 (0: no key).
[[nodiscard]] constexpr std::uint32_t packKey(KeyInfo key) noexcept
{
    return static_cast<std::uint32_t>(key.notes & 0xfffu) | (static_cast<std::uint32_t>(key.root + 1) << 12);
}
[[nodiscard]] constexpr KeyInfo unpackKey(std::uint32_t packed) noexcept
{
    return {static_cast<int>((packed >> 12) & 0xfu) - 1, static_cast<std::uint16_t>(packed & 0xfffu)};
}

/// The tree a module watches for changes of the song's key.
[[nodiscard]] juce::ValueTree songKeyState(const te::Edit& edit);
} // namespace fd::engine
