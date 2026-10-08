#pragma once

#include "model/TempoMap.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fd::model
{
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
    int colour = 0;      // index into the clip palette
    std::string sourcePath; // audio file, if any

    [[nodiscard]] double end() const noexcept { return start + length; }
};

struct TrackInfo
{
    enum class Kind { Audio, Instrument };

    std::uint64_t id = 0;
    Kind kind = Kind::Audio;
    std::string name;
    int colour = 0;
    std::vector<ClipInfo> clips; // sorted by start
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
