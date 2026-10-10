#pragma once

#include "model/Snapshot.h"
#include "model/Timeline.h"
#include "model/TrackLayout.h"

#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace fd::model
{
/// Clip editing in the arrangement (C4): what the mouse is over, and where clips end up for a
/// gesture. Pure functions; the view previews their result and the Session applies it.

struct ClipHit
{
    enum class Zone { Body, StartEdge, EndEdge };
    std::uint64_t clip = 0;
    int track = 0;
    Zone zone = Zone::Body;
};

/// The clip under (x, y): x in lane pixels (0 = timeline x 0), y in view pixels from the top of
/// the lanes. The edges are the outer `edgePixels` of a clip (less for very short clips); an edge
/// between two parts of one clip's takes is its body (ADR 0032). Below a row's own height (its
/// open takes) there are no clips.
[[nodiscard]] std::optional<ClipHit> hitClip(const Snapshot& snapshot, const TrackLayout& layout,
                                             const Timeline& timeline, double x, double y, double edgePixels = 6.0);
/// The same where rows aren't all tracks (lanes, returns): `rowTracks[row]` is the row's track
/// index in the snapshot, or -1 for a row without clips. ClipHit::track is the track index.
[[nodiscard]] std::optional<ClipHit> hitClip(const Snapshot& snapshot, const TrackLayout& layout,
                                             const std::vector<int>& rowTracks, const Timeline& timeline, double x,
                                             double y, double edgePixels = 6.0);

/// Where a clip goes: its track and placement.
struct ClipEdit
{
    std::uint64_t clip = 0; // the clip (or, for duplicates, the source clip)
    std::uint64_t track = 0;
    double start = 0.0, length = 0.0, offset = 0.0;
};

enum class DragKind { Move, TrimStart, TrimEnd };

struct Drag
{
    DragKind kind = DragKind::Move;
    std::uint64_t anchor = 0;  // the grabbed clip: it snaps; the others follow by the same amount
    double deltaSeconds = 0.0; // mouse movement in time
    int deltaTracks = 0;       // mouse movement in tracks (moves only)
};

/// Smallest clip a trim can leave.
inline constexpr double minClipLength = 0.01;

/// Placements of the selected clips for a drag. `snap` maps a time to the grid (identity when
/// snapping is off). Constraints: nothing before time zero or outside the track list; trims
/// stay within the source material and never shorter than minClipLength. Trims leave the edges
/// between a clip's parts alone (only its outer edges trim).
[[nodiscard]] std::vector<ClipEdit> dragClips(const Snapshot& snapshot, const std::set<std::uint64_t>& selection,
                                              const Drag& drag, const std::function<double(double)>& snap);

/// Clips overlapping the time range [t0, t1] on tracks [track0, track1] (any order).
[[nodiscard]] std::vector<std::uint64_t> clipsInBox(const Snapshot& snapshot, double t0, double t1, int track0,
                                                    int track1);

/// Placements for copies of the selection, right after it on the same tracks.
[[nodiscard]] std::vector<ClipEdit> duplicatePlacements(const Snapshot& snapshot,
                                                        const std::set<std::uint64_t>& selection);

/// What placing a clip over [start, end) does to the other clips on its track (ADR 0019: no
/// overlaps; the placed clip wins).
struct ClipCut
{
    enum class Kind
    {
        Remove,     // fully covered
        Trim,       // partly covered: keeps start/length/offset below
        SplitAround // covered in its middle: keeps [clip start, from) and [to, clip end)
    };
    std::uint64_t clip = 0;
    Kind kind = Kind::Remove;
    double start = 0.0, length = 0.0, offset = 0.0; // Trim
    double from = 0.0, to = 0.0;                     // SplitAround
};

/// Cuts for the clips in `clips` (one track) that overlap [start, end), ignoring `except`
/// (the clips being placed). Overlaps under a microsecond are ignored.
[[nodiscard]] std::vector<ClipCut> clearRange(const std::vector<ClipInfo>& clips, double start, double end,
                                              const std::set<std::uint64_t>& except);

/// The clip and its track index, if the id exists.
[[nodiscard]] std::optional<std::pair<const ClipInfo*, int>> findClip(const Snapshot& snapshot, std::uint64_t id);
} // namespace fd::model
