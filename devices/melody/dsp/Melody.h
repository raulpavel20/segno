#pragma once

#include "../../midi-shared/Harmony.h"

#include <fd/device/Device.h>
#include <fd/device/Registry.h>

namespace fd::devices
{
/// Melody (devices/melody/SPEC.md, an early draft): a step sequencer that also writes, from the
/// song, and changes as it goes. Drawn in step 0 as the generators' example; built in step 2.
class Melody final : public Device
{
public:
    enum Param : std::size_t
    {
        Sync,
        Rate,
        Hz,
        PitchLength,
        VelocityLength,
        GateLength,
        ChanceLength,
        RatchetLength,
        SlideLength,
        Density,
        Leaps,
        Contour,
        Phrase,
        Lowest,
        Highest,
        Change,
        ChangeMode,
        Climb,
        Plays,
        Sections,
        Notes,
        Answer,
        Fill,
        HarmonyFirst,
        Count = HarmonyFirst + harmony::Count
    };
    enum Lane
    {
        PitchLane,
        VelocityLane,
        GateLane,
        ChanceLane,
        RatchetLane,
        SlideLane,
        LaneCount
    };

    static constexpr int maxSteps = 64;
    /// What a lane's step holds before it's drawn: nothing changes.
    [[nodiscard]] static float neutral(int lane) noexcept;

    Melody();
    /// A lane's steps (extra state: devices/melody/SPEC.md, "Non-parameter state"), read by the
    /// panel, as displays read non-parameter state (ADR 0017).
    [[nodiscard]] float step(int lane, int index) const noexcept
    {
        return lanes_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(index)];
    }
    void setStep(int lane, int index, float value) noexcept
    {
        lanes_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(index)] = value;
    }
    /// The step each lane plays now, −1 when none (for the display).
    [[nodiscard]] int playingStep(int lane) const noexcept
    {
        return playing_[static_cast<std::size_t>(lane)];
    }
    void setPlayingStep(int lane, int index) noexcept { playing_[static_cast<std::size_t>(lane)] = index; }
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(ProcessContext& context) noexcept override;

private:
    std::array<std::array<float, maxSteps>, LaneCount> lanes_{};
    std::array<int, LaneCount> playing_{};
};

const DeviceFactory& melodyFactory();
} // namespace fd::devices
