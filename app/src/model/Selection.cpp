#include "model/Selection.h"

#include "model/Editing.h"
#include "model/Groups.h"
#include "model/Takes.h"

#include <algorithm>
#include <set>

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
    // A clip's parts select together (ADR 0032).
    Selection s{.channel = otherwise, .clips = withGroups(snapshot, std::move(clips))};
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

Selection withSections(const Snapshot& snapshot, std::set<std::uint64_t> sections)
{
    Selection s{.channel = snapshot.master.id};
    for (const auto id : sections)
        if (snapshot.section(id) != nullptr)
            s.sections.insert(id);
    return s;
}

Selection withTime(const Snapshot& snapshot, TimeSpan span, const std::set<std::uint64_t>& tracks,
                   const std::set<std::uint64_t>& lanes, std::optional<std::uint64_t> channel)
{
    Selection s;
    if (!(span.end > span.start))
        return s;
    for (const auto id : tracks)
        if (const auto* t = snapshot.channel(id); t != nullptr && (t->holdsClips() || t->isGroup()))
        {
            s.rowTracks.insert(id);
            // A group's row covers every row in it, folded or not (groups.md §6.5).
            if (t->isGroup())
                for (const auto* m : membersOf(snapshot, id, true))
                    s.rowTracks.insert(m->id);
        }
    for (const auto id : lanes)
        if (snapshot.automationLane(id) != nullptr)
            s.rowLanes.insert(id);
    if (s.rowTracks.empty() && s.rowLanes.empty())
        return {};
    s.time = span;
    // The row it was started on; else the topmost track; else the first lane's first assignment's.
    if (channel && snapshot.channel(*channel) != nullptr)
        s.channel = channel;
    else if (!s.rowTracks.empty())
    {
        for (const auto& t : snapshot.tracks)
            if (s.rowTracks.contains(t.id))
            {
                s.channel = t.id;
                break;
            }
    }
    else
        for (const auto& lane : snapshot.automationLanes)
            if (s.rowLanes.contains(lane.id))
            {
                if (!lane.assignments.empty() &&
                    snapshot.channel(lane.assignments.front().channel) != nullptr)
                    s.channel = lane.assignments.front().channel;
                break;
            }
    return s;
}

Selection withLane(const Snapshot& snapshot, std::uint64_t lane, std::set<size_t> points)
{
    Selection s{.lane = lane, .points = std::move(points)};
    if (const auto* l = snapshot.automationLane(lane); l != nullptr && !l->assignments.empty() &&
                                                       snapshot.channel(l->assignments.front().channel) != nullptr)
        s.channel = l->assignments.front().channel;
    return s;
}

namespace
{
bool chosenTogether(const TrackInfo* t)
{
    return t != nullptr && (t->holdsClips() || t->isGroup());
}

/// Several, or one alone (its channel, no set).
Selection severalOrOne(std::set<std::uint64_t> tracks, std::optional<std::uint64_t> channel)
{
    if (tracks.size() < 2)
        return {.channel = tracks.empty() ? channel : std::optional(*tracks.begin())};
    return {.channel = channel, .rowTracks = std::move(tracks)};
}
} // namespace

Selection withTrackToggled(const Snapshot& snapshot, const Selection& selection, std::uint64_t id)
{
    if (!chosenTogether(snapshot.channel(id)))
        return {.channel = id};
    auto tracks = selection.hasTime() ? std::set<std::uint64_t>{} : selection.rowTracks;
    // The one selected alone joins them.
    if (tracks.empty() && selection.clips.empty() && !selection.lane && selection.channel &&
        chosenTogether(snapshot.channel(*selection.channel)))
        tracks.insert(*selection.channel);
    if (tracks.erase(id) == 0)
    {
        tracks.insert(id);
        return severalOrOne(std::move(tracks), id);
    }
    // The one clicked last went: the last of those left stands for them.
    std::optional<std::uint64_t> channel;
    for (const auto& t : snapshot.tracks)
        if (tracks.contains(t.id))
            channel = t.id;
    return severalOrOne(std::move(tracks), channel);
}

Selection withTrackRun(const Snapshot& snapshot, const Selection& selection, std::uint64_t id,
                       const std::vector<std::uint64_t>& shown)
{
    if (!chosenTogether(snapshot.channel(id)))
        return {.channel = id};
    const auto at = [&](std::uint64_t c) { return std::find(shown.begin(), shown.end(), c); };
    const auto to = at(id);
    const auto from = selection.channel ? at(*selection.channel) : shown.end();
    if (to == shown.end() || from == shown.end())
        return {.channel = id};
    std::set<std::uint64_t> tracks;
    for (auto it = std::min(from, to); it <= std::max(from, to); ++it)
        if (chosenTogether(snapshot.channel(*it)))
            tracks.insert(*it);
    return severalOrOne(std::move(tracks), id);
}

std::vector<std::uint64_t> selectedTracks(const Snapshot& snapshot, const Selection& selection)
{
    std::vector<std::uint64_t> out;
    if (selection.hasTime())
        return out;
    for (const auto& t : snapshot.tracks)
        if (selection.rowTracks.empty()
                ? selection.channel == t.id && selection.clips.empty() && !selection.lane
                : selection.rowTracks.contains(t.id))
            out.push_back(t.id);
    return out;
}

Selection pruned(const Selection& selection, const Snapshot& snapshot)
{
    // A time selection keeps its stretch on the rows that still exist (time-selection.md §4).
    if (selection.hasTime())
        return withTime(snapshot, *selection.time, selection.rowTracks, selection.rowLanes,
                        selection.channel);
    Selection s;
    for (const auto id : selection.clips)
        if (findClip(snapshot, id))
            s.clips.insert(id);
    s.clips = withGroups(snapshot, std::move(s.clips));
    if (selection.channel && snapshot.channel(*selection.channel) != nullptr)
        s.channel = selection.channel;
    if (!s.clips.empty() && !(s.channel && holdsAny(*snapshot.channel(*s.channel), s.clips)))
        s.channel = topmostHolding(snapshot, s.clips);
    // Takes stay while their track has them.
    if (s.clips.empty() && s.channel)
        if (const auto* t = snapshot.channel(*s.channel))
            for (const auto id : selection.takes)
                if (t->take(id) != nullptr)
                    s.takes.insert(id);
    // Several selected tracks and groups stay while they exist (groups.md §8).
    if (s.clips.empty())
        for (const auto id : selection.rowTracks)
            if (snapshot.channel(id) != nullptr)
                s.rowTracks.insert(id);
    // Sections stay while they exist, with the master.
    if (!selection.sections.empty() && s.clips.empty() && !selection.lane)
    {
        auto kept = withSections(snapshot, selection.sections);
        if (!kept.sections.empty())
            return kept;
    }
    // A lane stays while it exists (its channel following its assignments); points while they do.
    if (selection.lane && s.clips.empty())
        if (const auto* lane = snapshot.automationLane(*selection.lane))
        {
            std::set<size_t> points;
            for (const auto i : selection.points)
                if (i < lane->points.size())
                    points.insert(i);
            return withLane(snapshot, lane->id, std::move(points));
        }
    return s;
}

PanelContext panelContext(const Selection& selection, const Snapshot& snapshot, CentreView view)
{
    PanelContext ctx;
    ctx.key.selection = selection;
    ctx.clipViewOffered = view == CentreView::Arrange;

    // Clips count as the user sees them: a clip's parts are one clip (its first part stands for it).
    std::optional<std::pair<const ClipInfo*, int>> single; // the earliest selected clip
    std::set<std::pair<int, std::uint64_t>> groups;        // (track, stack or clip)
    for (const auto id : selection.clips)
        if (const auto found = findClip(snapshot, id))
        {
            groups.insert({found->second, found->first->stack != 0 ? found->first->stack : found->first->id});
            if (!single || found->first->start < single->first->start)
                single = found;
        }
    ctx.clipCount = static_cast<int>(groups.size());

    const TrackInfo* channel = selection.channel ? snapshot.channel(*selection.channel) : nullptr;
    const AutomationLaneInfo* lane =
        selection.lane && selection.clips.empty() ? snapshot.automationLane(*selection.lane) : nullptr;
    if (lane != nullptr)
    {
        ctx.subject = Subject::Lane;
        // Its context is its first assignment's channel (automation.md §9).
        channel = lane->assignments.empty() ? nullptr : snapshot.channel(lane->assignments.front().channel);
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
    else if (channel != nullptr && selection.clips.empty() &&
             std::any_of(selection.takes.begin(), selection.takes.end(), [&](auto id) { return channel->take(id) != nullptr; }))
    {
        for (const auto id : selection.takes)
            if (const auto* take = channel->take(id))
            {
                ++ctx.takeCount;
                ctx.take = *take;
            }
        ctx.subject = ctx.takeCount == 1 ? Subject::Take : Subject::Takes;
        if (ctx.takeCount != 1)
            ctx.take.reset();
    }
    else if (selection.hasTime())
    {
        // A time selection (time-selection.md §7): its stretch and rows; its channel the row it was
        // started on.
        ctx.subject = Subject::Time;
        ctx.time = selection.time;
        for (const auto id : selection.rowTracks)
            ctx.timeTracks += snapshot.channel(id) != nullptr ? 1 : 0;
        for (const auto id : selection.rowLanes)
            ctx.timeLanes += snapshot.automationLane(id) != nullptr ? 1 : 0;
        // Inside one clip on one track, the clip view shows that clip (§14.3).
        if (selection.rowTracks.size() == 1 && selection.rowLanes.empty())
            if (const auto* track = snapshot.channel(*selection.rowTracks.begin()))
                for (const auto& c : track->clips)
                    if (c.start <= selection.time->start + 1e-9 && c.end() >= selection.time->end - 1e-9)
                    {
                        ctx.clip = c;
                        if (c.kind == ClipInfo::Kind::Midi)
                            ctx.midiClip = c;
                        break;
                    }
    }
    else if (selection.clips.empty() && !selection.sections.empty())
    {
        // Sections (arranger.md §10): their channel is the master's.
        for (const auto id : selection.sections)
            if (const auto* section = snapshot.section(id))
            {
                ++ctx.sectionCount;
                ctx.section = *section;
            }
        if (ctx.sectionCount != 1)
            ctx.section.reset();
        if (ctx.sectionCount > 0)
        {
            ctx.subject = ctx.sectionCount == 1 ? Subject::Section : Subject::Sections;
            channel = &snapshot.master;
        }
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
    {
        ctx.subject = Subject::Channel;
        // Several tracks and groups, chosen together (groups.md §8): the channel is the one
        // clicked last.
        if (!selection.hasTime())
            for (const auto id : selection.rowTracks)
                ctx.trackCount += snapshot.channel(id) != nullptr ? 1 : 0;
        if (ctx.trackCount < 2)
            ctx.trackCount = 0;
    }

    if (channel != nullptr)
    {
        ctx.channel = *channel;
        if (const auto* g = parentGroup(snapshot, *channel))
            ctx.group = *g;
        if (channel->isGroup())
            ctx.groupTracks = tracksIn(snapshot, channel->id);
    }
    ctx.key.clip = ctx.clip ? std::optional(ctx.clip->id) : std::nullopt;
    ctx.opensClipView = ctx.clip.has_value();
    if (channel != nullptr && channel->holdsClips() && channel->kind == TrackInfo::Kind::Instrument)
        ctx.midiInputTarget = channel->id;

    // The inspector belongs to the arrangement: in the mixer view the strips show every channel
    // control (ADR 0027).
    if (view == CentreView::Mix || ctx.subject == Subject::Nothing)
        return ctx;
    if (ctx.subject == Subject::Lane)
    {
        // A lane belongs to no track: no track, mix or send sections.
        if (ctx.pointCount == 1)
            ctx.inspector.push_back(InspectorSection::Point);
        else if (ctx.pointCount > 1)
            ctx.inspector.push_back(InspectorSection::Points);
        ctx.inspector.insert(ctx.inspector.end(), {InspectorSection::Lane, InspectorSection::Assignments});
        return ctx;
    }
    if (ctx.subject == Subject::Take)
        ctx.inspector.push_back(InspectorSection::Take);
    else if (ctx.subject == Subject::Takes)
        ctx.inspector.push_back(InspectorSection::Takes);
    if (ctx.subject == Subject::Time)
        ctx.inspector.push_back(InspectorSection::Time);
    if (ctx.subject == Subject::Section)
        ctx.inspector.push_back(InspectorSection::Section);
    else if (ctx.subject == Subject::Sections)
        ctx.inspector.push_back(InspectorSection::Sections);
    if (ctx.subject == Subject::Clip)
    {
        ctx.inspector.push_back(InspectorSection::Clip);
        if (ctx.clip->kind == ClipInfo::Kind::Audio)
            ctx.inspector.push_back(InspectorSection::Audio);
    }
    else if (ctx.subject == Subject::Clips)
        ctx.inspector.push_back(InspectorSection::Clips);
    if (channel == nullptr)
        return ctx;
    if (ctx.trackCount > 1)
    {
        // Every track and group sends to the same returns: their sends show too (groups.md §8).
        ctx.inspector.insert(ctx.inspector.end(),
                             {InspectorSection::Tracks, InspectorSection::Mix, InspectorSection::Sends});
        return ctx;
    }
    switch (channel->role)
    {
    case TrackInfo::Role::Group:
        ctx.inspector.insert(ctx.inspector.end(),
                             {InspectorSection::Group, InspectorSection::Mix, InspectorSection::Sends});
        break;
    case TrackInfo::Role::Track:
        ctx.inspector.insert(ctx.inspector.end(),
                             {InspectorSection::Track, InspectorSection::Input, InspectorSection::Mix, InspectorSection::Sends});
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
