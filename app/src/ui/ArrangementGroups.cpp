// Groups in the arrangement (ADR 0072, docs/design/groups.md §6): a group's row with its fold
// triangle and its members in miniature, the colour bars of the groups each row is in, folding, and
// where a dragged header lands, at its depth.

#include "model/Groups.h"
#include "ui/ArrangementView.h"

#include <algorithm>
#include <cmath>

namespace fd::app
{
void ArrangementView::setFoldedGroups(std::set<std::uint64_t> folded)
{
    if (folded == folded_)
        return;
    folded_ = std::move(folded);
    syncRows();
}

void ArrangementView::previewRowDrop(int row, std::optional<model::RowDrop> drop)
{
    if (drop)
        rowDrag_ = RowDrag{.row = row, .moving = true, .drop = *drop};
    else
        rowDrag_.reset();
    repaint();
}

void ArrangementView::paintMiniature(juce::Graphics& g, juce::Rectangle<float> body,
                                     const std::vector<MiniatureLine>& lines, float maxLine) const
{
    const auto& t = ui::Theme::current();
    if (lines.empty() || body.getHeight() < 1.0f)
        return;
    // A band each, top to bottom; a line a pixel thinner than its band, up to `maxLine`. More lines
    // than pixels share them.
    const float band = body.getHeight() / static_cast<float>(lines.size());
    const float line = std::clamp(band - 1.0f, 1.0f, maxLine);
    const float left = body.getX(), right = body.getRight();
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const auto& l = lines[i];
        const float y = std::floor(body.getY() + static_cast<float>(i) * band + (band - line) * 0.5f);
        const auto colour = t.clipColour(l.colour);
        for (const auto& s : l.spans)
        {
            // On whole pixels, as the sections over the song row's are.
            const float x0 = std::max(left, std::round(laneX(s.start))),
                        x1 = std::min(right, std::round(laneX(s.end)));
            if (x1 < left || x0 > right)
                continue;
            const juce::Rectangle<float> box(x0, y, std::max(1.0f, x1 - x0), line);
            g.setColour(s.muted ? t.clipDimmed(colour) : colour);
            g.fillRect(box);
            // Selected (a member, or clips, folded away): outlined as a selected clip is.
            if (l.selected || s.selected)
            {
                g.setColour(t.textPrimary);
                g.drawRect(box.expanded(static_cast<float>(t.shell.clipOutline)),
                           static_cast<float>(t.shell.clipOutline));
            }
        }
    }
}

void ArrangementView::paintGroupLane(juce::Graphics& g, int row, juce::Rectangle<int> lanes)
{
    const auto& t = ui::Theme::current();
    const auto& group = snapshot_->tracks[static_cast<size_t>(rows_[static_cast<size_t>(row)].track)];
    const bool folded = folded_.contains(group.id);
    // A member is selected while folded away: its line in the selection's colour (§6.4).
    const auto memberSelected = [&](const model::TrackInfo& m)
    {
        return folded &&
               (selection_.channel == m.id || (!selection_.hasTime() && selection_.rowTracks.contains(m.id)));
    };
    const auto spansOf = [&](const model::TrackInfo& track, std::vector<MiniatureLine::Span>& out)
    {
        for (const auto& c : track.clips)
            out.push_back({c.start, c.end(), c.muted, folded && selection_.clips.contains(c.id)});
    };
    std::vector<MiniatureLine> lines;
    for (const auto* m : model::membersOf(*snapshot_, group.id, false))
    {
        MiniatureLine line{.colour = m->colour, .selected = memberSelected(*m)};
        if (m->isGroup()) // one line wherever any of its tracks play (§6.2)
        {
            for (const auto* inside : model::membersOf(*snapshot_, m->id, true))
                if (inside->holdsClips())
                    spansOf(*inside, line.spans);
        }
        else
            spansOf(*m, line.spans);
        lines.push_back(std::move(line));
    }
    const auto body = rowBounds(row, lanes)
                          .withHeight(static_cast<float>(layout_.baseHeight(row)))
                          .withTrimmedBottom(1.0f)
                          .reduced(0.0f, static_cast<float>(t.shell.songOverviewInset));
    paintMiniature(g, body, lines, static_cast<float>(t.shell.miniatureLine));
}

void ArrangementView::paintGroupBars(juce::Graphics& g, int row, juce::Rectangle<float> header,
                                     const model::TrackHeaderSlots& slots)
{
    const auto& t = ui::Theme::current();
    const auto& r = rows_[static_cast<size_t>(row)];
    const auto at = [&header](const model::HeaderRect& rect)
    {
        return juce::Rectangle<float>(static_cast<float>(rect.x), static_cast<float>(rect.y),
                                      static_cast<float>(rect.w), static_cast<float>(rect.h))
            .translated(header.getX(), header.getY());
    };
    // The groups it's in, outermost first: a lane's are those of the row it follows, as deep as it is.
    std::vector<const model::TrackInfo*> above;
    if (r.track >= 0 && r.track < static_cast<int>(snapshot_->tracks.size()))
    {
        const auto& anchor = snapshot_->tracks[static_cast<size_t>(r.track)];
        above = model::groupsAbove(*snapshot_, anchor);
        if (r.isLane() && anchor.isGroup() && static_cast<int>(above.size()) < r.depth)
            above.push_back(&anchor);
    }
    above.resize(std::min(above.size(), static_cast<size_t>(r.depth)));
    for (size_t k = 0; k < above.size() && k < slots.groupBars.size(); ++k)
    {
        g.setColour(t.clipColour(above[k]->colour));
        g.fillRect(at(slots.groupBars[k]));
    }
    if (!slots.fold.isEmpty() && r.isGroup())
        ui::drawIcon(g, folded_.contains(r.channel) ? ui::Icon::FoldClosed : ui::Icon::FoldOpen,
                     at(slots.fold), t.textSecondary);
}
} // namespace fd::app
