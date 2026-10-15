#pragma once

#include "model/Warp.h"

#include <fd/music/Key.h>
#include <memory>
#include <optional>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::engine
{
// Warped audio clips in the engine (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7, §8.1,
// §22): Segno's warp state on a clip, and the piece player that plays it. Warp is tracktion's
// autoTempo, so the clip's place, length and loop are kept in beats by tracktion; who reads its
// file is Segno's piece player: each piece between pinned moments read from its own start at its
// own speed, with a short crossfade into the next.

/// Segno's warp state on an audio clip (tracktion's autoTempo is Warp itself).
namespace warpIds
{
inline const juce::Identifier tempo{"fdWarpTempo"}, mode{"fdWarpMode"}, tails{"fdWarpTails"},
    formants{"fdFormants"}, sensitivity{"fdSensitivity"};
/// Transposition (semitones and cents), with or without Warp.
inline const juce::Identifier transpose{"fdTranspose"}, fine{"fdFine"};
/// Transients added and removed by hand (§7.4): seconds of the file as recorded, space-separated.
inline const juce::Identifier transientsAdded{"fdTransientsAdded"}, transientsRemoved{"fdTransientsRemoved"};
/// The warp markers: a child of the clip, each marker a child of it (seconds of the file as
/// recorded, beat of the content).
inline const juce::Identifier markers{"FDWARPMARKERS"}, marker{"FDWARPMARKER"}, file{"f"}, beat{"b"};
} // namespace warpIds

/// The clip's key and Follow key (P6c, §8.2, §8.3): a key set by hand ("" for none, else its root
/// and scale ID), or pinned from what's found (`found`) when Follow key went on; and whether it
/// plays in the song's key.
namespace keyIds
{
inline const juce::Identifier key{"fdKey"}, found{"fdKeyFound"}, follow{"fdFollowKey"};
} // namespace keyIds

/// The clip's warping as stored on it (Warp off for a clip that doesn't follow the tempo). A clip
/// made to follow the tempo outside Segno has the tempo of its file's loop metadata.
[[nodiscard]] model::AudioWarp warpOf(const tracktion::AudioClipBase& clip);
/// The same from a clip's state (a take's, kept outside the edit): `fallbackTempo` where it has none.
[[nodiscard]] model::AudioWarp warpOf(const juce::ValueTree& clip, double fallbackTempo);
/// The clip's transposition as stored on it.
[[nodiscard]] model::AudioPitch pitchOf(const juce::ValueTree& clip);
/// The key stored on a clip: nullopt when none is (what's found in its file stands), else the key
/// set or pinned (an empty one: set to none by hand).
[[nodiscard]] std::optional<std::optional<music::Key>> storedKeyOf(const juce::ValueTree& clip);
/// Stores a key on a clip (`found`: pinned from what's found), or removes it (nullopt), undoably.
void writeStoredKey(juce::ValueTree& clip, const std::optional<std::optional<music::Key>>& key, bool found,
                    juce::UndoManager* um);
[[nodiscard]] bool followsKey(const juce::ValueTree& clip);
/// The semitones Follow key adds (§8.3): the clip's stored key moved onto the song's by the
/// smallest step. 0 when it doesn't follow, has no key, the song has none, or it warps in Repitch
/// (whose speed sets its pitch).
[[nodiscard]] int followStepsOf(const tracktion::AudioClipBase& clip);
/// The semitones a clip plays at: its Transpose and Fine, and what Follow key adds.
[[nodiscard]] double playedSemitones(const tracktion::AudioClipBase& clip);
/// Keeps the engine's own transposition (a clip without Warp) to playedSemitones, undoably: after
/// a clip's transposition, key or Follow key changes, and the song's key.
void syncPitch(tracktion::AudioClipBase& clip);

/// Where the clip's content plays its file (model::ContentMap): its warp seen as the file plays,
/// at the song's tempo where it starts.
[[nodiscard]] model::ContentMap contentMapOf(const tracktion::AudioClipBase& clip);
/// Times kept as text on a clip (the transients' edits), and back.
[[nodiscard]] std::vector<double> timesOf(const juce::var& text);
[[nodiscard]] juce::String textOf(const std::vector<double>& times);
/// Keeps the engine's loop metadata to the clip's map: its length in beats is its file's, as
/// warped (what bounds a clip's length while it follows the tempo).
void writeWarpLoopInfo(tracktion::AudioClipBase& clip, const model::AudioWarp& warp);
/// Writes the clip's warp markers (none removes them), undoably.
void writeWarpMarkers(juce::ValueTree& clip, const std::vector<model::WarpMarker>& markers,
                      juce::UndoManager* um);
/// Whether a change to `tree` changes how a clip's file is read: Segno's warp state, its markers.
[[nodiscard]] bool affectsWarp(const juce::ValueTree& tree, const juce::Identifier& id);

/// The piece player for a clip that follows the tempo, over `played` (its range with any
/// extension); nullptr for any other clip. `transients`: its file's, as recorded (seconds), where
/// Beats starts pieces.
[[nodiscard]] std::unique_ptr<tracktion::graph::Node>
makeWarpPlayer(tracktion::AudioClipBase& clip, tracktion::TimeRange played, tracktion::ProcessState& state,
               bool offline, const std::vector<double>& transients);
} // namespace fd::engine
