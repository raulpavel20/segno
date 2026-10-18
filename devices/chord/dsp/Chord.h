#pragma once

#include "../../midi-shared/dsp/NoteEffect.h"

#include <atomic>
#include <fd/device/Registry.h>

namespace fd::devices
{
/// Chord (devices/chord/SPEC.md): adds notes at intervals, plays the song's chord, or the key's
/// chord for each key; strums them.
class Chord final : public NoteEffect
{
public:
    enum Param : std::size_t
    {
        Mode,
        Shift1,
        Shift6 = Shift1 + 5,
        InKey,
        Level,
        Kind,
        Voicing,
        Place,
        Change,
        Around,
        Strum,
        Curve,
        Swell,
        Direction,
        HarmonyFirst,
        Count = HarmonyFirst + harmony::Count
    };
    enum ModeChoice
    {
        Add,
        Song,
        Keys
    };

    static constexpr int maxNotes = 8;

    Chord();
    /// The notes held coming in, low to high, up to `out.size()`: how many (Learn reads them, off
    /// the audio thread).
    std::size_t heldNotes(std::span<int> out) const noexcept;

private:
    /// A note played and the chord it started.
    struct Source
    {
        std::array<std::uint8_t, maxNotes> notes{};
        std::uint8_t count = 0;
        float velocity = 0.0f;
        bool active = false;
    };

    void processNotes(ProcessContext& c, const BlockBeats& beats) noexcept override;
    void clearNotes() noexcept override;
    void play(ProcessContext& c, const Event& e, const BlockBeats& beats) noexcept;
    /// The chord a note plays at a beat, low to high, each with its velocity: how many.
    std::size_t chordFor(int key, float velocity, double beat, std::span<int> notes,
                         std::span<float> velocities) noexcept;
    /// Starts a source's chord at `offset`, strummed.
    void start(ProcessContext& c, std::size_t source, int offset, std::span<const int> notes,
               std::span<const float> velocities) noexcept;
    /// Ends a source's chord: the notes it started, and those of its strum still to come.
    void stop(ProcessContext& c, std::size_t source, int offset) noexcept;
    /// At the song's chord change (Song, At a change on Move): each chord held moves to the new one.
    void move(ProcessContext& c, int offset, const BlockBeats& beats) noexcept;
    void publishHeld() noexcept;

    Follower follower_;
    std::array<Source, 16 * 128> sources_{};
    int holding_ = 0;
    std::array<int, 16> previous_{}; // the last chord played, for voice leading
    std::size_t previousCount_ = 0;
    bool down_ = false; // Alt: the next strum's way
    std::array<std::uint8_t, 128> heldIn_{};
    std::array<std::atomic<std::uint64_t>, 2> heldMask_{};
};

const DeviceFactory& chordFactory();
} // namespace fd::devices
