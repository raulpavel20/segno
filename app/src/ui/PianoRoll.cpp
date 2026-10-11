#include "ui/PianoRoll.h"

#include "ui/time/TimeView.h"

#include <cmath>

namespace fd::app
{
namespace
{
bool isBlackKey(int pitch)
{
    const int k = pitch % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

/// "C3" for 60 (the suite's convention, Unit::Note).
juce::String noteName(int pitch)
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[pitch % 12]) + juce::String(pitch / 12 - 2);
}

} // namespace

PianoRoll::PianoRoll(engine::Session& session) : session_(session)
{
    timeline_.setLineSpacing(lineSpacing);
    setWantsKeyboardFocus(false);
}

PianoRoll::~PianoRoll()
{
    if (previewPitch_)
        session_.computerKeyboardNote(*previewPitch_, 0.0f);
}

// ---- geometry ------------------------------------------------------------------------------

juce::Rectangle<int> PianoRoll::rulerArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().removeFromTop(s.rulerHeight).withTrimmedLeft(s.keyboardWidth);
}

juce::Rectangle<int> PianoRoll::velocityArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().removeFromBottom(s.velocityHeight).withTrimmedLeft(s.keyboardWidth);
}

juce::Rectangle<int> PianoRoll::keysArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedTop(s.rulerHeight).withTrimmedBottom(s.velocityHeight + 1).removeFromLeft(s.keyboardWidth);
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedTop(s.rulerHeight).withTrimmedBottom(s.velocityHeight + 1).withTrimmedLeft(s.keyboardWidth);
}

double PianoRoll::contentStartSeconds() const
{
    return clip_ ? clip_->start - clip_->offset : 0.0;
}

double PianoRoll::beatForSeconds(double seconds) const
{
    return tempo_.beatsForSeconds(seconds - contentStartSeconds());
}

double PianoRoll::secondsForBeat(double beat) const
{
    return contentStartSeconds() + tempo_.secondsForBeats(beat);
}

model::NoteGeometry PianoRoll::geometry() const
{
    return {.originX = timeline_.xForTime(contentStartSeconds()),
            .pixelsPerBeat = timeline_.pixelsPerSecond() * tempo_.secondsForBeats(1.0),
            .topEdge = topEdge_,
            .rowHeight = rowHeight_};
}

double PianoRoll::gridStep() const
{
    return timeline_.gridStepBeats(tempo_);
}

model::BeatGrid PianoRoll::grid(bool bypass) const
{
    return {.step = snap_ && !bypass ? gridStep() : 0.0, .origin = clip_ ? clip_->contentStartBeat : 0.0};
}

juce::Point<float> PianoRoll::pointFor(double beat, int pitch) const
{
    const auto g = geometry();
    const auto area = gridArea();
    return {static_cast<float>(area.getX() + g.xForBeat(beat)),
            static_cast<float>(area.getY() + g.yForPitch(pitch) + rowHeight_ * 0.5)};
}

void PianoRoll::clampPitchView()
{
    const double rows = gridArea().getHeight() / rowHeight_;
    topEdge_ = std::clamp(topEdge_, std::min(128.0, rows), 128.0);
}

void PianoRoll::fitToClip()
{
    const auto area = gridArea();
    if (!clip_ || area.getWidth() <= 1 || area.getHeight() <= 1)
        return; // once laid out
    needsFit_ = false;
    timeline_.setWidth(area.getWidth());
    const double length = std::max(clip_->length, tempo_.secondsForBeats(tempo_.beatsPerBar()));
    const double margin = length * 0.03;
    timeline_.setView(std::max(0.0, clip_->start - margin), area.getWidth() / (length + 2.0 * margin));

    // Pitches: centred on the notes, or on C3..C4 for an empty clip.
    int lo = 60, hi = 72;
    if (!notes_.empty())
    {
        lo = 127;
        hi = 0;
        for (const auto& n : notes_)
        {
            lo = std::min(lo, n.pitch);
            hi = std::max(hi, n.pitch);
        }
    }
    topEdge_ = (lo + hi + 1) * 0.5 + area.getHeight() / rowHeight_ * 0.5;
    clampPitchView();
}

void PianoRoll::resized()
{
    // Keep the pitch in the middle where it is when the height changes.
    const double height = gridArea().getHeight();
    if (lastGridHeight_ > 0.0 && height > 0.0)
        topEdge_ += (height - lastGridHeight_) / rowHeight_ * 0.5;
    lastGridHeight_ = height;
    timeline_.setWidth(gridArea().getWidth());
    if (needsFit_)
        fitToClip();
    clampPitchView();
}

// ---- state ---------------------------------------------------------------------------------

void PianoRoll::setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo)
{
    tempo_ = tempo;
    if (!clip || clip->kind != model::ClipInfo::Kind::Midi)
    {
        previewOff();
        clip_.reset();
        notes_.clear();
        selection_.clear();
        preview_.reset();
        gesture_ = Gesture::None;
        repaint();
        return;
    }
    if (clip_ && clip_->id == clip->id)
    {
        if (clip->notes != notes_)
        {
            // Undo, redo or an edit elsewhere: keep selecting the notes that are still there.
            std::set<std::size_t> selection;
            std::vector<bool> used(clip->notes.size(), false);
            for (auto i : selection_)
                for (std::size_t j = 0; i < notes_.size() && j < clip->notes.size(); ++j)
                    if (!used[j] && clip->notes[j] == notes_[i])
                    {
                        used[j] = true;
                        selection.insert(j);
                        break;
                    }
            selection_ = std::move(selection);
            notes_ = clip->notes;
        }
        clip_ = std::move(clip);
    }
    else
    {
        previewOff();
        clip_ = std::move(clip);
        notes_ = clip_->notes;
        selection_.clear();
        preview_.reset();
        gesture_ = Gesture::None;
        needsFit_ = true;
        fitToClip();
    }
    repaint();
}

void PianoRoll::frame(double playheadSeconds)
{
    playhead_ = playheadSeconds;
    playheadTracker_.update(*this, timeline_, gridArea(), playhead_);
}

const std::vector<model::Note>& PianoRoll::shownNotes() const
{
    return preview_ ? preview_->notes : notes_;
}

const std::set<std::size_t>& PianoRoll::shownSelection() const
{
    return preview_ ? preview_->selection : selection_;
}

void PianoRoll::commit(model::NoteEdit edit)
{
    preview_.reset();
    if (!clip_)
        return;
    const bool changed = edit.notes != notes_;
    notes_ = std::move(edit.notes);
    selection_ = std::move(edit.selection);
    if (changed)
        session_.setNotes(clip_->id, notes_); // one undo step; the snapshot follows
    repaint();
}

// ---- commands ------------------------------------------------------------------------------

void PianoRoll::deleteSelection()
{
    if (clip_ && !selection_.empty())
        commit(model::deleteNotes(notes_, selection_));
}

void PianoRoll::duplicateSelection()
{
    if (clip_ && !selection_.empty())
        commit(model::duplicateNotes(notes_, selection_, gridStep(), tempo_.beatsPerBar()));
}

std::vector<model::Note> PianoRoll::copySelection() const
{
    std::vector<model::Note> out;
    for (const auto i : selection_)
        if (i < notes_.size())
            out.push_back(notes_[i]);
    if (out.empty())
        return out;
    const auto first = std::min_element(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; })->start;
    for (auto& n : out)
        n.start -= first;
    return out;
}

void PianoRoll::pasteNotes(const std::vector<model::Note>& notes, double seconds)
{
    if (!clip_ || notes.empty())
        return;
    const bool over = seconds >= clip_->start && seconds < clip_->end();
    const double at = beatForSeconds(over ? seconds : clip_->start);
    model::NoteEdit edit{notes_, {}};
    for (const auto& n : notes)
    {
        auto pasted = n;
        pasted.start = std::max(0.0, n.start + at);
        edit.selection.insert(edit.notes.size());
        edit.notes.push_back(pasted);
    }
    commit(std::move(edit));
}

void PianoRoll::selectAll()
{
    selection_.clear();
    for (std::size_t i = 0; i < notes_.size(); ++i)
        selection_.insert(i);
    repaint();
}

void PianoRoll::transpose(int semitones)
{
    if (clip_ && !selection_.empty())
        commit(model::transposeNotes(notes_, selection_, semitones));
}

void PianoRoll::nudge(int gridSteps)
{
    if (clip_ && !selection_.empty())
        commit(model::nudgeNotes(notes_, selection_, gridSteps * gridStep()));
}

void PianoRoll::quantise()
{
    if (clip_ && !notes_.empty())
        commit(model::quantiseNotes(notes_, selection_, {.step = gridStep(), .origin = clip_->contentStartBeat}));
}

// ---- hearing notes -------------------------------------------------------------------------

void PianoRoll::previewOn(int pitch, float velocity)
{
    if (previewPitch_ == pitch)
        return;
    previewOff();
    session_.computerKeyboardNote(pitch, velocity);
    previewPitch_ = pitch;
    repaint(keysArea());
}

void PianoRoll::previewOff()
{
    if (!previewPitch_)
        return;
    session_.computerKeyboardNote(*previewPitch_, 0.0f);
    previewPitch_.reset();
    repaint(keysArea());
}

// ---- painting ------------------------------------------------------------------------------

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (!clip_)
        return; // the detail panel says what to select
    timeline_.gridLines(tempo_, lines_);
    paintRows(g);
    paintNotes(g);
    paintVelocities(g);
    paintKeys(g);
    paintRuler(g);

    // The playhead through the grid and the velocity lane.
    juce::Graphics::ScopedSaveState saved(g);
    const auto lanes = gridArea().withBottom(getHeight());
    g.reduceClipRegion(lanes);
    time::paintPlayhead(g, timeline_, lanes, playhead_);
}

void PianoRoll::paintRows(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = gridArea();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    const auto geo = geometry();

    // Rows: black keys a tone darker; a line under every C (octaves) and between E and F.
    const auto white = t.roles.keyRowLight;
    const auto black = t.roles.keyRowDark;
    const int highest = std::min(127, static_cast<int>(std::ceil(topEdge_)) - 1);
    const int lowest = std::max(0, static_cast<int>(std::floor(topEdge_ - 1.0 - area.getHeight() / rowHeight_)));
    const auto x = static_cast<float>(area.getX()), w = static_cast<float>(area.getWidth());
    const auto rh = static_cast<float>(rowHeight_);
    for (int p = lowest; p <= highest; ++p)
    {
        const float y = static_cast<float>(area.getY() + geo.yForPitch(p));
        g.setColour(isBlackKey(p) ? black : white);
        g.fillRect(x, y, w, rh);
        if (p % 12 == 0 || p % 12 == 5)
        {
            g.setColour(p % 12 == 0 ? t.roles.gridOctave : t.roles.gridMinor);
            g.fillRect(x, y + rh - 1.0f, w, 1.0f);
        }
    }

    // Outside the clip: notes there are kept but don't play.
    const float clipX0 = static_cast<float>(area.getX() + timeline_.xForTime(clip_->start));
    const float clipX1 = static_cast<float>(area.getX() + timeline_.xForTime(clip_->end()));
    g.setColour(t.roles.shade);
    if (clipX0 > x)
        g.fillRect(x, static_cast<float>(area.getY()), clipX0 - x, static_cast<float>(area.getHeight()));
    if (clipX1 < x + w)
        g.fillRect(clipX1, static_cast<float>(area.getY()), x + w - clipX1, static_cast<float>(area.getHeight()));

    time::paintGrid(g, lines_, timeline_, area);
}

void PianoRoll::paintNotes(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = gridArea();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    const auto geo = geometry();
    const auto& notes = shownNotes();
    const auto& selection = shownSelection();
    const auto colour = t.clipColour(clip_->colour);
    const double clipStart = beatForSeconds(clip_->start), clipEnd = beatForSeconds(clip_->end());
    const bool labels = rowHeight_ >= 10.0;
    g.setFont(ui::font(ui::TextStyle::Micro));

    for (int pass = 0; pass < 2; ++pass) // selected notes on top
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const bool selected = selection.contains(i);
            if (selected != (pass == 1))
                continue;
            const auto& n = notes[i];
            const float x0 = static_cast<float>(area.getX() + geo.xForBeat(n.start));
            const float x1 = static_cast<float>(area.getX() + geo.xForBeat(n.start + n.length));
            auto r = juce::Rectangle<float>(x0, static_cast<float>(area.getY() + geo.yForPitch(n.pitch)),
                                            std::max(2.0f, x1 - x0), static_cast<float>(rowHeight_))
                         .reduced(0.0f, 1.0f);
            if (r.getWidth() > 4.0f)
                r = r.withTrimmedRight(1.0f); // a hairline between notes that touch
            if (!r.intersects(area.toFloat()))
                continue;
            const bool inside = n.start < clipEnd && n.start + n.length > clipStart;
            auto fill = t.noteFill(colour, n.velocity, selected);
            g.setColour(inside ? fill : t.faded(fill));
            g.fillRoundedRectangle(r, 2.0f);
            if (selected)
            {
                g.setColour(t.textPrimary);
                g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
            }
            if (labels && r.getWidth() > 30.0f)
            {
                g.setColour(t.roles.onClip);
                g.drawText(noteName(n.pitch), r.reduced(4.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }

    if (gesture_ == Gesture::Box && moved_)
        time::paintBoxSelection(g, juce::Rectangle<float>(dragStart_, lastDrag_));
}

void PianoRoll::paintVelocities(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = velocityArea();
    g.setColour(t.line);
    g.fillRect(getLocalBounds().withY(area.getY() - 1).withHeight(1));
    g.setColour(t.roles.subLane);
    g.fillRect(area);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    time::paintGrid(g, lines_, timeline_, area, false);

    const auto geo = geometry();
    const auto inner = area.reduced(0, 5).toFloat();
    const auto& notes = shownNotes();
    const auto& selection = shownSelection();
    const auto colour = t.clipColour(clip_->colour);
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const bool selected = selection.contains(i);
            if (selected != (pass == 1))
                continue;
            const float x = static_cast<float>(area.getX() + geo.xForBeat(notes[i].start));
            if (x < static_cast<float>(area.getX()) - 4.0f || x > static_cast<float>(area.getRight()) + 4.0f)
                continue;
            const float top = inner.getBottom() - notes[i].velocity * inner.getHeight();
            g.setColour(selected ? t.textPrimary : t.noteFill(colour, notes[i].velocity, false));
            g.fillRect(x, top, 1.0f, inner.getBottom() - top);
            g.fillRect(x - 2.0f, top - 1.0f, 5.0f, 3.0f);
        }

    if (gesture_ == Gesture::Velocity || gesture_ == Gesture::VelocityOffset)
    {
        // The value under the gesture: the note nearest the mouse.
        const auto geoX = lastDrag_.x - static_cast<float>(area.getX());
        std::optional<std::size_t> nearest;
        double best = 1e9;
        for (std::size_t i = 0; i < notes.size(); ++i)
            if (const double d = std::abs(geo.xForBeat(notes[i].start) - geoX); d < best)
            {
                best = d;
                nearest = i;
            }
        if (nearest)
        {
            g.setColour(t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::Value));
            g.drawText("Velocity " + juce::String(juce::roundToInt(notes[*nearest].velocity * 127.0f)),
                       area.reduced(6, 2).removeFromTop(14), juce::Justification::topRight, false);
        }
    }
}

void PianoRoll::paintKeys(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = keysArea();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    const auto geo = geometry();
    const auto whiteKey = t.roles.keyWhite;
    const auto blackKey = t.roles.keyBlack;
    g.setColour(whiteKey);
    g.fillRect(area);

    const auto x = static_cast<float>(area.getX()), w = static_cast<float>(area.getWidth());
    const auto rh = static_cast<float>(rowHeight_);
    const int highest = std::min(127, static_cast<int>(std::ceil(topEdge_)) - 1);
    const int lowest = std::max(0, static_cast<int>(std::floor(topEdge_ - 1.0 - area.getHeight() / rowHeight_)));
    g.setFont(ui::font(ui::TextStyle::Micro));
    for (int p = lowest; p <= highest; ++p)
    {
        const float y = static_cast<float>(area.getY() + geo.yForPitch(p));
        const bool held = previewPitch_ == p;
        if (isBlackKey(p))
        {
            g.setColour(held ? t.active : blackKey);
            g.fillRect(x, y, w * 0.62f, rh);
            g.setColour(t.roles.keySeam); // where the white keys meet, beside it
            g.fillRect(x + w * 0.62f, y + rh * 0.5f, w * 0.38f, 1.0f);
            continue;
        }
        if (held)
        {
            g.setColour(t.active);
            g.fillRect(x, y, w, rh);
        }
        if (p % 12 == 0 || p % 12 == 5)
        {
            g.setColour(t.roles.keySeam);
            g.fillRect(x, y + rh - 1.0f, w, 1.0f);
        }
        if (p % 12 == 0 && rowHeight_ >= 8.0)
        {
            g.setColour(held ? t.onActive : t.surface0);
            g.drawText(noteName(p), juce::Rectangle<float>(x, y, w - 4.0f, rh), juce::Justification::centredRight, false);
        }
    }
    g.setColour(t.surface0);
    g.fillRect(area.getRight() - 1, area.getY(), 1, area.getHeight());
}

void PianoRoll::paintRuler(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.surface1);
    g.fillRect(getLocalBounds().removeFromTop(t.shell.rulerHeight)); // the corner above the keys too
    time::paintRuler(g, lines_, timeline_, rulerArea(),
                     {.range = std::pair{clip_->start, clip_->end()}, .rangeColour = t.clipColour(clip_->colour)});
}

// ---- input ---------------------------------------------------------------------------------

std::optional<std::size_t> PianoRoll::velocityStemAt(float localX) const
{
    const auto geo = geometry();
    std::optional<std::size_t> best;
    double bestDistance = 5.0;
    for (std::size_t i = 0; i < notes_.size(); ++i)
    {
        const double d = std::abs(geo.xForBeat(notes_[i].start) - localX);
        if (d < bestDistance - 0.01 || (best && std::abs(d - bestDistance) <= 0.01 && selection_.contains(i)))
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

float PianoRoll::velocityAtY(float y) const
{
    const auto inner = velocityArea().reduced(0, 5).toFloat();
    return std::clamp(1.0f - (y - inner.getY()) / inner.getHeight(), 0.0f, 1.0f);
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;
    const auto pos = e.getPosition();
    if (clip_ && keysArea().contains(pos))
        cursor = juce::MouseCursor::PointingHandCursor;
    else if (clip_ && gridArea().contains(pos))
    {
        const auto local = e.position - gridArea().getPosition().toFloat();
        if (const auto hit = model::hitNote(notes_, geometry(), local.x, local.y, ui::Theme::current().input.edgeGrab))
            cursor = hit->zone == model::NoteHit::Zone::Body ? juce::MouseCursor::NormalCursor
                                                              : juce::MouseCursor::LeftRightResizeCursor;
    }
    setMouseCursor(cursor);
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    if (onActivated)
        onActivated();
    gesture_ = Gesture::None;
    preview_.reset();
    if (!clip_)
        return;
    dragStart_ = lastDrag_ = e.position;
    moved_ = false;
    const auto pos = e.getPosition();
    const auto area = gridArea();
    const auto local = e.position - area.getPosition().toFloat();
    const auto geo = geometry();
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();

    if (keysArea().contains(pos))
    {
        gesture_ = Gesture::Keys;
        previewOn(std::clamp(geo.pitchForY(local.y), 0, 127), drawVelocity_);
        return;
    }
    if (rulerArea().contains(pos))
    {
        gesture_ = Gesture::Ruler;
        rulerGesture_.begin(local.x);
        return;
    }
    if (velocityArea().contains(pos))
    {
        base_ = {notes_, selection_};
        if (const auto stem = velocityStemAt(local.x); stem && selection_.contains(*stem))
        {
            gesture_ = Gesture::VelocityOffset; // drag the selection's velocities together
            return;
        }
        gesture_ = Gesture::Velocity; // draw velocities (of the selection, if any)
        const double tolerance = 4.0 / geo.pixelsPerBeat;
        const double beat = geo.beatForX(local.x);
        preview_ = model::NoteEdit{model::paintVelocity(notes_, beat - tolerance, beat + tolerance, velocityAtY(e.position.y),
                                                        selection_.empty() ? nullptr : &selection_),
                                   selection_};
        repaint();
        return;
    }
    if (!area.contains(pos))
        return;

    const auto hit = model::hitNote(notes_, geo, local.x, local.y, ui::Theme::current().input.edgeGrab);
    if (e.mods.isPopupMenu())
    {
        // The notes' menu, on the note under the pointer (selected first unless it's in the
        // selection), else on the selection.
        if (hit && !selection_.contains(hit->index))
        {
            selection_ = {hit->index};
            repaint();
        }
        if (onMenu)
            onMenu();
        return;
    }
    if (e.getNumberOfClicks() >= 2)
    {
        if (hit)
        {
            commit(model::deleteNotes(notes_, {hit->index}));
            return;
        }
        // Draw a note one grid step long in the step under the mouse; dragging lengthens it.
        const auto snapping = grid(e.mods.isAltDown());
        const double beat = std::max(0.0, geo.beatForX(local.x));
        const model::Note note{.pitch = std::clamp(geo.pitchForY(local.y), 0, 127),
                               .start = std::max(0.0, snapping.floor(beat)),
                               .length = gridStep(),
                               .velocity = drawVelocity_};
        base_ = model::addNote(notes_, note);
        if (base_.selection.empty())
            return;
        preview_ = base_;
        gesture_ = Gesture::Draw;
        drag_ = {.kind = model::NoteDragKind::ResizeEnd, .anchor = *base_.selection.begin()};
        previewOn(note.pitch, note.velocity);
        repaint();
        return;
    }
    clickedInSelection_.reset();
    if (hit)
    {
        if (!additive && selection_.size() > 1 && selection_.contains(hit->index))
            clickedInSelection_ = hit->index; // stays a group for a drag; just this note for a click
        auto selection = selection_;
        if (additive && selection.contains(hit->index))
            selection.erase(hit->index);
        else if (additive)
            selection.insert(hit->index);
        else if (!selection.contains(hit->index))
            selection = {hit->index};
        selection_ = std::move(selection);
        repaint();
        if (!selection_.contains(hit->index))
            return; // Shift-click removed it
        gesture_ = Gesture::Notes;
        base_ = {notes_, selection_};
        drag_ = {.kind = hit->zone == model::NoteHit::Zone::StartEdge ? model::NoteDragKind::ResizeStart
                         : hit->zone == model::NoteHit::Zone::EndEdge ? model::NoteDragKind::ResizeEnd
                                                                      : model::NoteDragKind::Move,
                 .anchor = hit->index};
        previewOn(notes_[hit->index].pitch, notes_[hit->index].velocity);
        return;
    }
    // Empty grid: box selection (Shift adds to the selection).
    gesture_ = Gesture::Box;
    boxBase_ = additive ? selection_ : std::set<std::size_t>{};
    if (!additive && !selection_.empty())
    {
        selection_.clear();
        repaint();
    }
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    if (!clip_ || gesture_ == Gesture::None)
        return;
    const auto area = gridArea();
    const auto local = e.position - area.getPosition().toFloat();
    const auto geo = geometry();
    const auto previous = lastDrag_;
    lastDrag_ = e.position;

    if (gesture_ == Gesture::Keys)
    {
        previewOn(std::clamp(geo.pitchForY(local.y), 0, 127), drawVelocity_);
        return;
    }
    if (gesture_ == Gesture::Ruler)
    {
        rulerGesture_.drag(timeline_, e.position - previous, local.x);
        repaint();
        return;
    }
    const bool immediate = gesture_ == Gesture::Draw || gesture_ == Gesture::Velocity || gesture_ == Gesture::VelocityOffset;
    if (!immediate && !moved_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        return; // a click, not a drag
    moved_ = true;
    const double deltaBeats = (e.position.x - dragStart_.x) / geo.pixelsPerBeat;

    switch (gesture_)
    {
    case Gesture::Notes:
    case Gesture::Draw:
        drag_.deltaBeats = deltaBeats;
        if (drag_.kind == model::NoteDragKind::Move)
            drag_.deltaPitch = -static_cast<int>(std::lround((e.position.y - dragStart_.y) / rowHeight_));
        preview_ = model::dragNotes(base_.notes, base_.selection, drag_, grid(e.mods.isAltDown()));
        if (drag_.kind == model::NoteDragKind::Move)
        {
            const auto& anchor = base_.notes[drag_.anchor];
            previewOn(std::clamp(anchor.pitch + drag_.deltaPitch, 0, 127), anchor.velocity);
        }
        break;
    case Gesture::Box:
    {
        const double b0 = geo.beatForX(dragStart_.x - static_cast<float>(area.getX()));
        const int p0 = geo.pitchForY(dragStart_.y - static_cast<float>(area.getY()));
        auto selection = boxBase_;
        for (auto i : model::notesInBox(notes_, b0, geo.beatForX(local.x), p0, geo.pitchForY(local.y)))
            selection.insert(i);
        selection_ = std::move(selection);
        break;
    }
    case Gesture::Velocity:
    {
        // Every note start the mouse passed over since the last event.
        const double tolerance = 4.0 / geo.pixelsPerBeat;
        const double from = geo.beatForX(previous.x - static_cast<float>(area.getX())), to = geo.beatForX(local.x);
        auto notes = model::paintVelocity(preview_ ? preview_->notes : notes_, std::min(from, to) - tolerance,
                                          std::max(from, to) + tolerance, velocityAtY(e.position.y),
                                          selection_.empty() ? nullptr : &selection_);
        preview_ = model::NoteEdit{std::move(notes), selection_};
        break;
    }
    case Gesture::VelocityOffset:
    {
        const float delta = -(e.position.y - dragStart_.y) / static_cast<float>(velocityArea().reduced(0, 5).getHeight());
        preview_ = model::NoteEdit{model::offsetVelocity(base_.notes, base_.selection, delta), base_.selection};
        break;
    }
    case Gesture::None:
    case Gesture::Keys:
    case Gesture::Ruler: break; // handled above
    }
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent& e)
{
    if (gesture_ == Gesture::Ruler && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
    {
        // A click in the ruler moves the playhead (snapped; Alt for free).
        session_.setPosition(time::rulerTime(timeline_, tempo_, e.position.x - static_cast<float>(gridArea().getX()),
                                             !snap_ || e.mods.isAltDown()));
    }
    if (gesture_ == Gesture::Notes && !moved_ && clickedInSelection_)
        selection_ = {*clickedInSelection_};
    clickedInSelection_.reset();
    const bool edits = gesture_ == Gesture::Draw || gesture_ == Gesture::Velocity || gesture_ == Gesture::VelocityOffset ||
                       (gesture_ == Gesture::Notes && moved_);
    if (edits && preview_)
        commit(std::move(*preview_));
    preview_.reset();
    if (gesture_ == Gesture::Notes || gesture_ == Gesture::Draw || gesture_ == Gesture::Keys)
        previewOff();
    gesture_ = Gesture::None;
    repaint();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!clip_)
        return;
    const auto area = gridArea();
    const double x = e.position.x - static_cast<float>(area.getX());
    if (e.mods.isAltDown() && !e.mods.isCommandDown())
    {
        // Row height (this view's own axis), keeping the pitch under the mouse in place.
        const double y = e.position.y - static_cast<float>(area.getY());
        const double dy = wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);
        const double pitch = topEdge_ - y / rowHeight_;
        rowHeight_ = std::clamp(rowHeight_ * std::pow(2.0, dy), 6.0, 32.0);
        topEdge_ = pitch + y / rowHeight_;
    }
    else
        time::applyWheel(timeline_, x, e.mods, wheel, [this](double pixels) { topEdge_ -= pixels / rowHeight_; });
    clampPitchView();
    repaint();
}

void PianoRoll::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    time::applyMagnify(timeline_, e.position.x - static_cast<float>(gridArea().getX()), scaleFactor);
    repaint();
}
} // namespace fd::app
