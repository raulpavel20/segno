#pragma once

#include <array>
#include <fd/device/Device.h>
#include <optional>
#include <string_view>

// The modulators (docs/design/modulators.md, ADR 0061): modules that make no sound and move
// other parameters on their track. Each is described as a module is (a DeviceInfo, its
// parameters on the module grid's controls), so the panel kit, presets and assistants take it as
// any module. Their signals are fd::dsp::mod's.
namespace fd::model
{
enum class ModulatorKind : std::uint8_t
{
    Lfo,
    Random,
    Envelope,
    Follower,
    Steps
};
inline constexpr std::array<ModulatorKind, 5> allModulators{ModulatorKind::Lfo, ModulatorKind::Random,
                                                            ModulatorKind::Envelope, ModulatorKind::Follower,
                                                            ModulatorKind::Steps};

/// A modulator's description: its type id (`mod-lfo`, permanent: docs/ids.md), name and
/// parameters.
[[nodiscard]] const DeviceInfo& modulatorInfo(ModulatorKind kind) noexcept;
/// The modulator a module type id names, if it's one.
[[nodiscard]] std::optional<ModulatorKind> modulatorKind(std::string_view typeId) noexcept;
[[nodiscard]] inline bool isModulator(std::string_view typeId) noexcept
{
    return modulatorKind(typeId).has_value();
}

/// What each has (§8, §10): Per Target where each target can have a copy of its own; Both Ways
/// where it swings around the knob; Keep where it rolls dice. The parameters by index.
[[nodiscard]] size_t depthParam(ModulatorKind kind) noexcept;
[[nodiscard]] std::optional<size_t> perTargetParam(ModulatorKind kind) noexcept;
[[nodiscard]] std::optional<size_t> bothWaysParam(ModulatorKind kind) noexcept;
[[nodiscard]] inline bool hasPerTarget(ModulatorKind kind) noexcept
{
    return perTargetParam(kind).has_value();
}
[[nodiscard]] inline bool hasBothWays(ModulatorKind kind) noexcept
{
    return bothWaysParam(kind).has_value();
}
[[nodiscard]] bool hasKeep(ModulatorKind kind) noexcept;

/// Each modulator's parameters, by index into its DeviceInfo's.
namespace modulator
{
struct Lfo
{
    enum Param : size_t
    {
        Shape,
        Rate,
        Sync,
        Mode,
        Phase,
        Slant,
        Smooth,
        Fade,
        Depth,
        BothWays,
        PerTarget,
        Count
    };
};
struct Random
{
    enum Param : size_t
    {
        Rate,
        Sync,
        Smooth,
        Slant,
        Cyclic,
        Jitter,
        Freeze,
        Depth,
        BothWays,
        PerTarget,
        Count
    };
};
struct Envelope
{
    enum Param : size_t
    {
        Trigger,
        Every,
        Legato,
        Delay,
        Attack,
        Hold,
        Decay,
        Sustain,
        Release,
        AttackCurve,
        DecayCurve,
        ReleaseCurve,
        Mode,
        Repeat,
        Chance,
        Fire,
        Depth,
        PerTarget,
        Count
    };
};
struct Follower
{
    enum Param : size_t
    {
        Gain,
        Attack,
        Release,
        Band,
        Low,
        High,
        Depth,
        Count
    };
};
struct Steps
{
    enum Param : size_t
    {
        Rate,
        Sync,
        Length,
        Advance,
        Restart,
        Smooth,
        Jitter,
        Fire,
        Depth,
        BothWays,
        PerTarget,
        Count
    };
    static constexpr int maxSteps = 32;
};
} // namespace modulator
} // namespace fd::model
