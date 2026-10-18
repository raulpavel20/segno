#pragma once

#include "model/Automation.h"
#include "model/NoteEditing.h"
#include "model/Sections.h"
#include "model/Selection.h"
#include "model/Snapshot.h"

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace fd::model
{
// A time selection's edits (docs/design/time-selection.md §8–§11, ADR 0073): what a stretch on
// rows does to the lanes' points and the clips' fades. Pure: the Session carries them out; clips
// are cut at a stretch's edges by the clips' own splitting and clearing.

/// A stretch on rows, as the commands take it: tracks and automation lanes.
struct TimeSelection
{
    TimeSpan span;
    std::set<std::uint64_t> tracks, lanes;

    [[nodiscard]] bool empty() const noexcept
    {
        return !(span.end > span.start) || (tracks.empty() && lanes.empty());
    }
    friend bool operator==(const TimeSelection&, const TimeSelection&) = default;
};
/// The time selection a selection holds (empty if it holds none).
[[nodiscard]] TimeSelection timeSelectionOf(const Selection& selection);

// ---- a lane's points over a stretch (§11) ----------------------------------------------------

/// The lane with the stretch cleared: its points inside go, and a point at each edge, at the
/// curve's value there, keeps the curve outside as it was; inside, a line between them. A lane
/// with no point inside the stretch is unchanged.
[[nodiscard]] AutomationPoints clearPoints(const AutomationPoints& points, double start, double end);

/// The stretch's piece of a lane: its points from `start`, its values pinned at both edges, so it
/// carries the curve it had there. Empty for a lane without points.
[[nodiscard]] AutomationPoints piecePoints(const AutomationPoints& points, double start, double end);

/// `piece` (a piecePoints, `length` long) laid over the lane at `at`, replacing what was there:
/// the curve outside keeps its values, pinned by a point at each edge. An empty piece leaves the
/// lane as it is.
[[nodiscard]] AutomationPoints placePoints(const AutomationPoints& points, const AutomationPoints& piece,
                                           double at, double length);

/// The points of a lane inside a stretch, by index (a time selection's points, §11).
[[nodiscard]] std::set<size_t> pointsIn(const AutomationPoints& points, double start, double end);

// ---- Fade to selection (§8) ------------------------------------------------------------------

/// What Fade to selection does on one track: where the stretch lies against each audio clip.
struct StretchFades
{
    struct Edge
    {
        std::uint64_t clip = 0;
        double length = 0.0;
        friend bool operator==(const Edge&, const Edge&) = default;
    };
    struct Seam
    {
        std::uint64_t out = 0; // the clip before the join
        double length = 0.0;   // the stretch's
        double from = 0.0;     // its start against the join (the Crossfade's `from`)
        friend bool operator==(const Seam&, const Seam&) = default;
    };
    std::vector<Edge> fadeIns, fadeOuts;
    std::vector<Seam> crossfades;

    [[nodiscard]] bool empty() const noexcept
    {
        return fadeIns.empty() && fadeOuts.empty() && crossfades.empty();
    }
};
/// For a track's main-lane clips: a stretch from a clip's start (or before it, in a gap) into it
/// fades it in to the stretch's end; one from inside a clip to its end (or past it) fades it out
/// from the stretch's start; one across a join of two audio clips crossfades them over the
/// stretch. A clip wholly inside, a MIDI clip, or a stretch inside a clip changes nothing.
[[nodiscard]] StretchFades stretchFades(const std::vector<ClipInfo>& clips, TimeSpan span);

// ---- time inside a MIDI clip (§14.1) ---------------------------------------------------------
// Times in beats from the clip's content start, as its notes' and controller points'.

/// The commands on a stretch [a, b) inside a clip: the Song menu's, inside the clip.
enum class ClipTimeCommand
{
    Insert,    // empty time as long as the stretch at its start
    Delete,    // the stretch taken out, what's after it moving earlier
    Duplicate, // a copy of it right after it, what's after it moving later
};
/// The plan (Sections.h) a command carries out.
[[nodiscard]] Plan clipTimePlan(ClipTimeCommand command, double a, double b);

/// A plan carried out on a clip's notes: notes crossing its cuts are cut there and each piece goes
/// with its stretch (twice for a copy, not at all when it's gone); the pieces of one note that meet
/// again are one note again, so a note held across deleted time is shortened. The copies are
/// selected.
[[nodiscard]] NoteEdit planNotes(const std::vector<Note>& notes, const Plan& plan);
/// ... and on its controller lanes' points, as on a song's automation lanes (applyPlan): the curve
/// keeps its values, inserted time holding the value where it went in.
[[nodiscard]] std::vector<ControllerLane> planControllers(const std::vector<ControllerLane>& lanes,
                                                          const Plan& plan);
/// Where a clip's edge (its end, or its loop's start or end) at `t` goes: with the time it's in,
/// a duplicate's copy inside what an end edge bounds when the stretch starts before it.
[[nodiscard]] double clipTimeEdge(ClipTimeCommand command, double a, double b, double t, bool isEnd);

/// The notes crossing `a` or `b` cut there (Ctrl+E on a stretch): the selected ones, or every one
/// with none selected. The pieces inside the stretch of the selected notes stay selected.
[[nodiscard]] NoteEdit splitNotesAt(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                                    double a, double b);
} // namespace fd::model
