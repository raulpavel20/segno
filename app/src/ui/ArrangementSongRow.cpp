// The song row (M3 step C3, ADR 0049, docs/design/arranger.md §5-§11): the master's row, pinned
// under the ruler. Its header is the master's; its lane shows the song's sections over a miniature
// of every track. A drag makes a section; an edge moves its mark; its body moves it with its music
// (Ctrl copies), landing at another section's start or end.

#include "model/Sections.h"
#include "model/TrackHeaderLayout.h"
#include "ui/ArrangementView.h"
#include "ui/time/TimeView.h"

#include <utility>

namespace fd::app
{
namespace
{
constexpr const char* kSongKey = "song"; // its height among the row heights (view state)
/// How close to the song row's bottom edge (in its header) a drag resizes it, as for any row.
constexpr float kResizeGrab = 3.0f;
} // namespace

// ---- geometry ------------------------------------------------------------------------------

int ArrangementView::songRowHeight() const
{
    const auto& sh = ui::Theme::current().shell;
    const auto it = heights_.find(kSongKey);
    const double height = it != heights_.end() ? it->second : static_cast<double>(sh.songRowHeight);
    return juce::roundToInt(
        std::clamp(height, static_cast<double>(sh.songRowMinHeight), model::TrackLayout::maxHeight));
}

juce::Rectangle<int> ArrangementView::songArea() const
{
    return getLocalBounds().withTrimmedTop(rulerHeight()).removeFromTop(songRowHeight());
}

juce::Rectangle<int> ArrangementView::songHeaderArea() const
{
    return songArea().removeFromLeft(headerWidth());
}

juce::Rectangle<int> ArrangementView::songLaneArea() const
{
    return songArea().withTrimmedLeft(headerWidth());
}

std::vector<model::SectionInfo> ArrangementView::shownSections() const
{
    auto sections = snapshot_->sections;
    if (!sectionDrag_ || !sectionDrag_->moved)
        return sections;
    using K = SectionDrag::Kind;
    const auto& d = *sectionDrag_;
    if (d.kind == K::Start || d.kind == K::End)
        for (auto& s : sections)
            if (s.id == d.section)
            {
                s.start = d.from;
                s.end = d.to;
            }
    if (d.kind == K::Make && std::abs(d.to - d.from) > 1e-6)
    {
        sections.push_back({.name = model::newSectionName(sections),
                            .colour = model::newSectionColour(sections, model::kPaletteSize),
                            .start = std::min(d.from, d.to),
                            .end = std::max(d.from, d.to)});
        std::sort(sections.begin(), sections.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
    }
    return sections;
}

std::optional<ArrangementView::SectionAt> ArrangementView::sectionAt(float x) const
{
    const auto sections = shownSections();
    const float grab = ui::Theme::current().input.edgeGrab;
    using Z = SectionAt::Zone;
    // Inside one: near its start or end, an edge (at most a third of it), else its body.
    for (const auto& s : sections)
    {
        const float x0 = laneX(s.start), x1 = laneX(s.end);
        if (x >= x0 && x < x1)
        {
            const float edge = std::min(grab, (x1 - x0) / 3.0f);
            const auto zone = x - x0 <= edge ? Z::Start : x1 - x <= edge ? Z::End : Z::Body;
            return SectionAt{s.id, zone};
        }
    }
    // Just outside an edge.
    for (const auto& s : sections)
    {
        if (std::abs(x - laneX(s.end)) <= grab)
            return SectionAt{s.id, Z::End};
        if (std::abs(x - laneX(s.start)) <= grab)
            return SectionAt{s.id, Z::Start};
    }
    return std::nullopt;
}

std::optional<juce::Rectangle<int>> ArrangementView::sectionNameArea(std::uint64_t section) const
{
    const auto* s = snapshot_->section(section);
    if (s == nullptr)
        return std::nullopt;
    const auto lane = songLaneArea();
    const int x0 = std::max(lane.getX(), juce::roundToInt(laneX(s->start)));
    const int x1 = std::min(lane.getRight(), juce::roundToInt(laneX(s->end)));
    if (x1 - x0 < ui::Theme::current().shell.rowHeight)
        return std::nullopt; // too little of it in view to type in
    return juce::Rectangle<int>(x0, lane.getY(), x1 - x0, ui::Theme::current().shell.songTitleHeight);
}

std::optional<double> ArrangementView::sectionDrop() const
{
    return sectionDrag_ && sectionDrag_->moved ? sectionDrag_->drop : std::nullopt;
}

// ---- selection and names -------------------------------------------------------------------

void ArrangementView::selectSections(std::set<std::uint64_t> sections)
{
    // Not checked against this snapshot: a section just made is in the next one (pruning keeps it).
    sectionAnchor_ = sections.empty() ? std::nullopt : std::optional(*sections.begin());
    setSelection({.channel = snapshot_->master.id, .sections = std::move(sections)});
}

std::vector<std::uint64_t> ArrangementView::selectedSections() const
{
    std::vector<std::uint64_t> ids;
    for (const auto& s : snapshot_->sections) // by start
        if (selection_.sections.contains(s.id))
            ids.push_back(s.id);
    return ids;
}

bool ArrangementView::renameSection(std::uint64_t section)
{
    const auto area = sectionNameArea(section);
    const auto* s = snapshot_->section(section);
    if (!area || s == nullptr)
        return false;
    const auto& sh = ui::Theme::current().shell;
    names_.show(area->withHeight(std::max(area->getHeight(), sh.rowHeight)),
                juce::String::fromUTF8(s->name.c_str()), ui::TextStyle::Control,
                [this, section](const juce::String& name)
                { session_.renameSection(section, name.toStdString()); });
    return true;
}

bool ArrangementView::cancelSectionDrag()
{
    if (!sectionDrag_ && drag_ != Drag::SongResize)
        return false;
    sectionDrag_.reset();
    drag_ = Drag::None;
    repaint();
    return true;
}

// ---- drawing -------------------------------------------------------------------------------

void ArrangementView::paintSongRow(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& sh = t.shell;
    const auto header = songHeaderArea();
    const auto lane = songLaneArea();

    // The header: the master's (its name, its meter; its volume when tall enough).
    const bool masterSelected = selection_.channel == snapshot_->master.id && selection_.clips.empty() &&
                                selection_.sections.empty() && !selection_.lane;
    g.setColour(masterSelected ? t.surface2 : t.surface1);
    g.fillRect(header);
    const auto box = header.toFloat();
    const auto at = [&box](const model::HeaderRect& r)
    {
        return juce::Rectangle<float>(static_cast<float>(r.x), static_cast<float>(r.y),
                                      static_cast<float>(r.w), static_cast<float>(r.h))
            .translated(box.getX(), box.getY());
    };
    const auto slots = model::layoutTrackHeader(box.getWidth(), box.getHeight(), 0, true);
    g.setColour(t.textTertiary);
    g.fillRect(at(model::layoutTrackHeader(box.getWidth(), box.getHeight(), 0, false).colour));
    g.setColour(t.textPrimary);
    g.setFont(ui::font(ui::TextStyle::Name));
    g.drawText(juce::String::fromUTF8(snapshot_->master.name.c_str()), at(slots.name),
               juce::Justification::centredLeft, true);

    // The lane: the grid, every track as a thin line, the sections over them, the playhead.
    g.setColour(t.roles.subLane);
    g.fillRect(lane);
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(lane);
        time::paintGrid(g, grid_, timeline_, lane);
        paintOverview(g, lane.withTrimmedTop(sh.songTitleHeight));
        const auto sections = shownSections();
        const float top = static_cast<float>(lane.getY());
        const float height = static_cast<float>(lane.getHeight() - sh.dropLine); // above the divider
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto& s = sections[i];
            float x0 = laneX(s.start), x1 = laneX(s.end);
            if (x1 < static_cast<float>(lane.getX()) || x0 > static_cast<float>(lane.getRight()))
                continue;
            // A gap of canvas where the next one starts at its end, as between clips.
            if (i + 1 < sections.size() && std::abs(sections[i + 1].start - s.end) < 1e-6)
                x1 -= static_cast<float>(sh.clipGap);
            x0 = std::round(x0);
            x1 = std::round(x1);
            const auto block = juce::Rectangle<float>(x0, top, std::max(1.0f, x1 - x0), height);
            const auto colour = t.clipColour(s.colour);
            g.setColour(t.fill(colour));
            g.fillRect(block.withTrimmedTop(static_cast<float>(sh.songTitleHeight)));
            const auto title = block.withHeight(static_cast<float>(sh.songTitleHeight));
            g.setColour(colour);
            g.fillRect(title);
            // Its name stays in sight while its start is scrolled out to the left.
            const auto shown = title.withLeft(std::max(title.getX(), static_cast<float>(lane.getX())));
            if (titleShowsText(shown))
                paintTitleText(g, shown, s.name, false);
            if (selection_.sections.contains(s.id) && s.id != 0)
            {
                g.setColour(t.textPrimary);
                g.drawRect(block, static_cast<float>(sh.clipOutline));
            }
        }
        if (sections.empty())
            ui::paintEmptyState(g, lane.withHeight(sh.songTitleHeight), "Drag here to mark a section");
        time::paintPlayhead(g, timeline_, lane, playhead_);
    }
    // The divider below it: the rows scroll under it.
    g.setColour(t.line);
    g.fillRect(songArea().removeFromBottom(sh.dropLine));
}

void ArrangementView::paintOverview(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& t = ui::Theme::current();
    const auto& tracks = snapshot_->tracks;
    const auto body =
        area.withTrimmedBottom(t.shell.dropLine).reduced(0, t.shell.songOverviewInset).toFloat();
    if (tracks.empty() || body.getHeight() < 1.0f)
        return;
    // A band each, top to bottom in the arrangement's order; a line at most two pixels thick, a
    // pixel apart where there's room. More tracks than pixels share lines.
    const float band = body.getHeight() / static_cast<float>(tracks.size());
    const float line = std::clamp(band - 1.0f, 1.0f, 2.0f);
    const float left = static_cast<float>(area.getX()), right = static_cast<float>(area.getRight());
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        const float y = std::floor(body.getY() + static_cast<float>(i) * band + (band - line) * 0.5f);
        const auto colour = t.clipColour(track.colour);
        for (const auto& c : track.clips)
        {
            // On whole pixels, as the sections over them are.
            const float x0 = std::max(left, std::round(laneX(c.start))),
                        x1 = std::min(right, std::round(laneX(c.end())));
            if (x1 < left || x0 > right)
                continue;
            g.setColour(c.muted ? t.clipDimmed(colour) : colour);
            g.fillRect(juce::Rectangle<float>(x0, y, std::max(1.0f, x1 - x0), line));
        }
    }
}

void ArrangementView::paintSectionDrop(juce::Graphics& g)
{
    const auto drop = sectionDrop();
    if (!drop)
        return;
    const auto& t = ui::Theme::current();
    const auto lanes = songLaneArea().withBottom(getHeight());
    const float x = laneX(*drop);
    if (x < static_cast<float>(lanes.getX()) || x > static_cast<float>(lanes.getRight()))
        return;
    // Where the run lands, across every row: everything there moves.
    g.setColour(t.active);
    const auto width = static_cast<float>(t.shell.dropLine);
    g.fillRect(juce::Rectangle<float>(x - width * 0.5f, static_cast<float>(lanes.getY()), width,
                                      static_cast<float>(lanes.getHeight())));
}

void ArrangementView::placeSongControls()
{
    songLayer_.setBounds(songHeaderArea());
    if (masterControls_ == nullptr)
        return;
    auto& c = *masterControls_;
    const auto area = songLayer_.getLocalBounds();
    const auto slots = model::layoutTrackHeader(area.getWidth(), area.getHeight(), 0, true);
    const auto bounds = [](const model::HeaderRect& r)
    { return juce::Rectangle<double>(r.x, r.y, r.w, r.h).toNearestInt(); };
    const auto show = [](juce::Component& comp, const model::HeaderRect& r, juce::Rectangle<int> b)
    {
        comp.setVisible(!r.isEmpty());
        if (comp.isVisible())
            comp.setBounds(b);
    };
    auto controls = bounds(slots.controls);
    const int gap = ui::Theme::current().shell.itemGap;
    show(c.volume, slots.controls, controls.removeFromLeft((controls.getWidth() - gap) / 2));
    show(c.pan, slots.controls, controls.withTrimmedLeft(gap));
    show(c.meter, slots.meter, bounds(slots.meter));
}

// ---- the mouse -----------------------------------------------------------------------------

void ArrangementView::songMouseMove(const juce::MouseEvent& e)
{
    juce::MouseCursor cursor = juce::MouseCursor::NormalCursor;
    const auto header = songHeaderArea();
    if (header.contains(e.getPosition()))
    {
        if (e.position.y >= static_cast<float>(header.getBottom()) - 2.0f * kResizeGrab)
            cursor = juce::MouseCursor::UpDownResizeCursor; // its bottom edge resizes it
    }
    else if (const auto hit = sectionAt(e.position.x); hit && hit->zone != SectionAt::Zone::Body)
        cursor = juce::MouseCursor::LeftRightResizeCursor;
    setMouseCursor(cursor);
}

void ArrangementView::songMouseDown(const juce::MouseEvent& e)
{
    const auto header = songHeaderArea();
    const auto lane = songLaneArea();
    const auto& sh = ui::Theme::current().shell;
    headerActive_ = false;
    if (header.contains(e.getPosition()))
    {
        // Its bottom edge resizes it; else a click selects the master.
        if (e.position.y >= static_cast<float>(header.getBottom()) - 2.0f * kResizeGrab &&
            !e.mods.isPopupMenu())
        {
            drag_ = Drag::SongResize;
            resizeFrom_ = songRowHeight();
            return;
        }
        selectTrack(snapshot_->master.id);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            if (onAddMenu)
                onAddMenu(menu);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
        }
        return;
    }
    const auto hit = sectionAt(e.position.x);
    if (e.mods.isPopupMenu())
    {
        // A section's menu (it's selected first), or the empty row's.
        if (hit && !selection_.sections.contains(hit->section))
            selectSections({hit->section});
        juce::PopupMenu menu;
        if (onSectionMenu)
            onSectionMenu(menu, hit ? hit->section : 0);
        if (!hit && onAddMenu)
        {
            menu.addSeparator();
            onAddMenu(menu);
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
        if (onSectionActivated)
            onSectionActivated();
        return;
    }
    if (!e.mods.isLeftButtonDown())
        return;
    if (onSectionActivated)
        onSectionActivated();
    using K = SectionDrag::Kind;
    if (e.getNumberOfClicks() >= 2 && hit)
    {
        // On its name, rename it there; on its body, loop it.
        if (e.position.y < static_cast<float>(lane.getY() + sh.songTitleHeight))
            renameSection(hit->section);
        else if (onLoopSection)
        {
            if (!selection_.sections.contains(hit->section))
                selectSections({hit->section});
            onLoopSection();
        }
        return;
    }
    drag_ = Drag::Section;
    if (!hit)
    {
        // Empty time: a drag makes a section (a click selects the master, on release).
        const double t =
            snapped(timeline_.timeForX(e.position.x - static_cast<float>(lane.getX())), e.mods.isAltDown());
        sectionDrag_ = SectionDrag{.kind = K::Make, .from = t, .to = t};
        return;
    }
    const auto* s = snapshot_->section(hit->section);
    if (s == nullptr)
        return;
    if (hit->zone != SectionAt::Zone::Body)
    {
        selectSections({hit->section});
        sectionDrag_ = SectionDrag{.kind = hit->zone == SectionAt::Zone::Start ? K::Start : K::End,
                                   .section = hit->section,
                                   .from = s->start,
                                   .to = s->end};
        return;
    }
    // Its body: select it (Shift: the run from the last one picked to it), then a drag moves the
    // run with its music; Ctrl copies it.
    if (e.mods.isShiftDown() && sectionAnchor_ && snapshot_->section(*sectionAnchor_) != nullptr)
    {
        const auto* anchor = snapshot_->section(*sectionAnchor_);
        const double a = std::min(anchor->start, s->start), b = std::max(anchor->end, s->end);
        std::set<std::uint64_t> run;
        for (const auto& x : snapshot_->sections)
            if (x.start >= a - 1e-6 && x.end <= b + 1e-6)
                run.insert(x.id);
        const auto keep = sectionAnchor_;
        setSelection(model::withSections(*snapshot_, std::move(run)));
        sectionAnchor_ = keep;
    }
    std::optional<std::uint64_t> clickedInSelection;
    if (!e.mods.isShiftDown())
    {
        if (selection_.sections.contains(hit->section) && selection_.sections.size() > 1)
            clickedInSelection = hit->section; // a click without a drag selects just it
        else
            selectSections({hit->section});
    }
    sectionDrag_ = SectionDrag{.kind = e.mods.isCommandDown() ? K::Copy : K::Move,
                               .section = hit->section,
                               .clickedInSelection = clickedInSelection};
}

void ArrangementView::songMouseDrag(const juce::MouseEvent& e)
{
    const auto& t = ui::Theme::current();
    if (drag_ == Drag::SongResize)
    {
        heights_[kSongKey] =
            std::clamp(resizeFrom_ + static_cast<double>(e.position.y - dragStart_.y),
                       static_cast<double>(t.shell.songRowMinHeight), model::TrackLayout::maxHeight);
        syncLayout();
        placeHeaderControls();
        placeSongControls();
        repaint();
        return;
    }
    if (!sectionDrag_)
        return;
    auto& d = *sectionDrag_;
    if (!d.moved && e.getDistanceFromDragStart() < t.input.dragThreshold)
        return;
    d.moved = true;
    const auto lane = songLaneArea();
    const double at =
        snapped(timeline_.timeForX(e.position.x - static_cast<float>(lane.getX())), e.mods.isAltDown());
    const double shortest = std::max(1e-3, gridStepSeconds());
    using K = SectionDrag::Kind;
    switch (d.kind)
    {
    case K::Make:
        d.to = std::max(0.0, at);
        break;
    case K::Start:
    case K::End:
    {
        // Within its neighbours, never shorter than a grid step.
        double before = 0.0, after = model::kSongEnd;
        for (const auto& s : snapshot_->sections)
        {
            if (s.id == d.section)
                continue;
            if (s.end <= d.from + 1e-6)
                before = std::max(before, s.end);
            if (s.start >= d.to - 1e-6)
                after = std::min(after, s.start);
        }
        if (d.kind == K::Start)
            d.from = std::clamp(at, before, d.to - shortest);
        else
            d.to = std::clamp(at, d.from + shortest, after);
        break;
    }
    case K::Move:
    case K::Copy:
    {
        d.kind = e.mods.isCommandDown() ? K::Copy : K::Move; // Ctrl may come or go while dragging
        const auto span = model::spanOf(snapshot_->sections, selectedSections());
        if (!span)
            break;
        // The nearest boundary it can land at: outside the run (a copy may land at its own edges).
        std::optional<double> best;
        const double pointer = timeline_.timeForX(e.position.x - static_cast<float>(lane.getX()));
        for (const double b : model::sectionBoundaries(snapshot_->sections))
        {
            const bool inside = d.kind == K::Copy ? b > span->first + 1e-6 && b < span->second - 1e-6
                                                  : b > span->first - 1e-6 && b < span->second + 1e-6;
            if (!inside && (!best || std::abs(b - pointer) < std::abs(*best - pointer)))
                best = b;
        }
        d.drop = best;
        break;
    }
    }
    repaint();
}

void ArrangementView::songMouseUp(const juce::MouseEvent&)
{
    const auto was = std::exchange(drag_, Drag::None);
    const auto d = std::exchange(sectionDrag_, std::nullopt);
    repaint();
    if (was == Drag::SongResize || !d)
        return;
    using K = SectionDrag::Kind;
    switch (d->kind)
    {
    case K::Make:
    {
        const double a = std::min(d->from, d->to), b = std::max(d->from, d->to);
        if (!d->moved || b - a < 1e-6)
        {
            selectTrack(snapshot_->master.id); // a click on empty time: the master
            return;
        }
        if (const auto id = session_.addSection(a, b))
            selectSections({id});
        else if (onMessage)
            onMessage("A section can't overlap another one");
        return;
    }
    case K::Start:
    case K::End:
        if (d->moved)
            session_.setSectionMarks(d->section, d->from, d->to);
        return;
    case K::Move:
    case K::Copy:
        if (!d->moved)
        {
            if (d->clickedInSelection)
                selectSections({*d->clickedInSelection});
            return;
        }
        if (!d->drop)
            return;
        if (d->kind == K::Move)
            session_.moveSections(selectedSections(), *d->drop);
        else if (const auto copies = session_.copySections(selectedSections(), *d->drop); !copies.empty())
            selectSections({copies.begin(), copies.end()});
        return;
    }
}
} // namespace fd::app
