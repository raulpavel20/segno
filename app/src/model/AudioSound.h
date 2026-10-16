#pragma once

#include "model/Automation.h"
#include "model/ClipLoops.h"
#include "model/Warp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// An audio clip's sound (M3 step P6, ADR 0063, docs/design/audio-editor.md §6): its gain, its gain
// line, reverse, polarity and how a stereo file's channels play. Settings of the clip, never of its
// file. Pure functions: the engine plays them and the editor draws them with the same ones.

/// How a stereo file's channels play (§6.3).
enum class ChannelMode
{
    Stereo,  // as recorded
    Left,    // the left channel on both sides
    Right,   // the right channel on both sides
    Mono,    // both summed (halved) on both sides
    Swapped, // left and right exchanged
};
[[nodiscard]] std::string channelModeName(ChannelMode mode);
/// "Stereo", "left", "mono"... (case ignored); nullopt if unknown.
[[nodiscard]] std::optional<ChannelMode> channelModeFromName(const std::string& name);

/// A clip's gain: −∞ (model::minusInfinityDb) to this.
inline constexpr float kMaxClipGainDb = 24.0f;
/// The gain line's value for 0 dB (the volume taper's: docs/design/mixer.md §3). The line runs from
/// −∞ (0) to +6 dB (1), as every volume lane does.
[[nodiscard]] float lineValueForDb(double db) noexcept;
[[nodiscard]] double lineDbForValue(float value) noexcept;
/// A ramp at a range's edges on the gain line (§5): the edge fades' length.
inline constexpr double kLineRampSeconds = 0.004;

/// An audio clip's sound, as set on it.
struct AudioSound
{
    float gainDb = 0.0f; // −∞ (minusInfinityDb) … +24
    bool reversed = false;
    bool inverted = false; // polarity
    ChannelMode channels = ChannelMode::Stereo;
    /// The gain line (§6.2): times in seconds of the file as recorded (a reversed clip's too), values
    /// on the volume taper. Empty: 0 dB throughout.
    AutomationPoints line;

    /// Whether it plays the file as it is: no gain, a flat line at 0 dB, polarity and channels as
    /// recorded.
    [[nodiscard]] bool neutral() const noexcept;
    friend bool operator==(const AudioSound&, const AudioSound&) = default;
};

/// The gain line's gain (linear) at `fileSeconds`: 1 without points. Allocation-free.
[[nodiscard]] float lineGainAt(const AutomationPoints& line, double fileSeconds) noexcept;
/// A clip's gain (linear) at `fileSeconds` of its file: its gain times its line. Allocation-free.
[[nodiscard]] float soundGainAt(const AudioSound& sound, double fileSeconds) noexcept;

/// The gain line on the clip view's time, where content time c shows at `origin` + c, and back
/// (audio-editor.md §4): each point where its moment of the file plays (`map`: warped, at its beat).
/// A reversed clip reads its file backwards, so its line shows mirrored, each segment's bend the
/// other way.
[[nodiscard]] AutomationPoints lineToView(const AutomationPoints& line, double origin, const ContentMap& map);
[[nodiscard]] AutomationPoints lineFromView(const AutomationPoints& view, double origin,
                                            const ContentMap& map);

/// One frame's two channels as `mode` plays them (in place). Allocation-free.
void applyChannels(ChannelMode mode, float& left, float& right) noexcept;

/// What the gain line says over [from, to) of the file: a stretch of it to edit (§5).
struct LineSpan
{
    double from = 0.0, to = 0.0;
    friend bool operator==(const LineSpan&, const LineSpan&) = default;
};
/// The stretches of the file a clip plays over [from, to) on the timeline, in file time as recorded
/// (through its warp and its reversal): what the laps of a looped clip play there, joined where they
/// overlap or touch, in order, each from < to.
[[nodiscard]] std::vector<LineSpan> fileSpans(const ClipInfo& clip, double from, double to);

/// Where Slice cuts a clip (§9).
enum class SliceAt
{
    Transients,
    Markers, // its warp markers, while it warps
    Grid,    // every step of the song's grid
    Bars,    // every bar of the song
};
struct TrackInfo;
/// Why Slice can't cut `clip` on `track` (§9): a sentence, or empty if it can. A clip with takes is
/// one clip (ADR 0032): its parts would move together.
[[nodiscard]] std::string whyCantSlice(const ClipInfo& clip, const TrackInfo& track);
/// "transients", "markers", "grid", "bars"; and back (case ignored), nullopt if unknown.
[[nodiscard]] std::string sliceAtName(SliceAt at);
[[nodiscard]] std::optional<SliceAt> sliceAtFromName(const std::string& name);
/// The times on the timeline where Slice cuts a clip (§9), in order: at its transients
/// (`transients`, seconds of its file as recorded) or its warp markers, in every lap it plays; or at
/// every `gridBeats` of the song, or every bar. Within `range` of the timeline (the whole clip
/// without one), each at least minClipLength from the next and from the clip's edges.
[[nodiscard]] std::vector<double> sliceTimes(const ClipInfo& clip, SliceAt at,
                                             const std::vector<double>& transients, double gridBeats,
                                             const TempoMap& tempo,
                                             std::optional<std::pair<double, double>> range = std::nullopt);

/// The line with its gain changed by `deltaDb` over [from, to) of the file: the points inside move,
/// the level at each edge is kept, and a ramp of `ramp` seconds inside each edge joins them (§5).
/// A span shorter than two ramps ramps over half of it each way. Values stay within the line's
/// range (+6 dB at most).
[[nodiscard]] AutomationPoints lineWithGain(const AutomationPoints& line, LineSpan span, double deltaDb,
                                            double ramp = kLineRampSeconds);
/// The line falling to silence over [from, to) of the file, ramped as lineWithGain (§5, Silence).
[[nodiscard]] AutomationPoints lineSilenced(const AutomationPoints& line, LineSpan span,
                                            double ramp = kLineRampSeconds);

/// What Normalise reaches (§6.4).
enum class NormaliseTo
{
    Peak,
    Loudness,
};
/// A clip's levels as it plays (its gain line included, its gain not): the sample peak and the
/// integrated loudness (−∞: silent, or too short to gate).
struct ClipLevels
{
    static constexpr double silent = -100.0; // minusInfinityDb
    double peakDb = silent;
    double lufs = silent;
};
/// Measures what a clip plays of its file: `spans` of it (fileSpans: in order, apart) through its gain
/// line and channels. `path` is the file as recorded (a reversed clip's original, which its line
/// follows). Streamed: not real-time, and never the whole file in memory. Nullopt, with `error`, if
/// the file can't be read.
[[nodiscard]] std::optional<ClipLevels> measureClip(const std::string& path,
                                                    const std::vector<LineSpan>& spans,
                                                    const AudioSound& sound, std::string* error = nullptr);
/// The gain (dB) each clip needs for Normalise: each reaching `targetDb` (dBFS or LUFS) by itself,
/// or `together`, all moved by the amount that brings the loudest to it. A silent clip keeps its
/// gain (nullopt). Gains are kept within −∞ … +24 dB.
[[nodiscard]] std::vector<std::optional<float>> normaliseGains(const std::vector<ClipLevels>& levels,
                                                               const std::vector<float>& currentGainDb,
                                                               NormaliseTo to, double targetDb,
                                                               bool together);
} // namespace fd::model
