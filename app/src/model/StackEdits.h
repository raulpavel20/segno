#pragma once

#include "model/Editing.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace fd::model
{
// What clip commands do to the clips' takes (ADR 0032, recording.md §6): a clip and its takes are
// edited as one. Pure; the Session builds the inputs from the engine's state and applies the
// results in the command's undo step.

/// A main-lane clip while editing: where it is, the stack it plays from (0: a plain clip) and the
/// take it plays.
struct ClipPlace
{
    std::uint64_t clip = 0, track = 0, stack = 0;
    double start = 0.0, length = 0.0, offset = 0.0;
    std::uint64_t take = 0;
    [[nodiscard]] double end() const noexcept { return start + length; }
};

/// A stack's range: the clip's (stored, so trimming the clip never trims its takes).
struct StackRange
{
    std::uint64_t id = 0;
    double start = 0.0, end = 0.0;
    friend bool operator==(const StackRange&, const StackRange&) = default;
};

struct StackMove
{
    std::uint64_t stack = 0, toTrack = 0;
    double delta = 0.0;
};

struct StackTrim
{
    std::uint64_t stack = 0;
    std::optional<double> start, end;
};

struct GroupedEdits
{
    std::vector<ClipEdit> edits; // every clip edit to apply, with the parts that follow
    std::vector<StackMove> moves;
    std::vector<StackTrim> trims;
};

/// Clip edits (moves and trims) with their stacks: moving a part moves its whole stack (the other
/// parts follow by the same amount; a stack's first move wins); trimming a stack's first start or
/// its last end trims its range. Inner edges (between parts) pass through. `clips`: every clip on
/// the tracks involved.
[[nodiscard]] GroupedEdits groupEdits(const std::vector<ClipPlace>& clips, const std::vector<ClipEdit>& edits);

struct StackCopy
{
    std::uint64_t stack = 0, toTrack = 0;
    double delta = 0.0;
};

/// Duplicates (placements: ClipEdit::clip is the source) with their stacks: copying a part copies
/// its whole stack once, its other parts by the same amount.
[[nodiscard]] std::pair<std::vector<ClipEdit>, std::vector<StackCopy>> groupPlacements(const std::vector<ClipPlace>& clips,
                                                                                         const std::vector<ClipEdit>& placements);

/// `ids` with their stacks' other parts.
[[nodiscard]] std::vector<std::uint64_t> withParts(const std::vector<ClipPlace>& clips, const std::vector<std::uint64_t>& ids);

/// A split at `at`: the clips it cuts (any part targets its stack's parts; only those strictly
/// containing `at` are cut) and the stacks whose range it cuts.
struct SplitPlan
{
    std::vector<std::uint64_t> clips, stacks;
};
[[nodiscard]] SplitPlan splitPlan(const std::vector<ClipPlace>& clips, const std::vector<StackRange>& stacks,
                                  const std::vector<std::uint64_t>& ids, double at);

/// What a track's stacks become after an edit: a stack without parts goes; parts after a clip
/// that isn't its own become plain (a set of takes always has one clip); clips naming a stack that
/// isn't there become plain; a range widens to cover its parts, and never reaches over another
/// clip (ADR 0037: choosing a take changes only the clip you see). Only changed ranges are listed.
struct StackSettle
{
    std::vector<std::uint64_t> plain, remove;
    std::vector<StackRange> ranges;
};
[[nodiscard]] StackSettle settleStacks(const std::vector<ClipPlace>& trackClips, const std::vector<StackRange>& stacks);

/// What a recording pass over [from, to] does to the clips it overlaps (ADR 0037). It makes its
/// own clip, which cuts the clips it covers in part (ADR 0019); the stacks it covers entirely give
/// that clip their takes; what played over its range is kept as "Before take N" (each clip trimmed
/// to the range), unless that all plays one take of a stack it takes in (it would repeat that
/// take). Within a millisecond counts as covered. `trackClips`: the track's clips but the pass's.
struct PassCover
{
    std::vector<std::uint64_t> join;   // stacks, in time order
    std::vector<std::uint64_t> before; // clips, in time order; none: no "Before take"
};
[[nodiscard]] PassCover passCover(const std::vector<ClipPlace>& trackClips, double from, double to);

/// A clip trimmed to [from, to] (its offset follows its start).
[[nodiscard]] ClipPlace trimPlace(const ClipPlace& clip, double from, double to);
} // namespace fd::model
