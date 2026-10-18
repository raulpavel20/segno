#pragma once

#include "../../midi-shared/dsp/NoteEffect.h"

#include <atomic>
#include <fd/device/Registry.h>

namespace fd::devices
{
/// Arpeggiator (devices/arpeggiator/SPEC.md): plays the notes held one at a time, with step lanes
/// of their own lengths; with no notes held, the song's chord.
class Arpeggiator final : public NoteEffect
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
    static constexpr int maxHeld = 32;
    /// What a lane's step holds before it's drawn: nothing changes.
    [[nodiscard]] static float neutral(int lane) noexcept;
    /// A lane's extra-state key ("lane.velocity").
    [[nodiscard]] static std::string_view laneKey(int lane) noexcept;

    Arpeggiator();
    /// A lane's steps (extra state: devices/arpeggiator/SPEC.md, "Non-parameter state"), read by the
    /// panel, as displays read non-parameter state (ADR 0017).
    [[nodiscard]] float step(int lane, int index) const noexcept
    {
        return lanes_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(index)].load(
            std::memory_order_relaxed);
    }
    void setStep(int lane, int index, float value) noexcept
    {
        lanes_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(index)].store(
            value, std::memory_order_relaxed);
    }
    /// The step each lane plays now, −1 when none (for the display).
    [[nodiscard]] int playingStep(int lane) const noexcept
    {
        return playing_[static_cast<std::size_t>(lane)].load(std::memory_order_relaxed);
    }
    void setPlayingStep(int lane, int index) noexcept
    {
        playing_[static_cast<std::size_t>(lane)].store(index, std::memory_order_relaxed);
    }
    void writeExtraState(nlohmann::json& extra) const override;
    void readExtraState(const nlohmann::json& extra) override;

private:
    struct Held
    {
        std::uint8_t key = 0, channel = 0;
        float velocity = 0.0f;
        std::uint32_t order = 0; // when it was pressed, for Played
        bool down = false;       // held, not only kept by Hold
    };
    struct Note
    {
        int key = 0, channel = 0;
        float velocity = 0.0f;
    };

    void processNotes(ProcessContext& c, const BlockBeats& beats) noexcept override;
    void clearNotes() noexcept override;
    void play(const Event& e, int offset) noexcept;
    /// The next step's sample in this block at or after `from`, its number; INT_MAX if none.
    [[nodiscard]] int nextStep(const BlockBeats& beats, int from, std::int64_t& step) const noexcept;
    void playStep(ProcessContext& c, const BlockBeats& beats, int offset, std::int64_t step) noexcept;
    /// The notes a step chooses from, sorted for the style (as pressed for Played): how many.
    std::size_t notesAt(double beat, std::span<Note> out) const noexcept;
    /// The step the pattern counts from, for a step at `beat` (Retrigger).
    [[nodiscard]] std::int64_t originFor(std::int64_t step, double beat,
                                         const ProcessContext& c) const noexcept;
    [[nodiscard]] double stepBeats() const noexcept;
    [[nodiscard]] double swingBeats(double length) const noexcept;
    void stopped() noexcept;

    std::array<std::array<std::atomic<float>, maxSteps>, LaneCount> lanes_{};
    std::array<std::atomic<int>, LaneCount> playing_{};

    Follower follower_;
    std::array<Held, maxHeld> held_{};
    std::size_t heldCount_ = 0;
    std::uint32_t pressed_ = 0;
    int down_ = 0;
    bool restart_ = false;        // Retrigger on Note: the next step starts the pattern
    std::int64_t noteOrigin_ = 0; // the step it started at
    // Unsynced, its own clock: the next step's time, and how many it has played.
    bool freeRunning_ = false;
    double freeNext_ = 0.0;
    std::int64_t freeSteps_ = 0;
};

const DeviceFactory& arpeggiatorFactory();
} // namespace fd::devices
