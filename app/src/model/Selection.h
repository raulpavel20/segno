#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace fd::model
{
/// A stretch of the song's time, in seconds (docs/design/time-selection.md, ADR 0073).
struct TimeSpan
{
    double start = 0.0, end = 0.0;
    [[nodiscard]] double length() const noexcept { return end - start; }
    friend bool operator==(const TimeSpan&, const TimeSpan&) = default;
};

/// The window's one selection (M2 step A5, docs/design/selection.md, ADR 0027): a channel and
/// clips, shared by the arrangement and the mixer view. The channel is always the channel of what
/// was selected last, so a selected track and a selected clip never disagree; withClips() and
/// pruned() keep it that way.
struct Selection
{
    std::optional<std::uint64_t> channel; // a track, a return track or the master
    std::set<std::uint64_t> clips;
    /// An automation lane (its id), and points on it by index (C4, docs/design/automation.md §9).
    /// Never together with clips; the channel is then its first assignment's.
    std::optional<std::uint64_t> lane;
    std::set<size_t> points;
    /// Takes of the channel's clips (recording.md §6, §7): one, or several (Ctrl+click). Never
    /// together with clips or a lane.
    std::set<std::uint64_t> takes;
    /// Sections of the song (ADR 0049, arranger.md §10): a run of them. Never together with clips,
    /// a lane or takes; the channel is then the master.
    std::set<std::uint64_t> sections;
    /// Rows chosen together (time-selection.md §4): tracks and automation lanes, next to each
    /// other or not. With `time` it's a time selection on them, never together with clips, a
    /// lane's points, takes or sections; the channel is the row it was started on (a lane's: its
    /// first assignment's). A group's row brings every row in it (groups.md §6.5). Without `time`,
    /// several selected tracks and groups (groups.md §8).
    std::set<std::uint64_t> rowTracks, rowLanes;
    std::optional<TimeSpan> time;

    /// A time selection: a stretch on at least one row.
    [[nodiscard]] bool hasTime() const noexcept
    {
        return time.has_value() && (!rowTracks.empty() || !rowLanes.empty());
    }

    friend bool operator==(const Selection&, const Selection&) = default;
};

/// Clips and their channel: `prefer` if it holds one of them, else the topmost track that holds
/// one, else `otherwise` (no clips, or none in the snapshot yet).
[[nodiscard]] Selection withClips(const Snapshot& snapshot, std::set<std::uint64_t> clips,
                                  std::optional<std::uint64_t> prefer, std::optional<std::uint64_t> otherwise);

/// Sections, with the master as their channel (arranger.md §10).
[[nodiscard]] Selection withSections(const Snapshot& snapshot, std::set<std::uint64_t> sections);

/// A time selection (time-selection.md §4): `span` (start before end) on the rows that exist of
/// `tracks` (a group with everything in it) and `lanes`, its channel `channel` if it's one of them,
/// else the topmost track's, else the first lane's first assignment's. An empty selection if no
/// row exists or the span is empty.
[[nodiscard]] Selection withTime(const Snapshot& snapshot, TimeSpan span,
                                 const std::set<std::uint64_t>& tracks, const std::set<std::uint64_t>& lanes,
                                 std::optional<std::uint64_t> channel = std::nullopt);

/// Several tracks and groups (groups.md §8). Ctrl+click: `id` in or out of them, the channel the
/// one clicked last (if `id` goes, the last of those left, as listed in the snapshot); one left is
/// that channel alone. A return or the master is selected alone.
[[nodiscard]] Selection withTrackToggled(const Snapshot& snapshot, const Selection& selection,
                                         std::uint64_t id);
/// Shift+click: the run of `shown` (the tracks and groups as their rows show, in order; lanes
/// aren't in it) from the channel selected last to `id`, the channel `id`.
[[nodiscard]] Selection withTrackRun(const Snapshot& snapshot, const Selection& selection, std::uint64_t id,
                                     const std::vector<std::uint64_t>& shown);
/// The tracks and groups a command on several acts on (groups.md §8), in the snapshot's order:
/// those chosen together, else the channel when it's a track or group; none for a time selection.
[[nodiscard]] std::vector<std::uint64_t> selectedTracks(const Snapshot& snapshot, const Selection& selection);

/// A lane and points on it, with its first assignment's channel (none if it has none).
[[nodiscard]] Selection withLane(const Snapshot& snapshot, std::uint64_t lane, std::set<size_t> points = {});

/// The selection after the project changed: clips, channels, lanes and points that are gone are
/// dropped, and if the channel no longer holds any of the clips (they moved), it follows them.
[[nodiscard]] Selection pruned(const Selection& selection, const Snapshot& snapshot);

/// The view in the centre of the window.
enum class CentreView { Arrange, Mix };

/// What the context panels show (selection.md §3).
enum class Subject
{
    Nothing,
    Channel,
    Clip,
    Clips,
    Lane,
    Take,
    Takes,
    Section,
    Sections,
    Time // a time selection (time-selection.md §7)
};

/// The inspector's sections, top to bottom (selection.md §5).
enum class InspectorSection
{
    Point,
    Points,
    Lane,
    Assignments,
    Take,
    Takes,
    Clip,
    Audio, // one audio clip's sound (audio-editor.md §12), under its CLIP
    Clips,
    Notes, // the clip view's selected notes (piano-roll.md §9.6), added by the inspector
    Section,
    Sections,
    Time,   // a time selection's Start, End, Length and rows (time-selection.md §7)
    Tracks, // several selected tracks: how many (groups.md §8)
    Track,
    Group, // a group's Name, Colour, Tracks and Modules (groups.md §10)
    Input,
    Return,
    Master,
    Mix,
    Sends,
    Module, // the module selected in the chain (selection.md §8.2), added by the shell
    Routes  // a selected modulator's routes, under MODULE
};

/// Everything the context panels need for a selection in a view: the rules of selection.md §3-5
/// in one place.
struct PanelContext
{
    Subject subject = Subject::Nothing;
    std::optional<TrackInfo> channel; // the chain below, the highlighted strip, the inspector
    std::optional<ClipInfo> clip;     // the one selected clip
    int clipCount = 0;                // selected clips in the snapshot
    std::optional<ClipInfo> midiClip; // the one selected clip when it's MIDI: the clip view's
    std::optional<AutomationLaneInfo> lane; // the selected lane
    std::optional<TakeInfo> take;           // the one selected take
    int takeCount = 0;                      // selected takes
    std::optional<AutomationPoint> point; // its one selected point
    int pointCount = 0;               // its selected points
    std::optional<SectionInfo> section; // the one selected section
    int sectionCount = 0;               // selected sections
    std::optional<TimeSpan> time;       // a time selection's stretch
    int timeTracks = 0, timeLanes = 0;  // ... and its rows
    int trackCount = 0;                 // several selected tracks and groups (groups.md §8)
    std::optional<TrackInfo> group;     // the group the channel is in (its TRACK's Group row)
    int groupTracks = 0;                // a group channel: the tracks it holds, at any depth

    bool clipViewOffered = false; // the detail panel has its Clip tab (the arrangement only)
    bool opensClipView =
        false; // a change to this subject opens the clip view (in the arrangement): one clip, of
               // either kind (the piano roll, or the audio editor: audio-editor.md §4)
    std::optional<std::uint64_t> midiInputTarget; // live MIDI: the channel, if a track with an instrument
    std::vector<InspectorSection> inspector;      // empty: no inspector content

    /// What identifies the subject: when it changes, the detail panel picks its mode again. It
    /// includes the clip, so a clip selected before its snapshot arrived counts once it does.
    struct Key
    {
        Selection selection;
        std::optional<std::uint64_t> clip;

        friend bool operator==(const Key&, const Key&) = default;
    } key;
};

[[nodiscard]] PanelContext panelContext(const Selection& selection, const Snapshot& snapshot, CentreView view);
} // namespace fd::model
