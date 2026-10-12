#pragma once

#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// Clip loops (M3 step C10, ADR 0048, docs/design/clip-loops.md): a stretch of a clip's content that
// it plays again and again for as long as it is. Pure functions: what a looped clip plays where,
// its laps and seams, and where switching or changing a loop puts it.
//
// While a clip loops, its offset is its phase: where in the loop it starts (the engine's own
// meaning). It may arrive unwrapped (a split adds to it); every function here wraps it.

/// A clip's loop: where it starts in the clip's content and how long it is, in seconds of the
/// content (a MIDI clip's at the song's tempo). Length 0: the clip doesn't loop.
struct ClipLoop
{
    double start = 0.0, length = 0.0;

    [[nodiscard]] bool on() const noexcept { return length > 0.0; }
    [[nodiscard]] double end() const noexcept { return start + length; }
    friend auto operator<=>(const ClipLoop&, const ClipLoop&) = default;
};

struct ClipInfo;
struct TrackInfo;
struct Note;
struct TempoMap;

/// Where in its loop a looped clip starts: `offset` wrapped into [0, loop length).
[[nodiscard]] double phaseOf(double offset, const ClipLoop& loop) noexcept;

/// The content (seconds into the source) that a clip starting at `clipStart` with `offset` plays
/// at `seconds` on the timeline: offset + (seconds − clipStart), wrapped into the loop when it
/// loops. Allocation-free.
[[nodiscard]] double contentAt(double clipStart, double offset, const ClipLoop& loop, double seconds) noexcept;
[[nodiscard]] double contentAt(const ClipInfo& clip, double seconds) noexcept;
/// The content a clip plays up to `seconds`: the same, except that where a lap ends there it's the
/// loop's end, not its start (what a clip's end edge or a split's left part ends on).
[[nodiscard]] double contentUpTo(double clipStart, double offset, const ClipLoop& loop, double seconds) noexcept;

/// Where the lap the clip starts in begins on the timeline: its start less its phase (its start
/// if it doesn't loop). Every later lap begins a loop length after the one before.
[[nodiscard]] double lapAnchor(const ClipInfo& clip) noexcept;

/// A stretch of a clip on the timeline that plays its content straight on: [from, to), and the
/// content it plays at `from`. A looped clip has one per lap; a clip that doesn't loop, one.
struct Lap
{
    double from = 0.0, to = 0.0;
    double content = 0.0;

    friend bool operator==(const Lap&, const Lap&) = default;
};
/// The laps of `clip` that overlap [from, to), cut to the clip and to the range.
[[nodiscard]] std::vector<Lap> laps(const ClipInfo& clip, double from, double to);
/// How many laps a clip plays, a part of one counting as one (1 if it doesn't loop).
[[nodiscard]] int lapCount(const ClipInfo& clip) noexcept;

/// Where a lap ends and the next begins inside a clip: its time on the timeline and its number (1
/// is the end of the lap the clip starts in).
struct Seam
{
    double at = 0.0;
    int number = 0;

    friend bool operator==(const Seam&, const Seam&) = default;
};
/// The seams inside `clip` (none if it doesn't loop), in time order.
[[nodiscard]] std::vector<Seam> seams(const ClipInfo& clip);
/// The seam within `tolerance` seconds of `seconds`, if any.
[[nodiscard]] std::optional<Seam> seamAt(const ClipInfo& clip, double seconds, double tolerance);

/// Where a clip goes and what it loops after a loop edit (or a trim): its start, length and
/// offset (the phase while it loops) and its loop (length 0: none).
struct LoopPlacement
{
    double start = 0.0, length = 0.0, offset = 0.0;
    ClipLoop loop;

    friend bool operator==(const LoopPlacement&, const LoopPlacement&) = default;
};
/// The clip as it is.
[[nodiscard]] LoopPlacement placementOf(const ClipInfo& clip) noexcept;
/// Loop on (§8): the loop is what the clip shows now, from its content at its start and as long as
/// it is (never past an audio file's end), so nothing moves or sounds different.
[[nodiscard]] LoopPlacement loopOn(const ClipInfo& clip);
/// Loop off: the repeats go. The clip ends where its first lap ends and plays from the same point.
[[nodiscard]] LoopPlacement loopOff(const ClipInfo& clip);
/// A new loop (the clip view's brace, the inspector): if the content at the clip's start is inside
/// it the clip keeps starting there, otherwise at the loop's start. Its place and length stay. The
/// loop is kept inside an audio file and at least `minClipLength` long.
[[nodiscard]] LoopPlacement withLoop(const ClipInfo& clip, ClipLoop loop);
/// A new loop length (a notch dragged): the loop's start stays, and so does the lap the clip
/// starts in where it can (the phase, wrapped if the loop becomes shorter than it).
[[nodiscard]] LoopPlacement withLoopLength(const ClipInfo& clip, double length);
/// The clip with its start moved to `start` (its end stays) or its end to `end` (its start
/// stays): never before time zero or shorter than `minClipLength`. A clip that doesn't loop stays
/// within its source; a looped one goes anywhere, its content staying where it is on the timeline.
[[nodiscard]] LoopPlacement trimmedStart(const ClipInfo& clip, double start);
[[nodiscard]] LoopPlacement trimmedEnd(const ClipInfo& clip, double end);

/// Why `clip` can't loop on `track` (clip-loops.md §8): a sentence, or empty if it can.
[[nodiscard]] std::string whyCantLoop(const ClipInfo& clip, const TrackInfo& track);

/// Where the clip view puts the clip's content zero on the song's timeline (§6): the lap the clip
/// starts in, moved on a whole number of loops if that would put any of the loop before time zero.
/// A clip that doesn't loop: its start less its offset, as always.
[[nodiscard]] double clipViewOrigin(const ClipInfo& clip) noexcept;
/// Where the clip view shows the song at `seconds` (inside the clip): in the loop, for a looped
/// clip; the same time for one that doesn't loop.
[[nodiscard]] double clipViewTime(const ClipInfo& clip, double seconds) noexcept;

/// A looped MIDI clip's new loop length when `written` notes (those an edit added or moved; beats
/// from the content's zero) end past its loop: to the end of the song bar, as the clip view shows
/// it, that the last of them ends in (the B15 rule, applied to the loop). nullopt leaves it.
[[nodiscard]] std::optional<double> lengthenedLoop(const ClipInfo& clip, const std::vector<Note>& written,
                                                   const TempoMap& tempo);
} // namespace fd::model
