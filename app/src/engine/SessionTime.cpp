// A time selection's commands on its rows (ADR 0073, docs/design/time-selection.md §8, §10, §11):
// a stretch cleared, split, cropped, muted, reversed, faded, given a MIDI clip, duplicated, moved,
// copied and pasted, on its tracks and lanes alone. The song-wide "… time" commands are the
// sections' plans (SessionSections.cpp).

#include "engine/SessionImpl.h"
#include "model/Groups.h"
#include "model/TimeEdits.h"

#include <algorithm>
#include <cmath>

namespace fd::engine
{
namespace
{
constexpr double kEpsilon = 1e-6;

bool crosses(const model::ClipInfo& c, double t)
{
    return c.start < t - kEpsilon && c.end() > t + kEpsilon;
}

bool overlaps(const model::ClipInfo& c, const model::TimeSpan& span)
{
    return c.start < span.end - kEpsilon && c.end() > span.start + kEpsilon;
}

bool within(const model::ClipInfo& c, const model::TimeSpan& span)
{
    return c.start >= span.start - kEpsilon && c.end() <= span.end + kEpsilon;
}

const char* undoNameOf(Session::StretchCommand command)
{
    using C = Session::StretchCommand;
    switch (command)
    {
    case C::Delete:
        return "Delete";
    case C::Split:
        return "Split";
    case C::Crop:
        return "Crop";
    case C::Mute:
        return "Mute";
    case C::Reverse:
        return "Reverse";
    case C::Fade:
        return "Fade";
    case C::NewMidiClip:
        return "New MIDI clip";
    case C::Duplicate:
        return "Duplicate";
    }
    return "Edit";
}
} // namespace

struct Session::StretchClipboard::Data
{
    ClipClipboard clips;   // the whole clips across it on its tracks, times from `from`
    std::vector<int> rows; // its tracks, from the topmost (each cleared where it lands)
    double before = 0.0;   // its start after `from`
    double length = 0.0;
    std::vector<std::pair<std::uint64_t, model::AutomationPoints>> lanes; // each lane's piece
};

bool Session::StretchClipboard::empty() const noexcept
{
    return data_ == nullptr;
}

double Session::StretchClipboard::length() const noexcept
{
    return data_ == nullptr ? 0.0 : data_->length;
}

model::StretchFades Session::fadesOf(const std::vector<model::ClipInfo>& clips,
                                     const model::TimeSpan& span) const
{
    // Fade to selection's plan, without crossfades where two halves of one clip play on seamlessly.
    auto fades = model::stretchFades(clips, span);
    std::erase_if(fades.crossfades, [this](const auto& x) { return seamlessJoin(x.out); });
    return fades;
}

std::string Session::stretchRefusal(const model::TimeSelection& stretch, StretchCommand command)
{
    using C = StretchCommand;
    if (stretch.empty())
        return "Select a stretch of time first";
    updateSnapshotNow();
    const auto s = snapshot();
    std::vector<const model::TrackInfo*> tracks;
    for (const auto& t : s->tracks)
        if (stretch.tracks.contains(t.id))
            tracks.push_back(&t);
    const auto anyClip = [&](auto&& test)
    {
        return std::any_of(tracks.begin(), tracks.end(), [&](const auto* t)
                           { return std::any_of(t->clips.begin(), t->clips.end(), test); });
    };
    const auto& span = stretch.span;
    const bool clipsIn = anyClip([&](const auto& c) { return overlaps(c, span); });
    switch (command)
    {
    case C::Delete:
    {
        const bool pointsIn = std::any_of(s->automationLanes.begin(), s->automationLanes.end(),
                                          [&](const auto& l)
                                          {
                                              return stretch.lanes.contains(l.id) &&
                                                     !model::pointsIn(l.points, span.start, span.end).empty();
                                          });
        return clipsIn || pointsIn ? std::string() : "Nothing in the selection to delete";
    }
    case C::Split:
    case C::Crop:
        return anyClip([&](const auto& c) { return crosses(c, span.start) || crosses(c, span.end); })
                   ? std::string()
                   : "No clip crosses the selection's edges";
    case C::Mute:
        return clipsIn ? std::string() : "No clip in the selection";
    case C::Reverse:
        return anyClip([&](const auto& c)
                       { return c.kind == model::ClipInfo::Kind::Audio && overlaps(c, span); })
                   ? std::string()
                   : "No audio clip in the selection";
    case C::Fade:
        return std::any_of(tracks.begin(), tracks.end(),
                           [&](const auto* t) { return !fadesOf(t->clips, span).empty(); })
                   ? std::string()
                   : "Put the selection over a clip's start or end, or across two audio clips that meet";
    case C::NewMidiClip:
        return tracks.empty() ? "Select time on a track" : std::string();
    case C::Duplicate:
        return std::string(); // even empty time: it overwrites where it lands
    }
    return {};
}

Session::StretchResult Session::editStretch(const model::TimeSelection& stretch, StretchCommand command)
{
    using C = StretchCommand;
    StretchResult result;
    if (!stretchRefusal(stretch, command).empty())
        return result;
    const auto span = stretch.span;
    std::vector<std::uint64_t>
        trackIds; // its tracks that hold clips, a group's row its tracks (groups.md §6.5)
    for (const auto& t : snapshot()->tracks) // in the project's order
        if (stretch.tracks.contains(t.id) && t.holdsClips())
            trackIds.push_back(t.id);

    // The clips on its tracks crossing `t`, split there; the right parts' ids.
    const auto splitAt = [&](double t)
    {
        updateSnapshotNow();
        std::vector<std::uint64_t> crossing;
        for (const auto& track : snapshot()->tracks)
            if (stretch.tracks.contains(track.id))
                for (const auto& c : track.clips)
                    if (crosses(c, t))
                        crossing.push_back(c.id);
        return crossing.empty() ? std::vector<std::uint64_t>() : splitClips(crossing, t);
    };
    // The clips inside it on its tracks (after splitting at its edges).
    const auto inside = [&](auto&& test)
    {
        updateSnapshotNow();
        std::vector<std::uint64_t> out;
        for (const auto& track : snapshot()->tracks)
            if (stretch.tracks.contains(track.id))
                for (const auto& c : track.clips)
                    if (within(c, span) && test(c))
                        out.push_back(c.id);
        return out;
    };
    const auto all = [](const model::ClipInfo&) { return true; };

    if (command == C::Duplicate)
    {
        const auto copied = copyStretch(stretch);
        return pasteStretch(copied, span.end, trackIds.empty() ? 0 : trackIds.front(), "Duplicate");
    }

    EditGroup group(*this, undoNameOf(command));
    result.span = span;
    result.changed = true;
    switch (command)
    {
    case C::Delete:
        for (const auto id : trackIds)
            if (auto* track = impl_->track(id))
                impl_->clearUnder(*track, span.start, span.end, {});
        for (const auto& lane : snapshot()->automationLanes)
            if (stretch.lanes.contains(lane.id))
                if (auto points = model::clearPoints(lane.points, span.start, span.end);
                    points != lane.points)
                    setLanePoints(lane.id, points);
        break;
    case C::Split:
        splitAt(span.start);
        splitAt(span.end);
        result.clips = inside(all);
        break;
    case C::Crop:
    {
        // The clips crossing it, and their parts once split: those outside it go.
        std::set<std::uint64_t> family;
        for (const auto& track : snapshot()->tracks)
            if (stretch.tracks.contains(track.id))
                for (const auto& c : track.clips)
                    if (overlaps(c, span) && !within(c, span))
                        family.insert(c.id);
        for (const double edge : {span.start, span.end})
            for (const auto id : splitAt(edge))
                family.insert(id);
        updateSnapshotNow();
        std::vector<std::uint64_t> outside;
        for (const auto& track : snapshot()->tracks)
            for (const auto& c : track.clips)
                if (family.contains(c.id) && !within(c, span))
                    outside.push_back(c.id);
        deleteClips(outside);
        result.clips = inside(all);
        break;
    }
    case C::Mute:
    {
        splitAt(span.start);
        splitAt(span.end);
        const auto clips = inside(all);
        const bool allMuted = std::all_of(clips.begin(), clips.end(), [&](auto id)
                                          { return model::findClip(*snapshot(), id)->first->muted; });
        setClipsMuted(clips, !allMuted);
        result.clips = clips;
        break;
    }
    case C::Reverse:
    {
        splitAt(span.start);
        splitAt(span.end);
        const auto isAudio = [](const model::ClipInfo& c) { return c.kind == model::ClipInfo::Kind::Audio; };
        std::vector<std::uint64_t> forward, backward;
        for (const auto id : inside(isAudio))
            (model::findClip(*snapshot(), id)->first->sound.reversed ? backward : forward).push_back(id);
        if (!forward.empty())
            setSound(forward, {.reversed = true}, "Reverse");
        if (!backward.empty())
            setSound(backward, {.reversed = false}, "Reverse");
        result.clips = inside(isAudio);
        break;
    }
    case C::Fade:
        for (const auto& track : snapshot()->tracks)
        {
            if (!stretch.tracks.contains(track.id))
                continue;
            const auto fades = fadesOf(track.clips, span);
            for (const auto& f : fades.fadeIns)
                setFades({f.clip}, model::Fade{f.length}, std::nullopt);
            for (const auto& f : fades.fadeOuts)
                setFades({f.clip}, std::nullopt, model::Fade{f.length});
            for (const auto& x : fades.crossfades)
                setCrossfade(x.out, model::Crossfade{.length = x.length, .from = x.from}, "Fade");
        }
        break;
    case C::NewMidiClip:
        for (const auto id : trackIds)
            if (const auto clip = createMidiClip(id, span.start, span.length()); clip != 0)
                result.clips.push_back(clip);
        break;
    case C::Duplicate:
        break; // above
    }
    return result;
}

Session::StretchClipboard Session::copyStretch(const model::TimeSelection& stretch) const
{
    StretchClipboard out;
    if (stretch.empty())
        return out;
    const auto s = snapshot();
    const auto& span = stretch.span;
    auto data = std::make_shared<StretchClipboard::Data>();
    data->length = span.length();
    // From the earliest clip across its start, so those come whole and are cut once pasted. Its
    // rows are tracks that hold clips, groups skipped (groups.md §11).
    double from = span.start;
    int top = -1, index = 0;
    for (const int i : model::clipTracks(*s))
    {
        if (const auto& track = s->tracks[static_cast<size_t>(i)]; stretch.tracks.contains(track.id))
        {
            top = top < 0 ? index : top;
            data->rows.push_back(index - top);
            for (const auto& c : track.clips)
                if (overlaps(c, span))
                    from = std::min(from, c.start);
        }
        ++index;
    }
    data->before = span.start - from;
    if (!stretch.tracks.empty())
        data->clips = copyClipsAcross(span.start, span.end, from, &stretch.tracks);
    for (const auto& lane : s->automationLanes)
        if (stretch.lanes.contains(lane.id))
            data->lanes.emplace_back(lane.id, model::piecePoints(lane.points, span.start, span.end));
    out.data_ = std::move(data);
    return out;
}

Session::StretchResult Session::pasteStretch(const StretchClipboard& clipboard, double at,
                                             std::uint64_t topTrack, const std::string& undoName)
{
    StretchResult result;
    if (clipboard.empty())
        return result;
    const auto& data = *clipboard.data_;
    at = std::max(0.0, at);
    const double length = data.length;
    const auto tracks = impl_->tracks();
    std::size_t first = 0;
    while (first < tracks.size() && tracks[first]->itemID.getRawID() != topTrack)
        ++first;
    if (!data.rows.empty() && first >= tracks.size())
        return result;

    EditGroup group(*this, undoName);
    // Where it lands is cleared, on each of its rows: it overwrites, gaps and all.
    for (const int row : data.rows)
        if (const auto index = first + static_cast<std::size_t>(row); index < tracks.size())
            impl_->clearUnder(*tracks[index], at, at + length, {});
        else
            ++result.rowsLeftOut;

    if (!data.clips.empty())
    {
        // Its clips come whole, so they're placed past the end of the song first, cut there to the
        // stretch, and the pieces moved into place: nothing they don't land on is touched.
        updateSnapshotNow();
        double end = at + length;
        for (const auto& track : snapshot()->tracks)
            for (const auto& c : track.clips)
                end = std::max(end, c.end());
        const double staged = std::ceil(end + length + data.before + 10.0);
        const double from = staged + data.before, to = from + length;
        // The clipboard's times run from its earliest clip: placed at `staged`, its stretch is [from, to).
        const auto pasted = placeClips(data.clips, first, staged, false);
        std::set<std::uint64_t> family(pasted.clips.begin(), pasted.clips.end());
        for (const double edge : {from, to})
        {
            updateSnapshotNow();
            std::vector<std::uint64_t> crossing;
            for (const auto& track : snapshot()->tracks)
                for (const auto& c : track.clips)
                    if (family.contains(c.id) && crosses(c, edge))
                        crossing.push_back(c.id);
            if (!crossing.empty())
                for (const auto id : splitClips(crossing, edge))
                    family.insert(id);
        }
        updateSnapshotNow();
        std::vector<std::uint64_t> outside;
        std::vector<model::ClipEdit> moves;
        for (const auto& track : snapshot()->tracks)
            for (const auto& c : track.clips)
                if (family.contains(c.id))
                {
                    if (!within(c, {from, to}))
                        outside.push_back(c.id);
                    else
                        moves.push_back({.clip = c.id,
                                         .track = track.id,
                                         .start = at + (c.start - from),
                                         .length = c.length,
                                         .offset = c.offset});
                }
        deleteClips(outside);
        applyClipEdits(moves, undoName);
        for (const auto& m : moves)
            result.clips.push_back(m.clip);
    }
    for (const auto& [id, piece] : data.lanes)
        if (const auto* lane = snapshot()->automationLane(id))
            if (auto points = model::placePoints(lane->points, piece, at, length); points != lane->points)
                setLanePoints(id, points);
    result.changed = true;
    result.span = {at, at + length};
    return result;
}

Session::StretchResult Session::moveStretch(const model::TimeSelection& stretch, double seconds,
                                            int trackShift, bool copy)
{
    StretchResult result;
    if (stretch.empty() || (std::abs(seconds) <= kEpsilon && trackShift == 0))
        return result;
    updateSnapshotNow();
    const auto& span = stretch.span;
    const double at = span.start + seconds;
    if (at < -kEpsilon)
        return result;
    // Each of its tracks lands `trackShift` tracks that hold clips down, groups skipped (groups.md
    // §6.5); none may land past the list.
    const auto& all = snapshot()->tracks;
    const auto holders = model::clipTracks(*snapshot());
    std::map<std::uint64_t, std::uint64_t> target;
    for (std::size_t i = 0; i < holders.size(); ++i)
        if (const auto& from = all[static_cast<std::size_t>(holders[i])]; stretch.tracks.contains(from.id))
        {
            const auto to = static_cast<long>(i) + trackShift;
            if (to < 0 || to >= static_cast<long>(holders.size()))
                return result;
            target[from.id] = all[static_cast<std::size_t>(holders[static_cast<std::size_t>(to)])].id;
        }
    if (copy)
    {
        std::uint64_t top = 0;
        for (const auto& t : all)
            if (target.contains(t.id))
            {
                top = target[t.id];
                break;
            }
        const auto copied = copyStretch(stretch);
        return pasteStretch(copied, at, top, "Copy");
    }

    EditGroup group(*this, "Move");
    const double length = span.length();
    // Its clips cut at its edges, then the pieces inside moved; what's where they land cleared.
    for (const double edge : {span.start, span.end})
    {
        updateSnapshotNow();
        std::vector<std::uint64_t> crossing;
        for (const auto& track : snapshot()->tracks)
            if (stretch.tracks.contains(track.id))
                for (const auto& c : track.clips)
                    if (crosses(c, edge))
                        crossing.push_back(c.id);
        if (!crossing.empty())
            splitClips(crossing, edge);
    }
    updateSnapshotNow();
    std::vector<model::ClipEdit> moves;
    std::set<std::uint64_t> moving;
    for (const auto& track : snapshot()->tracks)
        if (target.contains(track.id))
            for (const auto& c : track.clips)
                if (within(c, span))
                {
                    moves.push_back({.clip = c.id,
                                     .track = target[track.id],
                                     .start = c.start + seconds,
                                     .length = c.length,
                                     .offset = c.offset});
                    moving.insert(c.id);
                }
    for (const auto& [from, to] : target)
        if (auto* track = impl_->track(to))
            impl_->clearUnder(*track, at, at + length, moving);
    applyClipEdits(moves, "Move");
    // Lanes stay on their rows: the piece leaves its place and lands at its new time.
    for (const auto& lane : snapshot()->automationLanes)
        if (stretch.lanes.contains(lane.id))
        {
            const auto piece = model::piecePoints(lane.points, span.start, span.end);
            auto points =
                model::placePoints(model::clearPoints(lane.points, span.start, span.end), piece, at, length);
            if (points != lane.points)
                setLanePoints(lane.id, points);
        }
    for (const auto& m : moves)
        result.clips.push_back(m.clip);
    result.changed = true;
    result.span = {at, at + length};
    return result;
}

bool Session::editClipTime(std::uint64_t clipId, model::ClipTimeCommand command, double start, double end)
{
    updateSnapshotNow();
    const auto snap = snapshot();
    const auto found = model::findClip(*snap, clipId);
    if (!found || found->first->kind != model::ClipInfo::Kind::Midi)
        return false;
    const auto& clip = *found->first;
    const auto& tempo = snap->tempo;
    // The clip view's seconds as its notes' beats, and its content's seconds as beats and back.
    const double origin = model::clipViewOrigin(clip);
    const double a = std::max(0.0, tempo.beatsForSeconds(start - origin)),
                 b = tempo.beatsForSeconds(end - origin);
    if (!(b - a > kEpsilon))
        return false;
    const auto plan = model::clipTimePlan(command, a, b);
    const auto notes = model::planNotes(clip.notes, plan).notes;
    const auto controllers = model::planControllers(clip.controllers, plan);
    const auto edge = [&](double contentSeconds, bool isEnd)
    {
        return tempo.secondsForBeats(
            model::clipTimeEdge(command, a, b, tempo.beatsForSeconds(contentSeconds), isEnd));
    };
    std::optional<model::ClipLoop> loop;
    std::optional<double> length;
    if (clip.loop.on())
    {
        const double loopStart = edge(clip.loop.start, false), loopEnd = edge(clip.loop.end(), true);
        if (std::abs(loopStart - clip.loop.start) > kEpsilon ||
            std::abs(loopEnd - clip.loop.end()) > kEpsilon)
            loop = model::ClipLoop{loopStart, loopEnd - loopStart};
    }
    else if (const double to = edge(clip.offset + clip.length, true) - clip.offset;
             std::abs(to - clip.length) > kEpsilon)
        length = std::max(model::minClipLength, to);
    if (notes == clip.notes && controllers == clip.controllers && !loop && !length)
        return false;

    const char* name = command == model::ClipTimeCommand::Insert   ? "Insert time"
                       : command == model::ClipTimeCommand::Delete ? "Delete time"
                                                                   : "Duplicate time";
    const EditGroup group(*this, name);
    if (length)
        applyClipEdits({{.clip = clipId,
                         .track = snap->tracks[static_cast<size_t>(found->second)].id,
                         .start = clip.start,
                         .length = *length,
                         .offset = clip.offset}},
                       name);
    if (loop && loop->length > model::minClipLength)
        setClipLoop(clipId, *loop, name);
    setNotes(clipId, notes);
    if (controllers != clip.controllers)
        setControllers(clipId, controllers, name);
    return true;
}
} // namespace fd::engine
