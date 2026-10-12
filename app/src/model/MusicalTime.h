#pragma once

#include "model/TempoMap.h"

#include <string>

namespace fd::model
{
// Musical time in words (M3 step C10, docs/design/clip-loops.md): lengths as the value tags and the
// inspector show them. JUCE-free.

/// A length of `beats` (quarter notes) at `tempo`: "4 bars", "1 bar", "1 bar 2 beats", "3 beats",
/// "1.5 beats", in the signature's beats.
[[nodiscard]] std::string lengthText(double beats, const TempoMap& tempo);
} // namespace fd::model
