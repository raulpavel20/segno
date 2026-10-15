#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <fd/music/Detect.h>
#include <string>
#include <vector>

namespace fd::model
{
/// The notes key detection judges (docs/design/key-and-scale.md §11): those of `clips`, or of every
/// MIDI clip when none are given, muted clips left out. Only the notes a clip plays count (those
/// in its loop, or between its edges), once each, their lengths in beats.
[[nodiscard]] std::vector<music::DetectNote>
notesForKeyDetection(const Snapshot& snapshot, const std::vector<std::uint64_t>& clips = {});
/// What audio clips add (audio-editor.md §8.2): each with a key counts as that key's notes held as
/// long as it plays (its loop, or between its edges, in beats), the tonic and the fifth most, in
/// the key its Transpose puts it in. Those of `clips`, or of every audio clip when none are given;
/// muted clips and clips that follow the song's key (they say nothing of their own) left out.
[[nodiscard]] std::array<double, 12> audioForKeyDetection(const Snapshot& snapshot,
                                                          const std::vector<std::uint64_t>& clips = {});

/// The undo step's name for a change of key: "Set key to A minor", or "Remove the key".
[[nodiscard]] std::string keyStepName(const std::optional<music::Key>& key);
} // namespace fd::model
