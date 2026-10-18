#pragma once

#include "model/Snapshot.h"

#include <fd/device/SongView.h>
#include <optional>
#include <vector>

namespace fd::model
{
/// The song's harmony, read from its notes (docs/design/midi-effects.md §5.1, ADR 0074): the chords
/// of the MIDI clips on the tracks that make it (Harmony: Auto counts a track that plays three
/// notes or more together somewhere, never a Drum Kit's; On counts any; Off none), read as the
/// piano roll's chord lane reads a clip, all such tracks' notes together. Named in the song's key,
/// with their pitch classes and bass. A chord changes at most as often as the song allows (Song ▸
/// Chords change): a shorter one joins the chord before it.
[[nodiscard]] std::vector<SongChordInfo> songHarmony(const Snapshot& snapshot);

/// What a MIDI clip plays, in song beats: every lap of a loop, within its span, muted notes left
/// out; with a pass's seed (passSeed), its notes' chance decided as playback decides it. Sorted.
/// None for a muted clip.
[[nodiscard]] std::vector<Note> playedNotes(const ClipInfo& clip, const TempoMap& tempo,
                                            std::optional<std::uint32_t> seed = std::nullopt);

/// The tracks whose notes make the song's harmony now, in track order.
[[nodiscard]] std::vector<std::uint64_t> harmonyTracks(const Snapshot& snapshot);

/// What a MIDI effect on `track` reads (fd::SongView, §5): the song's chords and sections in beats
/// (a section's name without its copy's number, and which time that name plays), and where the
/// track's clips play and what they play. Needs `snapshot.songChords`.
[[nodiscard]] SongView songView(const Snapshot& snapshot, std::uint64_t track);
} // namespace fd::model
