#pragma once

#include "Harmony.h"

#include <array>
#include <cstdint>
#include <fd/device/Device.h>
#include <fd/music/Scales.h>

namespace fd::devices
{
/// The beats of a block: where each sample is in the song, and back. When the song isn't
/// playing a MIDI effect still plays (an arpeggio under your hands): its own beats then run on at
/// the song's tempo from where they were.
struct BlockBeats
{
    double first = 0.0, perSample = 0.0;
    int numSamples = 0;

    [[nodiscard]] double end() const noexcept { return first + perSample * numSamples; }
    [[nodiscard]] double at(int offset) const noexcept { return first + perSample * offset; }
    /// The sample nearest a beat, from the block's start; it's this block's when in [0, numSamples).
    /// Rounded, so a beat on a sample lands on it whatever the blocks (a partition's rounding
    /// differs by far less than half a sample).
    [[nodiscard]] long sampleOf(double beat) const noexcept;
};

/// What a MIDI effect follows (midi-effects.md §5.1; the HARMONY parameters, Harmony.h): the key,
/// and the chord at any moment from Follow's source: the song's (the song view), the listened
/// track's (its notes held), the chord held below the split, or its own. Without one at a place,
/// the key's home chord. Real-time safe.
class Follower
{
public:
    struct Chord
    {
        int root = 0;
        music::PitchSet notes = 0;
        bool fallback = false; // the key's home chord: nothing to follow here
        friend bool operator==(const Chord&, const Chord&) = default;
    };

    /// Reads the HARMONY parameters (from `first`) and the song for this block.
    void begin(const ProcessContext& context, const ParamStore& params, std::size_t first) noexcept;
    void reset() noexcept;

    [[nodiscard]] music::PitchSet key() const noexcept { return key_; }
    [[nodiscard]] int keyRoot() const noexcept { return keyRoot_; }
    [[nodiscard]] Chord chordAt(double beat) const noexcept;
    /// The samples of this block where the song's chord changes (Follow on Song), in order, up to
    /// `out.size()`: how many.
    std::size_t songChanges(const BlockBeats& beats, std::span<int> out) const noexcept;

    /// Follow on Played: a note below the split is the chord's, never played. True if `e` was one.
    bool takePlayed(const Event& e) noexcept;
    /// Whether a note is below the split.
    [[nodiscard]] bool takesKey(int key) const noexcept { return key < split_; }
    /// All notes off: none is held below the split any more (the chord they held stays).
    void endPlayed() noexcept { played_.fill(0); }
    /// Follow on Track: a note the listened track plays or ends.
    void hear(const Event& e) noexcept;
    /// After the notes of a moment are taken or heard: the chord they hold now (the last one held
    /// stays while none is). True if the chord followed changed.
    bool settle() noexcept;
    /// Whether the chord comes from notes held (Track, Played): it changes as they come and go.
    [[nodiscard]] bool followsHeld() const noexcept { return follow_ == 1 || follow_ == 2; }
    [[nodiscard]] int follow() const noexcept { return follow_; }

private:
    [[nodiscard]] static bool heldChord(const std::array<std::uint8_t, 128>& held, Chord& out) noexcept;
    [[nodiscard]] Chord home() const noexcept;

    music::PitchSet key_ = 0x0AB5; // C major until a block says otherwise
    int keyRoot_ = 0;
    int follow_ = 0;
    Chord own_;
    int split_ = 48;
    const SongView* song_ = nullptr;
    std::array<std::uint8_t, 128> played_{}, heard_{}; // notes held, by key, counted
    Chord playedChord_, heardChord_;
    bool hasPlayed_ = false, hasHeard_ = false;
};

/// The own chord's kinds (harmony::chordChoices), as intervals from the root.
[[nodiscard]] music::PitchSet ownChordIntervals(int kind) noexcept;
} // namespace fd::devices
