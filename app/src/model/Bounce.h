#pragma once

#include "model/Snapshot.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fd::model
{
// Bounce (M2 step C7, ADR 0035, docs/design/bounce.md): which clips can be bounced, how the
// selected ones are rendered (runs per track), where each bounce ends, and what things are named.

enum class BounceMode
{
    InPlace,  // through the clip's instrument (a MIDI clip) or as it plays (audio), on its track
    NewTrack, // through the whole chain before the fader, onto a new track
};

/// Why a clip can't be bounced (§3).
enum class BounceRefusal
{
    None,
    NoInstrument,             // a MIDI clip on a track without an instrument: it's silent (ADR 0033)
    ModulesBeforeInstrument,  // in place: audio modules before the instrument would process the bounce
    Muted,                    // it plays nothing
};

/// What a refusal says (a toast, the MCP error).
[[nodiscard]] std::string refusalText(BounceRefusal refusal);

/// Whether `clip` (a main-lane clip of `track`) can be bounced in `mode`.
[[nodiscard]] BounceRefusal bounceRefusal(const TrackInfo& track, const ClipInfo& clip, BounceMode mode);

/// The longest tail a bounce renders past its clip (§7).
inline constexpr double kBounceTailSeconds = 10.0;
/// Below this a tail is silent (dBFS), and above this a cut tail is audible (§7).
inline constexpr float kBounceSilenceDb = -100.0f;
inline constexpr float kBounceAudibleDb = -60.0f;
/// The fade-out where a clip that isn't bounced cuts a tail (§7).
inline constexpr double kBounceCutFadeSeconds = 0.004;

/// One clip to bounce: a plain clip, or a clip's parts playing its takes (ADR 0032).
struct BouncePiece
{
    std::vector<std::uint64_t> parts; // main-lane clips, in time order
    std::string name;
    int colour = 0;
    double start = 0.0, end = 0.0; // what plays now (seconds)
    /// Where the next clip on its track starts: a bounce in place never runs past it. In a bounce
    /// to a new track only the next piece of the run limits it. Infinity: nothing follows.
    double limit = 0.0;
    std::string limitName; // the clip there (to say whose start cut a tail)
    /// The limit is the next piece of the same run: the tail carries on in that piece's bounce.
    bool limitIsBounced = false;
    /// A stretch of time (time-selection.md §13): its clips crossing `start` or `end` are cut there
    /// first, in the render's copy and, as it lands, in the project, and only what's inside plays.
    bool cutAtEdges = false;
};

/// Pieces on one track rendered in one pass (§7): each within the tail allowance of the one
/// before, of one kind.
struct BounceRun
{
    std::uint64_t track = 0;
    ClipInfo::Kind kind = ClipInfo::Kind::Midi;
    std::vector<BouncePiece> pieces;
    double start = 0.0;     // the first piece's start
    double renderEnd = 0.0; // the last piece's end plus the tail allowance
};

/// The runs for bouncing `clips` (any parts of the clips meant; refused clips left out), in track
/// order then time.
[[nodiscard]] std::vector<BounceRun> bounceRuns(const Snapshot& snapshot, const std::set<std::uint64_t>& clips, BounceMode mode);

/// The runs for bouncing a stretch of time (docs/design/time-selection.md §13): on each of
/// `tracks`, the clips in [from, to] as one piece spanning it, gaps and all, so a bounce in place
/// keeps them as one "Before bounce" take; clips crossing an edge are cut there (`cutAtEdges`).
/// Tracks with nothing there are left out, and so is a track whose clips can't be bounced: a clip
/// refused on its own, a clip with takes in place (its takes would need a take of their own), or
/// MIDI and audio together in place (the audio would pass through the instrument). `why` gets the
/// first track's reason.
[[nodiscard]] std::vector<BounceRun> stretchRuns(const Snapshot& snapshot,
                                                 const std::set<std::uint64_t>& tracks, double from,
                                                 double to, BounceMode mode, std::string* why = nullptr);

/// The run for bouncing a group to a new track (groups.md §11): its whole sound before its fader,
/// one piece named after it from its tracks' first clip to their last one's end (and the tail),
/// whose parts are the clips of every track in it. None when it holds no clips (`why` says so).
[[nodiscard]] std::optional<BounceRun> groupBounceRun(const Snapshot& snapshot, std::uint64_t group,
                                                      std::string* why = nullptr);

/// The tail allowance after a clip: none for an audio clip bounced in place (nothing rings after
/// it), else kBounceTailSeconds.
[[nodiscard]] double tailAllowance(ClipInfo::Kind kind, BounceMode mode) noexcept;

/// A run's rendered level, as the loudest sample per block (linear), from the run's start.
struct BlockLevels
{
    std::vector<float> peaks;
    double blockSeconds = 0.0;
};

/// Where a piece's bounce ends (§7).
struct BounceEnd
{
    double end = 0.0;     // seconds on the timeline
    bool cut = false;     // a clip that isn't bounced cut a tail that was still sounding
    bool audible = false; // ... and what's cut is above kBounceAudibleDb
};
/// The end of `piece` in `run`: after its source's end, where the sound stays below
/// kBounceSilenceDb, within the tail allowance, never past its limit.
[[nodiscard]] BounceEnd bounceEnd(const BounceRun& run, const BouncePiece& piece, const BlockLevels& levels, BounceMode mode);

/// How much of a run's render a file keeps: up to where the last piece's tail falls silent
/// (seconds from the run's start), ignoring limits, so a cut tail can be brought back (§7).
[[nodiscard]] double keptLength(const BounceRun& run, const BlockLevels& levels, BounceMode mode);

/// The take a bounce in place adds (§4): "Bounce", then "Bounce 2", "Bounce 3"...
[[nodiscard]] std::string bounceTakeName(const std::vector<TakeInfo>& takes);
/// The take keeping a plain clip or a comp before its bounce (§4).
inline constexpr const char* kBeforeBounceName = "Before bounce";

/// A bounce file's name, without extension: "Bass - Bass 1 - Bounce".
[[nodiscard]] std::string bounceFileName(const std::string& track, const std::string& clip);

/// The new track of a bounce to a new track (§5): "Bass bounce".
[[nodiscard]] std::string bounceTrackName(const std::string& track);
} // namespace fd::model
