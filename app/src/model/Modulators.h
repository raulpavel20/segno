#pragma once

#include <array>
#include <cstdint>
#include <fd/device/Device.h>
#include <fd/device/Presets.h>
#include <fd/dsp/DrawnCurve.h>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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

/// A section of a modulator's panel (modulators.md §23): its id (stored in view state: never
/// renamed) and title, in order. The panel and assistants (get_module) read them here.
struct ModulatorSection
{
    std::string_view id, title;
};
[[nodiscard]] std::vector<ModulatorSection> modulatorSections(ModulatorKind kind);
/// What each is for, in a sentence (assistants' list_module_types).
[[nodiscard]] std::string_view modulatorPurpose(ModulatorKind kind) noexcept;

/// What a modulator's panel shows that isn't one of its parameters (docs/design/modulators.md
/// §9, §13): how many targets it moves, where it is now, the roll it plays and whether it's kept,
/// what a Follower heard, a Steps pattern. The DAW's Session keeps it; the drawn states stub it.
/// Message thread only.
class ModulatorHost
{
public:
    virtual ~ModulatorHost() = default;
    /// How many targets it moves: its handle's count, and its copies with Per Target.
    [[nodiscard]] virtual int routeCount() const = 0;
    /// Where it is now: cycles (LFO), steps (Random, Steps), milliseconds since it fired
    /// (Envelope), unused for the Follower.
    [[nodiscard]] virtual double position() const = 0;
    /// The Envelope: each copy's milliseconds since it fired, or -1 while it isn't playing (its
    /// chance didn't come up, or it has finished). One without Per Target.
    [[nodiscard]] virtual std::vector<double> playing() const { return {}; }
    /// The roll it plays now (§13), and whether it keeps it.
    [[nodiscard]] virtual std::uint64_t roll() const = 0;
    [[nodiscard]] virtual bool kept() const = 0;
    /// Keep (or Let it roll): one undo step.
    virtual void setKept(bool kept) = 0;
    /// The Follower: the levels it heard, 0..1, oldest first, the last few seconds.
    [[nodiscard]] virtual std::vector<float> heard() const { return {}; }
    /// Steps: each step's value (-1 ... +1) and chance (0..1); setStep changes one, one undo step.
    [[nodiscard]] virtual std::vector<float> stepValues() const { return {}; }
    [[nodiscard]] virtual std::vector<float> stepChances() const { return {}; }
    virtual void setStep(int /*step*/, float /*value*/, float /*chance*/) {}
    /// A drag drawing steps on its display (§12.5): every setStep until it ends is one undo step.
    virtual void beginSteps() {}
    virtual void endSteps() {}
    /// The LFO: the shape it plays while its Shape is Drawn (§12.1); setting it is one undo step.
    [[nodiscard]] virtual dsp::DrawnCurve drawnCurve() const { return {}; }
    virtual void setDrawnCurve(const dsp::DrawnCurve& /*curve*/) {}
};

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
