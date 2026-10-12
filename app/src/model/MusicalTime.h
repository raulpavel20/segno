#pragma once

#include "model/TempoMap.h"

#include <optional>
#include <string>

namespace fd::model
{
// Musical time as text (docs/design/mcp.md §3, clip-loops.md §7): positions and lengths as people
// type and read them, for the inspector, the value tags and the assistants' tools. Beats are
// quarter notes. JUCE-free.

/// A length of `beats` at `tempo`: "4 bars", "1 bar", "1 bar 2 beats", "3 beats", "1.5 beats".
[[nodiscard]] std::string lengthText(double beats, const TempoMap& tempo);
/// A position as "bar.beat.sixteenth", 1-based, with any remainder as hundredths of a sixteenth
/// when off the grid ("5.1.1", "5.2.3.50").
[[nodiscard]] std::string positionText(double beats, const TempoMap& tempo);

/// A position from text: "bar.beat.sixteenth" ("5.1.1"; "5" and "5.3" too) or a length from zero
/// ("4 bars"). nullopt if it can't be read.
[[nodiscard]] std::optional<double> positionFromText(const std::string& text, const TempoMap& tempo);
/// A length from text: "2 bars", "3 beats", "1 bar 2 beats", "1/16" (of a whole note), bars, beats
/// and sixteenths as a duration ("1.0.0" is a bar), or a number of beats. nullopt if it can't be
/// read, or isn't longer than zero.
[[nodiscard]] std::optional<double> lengthFromText(const std::string& text, const TempoMap& tempo);
} // namespace fd::model
