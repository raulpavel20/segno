#pragma once

#include "../../midi-shared/Harmony.h"

#include <fd/device/Device.h>
#include <fd/device/Registry.h>

namespace fd::devices
{
/// Arpeggiator (devices/arpeggiator/SPEC.md): plays the notes held one at a time, with step lanes
/// of their own lengths; with no notes held, the song's chord. Drawn in step 0; its processing
/// comes with step 1.
class Arpeggiator final : public Device
{
public:
    enum Param : std::size_t
    {
        Style,
        Sync,
        Rate,
        Hz,
        Gate,
        Swing,
        Steps,
        Distance,
        InKey,
        Retrigger,
        Hold,
        SongChords,
        From,
        VelocityLength,
        GateLength,
        OctaveLength,
        RatchetLength,
        ChanceLength,
        HarmonyFirst,
        Count = HarmonyFirst + harmony::Count
    };
    /// The step lanes, in the display's order.
    enum Lane
    {
        VelocityLane,
        GateLane,
        OctaveLane,
        RatchetLane,
        ChanceLane,
        LaneCount
    };

    static constexpr int maxSteps = 16;
    /// What a lane's step holds before it's drawn: nothing changes.
    [[nodiscard]] static float neutral(int lane) noexcept;

    Arpeggiator();
    /// A lane's steps (extra state: devices/arpeggiator/SPEC.md, "Non-parameter state"), read by the
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

const DeviceFactory& arpeggiatorFactory();
} // namespace fd::devices
