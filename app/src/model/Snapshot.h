#pragma once

#include "model/Automation.h"
#include "model/TempoMap.h"

#include <cstdint>
#include <optional>
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
    bool takesSidechain = false;                  // ADR 0025
    std::optional<std::uint64_t> sidechainSource; // the track feeding it, if any
};

/// The level at the bottom of a fader: silence (-inf dB).
inline constexpr float minusInfinityDb = -100.0f;

/// A track's send to a return track (C3, docs/design/mixer.md).
struct SendInfo
{
    std::uint64_t returnId = 0;
    float levelDb = minusInfinityDb; // -inf (off) .. 0
    bool preFader = false;
};

/// A channel's mixer settings.
struct MixInfo
{
    float volumeDb = 0.0f; // -inf .. +6
    float pan = 0.0f;      // -1 (left) .. 1 (right), a balance
    bool mute = false;
    bool solo = false;
    std::vector<SendInfo> sends; // tracks only: one per return track, in return order
};

/// One automation lane of a channel (C4, ADR 0028, docs/design/automation.md): a parameter's
/// points in the control's units (0..1 of its travel), whether it plays, and what it is.
struct LaneInfo
{
    /// Names the parameter within its channel, as long as it exists: "volume", "pan",
    /// "send/<return id>", or "m<module id>/<parameter id>".
    std::string key;
    std::uint64_t device = 0; // the module (0: the mixer's volume, pan or a send)
    std::string paramId;      // the module's parameter id
    std::string name;         // "Cutoff", "Volume", "Send A"
    std::string module;       // "Synth", "Mixer"
    int steps = 0;            // a stepped parameter's number of values (0: continuous)
    bool on = true;           // off: the parameter keeps the value set by hand (§7)
    AutomationPoints points;

    [[nodiscard]] LaneRules rules() const noexcept { return {.steps = steps}; }
};

struct TrackInfo
{
    enum class Kind { Audio, Instrument };
    /// What the channel is (ADR 0026): a track holds clips; returns and the master don't.
    enum class Role { Track, Return, Master };

    std::uint64_t id = 0;
    Kind kind = Kind::Audio; // Instrument when the chain has one (ADR 0019: one kind of track)
    Role role = Role::Track;
    std::string name;
    std::string instrument; // the first instrument's name, if any
    int colour = 0;
    std::vector<ClipInfo> clips; // sorted by start
    std::vector<DeviceSlot> devices; // the chain, in order (the engine's own mixer plugins are not listed)
    MixInfo mix;
    /// Its automation lanes, in the order they were shown (C4): every parameter with points, and
    /// the empty lanes the user added. Folded lanes are still listed.
    std::vector<LaneInfo> lanes;
    bool lanesFolded = false;

    [[nodiscard]] const LaneInfo* lane(const std::string& key) const noexcept
    {
        for (const auto& l : lanes)
            if (l.key == key)
                return &l;
        return nullptr;
    }

    [[nodiscard]] bool holdsClips() const noexcept { return role == Role::Track; }
};

struct Snapshot
{
    /// The tracks, which hold clips, in order.
    std::vector<TrackInfo> tracks;
    /// The return tracks (C3, ADR 0026), in order, and the master: channels without clips.
    std::vector<TrackInfo> returns;
    TrackInfo master{.role = TrackInfo::Role::Master};
    TempoMap tempo;
    std::uint64_t version = 0;

    /// Any channel by id: a track, a return or the master.
    [[nodiscard]] const TrackInfo* channel(std::uint64_t id) const noexcept
    {
        for (const auto* list : {&tracks, &returns})
            for (const auto& t : *list)
                if (t.id == id)
                    return &t;
        return master.id == id ? &master : nullptr;
    }

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
