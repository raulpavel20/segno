#pragma once

#include "../../midi-shared/dsp/NoteEffect.h"

#include <fd/device/Registry.h>

namespace fd::devices
{
/// Pitch (devices/pitch/SPEC.md): moves, fits, mirrors, varies and limits notes.
class Pitch final : public NoteEffect
{
public:
    enum Param : std::size_t
    {
        Semitones,
        Steps,
        Mirror,
        Axis,
        Fit,
        Toward,
        Passing,
        Change,
        Chance,
        Spread,
        Way,
        Lowest,
        Highest,
        Outside,
        HarmonyFirst, // then harmony::Offset
        Count = HarmonyFirst + harmony::Count
    };

    Pitch();

private:
    /// A note played and what it became.
    struct Held
    {
        std::int16_t out = -1;  // the note playing, −1 for none
        std::int16_t moved = 0; // before it was fitted: what a chord change fits again
        float velocity = 0.0f;
        std::int8_t tie = -1;
    };

    void processNotes(ProcessContext& c, const BlockBeats& beats) noexcept override;
    void clearNotes() noexcept override;
    void play(ProcessContext& c, const Event& e, const BlockBeats& beats) noexcept;
    /// At a chord change: each note held that no longer fits moves to the note that does.
    void refit(ProcessContext& c, int offset, const BlockBeats& beats) noexcept;

    Follower follower_;
    std::array<Held, 16 * 128> held_{};
    int holding_ = 0;
    int last_ = -1; // the last note played: a tie goes the way the melody moves
};

const DeviceFactory& pitchFactory();
} // namespace fd::devices
