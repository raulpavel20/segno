#pragma once

#include "mcp/protocol/Registry.h"
#include "model/TempoMap.h"

#include <cstdint>
#include <optional>
#include <string>

namespace fd::mcp
{
/// The conventions every tool follows (docs/design/mcp.md §3). JUCE-free.

/// Ids are strings with a type prefix: "t:12" a track, "c:7" a clip, "m:3" a module, "a:40" an
/// automation lane, "k:51" a take.
enum class IdKind : char
{
    Track = 't',
    Clip = 'c',
    Module = 'm',
    Lane = 'a',
    Take = 'k',
    Section = 's'
};
[[nodiscard]] std::string formatId(IdKind kind, std::uint64_t id);
/// "t:12" (or a bare number) -> 12; nullopt if it isn't an id of that kind.
[[nodiscard]] std::optional<std::uint64_t> parseId(const Json& value, IdKind kind);

/// Positions are in beats (quarter notes) from the song's start. Accepts a number of beats,
/// "bar.beat.sixteenth" ("5.1.1" is bar 5; "5" and "5.3" work too), or a length form below
/// (an offset from the start). nullopt (with `error`) if it can't be read.
[[nodiscard]] std::optional<double> parsePosition(const Json& value, const model::TempoMap& tempo,
                                                  std::string* error = nullptr);
/// Lengths in beats: a number, "2 bars", "3 beats", "1/16" (of a whole note), or bars, beats and
/// sixteenths as a duration ("1.0.0" is a bar, "0.2.0" two beats).
[[nodiscard]] std::optional<double> parseLength(const Json& value, const model::TempoMap& tempo,
                                                std::string* error = nullptr);
/// "bar.beat.sixteenth", 1-based, with any remainder as hundredths of a sixteenth when off the
/// grid ("5.1.1", "5.2.3.50").
[[nodiscard]] std::string formatPosition(double beats, const model::TempoMap& tempo);

/// A position as both forms: {"beats": 16.0, "bbt": "5.1.1"}.
[[nodiscard]] Json positionJson(double beats, const model::TempoMap& tempo);

/// A MIDI note number, or a name with middle C (60) = C3: "C3", "D#4", "Eb2", "c-1".
[[nodiscard]] std::optional<int> parsePitch(const Json& value);
/// The name of a MIDI note number: 60 -> "C3", 61 -> "C#3".
[[nodiscard]] std::string pitchName(int pitch);

/// Velocity 1..127 (as MIDI) <-> the model's 0..1.
[[nodiscard]] int velocityToMidi(float velocity) noexcept;
[[nodiscard]] float velocityFromMidi(double velocity) noexcept;

/// Seconds rounded for results (microseconds).
[[nodiscard]] double roundTime(double value) noexcept;
} // namespace fd::mcp
