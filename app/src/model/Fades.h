#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fd::model
{
// Clip fades and crossfades (M2 step C6, ADR 0036, docs/design/fades.md): the curves the audio
// plays and the arrangement draws, where a crossfade can sit, and what each audio clip plays.

/// An edge fade's length: every cut edge of an audio clip fades at least this much (§2).
inline constexpr double kEdgeFadeSeconds = 0.004;
/// A crossfade made by the Crossfade command or at a comp join (§4, §5).
inline constexpr double kCrossfadeSeconds = 0.010;
/// Shorter than this, a fade or crossfade is none.
inline constexpr double kShortestFade = 0.0001;

/// A fade at a clip's edge: its length (seconds) and bend (−1 … 1, as automation's).
struct Fade
{
    double length = 0.0;
    float bend = 0.0f;

    friend bool operator==(const Fade&, const Fade&) = default;
};

enum class CrossfadeShape
{
    EqualPower, // constant loudness across different material (the default)
    EqualGain,  // straight ramps, for the same material
    SCurve,     // equal gain, eased at both ends
};
[[nodiscard]] std::string shapeName(CrossfadeShape shape);
/// "equal power", "Equal gain", "s-curve"... (case and spaces ignored); nullopt if unknown.
[[nodiscard]] std::optional<CrossfadeShape> shapeFromName(const std::string& name);

/// A crossfade at a clip's end into the clip that starts there (stored on the earlier clip): its
/// length, where it starts relative to the join (−length: wholly before it; 0: wholly after;
/// −length/2: centred), and its shape.
struct Crossfade
{
    double length = 0.0;
    double from = 0.0;
    CrossfadeShape shape = CrossfadeShape::EqualPower;

    friend bool operator==(const Crossfade&, const Crossfade&) = default;
};

/// Where a crossfade sits against its join (fades.md §4): wholly before it, centred on it, or wholly
/// after it (Pro Tools' pre, centred and post crossfades).
enum class CrossfadePlacement
{
    Before,
    Centred,
    After,
};
/// A crossfade's placement, if it sits at one of the three (its start −length, −length/2 or 0).
[[nodiscard]] std::optional<CrossfadePlacement> placementOf(const Crossfade& crossfade) noexcept;
/// Where a crossfade of `length` would start for `placement` (relative to the join).
[[nodiscard]] double placementFrom(CrossfadePlacement placement, double length) noexcept;

/// A fade's gain (0 … 1) at `alpha` (0 … 1 through it): the automation segment from silence to
/// full level (fade-in) or back (fade-out), bent by `bend`. Allocation-free.
[[nodiscard]] float fadeInGain(double alpha, float bend) noexcept;
[[nodiscard]] float fadeOutGain(double alpha, float bend) noexcept;

/// A crossfade's gains at `alpha`: the outgoing clip's and the incoming clip's. Allocation-free.
struct CrossGains
{
    float out = 1.0f, in = 0.0f;
};
[[nodiscard]] CrossGains crossfadeGains(double alpha, CrossfadeShape shape) noexcept;

/// An audio clip as fades see it.
struct FadeClip
{
    std::uint64_t id = 0;
    double start = 0.0, length = 0.0;
    double offset = 0.0;       // seconds into its source where it starts
    double sourceLength = 0.0; // its source's length (0: unknown, no spare audio after it)
    std::string source;
    bool muted = false;
    bool canExtend = true; // false: it can't play past its edges (stretched, synced, looped)
    Fade in, out;
    std::optional<Crossfade> crossOut; // stored on it, into the clip that starts where it ends

    [[nodiscard]] double end() const noexcept { return start + length; }
};

/// The spare audio of a clip's source before its start and after its end.
[[nodiscard]] double spareBefore(const FadeClip& clip) noexcept;
[[nodiscard]] double spareAfter(const FadeClip& clip) noexcept;

/// Whether two clips meet: the first ends where the second starts.
[[nodiscard]] bool meet(const FadeClip& first, const FadeClip& second) noexcept;
/// Whether `next` runs on seamlessly from `prev`: they meet, play the same file, and `next`
/// starts in it where `prev` ends (a split, a bounce run, comp parts from one take).
[[nodiscard]] bool seamless(const FadeClip& prev, const FadeClip& next) noexcept;

/// The crossfade that fits between two clips that meet, as near as their spare audio allows to
/// `length` and `from` (centred when not given): shorter where the spare audio is short, moved to
/// the side that has it. Nullopt if they don't meet or neither has any.
[[nodiscard]] std::optional<Crossfade> placeCrossfade(const FadeClip& out, const FadeClip& in, double length,
                                                      std::optional<double> from = std::nullopt,
                                                      CrossfadeShape shape = CrossfadeShape::EqualPower);

/// A crossfade at `placement` between two clips that meet, if their spare audio lets it sit there
/// whole (`length` and its start as asked); nullopt otherwise.
[[nodiscard]] std::optional<Crossfade> placeCrossfadeAt(const FadeClip& out, const FadeClip& in, double length,
                                                        CrossfadePlacement placement, CrossfadeShape shape);

/// What a crossfade sounds like: `outgoing` and `incoming` (interleaved, `channels` per frame, as
/// many frames each) through the shape's gains from the crossfade's start (alpha 0 at the first
/// frame) to its end, summed. What the arrangement draws across a crossfade (fades.md §3).
[[nodiscard]] std::vector<float> crossfadeSum(const std::vector<float>& outgoing, const std::vector<float>& incoming, int channels,
                                              CrossfadeShape shape);

/// A gain ramp at one end of what a clip plays.
struct Ramp
{
    enum class Kind { None, Fade, Cross };
    Kind kind = Kind::None;
    double length = 0.0;
    float bend = 0.0f;                                // a fade's
    CrossfadeShape shape = CrossfadeShape::EqualPower; // a crossfade's
    bool edge = false;                                // an edge fade: the automatic 4 ms floor (§2)

    friend bool operator==(const Ramp&, const Ramp&) = default;
};

/// What an audio clip plays (ADR 0036): how much before its start and after its end (a
/// crossfade's spare audio), and the ramps at the two ends of that.
struct ClipPlayback
{
    double extendStart = 0.0, extendEnd = 0.0;
    Ramp in, out;

    [[nodiscard]] bool any() const noexcept { return in.kind != Ramp::Kind::None || out.kind != Ramp::Kind::None; }
    friend bool operator==(const ClipPlayback&, const ClipPlayback&) = default;
};

/// The playback of `clips[index]`: `clips` are a track's audio clips in time order (muted ones
/// too: they don't play, so nothing meets them); `edgeFades` is the project's setting (§2).
[[nodiscard]] ClipPlayback clipPlayback(const std::vector<FadeClip>& clips, size_t index, bool edgeFades);

/// The crossfade in effect at the end of `clips[index]`: its stored one, fitted to the spare audio
/// now, and sharing its clips with the crossfades at their other ends (where their reaches into a
/// clip would overlap, each keeps its share: fades.md §4). None if there's none.
[[nodiscard]] std::optional<Crossfade> crossfadeOut(const std::vector<FadeClip>& clips, size_t index);

/// A new crossfade at the end of `clips[index]` (into the next clip): placeCrossfade, then stopped
/// where the crossfades at the two clips' other ends begin, so it never overlaps them.
[[nodiscard]] std::optional<Crossfade> placeCrossfadeOn(const std::vector<FadeClip>& clips, size_t index, double length,
                                                        std::optional<double> from = std::nullopt,
                                                        CrossfadeShape shape = CrossfadeShape::EqualPower);

/// How far a crossfade reaches into the clip before its join, and into the clip after it.
[[nodiscard]] double crossfadeReachBefore(const Crossfade& crossfade) noexcept;
[[nodiscard]] double crossfadeReachAfter(const Crossfade& crossfade) noexcept;

/// The gain of a clip's playback at `seconds` on the timeline (0 outside what it plays).
/// Allocation-free.
[[nodiscard]] float playbackGain(const FadeClip& clip, const ClipPlayback& playback, double seconds) noexcept;
/// The same for a ramp alone: `alpha` through it, fading in or out.
[[nodiscard]] float rampGain(const Ramp& ramp, double alpha, bool fadingIn) noexcept;
} // namespace fd::model
