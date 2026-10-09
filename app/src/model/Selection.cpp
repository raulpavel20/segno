#include "model/Selection.h"

#include "model/Editing.h"

namespace fd::model
{
namespace
{
bool holdsAny(const TrackInfo& track, const std::set<std::uint64_t>& clips)
{
    for (const auto& c : track.clips)
        if (clips.contains(c.id))
            return true;
    return false;
}

std::optional<std::uint64_t> topmostHolding(const Snapshot& snapshot, const std::set<std::uint64_t>& clips)
{
    for (const auto& t : snapshot.tracks)
        if (holdsAny(t, clips))
            return t.id;
    return std::nullopt;
}
} // namespace

Selection withClips(const Snapshot& snapshot, std::set<std::uint64_t> clips, std::optional<std::uint64_t> prefer,
                    std::optional<std::uint64_t> otherwise)
{
    Selection s{.channel = otherwise, .clips = std::move(clips)};
    if (prefer)
        if (const auto* t = snapshot.channel(*prefer); t != nullptr && holdsAny(*t, s.clips))
        {
            s.channel = prefer;
            return s;
        }
    if (const auto top = topmostHolding(snapshot, s.clips))
        s.channel = top;
    return s;
}

Selection pruned(const Selection& selection, const Snapshot& snapshot)
{
    Selection s;
    for (const auto id : selection.clips)
        if (findClip(snapshot, id))
            s.clips.insert(id);
    if (selection.channel && snapshot.channel(*selection.channel) != nullptr)
        s.channel = selection.channel;
    if (!s.clips.empty() && !(s.channel && holdsAny(*snapshot.channel(*s.channel), s.clips)))
        s.channel = topmostHolding(snapshot, s.clips);
    // A lane stays while its channel shows it; points while they exist.
    if (selection.lane && s.channel && s.clips.empty())
        if (const auto* lane = snapshot.channel(*s.channel)->lane(*selection.lane))
        {
            s.lane = selection.lane;
            for (const auto i : selection.points)
                if (i < lane->points.size())
                    s.points.insert(i);
        }
    return s;
}

PanelContext panelContext(const Selection& selection, const Snapshot& snapshot, CentreView view)
{
    PanelContext ctx;
    ctx.key.selection = selection;
    ctx.clipViewOffered = view == CentreView::Arrange;

    std::optional<std::pair<const ClipInfo*, int>> single;
    for (const auto id : selection.clips)
        if (const auto found = findClip(snapshot, id))
        {
            ++ctx.clipCount;
            single = found;
        }

    const TrackInfo* channel = selection.channel ? snapshot.channel(*selection.channel) : nullptr;
    const LaneInfo* lane = channel != nullptr && selection.lane && selection.clips.empty() ? channel->lane(*selection.lane)
                                                                                           : nullptr;
    if (lane != nullptr)
    {
        ctx.subject = Subject::Lane;
        ctx.lane = *lane;
        for (const auto i : selection.points)
            if (i < lane->points.size())
            {
                ++ctx.pointCount;
                ctx.point = lane->points[i];
            }
        if (ctx.pointCount != 1)
            ctx.point.reset();
    }
    else if (ctx.clipCount == 1)
    {
        ctx.subject = Subject::Clip;
        ctx.clip = *single->first;
        channel = &snapshot.tracks[static_cast<size_t>(single->second)]; // the clip's track
        if (ctx.clip->kind == ClipInfo::Kind::Midi)
            ctx.midiClip = ctx.clip;
    }
    else if (ctx.clipCount > 1)
        ctx.subject = Subject::Clips;
    else if (channel != nullptr)
        ctx.subject = Subject::Channel;

    if (channel != nullptr)
        ctx.channel = *channel;
    ctx.key.midiClip = ctx.midiClip ? std::optional(ctx.midiClip->id) : std::nullopt;
    ctx.opensClipView = ctx.midiClip.has_value();
    if (channel != nullptr && channel->holdsClips() && channel->kind == TrackInfo::Kind::Instrument)
        ctx.midiInputTarget = channel->id;

    // The inspector belongs to the arrangement: in the mixer view the strips show every channel
    // control (ADR 0027).
    if (view == CentreView::Mix || ctx.subject == Subject::Nothing)
        return ctx;
    if (ctx.subject == Subject::Lane)
    {
        if (ctx.pointCount == 1)
            ctx.inspector.push_back(InspectorSection::Point);
        else if (ctx.pointCount > 1)
            ctx.inspector.push_back(InspectorSection::Points);
        ctx.inspector.push_back(InspectorSection::Lane);
    }
    if (ctx.subject == Subject::Clip)
        ctx.inspector.push_back(InspectorSection::Clip);
    else if (ctx.subject == Subject::Clips)
        ctx.inspector.push_back(InspectorSection::Clips);
    if (channel == nullptr)
        return ctx;
    switch (channel->role)
    {
    case TrackInfo::Role::Track:
        ctx.inspector.insert(ctx.inspector.end(), {InspectorSection::Track, InspectorSection::Mix, InspectorSection::Sends});
        break;
    case TrackInfo::Role::Return:
        ctx.inspector.insert(ctx.inspector.end(), {InspectorSection::Return, InspectorSection::Mix});
        break;
    case TrackInfo::Role::Master:
        ctx.inspector.insert(ctx.inspector.end(), {InspectorSection::Master, InspectorSection::Mix});
        break;
    }
    return ctx;
}
} // namespace fd::model
