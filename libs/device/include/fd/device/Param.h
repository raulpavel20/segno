#pragma once

#include <array>
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
    /// One plain sentence: what the parameter does, for people (tooltips) and assistants (the
    /// MCP module reference). The same sentence as in the device's SPEC table (open question
    /// B22); the conformance suite compares them.
    std::string_view description{};

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
/// A parameter's description, by id, for describe().
struct ParamDescription
{
    std::string_view id;
    std::string_view text;
};

/// `specs` with each parameter's description from `descriptions` (matched by id).
template <std::size_t N>
[[nodiscard]] constexpr std::array<ParamSpec, N> describe(std::array<ParamSpec, N> specs,
                                                         std::span<const ParamDescription> descriptions)
{
    for (auto& s : specs)
        for (const auto& d : descriptions)
            if (d.id == s.id)
                s.description = d.text;
    return specs;
}

/// True when every parameter has a description (devices static_assert it).
template <std::size_t N>
[[nodiscard]] constexpr bool allDescribed(const std::array<ParamSpec, N>& specs)
{
    for (const auto& s : specs)
        if (s.description.empty())
            return false;
    return true;
}
} // namespace fd
