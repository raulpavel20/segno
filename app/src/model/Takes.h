#pragma once

#include "model/Snapshot.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fd::model
{
// Take lanes (M2 step C5b, ADR 0031, docs/design/recording.md §6): which parts of each take the
// main lane plays, and choosing among them.

/// The time ranges (seconds) of a take clip that the main lane plays: where a main-lane clip was
/// made from it.
[[nodiscard]] std::vector<std::pair<double, double>> inUse(const ClipInfo& take, const std::vector<ClipInfo>& main);

/// Whether the main lane plays any part of a take lane.
[[nodiscard]] bool isUsed(const TakeInfo& take, const std::vector<ClipInfo>& main);

/// The take lane whose clip the main lane plays at `seconds`, if any.
[[nodiscard]] std::optional<size_t> takeAt(const std::vector<TakeInfo>& takes, const std::vector<ClipInfo>& main,
                                           double seconds);

/// The next take lane (`direction` +1 or -1, wrapping) after `current` that has a clip covering
/// [from, to]; nullopt if none does.
[[nodiscard]] std::optional<size_t> stepTake(const std::vector<TakeInfo>& takes, std::optional<size_t> current,
                                             double from, double to, int direction);

/// The number for a new take: one more than the highest "Take N".
[[nodiscard]] int nextTakeNumber(const std::vector<TakeInfo>& takes);

// ---- takes on clips (ADR 0032, recording.md §6) ------------------------------------------------

/// The take the clip plays (its index), when all its parts play one take; nullopt for a comp
/// (parts playing different takes) or a stack without parts.
[[nodiscard]] std::optional<size_t> playingTake(const TakeStackInfo& stack, const std::vector<ClipInfo>& trackClips);

/// What the clip's title strip ends with: "3/4" (the take it plays and how many), "Comp", or on a
/// narrow clip nothing but the chevron. None below two takes: a single take shows nothing.
struct Badge
{
    std::string text;
    bool comp = false;
};
[[nodiscard]] std::optional<Badge> badge(const TakeStackInfo& stack, const std::vector<ClipInfo>& trackClips, bool narrow);

/// Whether a stack shows takes at all (two or more) and so has a badge and strips.
[[nodiscard]] inline bool hasTakesToShow(const TakeStackInfo& stack) noexcept { return stack.takes.size() >= 2; }

/// How much a track's row grows for its open takes: its tallest open stack, a strip per take.
[[nodiscard]] double takeStripsHeight(const TrackInfo& track, double strip);

/// The take strip at `seconds`, `below` pixels under the clips (strips are newest on top).
struct StripHit
{
    std::uint64_t stack = 0, take = 0;
    bool onClip = false; // over one of the take's clips (not a gap in it)
};
[[nodiscard]] std::optional<StripHit> hitStrip(const TrackInfo& track, double seconds, double below, double strip);

/// The clips of a track as the user sees them: a plain clip alone, or a stack's parts together.
struct ClipGroup
{
    std::vector<const ClipInfo*> parts; // in time order
    const TakeStackInfo* stack = nullptr;
    [[nodiscard]] double start() const noexcept { return parts.front()->start; }
    [[nodiscard]] double end() const noexcept { return parts.back()->end(); }
};
[[nodiscard]] std::vector<ClipGroup> clipGroups(const TrackInfo& track);

/// The clips selected with `clip`: itself, or all of its stack's parts.
[[nodiscard]] std::vector<std::uint64_t> clipGroup(const Snapshot& snapshot, std::uint64_t clip);
/// A selection grown to whole groups.
[[nodiscard]] std::set<std::uint64_t> withGroups(const Snapshot& snapshot, std::set<std::uint64_t> clips);

/// Whether a clip's start (or end) edge is between two parts of its stack: not a trim handle.
[[nodiscard]] bool isInnerEdge(const TrackInfo& track, const ClipInfo& clip, bool startEdge);

/// The take to play instead where a part played one of `deleted` (over [from, to]): the newest
/// other take covering it, else the newest other overlapping it; nullopt if none.
[[nodiscard]] std::optional<size_t> replacementTake(const TakeStackInfo& stack, const std::set<std::uint64_t>& deleted,
                                                    double from, double to);

// ---- comping (recording.md §7) -------------------------------------------------------------

/// What a swipe across a take uses: over [from, to] (either order) within the clip's range, each of
/// the take's clips there, trimmed to it. Where the take has no audio, nothing.
struct SwipePiece
{
    std::uint64_t takeClip = 0;
    double from = 0.0, to = 0.0;
};
[[nodiscard]] std::vector<SwipePiece> swipePieces(const TakeStackInfo& stack, std::uint64_t take, double from, double to);

/// A join between two parts of a clip with takes, within `tolerance` seconds of `seconds`:
/// dragging it moves where one take hands over to the next (recording.md §7).
struct CompJoin
{
    std::uint64_t stack = 0, left = 0, right = 0; // the parts either side
    std::uint64_t leftTake = 0, rightTake = 0;    // the takes they play
    double at = 0.0, from = 0.0, to = 0.0;        // the join, and how far it can move: the parts' far ends
};
[[nodiscard]] std::optional<CompJoin> compJoinAt(const TrackInfo& track, double seconds, double tolerance);

/// Neighbouring parts that play on seamlessly from one take clip (one stretch of it, cut in two),
/// to join into one: each pair (earlier, later), in time order. `parts`: a clip's parts.
[[nodiscard]] std::vector<std::pair<std::uint64_t, std::uint64_t>> seamlessParts(std::vector<ClipInfo> parts);

/// Groups of indices of `spans` that overlap, directly or through others (opening C5b projects:
/// overlapping take lanes become one clip's takes). Each group in index order; groups by first index.
[[nodiscard]] std::vector<std::vector<size_t>> overlapGroups(const std::vector<std::pair<double, double>>& spans);
} // namespace fd::model
