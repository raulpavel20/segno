// The arrangement's time selection (ADR 0073, docs/design/time-selection.md §5–§7, §10): made by a
// drag on a clip's body, on empty space, in a lane away from its points, or across the song row's
// overview; changed with Shift and Ctrl; a stretch dragged by a title strip inside it.

#include "model/Groups.h"
#include "ui/ArrangementView.h"

#include <algorithm>
#include <cmath>

namespace fd::app
{
std::pair<std::set<std::uint64_t>, std::set<std::uint64_t>> ArrangementView::rowsBetween(int a, int b) const
{
    std::set<std::uint64_t> tracks, lanes;
    if (a > b)
        std::swap(a, b);
    for (int i = std::max(0, a); i <= std::min(b, static_cast<int>(rows_.size()) - 1); ++i)
    {
        const auto& r = rows_[static_cast<size_t>(i)];
        if (r.holdsClips() || r.isGroup()) // a group's row: everything in it (groups.md §6.5)
            tracks.insert(r.channel);
        else if (r.isLane())
            lanes.insert(r.lane);
    }
    return {tracks, lanes};
}

std::pair<std::set<std::uint64_t>, std::set<std::uint64_t>> ArrangementView::everyRow() const
{
    return rowsBetween(0, static_cast<int>(rows_.size()) - 1);
}

int ArrangementView::rowAtY(float y) const
{
    if (layout_.trackCount() == 0)
        return -1;
    const auto lanes = laneArea();
    if (const auto hit = layout_.hit(y - static_cast<float>(lanes.getY())))
        return hit->track;
    return y < static_cast<float>(lanes.getY()) ? 0 : layout_.trackCount() - 1;
}

void ArrangementView::beginTimeDrag(const juce::MouseEvent& e, int row, bool allRows, bool fromClip)
{
    const auto lanes = laneArea();
    const double t =
        snapped(timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX())), e.mods.isAltDown());
    TimeDrag d{.anchor = t, .anchorRow = row, .allRows = allRows, .fromClip = fromClip};
    if (row >= 0 && row < static_cast<int>(rows_.size()))
    {
        const auto& r = rows_[static_cast<size_t>(row)];
        if (r.holdsClips() || r.isGroup())
            d.channel = r.channel;
        else if (const auto* lane = rowLane(row); lane != nullptr && !lane->assignments.empty())
            d.channel = lane->assignments.front().channel;
    }
    drag_ = Drag::Time;
    if (e.mods.isShiftDown() && selection_.hasTime())
    {
        // Shift extends the selection there is, on its rows and to this one: from its edge farther
        // from the click, so a click outside grows it and one inside shortens it (§6).
        const auto span = *selection_.time;
        d.anchor = std::abs(t - span.start) < std::abs(t - span.end) ? span.end : span.start;
        d.baseTracks = selection_.rowTracks;
        d.baseLanes = selection_.rowLanes;
        d.channel = selection_.channel;
        d.fromClip = false;
        timeDrag_ = d;
        dragMoved_ = true; // a Shift+click changes it at once
        timeDragTo(e);
        return;
    }
    timeDrag_ = d;
}

void ArrangementView::timeDragTo(const juce::MouseEvent& e)
{
    if (!timeDrag_)
        return;
    const auto& d = *timeDrag_;
    const auto lanes = laneArea();
    const double now =
        snapped(timeline_.timeForX(e.position.x - static_cast<float>(lanes.getX())), e.mods.isAltDown());
    const model::TimeSpan span{std::max(0.0, std::min(d.anchor, now)), std::max(d.anchor, now)};
    if (!(span.end - span.start > 1e-9))
        return; // nothing yet: a drag inside one grid step
    auto [tracks, laneIds] = d.allRows ? everyRow() : rowsBetween(d.anchorRow, rowAtY(e.position.y));
    if (!d.allRows && (!d.baseTracks.empty() || !d.baseLanes.empty()))
    {
        // Shift: the rows it had, and those from its nearest row to this one.
        int nearest = -1, here = rowAtY(e.position.y);
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (rowInTime(rows_[static_cast<size_t>(i)]) &&
                (nearest < 0 || std::abs(i - here) < std::abs(nearest - here)))
                nearest = i;
        auto [between, betweenLanes] = rowsBetween(nearest < 0 ? here : nearest, here);
        tracks = d.baseTracks;
        laneIds = d.baseLanes;
        tracks.insert(between.begin(), between.end());
        laneIds.insert(betweenLanes.begin(), betweenLanes.end());
    }
    setQuietly(model::withTime(*snapshot_, span, tracks, laneIds, d.channel));
    repaint();
}

void ArrangementView::endTimeDrag(const juce::MouseEvent&)
{
    const auto d = std::exchange(timeDrag_, std::nullopt);
    // A click: what the press selected stays (the clip, or the row's track).
    notifySelection();
    if (!d || !dragMoved_ || !selection_.hasTime())
        return;
    // Stopped, it puts the playhead at its start, so Space plays it (§12).
    if (!session_.isPlaying())
        session_.setPosition(selection_.time->start);
}

void ArrangementView::stretchDragTo(const juce::MouseEvent& e)
{
    if (!stretchDrag_)
        return;
    const auto& d = *stretchDrag_;
    const auto lanes = laneArea();
    const double dx = (e.position.x - dragStart_.x) / timeline_.pixelsPerSecond();
    const double start = std::max(0.0, snapped(d.stretch.span.start + dx, e.mods.isAltDown()));
    // Down or up by whole tracks that hold clips, every one of its tracks landing on one; over a
    // group's row, where it was last (groups.md §6.5).
    const auto holders = model::clipTracks(*snapshot_);
    const int tracks = static_cast<int>(holders.size());
    const auto row = layout_.hit(e.position.y - static_cast<float>(lanes.getY()));
    const int under =
        model::clipTrackRank(*snapshot_, model::clipTrackOf(rows_, row ? row->track : layout_.trackCount(),
                                                            static_cast<int>(snapshot_->tracks.size())));
    int lowest = tracks, highest = -1;
    for (int i = 0; i < tracks; ++i)
        if (d.stretch.tracks.contains(
                snapshot_->tracks[static_cast<size_t>(holders[static_cast<size_t>(i)])].id))
        {
            lowest = std::min(lowest, i);
            highest = std::max(highest, i);
        }
    int shift = under >= 0     ? under - model::clipTrackRank(*snapshot_, d.track)
                : stretchMove_ ? stretchMove_->tracks
                               : 0;
    if (highest >= 0)
        shift = std::clamp(shift, -lowest, tracks - 1 - highest);
    previewStretchMove(StretchMove{
        .seconds = start - d.stretch.span.start, .tracks = shift, .copy = e.mods.isCommandDown()});
}

void ArrangementView::endStretchDrag()
{
    const auto d = std::exchange(stretchDrag_, std::nullopt);
    const auto move = std::exchange(stretchMove_, std::nullopt);
    repaint();
    if (!d)
        return;
    if (!dragMoved_ || !move || (std::abs(move->seconds) < 1e-9 && move->tracks == 0))
    {
        // A click on a clip's title strip selects the clip, as anywhere.
        setSelection(model::withClips(*snapshot_, {d->clip}, std::nullopt, selection_.channel));
        return;
    }
    const auto result = session_.moveStretch(d->stretch, move->seconds, move->tracks, move->copy);
    if (result.changed)
        moveTimeTo(result.span, move->tracks);
}

void ArrangementView::moveTimeTo(model::TimeSpan span, int trackShift)
{
    if (!selection_.hasTime())
        return;
    std::set<std::uint64_t> tracks;
    std::optional<std::uint64_t> channel;
    // Moved by tracks that hold clips (groups.md §6.5): a group's row stays only when nothing moved
    // down or up.
    const auto& all = snapshot_->tracks;
    const auto holders = model::clipTracks(*snapshot_);
    if (trackShift == 0)
        for (const auto& t : all)
            if (t.isGroup() && selection_.rowTracks.contains(t.id))
                tracks.insert(t.id);
    for (int i = 0; i < static_cast<int>(holders.size()); ++i)
        if (const auto& from = all[static_cast<size_t>(holders[static_cast<size_t>(i)])];
            selection_.rowTracks.contains(from.id))
            if (const int to = i + trackShift; to >= 0 && to < static_cast<int>(holders.size()))
            {
                const auto& onto = all[static_cast<size_t>(holders[static_cast<size_t>(to)])];
                tracks.insert(onto.id);
                if (selection_.channel == from.id)
                    channel = onto.id;
            }
    setSelection(model::withTime(*snapshot_, span, tracks, selection_.rowLanes,
                                 channel ? channel : selection_.channel));
}

bool ArrangementView::toggleTimeRow(int row)
{
    if (!selection_.hasTime() || row < 0 || row >= static_cast<int>(rows_.size()))
        return false;
    const auto& r = rows_[static_cast<size_t>(row)];
    auto tracks = selection_.rowTracks;
    auto lanes = selection_.rowLanes;
    const auto flip = [](std::set<std::uint64_t>& set, std::uint64_t id)
    {
        if (!set.erase(id))
            set.insert(id);
    };
    if (r.holdsClips())
        flip(tracks, r.channel);
    else if (r.isLane())
        flip(lanes, r.lane);
    else if (r.isGroup() && tracks.contains(r.channel)) // out with everything in it (groups.md §6.5)
    {
        tracks.erase(r.channel);
        for (const auto* m : model::membersOf(*snapshot_, r.channel, true))
            tracks.erase(m->id);
    }
    else if (r.isGroup())
        tracks.insert(r.channel);
    else
        return true; // a row a time selection can't have: nothing changes
    auto next = model::withTime(*snapshot_, *selection_.time, tracks, lanes, selection_.channel);
    setSelection(next.hasTime() ? next : model::Selection{.channel = selection_.channel});
    return true;
}

void ArrangementView::widenTimeToAllRows()
{
    if (!selection_.hasTime())
        return;
    const auto [tracks, lanes] = everyRow();
    setSelection(model::withTime(*snapshot_, *selection_.time, tracks, lanes, selection_.channel));
}

void ArrangementView::nudgeTime(int steps)
{
    if (!selection_.hasTime() || steps == 0)
        return;
    const double by = std::max(-selection_.time->start, steps * gridStepSeconds());
    if (std::abs(by) < 1e-9)
        return;
    if (const auto result = session_.moveStretch(timeSelection(), by, 0, false); result.changed)
        moveTimeTo(result.span);
}

void ArrangementView::extendTime(int steps)
{
    if (!selection_.hasTime())
        return;
    auto span = *selection_.time;
    span.end = std::max(span.start + gridStepSeconds(), snapped(span.end + steps * gridStepSeconds(), false));
    setSelection(
        model::withTime(*snapshot_, span, selection_.rowTracks, selection_.rowLanes, selection_.channel));
}

void ArrangementView::selectTimeOnEveryRow(model::TimeSpan span)
{
    const auto [tracks, lanes] = everyRow();
    setSelection(model::withTime(*snapshot_, span, tracks, lanes));
}

void ArrangementView::zoomToTime()
{
    if (!selection_.hasTime())
        return;
    timeline_.animateZoomTo(selection_.time->start, selection_.time->end);
    repaint();
}
} // namespace fd::app
