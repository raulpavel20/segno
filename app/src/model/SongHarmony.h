#pragma once

#include "model/Snapshot.h"

#include <vector>

namespace fd::model
{
/// The song's harmony, read from its notes (docs/design/midi-effects.md §5.1, ADR 0074): the chords
/// of the MIDI clips on the tracks that play chords (Harmony Auto: three notes or more together
/// somewhere; never a Drum Kit's), read as the piano roll's chord lane reads a clip, all such
/// tracks' notes together. Named in the song's key. A chord changes at most once a beat: a shorter
/// one joins the chord before it.
[[nodiscard]] std::vector<SongChordInfo> songHarmony(const Snapshot& snapshot);
} // namespace fd::model
