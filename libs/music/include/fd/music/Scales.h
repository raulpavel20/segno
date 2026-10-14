#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace fd::music
{
/// A set of pitch classes, one bit each: bit n is pitch class n (0 = C … 11 = B), or, for a
/// scale's intervals, n semitones above its root.
using PitchSet = std::uint16_t;

/// A scale of the catalogue (docs/design/key-and-scale.md §5, ADR 0056). The ID is permanent
/// (docs/ids.md: it's stored in projects); the names can change.
struct Scale
{
    std::string_view id;        // "harmonic-minor"
    std::string_view name;      // "Harmonic minor"
    std::string_view shortName; // "harm min", where the name doesn't fit
    std::string_view group;     // "Common"
    PitchSet intervals = 1;     // bit n: n semitones above the root (bit 0 always set)
    /// Semitones from the root of the major scale whose key signature it's written with, up to
    /// this scale's root: 0 for major-like scales, 9 for minor-like ones (a minor key takes its
    /// relative major's signature), 2 for Dorian (D Dorian is written in C major).
    int signatureMode = 0;
    /// Its name is a proper noun, kept capitalised in a key's name ("D Dorian", but "C major").
    bool properName = false;
    /// Another name the search finds ("Bhairav").
    std::string_view alsoCalled = {};
};

/// Every scale, in the catalogue's order: by group, then as listed in the design.
[[nodiscard]] std::span<const Scale> scales() noexcept;

/// The groups in their order ("Common", "Modes", …).
[[nodiscard]] std::span<const std::string_view> scaleGroups() noexcept;

/// A scale by its ID, or by its name or other name (any case); null if there's none.
[[nodiscard]] const Scale* findScale(std::string_view idOrName) noexcept;

/// How many notes a set holds.
[[nodiscard]] int noteCount(PitchSet set) noexcept;
} // namespace fd::music
