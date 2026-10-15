#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
// Chance that rolls again each time it plays (docs/design/piano-roll.md §26, ADR 0062): a MIDI
// clip's notes left to chance are decided as they play, by a filter on the clip's node (the
// midi-playback-filter patch), from the clip's seed, the pass's roll and the transport loop's
// lap. A clip that keeps its roll plays the one it was heard with, in playback, a bounce and an
// export.

namespace chanceIds
{
/// On a MIDI clip: the roll it keeps (as text: a 64-bit number) and the loop's lap it was heard
/// on; absent: it rolls.
inline const juce::Identifier keptRoll{"fdKeptChance"}, keptLap{"fdKeptChanceLap"};
} // namespace chanceIds

/// What an Edit's clips roll this pass: a new roll each time playback starts (the Session sets
/// it); an Edit without a Session (a render's copy) rolls once.
struct ChancePass
{
    std::atomic<std::uint64_t> roll{1};
};
[[nodiscard]] std::shared_ptr<ChancePass> chancePassOf(const tracktion::Edit& edit);

/// What a clip was heard with last: the pass's roll and the loop's lap (Keep chance as played).
struct ChanceHeard
{
    std::atomic<std::uint64_t> roll{0};
    std::atomic<int> lap{0};
    std::atomic<bool> heard{false};
};
[[nodiscard]] std::shared_ptr<ChanceHeard> chanceHeardOf(const tracktion::Edit& edit, std::uint64_t clip);

/// The roll a clip keeps, if it keeps one.
struct KeptChance
{
    std::uint64_t roll = 0;
    int lap = 0;
};
[[nodiscard]] std::optional<KeptChance> keptChanceOf(const tracktion::Clip& clip);

/// Whether any of a MIDI clip's notes is left to chance (a chance under full, or a spread).
[[nodiscard]] bool usesChance(const tracktion::MidiClip& clip);

/// The filter a MIDI clip's node plays `sequence` (its looped sequence, in `timeBase`) through:
/// null when none of its notes is left to chance.
[[nodiscard]] std::shared_ptr<tracktion::MidiPlaybackFilter>
chanceFilter(tracktion::MidiClip& clip, const juce::MidiMessageSequence& sequence,
             tracktion::MidiList::TimeBase timeBase);
} // namespace fd::engine
