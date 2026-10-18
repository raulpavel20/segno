#pragma once

#include <cstdint>
#include <fd/device/Device.h>
#include <span>
#include <vector>

namespace fd::devices
{
/// A stretch of the song a chain of MIDI effects runs over, off the audio thread (Keep as notes,
/// midi-effects.md §9.1): its tempo, bar and key, the song it reads, the notes each module hears.
struct NoteRun
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    TransportInfo transport; // tempo, time signature and key; where it is and that it plays, set as it runs
    const SongView* song = nullptr;
    double from = 0.0, to = 0.0; // song beats
    /// For each module of the chain, the notes of the track it listens to (null: none).
    std::span<const std::vector<SongNote>* const> listen{};
    /// For each module, the roll it plays (ADR 0062: the one heard); none, its own.
    std::span<const std::uint64_t> rolls{};
};

/// `notes` (song beats) through `chain`, MIDI effects each, block by block as a host plays them:
/// what the last one plays from `run.from` to `run.to`, by start, a note still sounding at the end
/// cut there. The modules are prepared (so reset) by it; give it fresh ones set as you want them.
/// Not real-time: it allocates.
[[nodiscard]] std::vector<SongNote> runNotes(std::span<Device* const> chain, std::span<const SongNote> notes,
                                             const NoteRun& run);
} // namespace fd::devices
