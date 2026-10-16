#pragma once

#include "model/Snapshot.h"

#include <bitset>
#include <string>

/// The Drum Kit (docs/design/drum-kit.md, ADR 0064): a rack whose chains are its pads with sounds,
/// a pad for each MIDI note, 16 shown at a time.
namespace fd::model::kit
{
inline constexpr int padCount = 128;
inline constexpr int columns = 4; // pads a row, the lowest note at the left
inline constexpr int shown = 16;  // pads shown at a time: 4 rows of 4
inline constexpr int chokeGroups = 16;
inline constexpr int firstShown = 36; // a new kit shows C1 to D♯2, where the General MIDI drums start

/// The lowest of the 16 pads shown when they're asked to start at `from`: the start of its row of
/// 4, kept so all 16 are pads (0 … 112).
[[nodiscard]] int shownFrom(int from) noexcept;

/// The pad on `note`, if it has a sound (null: it's empty).
[[nodiscard]] const RackChain* padOn(const RackInfo& kit, int note) noexcept;

/// Every note that has a pad with a sound.
[[nodiscard]] std::bitset<padCount> padsWithSounds(const RackInfo& kit) noexcept;

/// A note's name on a pad, C3 = 60 (daw-shell.md): "C1", "C♯1".
[[nodiscard]] std::string noteName(int note);

/// What a folded kit says it holds: "12 pads", "1 pad", "No pads".
[[nodiscard]] std::string contents(const RackInfo& kit);

/// The frame's label under the selected pad's modules (§8): "Pad C1 · Kick 1"; "Pad C1" while the
/// pad is empty or its name is its note's.
[[nodiscard]] std::string frameLabel(const RackInfo& kit);

/// A choke group's name in its choice: "None", "1" … "16".
[[nodiscard]] std::string chokeName(int group);
} // namespace fd::model::kit
