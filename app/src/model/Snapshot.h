#pragma once

#include "model/Automation.h"
#include "model/Fades.h"
#include "model/KeyMap.h"
#include "model/Recording.h"
#include "model/TempoMap.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
    double offset = 0.0; // seconds into the source where the clip starts (its phase while it loops)
    double sourceLength = 0.0; // seconds of source material (0 = unbounded, e.g. MIDI)
    /// What it plays again and again (ADR 0048, model/ClipLoops.h): seconds of its content.
    ClipLoop loop;
    /// An audio clip the engine stretches to the song's tempo (placed before ADR 0048): it can't loop.
    bool synced = false;
    int colour = 0;      // index into the clip palette
    std::string sourcePath; // audio file, if any

    // MIDI clips: the notes in time order (noteOrder) and the song beat of their zero.
    std::vector<Note> notes;
    double contentStartBeat = 0.0;
    /// A main-lane clip made from a take (ADR 0031): that take clip's id (0: not from a take).
    std::uint64_t fromTake = 0;
    /// The takes it plays from (ADR 0032): its stack's id (0: a plain clip).
    std::uint64_t stack = 0;
    /// Muted (ADR 0035): it keeps its place and plays nothing.
    bool muted = false;
    /// Audio clips (C6, ADR 0036): its fades as set, the crossfade at its end into the clip that
    /// starts there (as it fits now), and what it plays: fades, edge fades and crossfades together.
    Fade fadeIn, fadeOut;
    std::optional<Crossfade> crossfadeOut;
    ClipPlayback playback;

    [[nodiscard]] double end() const noexcept { return start + length; }
};

struct RackInfo;

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
    bool midiEffect = false; // processes MIDI only (an arpeggiator), never audio
    bool takesSidechain = false;                  // ADR 0025
    std::optional<std::uint64_t> sidechainSource; // the track feeding it, if any
    /// A rack (docs/design/racks.md): its chains and macros. Null for any other module.
    std::shared_ptr<const RackInfo> rack;
};

/// A rack's mapping of a macro to a parameter of a module in it (racks.md §4).
struct MacroMappingInfo
{
    std::uint64_t module = 0;
    std::string param;            // the parameter's id
    std::string paramName;        // as its knob names it
    float from = 0.0f, to = 1.0f; // where the macro's 0 and 100 % take it, normalised
    float curve = 0.0f;           // -1 ... +1: bent toward its start or end; 0 straight
};

/// One of a rack's eight macros.
struct RackMacro
{
    std::string name;   // "Macro 1" until renamed
    float value = 0.0f; // 0 ... 1
    std::vector<MacroMappingInfo> mappings;
};

/// One of a rack's chains: its mixer row and its modules, in order (racks.md §3).
struct RackChain
{
    std::string name; // "Chain 1" until renamed
    bool on = true, solo = false;
    float volumeDb = 0.0f, pan = 0.0f;
    std::vector<DeviceSlot> devices;
};

/// A rack: chains side by side, summed, and eight macros (racks.md, ADR 0056).
struct RackInfo
{
    static constexpr int macroCount = 8;
    std::vector<RackChain> chains;
    std::array<RackMacro, macroCount> macros;
    int shownChain = 0; // the chain its frame shows (view state, kept with the project)
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

/// A parameter driven by an automation lane (C4, ADR 0029, docs/design/automation.md): which
/// one, through which range of its control's travel, and whether it plays.
struct AssignmentInfo
{
    std::uint64_t channel = 0; // a track, a return track or the master
    /// Names the parameter within its channel, as long as it exists: "volume", "pan",
    /// "send/<return id>", or "m<module id>/<parameter id>".
    std::string key;
    AssignmentRange range;
    bool on = true; // off: the parameter keeps the value set by hand (§7)
    std::string name;        // "Cutoff", "Volume", "Send A"
    std::string module;      // "Synth", "Mixer"
    std::string channelName; // "Bass"
    std::uint64_t device = 0; // the module (0: the mixer's volume, pan or a send)
    std::string paramId;      // the module's parameter id
    int steps = 0;            // a stepped parameter's number of values (0: continuous)

    [[nodiscard]] bool stepped() const noexcept { return steps >= 2; }
};

/// An automation lane (ADR 0029): a row of its own among the tracks, holding a curve in 0..1 that
/// drives its assignments.
struct AutomationLaneInfo
{
    std::uint64_t id = 0;
    std::string name; // "Automation 1" when made, or set by hand (tracks-and-clips.md §18)
    int colour = 0;   // index into the clip palette
    std::uint64_t after = 0; // the track it sits after (0: at the top)
    AutomationPoints points;
    std::vector<AssignmentInfo> assignments;

    /// Its name ("Automation lane" until it has one: every lane gets one when made or opened).
    [[nodiscard]] std::string displayName() const;
    /// Steps only for one full-range stepped assignment (§5).
    [[nodiscard]] LaneRules rules() const noexcept;
    /// Every assignment plays (true without assignments).
    [[nodiscard]] bool allOn() const noexcept;
    /// Some assignment is off.
    [[nodiscard]] bool anyOff() const noexcept;
    [[nodiscard]] const AssignmentInfo* assignment(std::uint64_t channel, const std::string& key) const noexcept;
};

/// A take (ADR 0031, recording.md §6): a recording pass or a loop lap, kept with its clip. Its
/// clips don't play; the main lane plays copies of what's chosen.
struct TakeInfo
{
    std::uint64_t id = 0;
    std::string name; // "Take 3", "Before take 3"
    std::vector<ClipInfo> clips; // sorted by start
};

/// A clip's takes (ADR 0032): the stack of takes behind the main-lane clip that plays from them
/// (its parts, more than one when it plays several takes).
struct TakeStackInfo
{
    std::uint64_t id = 0;
    double start = 0.0, end = 0.0; // the clip's range (stored: trimming the clip never trims takes)
    bool open = false;             // its takes are shown under the clip (view state)
    std::vector<TakeInfo> takes;   // oldest first
    std::vector<std::uint64_t> parts; // the main-lane clips playing from it, in time order

    [[nodiscard]] const TakeInfo* take(std::uint64_t takeId) const noexcept
    {
        for (const auto& t : takes)
            if (t.id == takeId)
                return &t;
        return nullptr;
    }
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
    InputInfo input; // tracks only (C5): what it listens to and records, and whether it's armed
    std::vector<TakeStackInfo> stacks; // tracks only: its clips' takes (ADR 0032), by start
    std::uint64_t audition = 0;        // the take heard in place of its clip (ADR 0038); 0: none

    [[nodiscard]] bool holdsClips() const noexcept { return role == Role::Track; }
    [[nodiscard]] const TakeStackInfo* stack(std::uint64_t stackId) const noexcept
    {
        for (const auto& s : stacks)
            if (s.id == stackId)
                return &s;
        return nullptr;
    }
    /// The stack holding a take, if any.
    [[nodiscard]] const TakeStackInfo* stackOfTake(std::uint64_t take) const noexcept
    {
        for (const auto& s : stacks)
            if (s.take(take) != nullptr)
                return &s;
        return nullptr;
    }
    /// A take by id, in any of its stacks.
    [[nodiscard]] const TakeInfo* take(std::uint64_t takeId) const noexcept
    {
        const auto* s = stackOfTake(takeId);
        return s != nullptr ? s->take(takeId) : nullptr;
    }
};

/// The clip palette's colours: a track's, clip's, lane's or section's colour is an index into it.
inline constexpr int kPaletteSize = 16;

/// A section of the song (ADR 0049, docs/design/arranger.md §7): a stretch of it with a name and a
/// colour, drawn on the song row. Sections never overlap; gaps between them are fine.
struct SectionInfo
{
    std::uint64_t id = 0;
    std::string name;
    int colour = 0;                // index into the clip palette
    double start = 0.0, end = 0.0; // seconds on the timeline

    [[nodiscard]] double length() const noexcept { return end - start; }
    friend bool operator==(const SectionInfo&, const SectionInfo&) = default;
};

struct Snapshot
{
    /// The tracks, which hold clips, in order.
    std::vector<TrackInfo> tracks;
    /// The return tracks (C3, ADR 0026), in order, and the master: channels without clips.
    std::vector<TrackInfo> returns;
    TrackInfo master{.role = TrackInfo::Role::Master};
    /// The automation lanes in row order (C4, ADR 0029): those at the top, then each track's.
    std::vector<AutomationLaneInfo> automationLanes;
    /// The song's sections, by start (ADR 0049).
    std::vector<SectionInfo> sections;
    TempoMap tempo;
    /// The song's key (ADR 0057): none until one is chosen.
    KeyMap keys;
    bool edgeFades = true; // the project's edge fades (fades.md §2)
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

    [[nodiscard]] const SectionInfo* section(std::uint64_t id) const noexcept
    {
        for (const auto& s : sections)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    [[nodiscard]] const AutomationLaneInfo* automationLane(std::uint64_t id) const noexcept
    {
        for (const auto& l : automationLanes)
            if (l.id == id)
                return &l;
        return nullptr;
    }

    /// The lane a parameter is assigned to, and its assignment; nulls if it has none.
    [[nodiscard]] std::pair<const AutomationLaneInfo*, const AssignmentInfo*> assignmentOf(std::uint64_t channel,
                                                                                          const std::string& key) const noexcept
    {
        for (const auto& l : automationLanes)
            if (const auto* a = l.assignment(channel, key))
                return {&l, a};
        return {nullptr, nullptr};
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
