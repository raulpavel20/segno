#pragma once

#include "model/TempoMap.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fd::model
{
/// A note in a MIDI clip (C2). Times are in beats (quarter notes) from the clip's content start:
/// the zero of its notes, which is the clip's start minus its offset.
struct Note
{
    int pitch = 60;
    double start = 0.0;
    double length = 0.25;
    float velocity = 100.0f / 127.0f; // 0..1, in steps of 1/127 (MIDI)

    friend bool operator==(const Note&, const Note&) = default;
};

/// What the arrangement draws, published by engine::Session after every change (ADR 0018).
/// Immutable once published; plain data only (no tracktion or JUCE types).
struct ClipInfo
{
    enum class Kind { Audio, Midi };

    std::uint64_t id = 0;
    Kind kind = Kind::Audio;
    std::string name;
    double start = 0.0;  // seconds on the timeline
    double length = 0.0; // seconds
    double offset = 0.0; // seconds into the source where the clip starts
    double sourceLength = 0.0; // seconds of source material (0 = unbounded, e.g. MIDI)
    int colour = 0;      // index into the clip palette
    std::string sourcePath; // audio file, if any

    // MIDI clips: the notes in time order (noteOrder) and the song beat of their zero.
    std::vector<Note> notes;
    double contentStartBeat = 0.0;

    [[nodiscard]] double end() const noexcept { return start + length; }
};

/// A device in a track's chain (C6): ours or a third-party plugin.
struct DeviceSlot
{
    enum class Kind { Native, External };

    std::uint64_t id = 0;
    Kind kind = Kind::Native;
    std::string name;
    std::string deviceId; // our device id ("eq"), empty for third-party plugins
    bool enabled = true;
    bool instrument = false;
};

struct TrackInfo
{
    enum class Kind { Audio, Instrument };

    std::uint64_t id = 0;
    Kind kind = Kind::Audio; // Instrument when the chain has one (ADR 0019: one kind of track)
    std::string name;
    std::string instrument; // the first instrument's name, if any
    int colour = 0;
    std::vector<ClipInfo> clips; // sorted by start
    std::vector<DeviceSlot> devices; // the chain, in order (the engine's own mixer plugins are not listed)
};

struct Snapshot
{
    std::vector<TrackInfo> tracks;
    TempoMap tempo;
    std::uint64_t version = 0;

    /// End of the last clip (seconds), 0 for an empty project.
    [[nodiscard]] double contentEnd() const noexcept
    {
        double end = 0.0;
        for (const auto& t : tracks)
            for (const auto& c : t.clips)
                end = c.end() > end ? c.end() : end;
        return end;
    }
};
} // namespace fd::model
