#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fd
{
enum class ParamKind : std::uint8_t
{
    Continuous,
    Discrete, // integer steps between min and max
    Choice,   // index into `choices`
    Toggle    // 0 or 1
};

enum class Unit : std::uint8_t
{
    None,
    Decibels,
    Hertz,
    Milliseconds,
    Seconds,
    Percent, // plain value is in percent (0..100, or beyond)
    Semitones,
    Cents,
    Ratio, // displayed as "n:1"
    Pan,   // plain -1..1, displayed 50L..C..50R
    Degrees,
    Note // MIDI note number, displayed as a name; middle C (60) = C3
};

enum class SmoothingKind : std::uint8_t
{
    None,
    Linear,
    Multiplicative
};

struct Smoothing
{
    SmoothingKind kind = SmoothingKind::Linear;
    float ms = 20.0f;
};

namespace ParamFlags
{
inline constexpr std::uint32_t None = 0;
inline constexpr std::uint32_t Automatable = 1u << 0;
inline constexpr std::uint32_t Hidden = 1u << 1;
/// The minimum value means "silence" and is displayed as -inf (e.g. a gain in dB).
inline constexpr std::uint32_t MinusInfinityAtMin = 1u << 2;
} // namespace ParamFlags

/// Static description of one parameter. Each device declares a constexpr table of these.
/// The `id` is permanent once the device's SPEC is approved (docs/ids.md).
///
/// Values are always handled as *plain* values (e.g. -6 dB, 440 Hz). Normalised values
/// (0..1) exist only at host boundaries.
struct ParamSpec
{
    std::string_view id;
    std::string_view name;
    std::string_view shortName;
    ParamKind kind = ParamKind::Continuous;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    /// Plain value shown at the middle of a control's travel. Outside (min, max) = linear.
    /// Same mapping as JUCE's NormalisableRange::setSkewForCentre.
    float skewCentre = 0.0f;
    Unit unit = Unit::None;
    std::span<const std::string_view> choices{};
    Smoothing smoothing{};
    std::uint32_t flags = ParamFlags::Automatable;
    /// Device state version in which this parameter was added (JUCE parameter version hint).
    std::uint16_t sinceVersion = 1;
    /// Decimal places for display.
    std::uint8_t decimals = 1;

    [[nodiscard]] bool isStepped() const noexcept { return kind != ParamKind::Continuous; }
    [[nodiscard]] bool hasFlag(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
    /// Number of discrete values (2 for a toggle), or 0 if continuous.
    [[nodiscard]] int numSteps() const noexcept;

    /// Clamp to [min, max] and round stepped kinds to an integer.
    [[nodiscard]] float sanitize(float plain) const noexcept;
    [[nodiscard]] float toNormalized(float plain) const noexcept;
    [[nodiscard]] float fromNormalized(float normalized) const noexcept;

    /// Display text with unit, e.g. "-6.0 dB", "1.20 kHz", "25L", "On".
    [[nodiscard]] std::string toText(float plain) const;
    /// Parses user-typed text ("-6", "-6 dB", "1.2k", "C", "25R", "on", choice name).
    [[nodiscard]] std::optional<float> fromText(std::string_view text) const;
};
} // namespace fd
