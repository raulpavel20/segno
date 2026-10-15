#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fd::model
{
// Warping (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7, §8.1, §22): an audio clip that
// follows the song's tempo, its file laid out in beats. The map from the beats of its content to
// the time of its file, pinned by warp markers; its mode; its transposition. Pure functions: the
// engine's piece player plays the map, the editor draws it, and every marker gesture is one of
// these.

/// How a warped clip's audio follows the tempo (§7.3).
enum class WarpMode
{
    Beats,   // each piece between transients from its transient, at its own speed
    Tones,   // stretched smoothly
    Repitch, // speed and pitch together, like a record played faster
};
[[nodiscard]] std::string warpModeName(WarpMode mode);
/// "Tones", "beats"... (case ignored); nullopt if unknown.
[[nodiscard]] std::optional<WarpMode> warpModeFromName(const std::string& name);

/// What Beats does with the gap after a piece played slower than its file (§7.3).
enum class WarpTails
{
    Fade, // the piece's tail fades before the next
    Loop, // the tail repeats to fill the gap
};
[[nodiscard]] std::string tailsName(WarpTails tails);
[[nodiscard]] std::optional<WarpTails> tailsFromName(const std::string& name);

/// A clip's tempo: its file's beats a minute (§7.1).
inline constexpr double kMinClipTempo = 20.0, kMaxClipTempo = 999.0;
/// A tempo in a file's name (§7.1): "Groove_92bpm", "Drums 120 BPM", "loop-87.5bpm". Nullopt
/// without one, or outside 40 … 300 BPM.
[[nodiscard]] std::optional<double> tempoFromName(const std::string& name);
/// How far a piece between markers stretches, against the clip's tempo (§7.5).
inline constexpr double kMinPieceStretch = 0.25, kMaxPieceStretch = 4.0;
/// Transpose (semitones) and Fine (cents), each either way (§8.1).
inline constexpr int kMaxTranspose = 48, kMaxFine = 50;
/// The crossfade into each piece the piece player plays (§22): the edge fades' length.
inline constexpr double kPieceFadeSeconds = 0.004;

/// A moment of the file pinned to a beat of the clip's content (§7.5): seconds of its file as
/// recorded, and quarter notes from the content's start. The file's start is always pinned to
/// beat 0, so it's never a marker of its own.
struct WarpMarker
{
    double file = 0.0;
    double beat = 0.0;
    friend bool operator==(const WarpMarker&, const WarpMarker&) = default;
};

/// An audio clip's warping, as set on it.
struct AudioWarp
{
    bool on = false;
    WarpMode mode = WarpMode::Tones;
    WarpTails tails = WarpTails::Fade; // Beats' setting
    bool formants = false;             // Tones' setting (§8.1)
    /// Its file's tempo (§7.1): the beats a minute it plays away from its markers.
    double tempo = 120.0;
    /// Pinned moments after the file's start, in order: each later in the file, and in beats, than
    /// the one before. Kept while Warp is off, for when it's on again.
    std::vector<WarpMarker> markers;
    /// How many transients are found (§7.4), 0 … 1.
    double sensitivity = 0.5;

    friend bool operator==(const AudioWarp&, const AudioWarp&) = default;
};

/// An audio clip's transposition (§8.1): it keeps its speed.
struct AudioPitch
{
    int transpose = 0; // semitones
    int fine = 0;      // cents
    [[nodiscard]] double semitones() const noexcept { return transpose + fine / 100.0; }
    friend bool operator==(const AudioPitch&, const AudioPitch&) = default;
};

struct ClipInfo;

// ---- the map -------------------------------------------------------------------------------------

/// The file's time (seconds) at a beat of the clip's content, and back: straight between markers,
/// at the clip's tempo before the first and after the last.
[[nodiscard]] double fileAtBeat(const AudioWarp& warp, double beat) noexcept;
[[nodiscard]] double beatAtFile(const AudioWarp& warp, double file) noexcept;

/// A stretch of the file between two pinned moments (its start, a marker, its end).
struct WarpPiece
{
    double beat0 = 0.0, beat1 = 0.0;
    double file0 = 0.0, file1 = 0.0;
    /// Its file's beats a minute: the clip's tempo where nothing pins it.
    [[nodiscard]] double tempo() const noexcept;
    friend bool operator==(const WarpPiece&, const WarpPiece&) = default;
};
/// The file's pieces, from its start through each marker to its end (`fileLength` seconds).
[[nodiscard]] std::vector<WarpPiece> warpPieces(const AudioWarp& warp, double fileLength);
/// How much a piece is stretched against the clip's tempo (the drag's tag, §7.5): its tempo over
/// the clip's. Above 1 it plays longer than at the clip's tempo.
[[nodiscard]] double pieceStretch(const AudioWarp& warp, const WarpPiece& piece) noexcept;

/// The same map for the file played backwards (a reversed clip, §6.3): what plays at its end plays
/// first. Its own inverse.
[[nodiscard]] AudioWarp reversedWarp(const AudioWarp& warp, double fileLength);

/// Where a clip's content plays its file: seconds of its content (the song's time, while it warps)
/// to seconds of its file as recorded (a reversed clip reads its file backwards), and back.
struct ContentMap
{
    bool warped = false, reversed = false;
    double fileLength = 0.0;
    double beatsPerSecond = 2.0;
    AudioWarp played; // the map as the file plays: a reversed clip's seen from the file's end

    [[nodiscard]] double fileAt(double content) const noexcept;
    [[nodiscard]] double contentAt(double file) const noexcept;
    /// The same in the file as it plays (a reversed clip's reversed copy): what its peaks are of.
    [[nodiscard]] double playedAt(double content) const noexcept;
    /// How long its content is: the file's length, as it plays.
    [[nodiscard]] double contentLength() const noexcept;
    friend bool operator==(const ContentMap&, const ContentMap&) = default;
};
[[nodiscard]] ContentMap contentMap(const ClipInfo& clip);
/// contentMap(clip).fileAt(content), for a single question.
[[nodiscard]] double fileTimeAt(const ClipInfo& clip, double content);

// ---- what the piece player plays (§22) ------------------------------------------------------------

/// Where the piece player starts a piece: the file's start, each marker, and in Beats each
/// transient too (`transients`: seconds of the file, in order), as moments pinned to beats, in
/// order. A transient within 1 ms of a piece already starting there adds none.
[[nodiscard]] std::vector<WarpMarker> pieceStarts(const AudioWarp& warp,
                                                  const std::vector<double>& transients, double fileLength);

// ---- editing markers (§7.5–§7.8) --------------------------------------------------------------------

/// The marker at `file`, if one is within `tolerance` seconds of it.
[[nodiscard]] std::optional<size_t> markerNear(const AudioWarp& warp, double file, double tolerance);
/// A marker added at `file` at the beat it plays now: nothing moves. At a marker already there, or
/// at the file's start, unchanged. Returns the marker's index (nullopt at the file's start).
[[nodiscard]] std::pair<AudioWarp, std::optional<size_t>> withMarkerAt(const AudioWarp& warp, double file);
/// Marker `index` moved to `beat` (a drag): the audio stretches between it and its neighbours,
/// each piece kept within ×0.25 … ×4 of the clip's tempo (the drag stops there).
[[nodiscard]] AudioWarp withMarkerMoved(const AudioWarp& warp, size_t index, double beat);
/// The audio slid under marker `index` (Shift + drag): it stays at its beat and pins `file`
/// instead, kept between its neighbours and within the same limits.
[[nodiscard]] AudioWarp withMarkerSlid(const AudioWarp& warp, size_t index, double file);
/// Several markers moved by `beats` together (← and →, a drag of a selection), each piece within
/// the limits: the move shrinks to fit.
[[nodiscard]] AudioWarp withMarkersMoved(const AudioWarp& warp, const std::vector<size_t>& indices,
                                         double beats);
/// Those markers removed.
[[nodiscard]] AudioWarp withoutMarkers(const AudioWarp& warp, const std::vector<size_t>& indices);
/// Warp from here: one tempo from marker `index` on, the markers after it gone.
[[nodiscard]] AudioWarp warpedFrom(const AudioWarp& warp, size_t index);
/// A marker at each transient in [fromFile, toFile) (Markers at transients), each at the beat it
/// plays now.
[[nodiscard]] AudioWarp withMarkersAtTransients(const AudioWarp& warp, const std::vector<double>& transients,
                                                double fromFile, double toFile);
/// The clip's tempo set to `tempo` (typed, ×2, ÷2, the Tempo tool): the file plays at it, every
/// marker's beat scaled with it.
[[nodiscard]] AudioWarp withTempo(const AudioWarp& warp, double tempo);
/// A range's time handles (§7.6): the audio from beat `from` to `to` stretched to span `newFrom`
/// to `newTo`, a marker at each end, the markers inside scaled with it, those outside kept. Each
/// piece within the limits (the drag stops there).
[[nodiscard]] AudioWarp withRangeStretched(const AudioWarp& warp, double from, double to, double newFrom,
                                           double newTo);

/// Quantise's settings (§7.7, the Quantise tool).
struct WarpQuantise
{
    double gridBeats = 0.25; // the grid's step, in beats
    double strength = 1.0;   // 0 … 1: how far towards the grid
    double swing = 0.0;      // 0 … 1: every second step pushed later by up to half a step
};
/// Each transient in [fromBeat, toBeat) of the content moved towards the nearest grid line by
/// `strength`, through warp markers (§7.7). `originBeat` is the song's beat at content beat 0, so
/// the grid is the song's. Each piece within the limits.
[[nodiscard]] AudioWarp quantised(const AudioWarp& warp, const std::vector<double>& transients,
                                  double fromBeat, double toBeat, double originBeat,
                                  const WarpQuantise& settings);
} // namespace fd::model
