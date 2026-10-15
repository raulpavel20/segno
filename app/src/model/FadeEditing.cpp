#include "model/FadeEditing.h"

#include <algorithm>

namespace fd::model
{
namespace
{
constexpr double kNear = 1e-6;

std::optional<size_t> indexOf(const std::vector<FadeClip>& clips, std::uint64_t id)
{
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i].id == id)
            return i;
    return std::nullopt;
}

/// The clip before `i` that a crossfade could join: it meets `i`, plays, and isn't seamless.
bool joinable(const FadeClip& a, const FadeClip& b) noexcept
{
    return meet(a, b) && !a.muted && !b.muted && !seamless(a, b);
}
} // namespace

std::vector<FadeClip> fadeClips(const TrackInfo& track)
{
    std::vector<FadeClip> out;
    for (const auto& c : track.clips)
        if (c.kind == ClipInfo::Kind::Audio)
            out.push_back({.id = c.id,
                           .start = c.start,
                           .length = c.length,
                           .offset = c.offset,
                           .sourceLength = c.sourceLength,
                           .loop = c.loop,
                           .source = c.sourcePath,
                           .muted = c.muted,
                           .canExtend = !c.synced, // as the engine plays it (engine/ClipFades.cpp)
                           .in = c.fadeIn,
                           .out = c.fadeOut,
                           .crossOut = c.crossfadeOut});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}

std::vector<FadeHandle> fadeHandles(const TrackInfo& track)
{
    using K = FadeHandle::Kind;
    std::vector<FadeHandle> out;
    const auto clips = fadeClips(track);
    auto stackOf = [&](std::uint64_t id) -> std::uint64_t {
        for (const auto& c : track.clips)
            if (c.id == id)
                return c.stack;
        return 0;
    };
    // Two parts of one clip with takes, meeting: an inner edge, not the clip's own.
    auto inner = [&](size_t a, size_t b) {
        const auto s = stackOf(clips[a].id);
        return s != 0 && s == stackOf(clips[b].id) && meet(clips[a], clips[b]);
    };
    for (size_t i = 0; i < clips.size(); ++i)
    {
        const auto& c = clips[i];
        const bool crossIn = i > 0 && crossfadeOut(clips, i - 1).has_value();
        const auto crossOut = crossfadeOut(clips, i);
        if (!crossIn && !(i > 0 && inner(i - 1, i)))
        {
            out.push_back({.kind = K::FadeIn, .clip = c.id, .time = c.start + c.in.length});
            if (c.in.length > kShortestFade)
                out.push_back({.kind = K::BendIn,
                               .clip = c.id,
                               .time = c.start + c.in.length * 0.5,
                               .height = 1.0f - fadeInGain(0.5, c.in.bend)});
        }
        if (!crossOut && !(i + 1 < clips.size() && inner(i, i + 1)))
        {
            out.push_back({.kind = K::FadeOut, .clip = c.id, .time = c.end() - c.out.length});
            if (c.out.length > kShortestFade)
                out.push_back({.kind = K::BendOut,
                               .clip = c.id,
                               .time = c.end() - c.out.length * 0.5,
                               .height = 1.0f - fadeOutGain(0.5, c.out.bend)});
        }
        else if (crossOut)
        {
            const double from = c.end() + crossOut->from;
            out.push_back({.kind = K::CrossStart, .clip = c.id, .time = from});
            out.push_back({.kind = K::CrossEnd, .clip = c.id, .time = from + crossOut->length});
        }
        // Its gain (audio-editor.md §10): one handle in its middle; a clip with takes, one for all
        // its parts, in the middle of them.
        if (!(i > 0 && inner(i - 1, i)))
        {
            size_t last = i;
            while (last + 1 < clips.size() && inner(last, last + 1))
                ++last;
            out.push_back({.kind = K::Gain, .clip = c.id, .time = (c.start + clips[last].end()) * 0.5});
        }
    }
    return out;
}

FadeEdit dragFade(const TrackInfo& track, const FadeHandle& handle, double seconds)
{
    using K = FadeHandle::Kind;
    const auto clips = fadeClips(track);
    const auto found = indexOf(clips, handle.clip);
    if (!found)
        return {};
    const auto i = *found;
    const auto& c = clips[i];
    FadeEdit edit;
    switch (handle.kind)
    {
    case K::FadeIn:
    {
        // Back past the start into a clip that meets it: a crossfade there (§3).
        if (seconds < c.start - kNear && i > 0 && joinable(clips[i - 1], c))
            if (const auto x = placeCrossfadeOn(clips, i - 1, 2.0 * (c.start - seconds)))
            {
                edit.crossClip = clips[i - 1].id;
                edit.crossfade = x;
                return edit;
            }
        // Up to what fades the clip out: its fade-out, or the crossfade at its end.
        const auto crossOut = crossfadeOut(clips, i);
        const double other = crossOut ? crossfadeReachBefore(*crossOut) : c.out.length;
        edit.clip = c.id;
        edit.in = Fade{std::clamp(seconds - c.start, 0.0, std::max(0.0, c.length - other)), c.in.bend};
        return edit;
    }
    case K::FadeOut:
    {
        if (seconds > c.end() + kNear && i + 1 < clips.size() && joinable(c, clips[i + 1]))
            if (const auto x = placeCrossfadeOn(clips, i, 2.0 * (seconds - c.end())))
            {
                edit.crossClip = c.id;
                edit.crossfade = x;
                return edit;
            }
        const auto crossIn = i > 0 ? crossfadeOut(clips, i - 1) : std::nullopt;
        const double other = crossIn ? crossfadeReachAfter(*crossIn) : c.in.length;
        edit.clip = c.id;
        edit.out = Fade{std::clamp(c.end() - seconds, 0.0, std::max(0.0, c.length - other)), c.out.bend};
        return edit;
    }
    case K::CrossStart:
    case K::CrossEnd:
    {
        const auto x = crossfadeOut(clips, i);
        if (!x || i + 1 >= clips.size())
            return {};
        const double join = c.end(), start = join + x->from, end = start + x->length;
        double from = x->from, length = x->length;
        if (handle.kind == K::CrossStart)
        {
            const double s = std::min(seconds, end - kShortestFade);
            from = s - join;
            length = end - s;
        }
        else
            length = std::max(seconds, start + kShortestFade) - start;
        edit.crossClip = c.id;
        // Up to the crossfades at the two clips' other ends: it never overlaps them (fades.md §4).
        edit.crossfade = placeCrossfadeOn(clips, i, length, from, x->shape).value_or(*x);
        return edit;
    }
    case K::BendIn:
    case K::BendOut:
    case K::Gain:
        return {};
    }
    return {};
}

FadeEdit bendFade(const TrackInfo& track, const FadeHandle& handle, float bend)
{
    const auto clips = fadeClips(track);
    const auto i = indexOf(clips, handle.clip);
    if (!i)
        return {};
    const auto& c = clips[*i];
    bend = std::clamp(bend, -1.0f, 1.0f);
    if (handle.kind == FadeHandle::Kind::BendIn)
        return {.clip = c.id, .in = Fade{c.in.length, bend}};
    if (handle.kind == FadeHandle::Kind::BendOut)
        return {.clip = c.id, .out = Fade{c.out.length, bend}};
    return {};
}

float bendFor(float base, float up, float travel, bool fadeIn) noexcept
{
    const float delta = travel > 0.0f ? up / travel : 0.0f;
    return std::clamp(fadeIn ? base - delta : base + delta, -1.0f, 1.0f);
}

TrackInfo withFadeEdit(TrackInfo track, const FadeEdit& edit, bool edgeFades)
{
    for (auto& c : track.clips)
    {
        if (c.id == edit.clip)
        {
            if (edit.in)
                c.fadeIn = *edit.in;
            if (edit.out)
                c.fadeOut = *edit.out;
        }
        if (edit.crossClip != 0 && c.id == edit.crossClip)
            c.crossfadeOut = edit.crossfade;
    }
    const auto clips = fadeClips(track);
    for (auto& c : track.clips)
        for (size_t i = 0; i < clips.size(); ++i)
            if (clips[i].id == c.id)
            {
                c.crossfadeOut = crossfadeOut(clips, i);
                c.playback = clipPlayback(clips, i, edgeFades);
            }
    return track;
}

std::vector<FadeClip> withEdit(std::vector<FadeClip> clips, const FadeEdit& edit)
{
    for (auto& c : clips)
    {
        if (c.id == edit.clip)
        {
            if (edit.in)
                c.in = *edit.in;
            if (edit.out)
                c.out = *edit.out;
        }
        if (edit.crossClip != 0 && c.id == edit.crossClip)
            c.crossOut = edit.crossfade;
    }
    return clips;
}
} // namespace fd::model
