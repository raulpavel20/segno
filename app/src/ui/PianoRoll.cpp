#include "ui/PianoRoll.h"

#include "model/ClipLoops.h"
#include "model/DrumKit.h"
#include "ui/GridMenu.h"
#include "ui/time/TimeView.h"

#include <cmath>

namespace fd::app
{
namespace
{
// Alt+wheel's row heights (the roll's own axis); the pads grow up to the tallest (§23.2).
constexpr double kMinRowHeight = 6.0, kMaxRowHeight = 32.0;

bool isBlackKey(int pitch)
{
    const int k = pitch % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

/// The beats a stretch moves a selection by, as a value tag reads it: "× 1.5", "× −1".
juce::String factorText(double factor)
{
    return juce::String::fromUTF8("× ") +
           juce::String(factor, std::abs(factor - std::round(factor)) < 0.005 ? 0 : 2)
               .replace("-", juce::String::fromUTF8("−"));
}
} // namespace

PianoRoll::PianoRoll(engine::Session& session) : session_(session)
{
    syncGrid();
    setWantsKeyboardFocus(false);
    // The bottom lane's head (§11): what it shows, and its menu.
    laneChoice_.setValue(model::bottomLaneLabel(options_.lane));
    laneChoice_.setTooltip("What the lane shows: velocity, chance, release velocity or a controller. "
                           "Click to choose");
    laneChoice_.onClick = [this] { laneChoice_.showMenu(laneMenu()); };
    addChildComponent(laneChoice_);
}

void PianoRoll::setGrid(const model::GridSettings& grid)
{
    session_.setGrid(engine::Session::GridEditor::PianoRoll, grid);
    syncGrid();
}

void PianoRoll::setSnapEnabled(bool on)
{
    auto g = grid();
    g.snap = on;
    setGrid(g);
}

juce::PopupMenu PianoRoll::gridMenu()
{
    return app::gridMenu(grid(),
                         [safe = juce::Component::SafePointer(this)](const model::GridSettings& g)
                         {
                             if (safe != nullptr)
                                 safe->setGrid(g);
                         });
}

juce::String PianoRoll::gridStepName() const
{
    return timeline_.gridStepName(tempo_);
}

void PianoRoll::syncGrid()
{
    if (const auto g = session_.grid(engine::Session::GridEditor::PianoRoll); g != timeline_.grid())
    {
        timeline_.setGrid(g);
        repaint();
    }
    session_.setGridStep(engine::Session::GridEditor::PianoRoll,
                         clip_ ? timeline_.gridStepName(tempo_) : std::string());
}

PianoRoll::~PianoRoll()
{
    previewOff();
}

// ---- options -------------------------------------------------------------------------------

void PianoRoll::setOptions(const model::PianoRollOptions& options)
{
    if (options == options_)
        return;
    const bool rows = options.wholeRoll != options_.wholeRoll;
    const bool others = options.others != options_.others || options.othersHidden != options_.othersHidden;
    const bool lane = options.lane != options_.lane;
    options_ = options;
    if (rows)
        updateRows();
    if (others)
        updateGhosts();
    if (lane)
    {
        // Another lane: its points, nothing of the last one selected.
        points_.clear();
        lanePreview_.reset();
        laneKept_.reset();
        hoveredPoint_.reset();
        updateLanePoints();
        updateLaneHead();
    }
    optionsChanged();
}

bool PianoRoll::inKey() const noexcept
{
    return key_.has_value() && clip_ && kit_ == 0 && !options_.wholeRoll.contains(clip_->id);
}

bool PianoRoll::onPads() const noexcept
{
    return clip_ && kit_ != 0 && !options_.wholeRoll.contains(clip_->id);
}

const PianoRoll::PadRow* PianoRoll::padOn(int note) const noexcept
{
    for (const auto& pad : pads_)
        if (pad.note == note)
            return &pad;
    return nullptr;
}

void PianoRoll::setInKey(bool on)
{
    setWholeRoll(!on);
}

void PianoRoll::setPads(bool on)
{
    setWholeRoll(!on);
}

void PianoRoll::setWholeRoll(bool whole)
{
    if (!clip_)
        return;
    auto o = options_;
    if (whole)
        o.wholeRoll.insert(clip_->id);
    else
        o.wholeRoll.erase(clip_->id); // the clip follows the key, or the kit, again
    setOptions(o);
}

void PianoRoll::setOthers(bool on)
{
    auto o = options_;
    o.others = on;
    setOptions(o);
}

void PianoRoll::setOthersHidden(std::uint64_t track, bool hidden)
{
    auto o = options_;
    if (hidden)
        o.othersHidden.insert(track);
    else
        o.othersHidden.erase(track);
    setOptions(o);
}

juce::PopupMenu PianoRoll::othersMenu()
{
    // The other tracks that have notes (§8), each ticked while it's shown.
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PianoRoll> safe(this);
    const auto snapshot = session_.snapshot();
    for (const auto& track : snapshot->tracks)
    {
        const bool own = clip_ && std::any_of(track.clips.begin(), track.clips.end(),
                                              [this](const auto& c) { return c.id == clip_->id; });
        const bool notes = std::any_of(track.clips.begin(), track.clips.end(), [](const auto& c)
                                       { return c.kind == model::ClipInfo::Kind::Midi && !c.notes.empty(); });
        if (own || !notes)
            continue;
        const bool shown = !options_.othersHidden.contains(track.id);
        menu.addItem(juce::PopupMenu::Item(juce::String::fromUTF8(track.name.c_str()))
                         .setTicked(shown)
                         .setEnabled(options_.others)
                         .setAction(
                             [safe, id = track.id, shown]
                             {
                                 if (safe != nullptr)
                                     safe->setOthersHidden(id, shown);
                             }));
    }
    if (menu.getNumItems() == 0)
        menu.addItem(juce::PopupMenu::Item("No other track has notes").setEnabled(false));
    return menu;
}

void PianoRoll::setLane(const model::BottomLane& lane)
{
    auto o = options_;
    o.lane = lane;
    setOptions(o);
}

juce::PopupMenu PianoRoll::laneMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PianoRoll> safe(this);
    using K = model::BottomLane::Kind;
    const auto item = [&](juce::PopupMenu& to, const model::BottomLane& lane, juce::String name)
    {
        to.addItem(juce::PopupMenu::Item(std::move(name))
                       .setTicked(options_.lane.kind == lane.kind &&
                                  (lane.kind != K::Controller || options_.lane.controller == lane.controller))
                       .setAction(
                           [safe, lane]
                           {
                               if (safe != nullptr)
                                   safe->setLane(lane);
                           }));
    };
    const auto named = [](const model::BottomLane& lane)
    { return juce::String::fromUTF8(model::bottomLaneName(lane).c_str()); };
    for (const auto kind : {K::Velocity, K::Chance, K::Release})
        item(menu, {.kind = kind}, named({.kind = kind}));
    // The controllers: the clip's, then the usual ones it hasn't, then any other.
    menu.addSectionHeader("Controllers");
    std::vector<int> shown;
    if (clip_)
        for (const auto& lane : clip_->controllers)
            shown.push_back(lane.controller);
    for (int c : model::commonControllers())
        if (std::find(shown.begin(), shown.end(), c) == shown.end())
            shown.push_back(c);
    if (showsController() && std::find(shown.begin(), shown.end(), options_.lane.controller) == shown.end())
        shown.push_back(options_.lane.controller);
    for (int c : shown)
        item(menu, {.kind = K::Controller, .controller = c}, named({.kind = K::Controller, .controller = c}));
    juce::PopupMenu other;
    for (int first = 0; first < 128; first += 16)
    {
        juce::PopupMenu group;
        for (int c = first; c < first + 16; ++c)
        {
            const auto name = model::controllerName(c);
            item(group, {.kind = K::Controller, .controller = c},
                 name.rfind("CC ", 0) == 0 ? juce::String(name)
                                           : "CC " + juce::String(c) + juce::String::fromUTF8(" · ") + name);
        }
        other.addSubMenu("CC " + juce::String(first) + juce::String::fromUTF8("–") + juce::String(first + 15),
                         group);
    }
    menu.addSubMenu("Other controller", other);
    return menu;
}

model::ControllerLane PianoRoll::controllerLane() const
{
    model::ControllerLane out{.controller = options_.lane.controller};
    if (clip_ && showsController())
        for (const auto& lane : clip_->controllers)
            if (lane.controller == options_.lane.controller)
                out.points = lane.points;
    return out;
}

model::AutomationPoints PianoRoll::copyPoints() const
{
    return model::copyPoints(laneKept_ ? laneKept_->points : lanePoints_, points_);
}

bool PianoRoll::pastePoints(const model::AutomationPoints& points, double seconds)
{
    if (!clip_ || !showsController() || points.empty())
        return false;
    const bool over = seconds >= clip_->start && seconds < clip_->end();
    const double at = over ? model::clipViewTime(*clip_, seconds) : playedRange().first;
    pointsUsed_ = true;
    keepLane(model::pastePoints(laneKept_ ? laneKept_->points : lanePoints_, points, at,
                                model::controllerRules(options_.lane.controller)),
             "Paste");
    return true;
}

void PianoRoll::duplicatePoints()
{
    if (!hasPointSelection())
        return;
    keepLane(model::duplicatePoints(laneKept_ ? laneKept_->points : lanePoints_, points_,
                                    tempo_.secondsForBeats(gridStep()),
                                    model::controllerRules(options_.lane.controller)),
             "Duplicate points");
}

model::ToolContext PianoRoll::toolContext() const
{
    model::ToolContext c{.key = keyOrNull(),
                         .beatsPerBar = tempo_.beatsPerBar(),
                         .secondsPerBeat = tempo_.secondsForBeats(1.0),
                         .origin = clip_ ? clip_->contentStartBeat : 0.0,
                         .gridStep = gridStep(),
                         .drums = onPads()};
    if (timeSpan_) // the time selection first (time-selection.md §14.1)
        c.span = {beatForSeconds(timeSpan_->start), beatForSeconds(timeSpan_->end)};
    else if (const auto span = model::notesSpan(notes_, selection_))
        c.span = *span;
    else
    {
        const auto [from, to] = playedRange();
        c.span = {beatForSeconds(from), beatForSeconds(to)};
    }
    return c;
}

model::ToolValues PianoRoll::toolValues(const model::ToolSpec& tool) const
{
    auto values = keptToolValues(std::string(tool.id), tool);
    if (!onPads() || pads_.empty())
        return values;
    // On the pads a pitch is a pad (§23.4): one kept that isn't a pad becomes the selected notes'
    // pad, else the row pressed last, else the lowest.
    for (const auto& s : tool.settings)
        if (s.unit == "pitch")
            if (auto& v = values[std::string(s.id)]; padOn(static_cast<int>(std::lround(v))) == nullptr)
            {
                std::optional<int> chosen;
                for (auto i : selection_)
                    if (padOn(notes_[i].pitch) != nullptr)
                    {
                        chosen = notes_[i].pitch;
                        break;
                    }
                if (!chosen && pressedPad_ && padOn(*pressedPad_) != nullptr)
                    chosen = pressedPad_;
                v = chosen.value_or(pads_.front().note);
            }
    return values;
}

void PianoRoll::setToolValues(const model::ToolSpec& tool, const model::ToolValues& values)
{
    keepToolValues(std::string(tool.id), tool, values);
}

model::ToolValues PianoRoll::keptToolValues(const std::string& key, const model::ToolSpec& tool) const
{
    const auto it = options_.tools.find(key);
    return model::toolValues(tool, it != options_.tools.end() ? it->second : model::ToolValues{});
}

void PianoRoll::keepToolValues(const std::string& key, const model::ToolSpec& tool,
                               const model::ToolValues& values)
{
    auto o = options_;
    o.tools[key] = model::toolValues(tool, values);
    setOptions(o);
}

void PianoRoll::rerollChance()
{
    if (clip_)
        session_.rerollChance(clip_->id);
}

void PianoRoll::keepChance(bool keep)
{
    if (clip_)
        session_.keepChance(clip_->id, keep);
}

model::NoteLevel PianoRoll::laneLevel() const noexcept
{
    return options_.lane.level().value_or(model::NoteLevel::Velocity);
}

void PianoRoll::updateLanePoints()
{
    lanePoints_.clear();
    for (auto p : controllerLane().points)
    {
        p.time = secondsForBeat(p.time);
        lanePoints_.push_back(p);
    }
}

LaneGeometry PianoRoll::laneGeometry() const
{
    return {.area = velocityArea().toFloat(),
            .laneX0 = static_cast<float>(gridArea().getX()),
            .timeline = &timeline_};
}

LaneGesture::Lane PianoRoll::gestureLane() const
{
    // The last edit kept, until the snapshot has it: a gesture right after another builds on it.
    return {.points = laneKept_ ? &laneKept_->points : &lanePoints_,
            .rules = model::controllerRules(options_.lane.controller),
            .geometry = laneGeometry(),
            .selection = points_,
            .snap = [this](double seconds, bool free)
            { return secondsForBeat(std::max(0.0, beatGrid(free).round(beatForSeconds(seconds)))); },
            .drawName = "Draw controller"};
}

juce::Point<float> PianoRoll::lanePointFor(double beat, float value) const
{
    const auto geo = laneGeometry();
    return {geo.x(secondsForBeat(beat)), geo.y(value)};
}

void PianoRoll::keepLane(const model::PointEdit& edit, const std::string& undoName)
{
    if (!clip_)
        return;
    model::ControllerLane lane{.controller = options_.lane.controller};
    for (auto p : edit.points)
    {
        p.time = beatForSeconds(p.time);
        lane.points.push_back(p);
    }
    lanePreview_.reset();
    laneKept_ = edit; // until the snapshot has it
    points_ = edit.selection;
    session_.setControllers(clip_->id, {lane}, undoName);
    repaint();
}

void PianoRoll::setDraw(model::DrawMode mode)
{
    auto o = options_;
    o.draw = mode;
    setOptions(o);
}

void PianoRoll::setChordStyle(const model::ChordStyle& style)
{
    auto o = options_;
    o.chord = style;
    setOptions(o);
}

void PianoRoll::optionsChanged()
{
    updateLabels(); // names, steps or both
    resized();      // the chord lane comes and goes
    repaint();
    if (onOptionsChanged)
        onOptionsChanged();
}

juce::PopupMenu PianoRoll::drawMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PianoRoll> safe(this);
    const auto item = [&](model::DrawMode mode, const char* name)
    {
        menu.addItem(juce::PopupMenu::Item(name)
                         .setTicked(options_.draw == mode)
                         .setAction(
                             [safe, mode]
                             {
                                 if (safe != nullptr)
                                     safe->setDraw(mode);
                             }));
    };
    item(model::DrawMode::Note, "Note");
    item(model::DrawMode::Chord, "Chord");
    item(model::DrawMode::ChordUnder, "Chord under, the note on top");
    return menu;
}

juce::PopupMenu PianoRoll::voicingMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<PianoRoll> safe(this);
    const auto set = [safe](auto change)
    {
        return [safe, change]
        {
            if (safe == nullptr)
                return;
            auto style = safe->options().chord;
            change(style);
            safe->setChordStyle(style);
        };
    };
    for (auto v : {music::Voicing::Smooth, music::Voicing::Close, music::Voicing::Open, music::Voicing::Drop2,
                   music::Voicing::Drop3, music::Voicing::Spread})
        menu.addItem(juce::PopupMenu::Item(model::voicingName(v))
                         .setTicked(options_.chord.voicing == v)
                         .setAction(set([v](model::ChordStyle& s) { s.voicing = v; })));
    menu.addSeparator();
    for (auto i : {music::Inversion::Auto, music::Inversion::Root, music::Inversion::First,
                   music::Inversion::Second, music::Inversion::Third})
        menu.addItem(juce::PopupMenu::Item(juce::String("Inversion: ") + model::inversionName(i))
                         .setTicked(options_.chord.inversion == i)
                         .setAction(set([i](model::ChordStyle& s) { s.inversion = i; })));
    return menu;
}

// ---- geometry ------------------------------------------------------------------------------

void PianoRoll::updateKeysWidth()
{
    // On the pads, as wide as their names and notes need, between the piano's and the column's
    // widest (§23.2); a longer name ends in an ellipsis.
    int width = 0;
    if (onPads())
    {
        const auto& s = ui::Theme::current().shell;
        int names = 0, notes = 0;
        for (const auto& pad : pads_)
            names = std::max(names, nameWidth(pad.name, ui::TextStyle::Control));
        for (const int p : rows_)
            notes = std::max(notes, nameWidth(noteName(p), ui::TextStyle::ValueSmall));
        // The cells end a pixel short of the column's edge, where its line is.
        width = std::clamp(s.listTextInset * 2 + names + (names > 0 ? s.groupGap : 0) + notes + 1,
                           s.keyboardWidth, s.padNamesWidth);
    }
    if (width == keysWidth_)
        return;
    keysWidth_ = width;
    resized();
    repaint();
}

int PianoRoll::keysWidth() const noexcept
{
    return keysWidth_ > 0 ? keysWidth_ : ui::Theme::current().shell.keyboardWidth;
}

int PianoRoll::chordLaneLines() const
{
    if (!showsChordLane())
        return 0;
    const bool own = !spans_.empty() || options_.draw != model::DrawMode::Note;
    const bool all = options_.others && !allSpans_.empty();
    return (own ? 1 : 0) + (all ? 1 : 0);
}

bool PianoRoll::showsChordLane() const
{
    // Never on the pads: a kick, a snare and a hat together aren't a chord (§23.4).
    return clip_ && !onPads() &&
           (!spans_.empty() || options_.draw != model::DrawMode::Note ||
            (options_.others && !allSpans_.empty()));
}

int PianoRoll::topHeight() const
{
    const auto& s = ui::Theme::current().shell;
    return s.loopStripHeight + s.rulerHeight + chordLaneLines() * s.rowHeight;
}

juce::Rectangle<int> PianoRoll::loopStripArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().removeFromTop(s.loopStripHeight).withTrimmedLeft(keysWidth());
}

juce::Rectangle<int> PianoRoll::rulerArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds()
        .withTrimmedTop(s.loopStripHeight)
        .removeFromTop(s.rulerHeight)
        .withTrimmedLeft(keysWidth());
}

juce::Rectangle<int> PianoRoll::chordLaneArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds()
        .withTrimmedTop(s.loopStripHeight + s.rulerHeight)
        .removeFromTop(chordLaneLines() * s.rowHeight)
        .withTrimmedLeft(keysWidth());
}

juce::Rectangle<int> PianoRoll::velocityArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().removeFromBottom(s.velocityHeight).withTrimmedLeft(keysWidth());
}

juce::Rectangle<int> PianoRoll::keysArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds()
        .withTrimmedTop(topHeight())
        .withTrimmedBottom(s.velocityHeight + 1)
        .removeFromLeft(keysWidth());
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds()
        .withTrimmedTop(topHeight())
        .withTrimmedBottom(s.velocityHeight + 1)
        .withTrimmedLeft(keysWidth());
}

double PianoRoll::contentStartSeconds() const
{
    return clip_ ? model::clipViewOrigin(*clip_) : 0.0; // a looped clip: the lap it starts in (§6)
}

std::pair<double, double> PianoRoll::playedRange() const
{
    return clip_ ? ClipLoopBrace::rangeOf(*clip_, contentStartSeconds()) : std::pair{0.0, 0.0};
}

double PianoRoll::beatForSeconds(double seconds) const
{
    return tempo_.beatsForSeconds(seconds - contentStartSeconds());
}

double PianoRoll::secondsForBeat(double beat) const
{
    return contentStartSeconds() + tempo_.secondsForBeats(beat);
}

double PianoRoll::songBeat(double contentBeat) const noexcept
{
    return (clip_ ? clip_->contentStartBeat : 0.0) + contentBeat;
}

model::NoteGeometry PianoRoll::geometry() const
{
    return {.originX = timeline_.xForTime(contentStartSeconds()),
            .pixelsPerBeat = timeline_.pixelsPerSecond() * tempo_.secondsForBeats(1.0),
            .topEdge = topEdge_,
            .rowHeight = rowHeight_,
            .rows = folded_ ? &rows_ : nullptr};
}

double PianoRoll::beatAtX(float x) const
{
    return geometry().beatForX(x - static_cast<float>(gridArea().getX()));
}

double PianoRoll::gridStep() const
{
    return timeline_.gridStepBeats(tempo_);
}

model::BeatGrid PianoRoll::beatGrid(bool bypass) const
{
    return {.step = isSnapEnabled() && !bypass ? gridStep() : 0.0,
            .origin = clip_ ? clip_->contentStartBeat : 0.0,
            .bar = tempo_.beatsPerBar()};
}

model::BeatGrid PianoRoll::quantiseGrid() const
{
    return {.step = gridStep(), .origin = clip_ ? clip_->contentStartBeat : 0.0, .bar = tempo_.beatsPerBar()};
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
    const double count = geometry().rowCount();
    topEdge_ = std::clamp(topEdge_, std::min(count, rows), count);
}

void PianoRoll::fitToClip()
{
    const auto area = gridArea();
    if (!clip_ || area.getWidth() <= 1 || area.getHeight() <= 1)
        return; // once laid out
    needsFit_ = false;
    timeline_.setWidth(area.getWidth());
    const auto [from, to] = playedRange(); // the clip, or its loop
    const double length = std::max(to - from, tempo_.secondsForBeats(tempo_.beatsPerBar()));
    const double margin = length * 0.03;
    timeline_.setView(std::max(0.0, from - margin), area.getWidth() / (length + 2.0 * margin));
    fitRows();
}

void PianoRoll::fitRows()
{
    const auto area = gridArea();
    padsShown_ = onPads();
    fittedRows_ = rows_.size();
    if (padsShown_)
    {
        // The pads fill the height, as tall as a row gets; with more than fit at the roll's row
        // height, they keep it and scroll, the lowest in view (§23.2).
        const int count = std::max(1, geometry().rowCount());
        rowHeight_ = std::clamp(area.getHeight() / static_cast<double>(count), rollRowHeight_, kMaxRowHeight);
        topEdge_ = std::min(static_cast<double>(count), area.getHeight() / rowHeight_);
        clampPitchView();
        return;
    }
    rowHeight_ = rollRowHeight_;
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
    const auto geo = geometry();
    topEdge_ = (geo.rowOf(lo) + geo.rowOf(hi) + 1) * 0.5 + area.getHeight() / rowHeight_ * 0.5;
    clampPitchView();
}

void PianoRoll::resized()
{
    {
        const auto& s = ui::Theme::current().shell;
        laneChoice_.setBounds(
            getLocalBounds().removeFromBottom(s.velocityHeight).removeFromLeft(keysWidth()));
    }
    // Keep the pitch in the middle where it is when the height changes; the pads fill it again
    // (§23.2).
    const double height = gridArea().getHeight();
    const bool refit = padsShown_ && onPads() && height > 0.0 && height != lastGridHeight_;
    if (!refit && lastGridHeight_ > 0.0 && height > 0.0)
        topEdge_ += (height - lastGridHeight_) / rowHeight_ * 0.5;
    lastGridHeight_ = height;
    if (refit)
        fitRows();
    timeline_.setWidth(gridArea().getWidth());
    if (needsFit_)
        fitToClip();
    clampPitchView();
}

// ---- state ---------------------------------------------------------------------------------

juce::String PianoRoll::noteName(int pitch) const
{
    // "C3" for 60 (the suite's convention, Unit::Note); "B♭3" in F major.
    return utf8(music::noteName(keyOrNull(), pitch));
}

bool PianoRoll::updateKit(const std::optional<model::ClipInfo>& clip)
{
    std::uint64_t kit = 0;
    std::vector<PadRow> pads;
    if (clip && clip->kind == model::ClipInfo::Kind::Midi)
        for (const auto& track : session_.snapshot()->tracks)
            if (std::any_of(track.clips.begin(), track.clips.end(),
                            [&](const auto& c) { return c.id == clip->id; }))
            {
                if (const auto* slot = model::kit::trackKit(track.devices))
                {
                    kit = slot->id;
                    for (const auto& pad : slot->rack->chains)
                        pads.push_back({pad.note, utf8(pad.name), pad.on});
                    std::sort(pads.begin(), pads.end(),
                              [](const auto& a, const auto& b) { return a.note < b.note; });
                }
                break;
            }
    if (kit == kit_ && pads == pads_)
        return false;
    kit_ = kit;
    pads_ = std::move(pads);
    padNotes_.reset();
    for (const auto& pad : pads_)
        if (pad.note >= 0 && pad.note < 128)
            padNotes_.set(static_cast<std::size_t>(pad.note));
    return true;
}

void PianoRoll::setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo)
{
    tempo_ = tempo;
    bool rows = false;
    if (updateKit(clip))
    {
        rows = true; // the pads' rows, and the chord lane and Others gone or back
        updateKeysWidth();
        resized();
    }
    if (auto key = session_.snapshot()->keys.keyAt(clip ? clip->contentStartBeat : 0.0); key != key_)
    {
        key_ = std::move(key);
        rows = true;
        updateLabels();
        repaint();
    }
    if (!clip || clip->kind != model::ClipInfo::Kind::Midi)
    {
        previewOff();
        clip_.reset();
        notes_.clear();
        selection_.clear();
        preview_.reset();
        gesture_ = Gesture::None;
        chordTarget_.reset();
        notesChanged();
        updateGhosts();
        syncGrid(); // no clip: no step to report
        points_.clear();
        lanePoints_.clear();
        lanePreview_.reset();
        laneKept_.reset();
        laneChoice_.setVisible(false);
        timeSpan_.reset();
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
            notesChanged();
        }
        else if (rows)
            updateRows();
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
        chordTarget_.reset();
        notesChanged();
        needsFit_ = true;
        fitToClip();
        points_.clear(); // another clip's lanes
        timeSpan_.reset();
    }
    // Its controller lane as the snapshot has it now (a gesture's preview until then).
    updateLanePoints();
    laneKept_.reset();
    std::erase_if(points_, [this](auto i) { return i >= lanePoints_.size(); });
    laneChoice_.setVisible(true);
    updateLaneHead();
    updateGhosts();
    repaint();
}

void PianoRoll::updateLaneHead()
{
    laneChoice_.setValue(model::bottomLaneLabel(options_.lane));
    // The Chance view of a clip that keeps its roll (§26): a lock.
    const bool kept = options_.lane.kind == model::BottomLane::Kind::Chance && clip_ && clip_->chanceKept;
    laneChoice_.setLocked(kept);
    laneChoice_.setTooltip(
        juce::String::fromUTF8(model::bottomLaneName(options_.lane).c_str()) +
        (kept ? juce::String::fromUTF8(", kept as played (Notes > Let chance roll rolls it again)")
              : juce::String()) +
        juce::String::fromUTF8(": what the lane shows. Click to choose another"));
}

void PianoRoll::notesChanged()
{
    spans_ = model::chordSpans(notes_);
    updateRows();
    if (options_.others)
        updateGhosts(); // the chords of everything shown include ours
    updateLabels();
    selectionChanged();
}

void PianoRoll::selectionChanged()
{
    ++editVersion_; // the notes too: notesChanged comes here
    if (onSelectionChanged)
        onSelectionChanged();
}

void PianoRoll::setSelection(std::set<std::size_t> selection)
{
    std::erase_if(selection, [this](auto i) { return i >= notes_.size(); });
    if (selection == selection_)
        return;
    selection_ = std::move(selection);
    selectionChanged();
    repaint();
}

void PianoRoll::updateRows()
{
    // Keep the pitch in the middle of the view in the middle as rows come and go.
    const auto before = geometry();
    const double middleRow = topEdge_ - gridArea().getHeight() / rowHeight_ * 0.5;
    const int middlePitch =
        before.rows == nullptr
            ? static_cast<int>(std::lround(middleRow))
            : (before.rows->empty()
                   ? 60
                   : (*before.rows)[static_cast<std::size_t>(std::clamp(
                         static_cast<int>(middleRow), 0, static_cast<int>(before.rows->size()) - 1))]);
    // A preview's notes outside the key (or off the pads) show their rows while it's shown; a
    // drag's land on rows.
    const auto* more = preview_ ? &preview_->notes : nullptr;
    auto rows = onPads()  ? model::padRows(padNotes_, notes_, more)
                : inKey() ? model::keyRows(keyOrNull(), notes_, more)
                          : std::vector<int>{};
    const bool folded = onPads() || (inKey() && rows.size() < 128); // every pitch: the whole roll
    if (!folded)
        rows.clear();
    // To the pads, or back to the roll's row height; on the pads, rows come and go filling the
    // height, though not under a gesture or a preview (they hold still until it ends).
    const bool idle = gesture_ == Gesture::None && !preview_;
    const bool refit = onPads() != padsShown_ || (onPads() && idle && rows.size() != fittedRows_);
    if (rows == rows_ && folded == folded_ && !refit)
        return;
    rows_ = std::move(rows);
    folded_ = folded;
    updateKeysWidth();
    if (refit && gridArea().getHeight() > 0)
    {
        fitRows();
        repaint();
        return;
    }
    topEdge_ = geometry().rowOf(middlePitch) + 0.5 + gridArea().getHeight() / rowHeight_ * 0.5;
    clampPitchView();
    repaint();
}

void PianoRoll::updateGhosts()
{
    ghosts_.clear();
    allSpans_.clear();
    if (!clip_ || !options_.others || onPads()) // another track's pitches aren't the pads (§23.4)
    {
        updateLabels();
        resized();
        return;
    }
    // Other tracks' MIDI clips, lap by lap, around the clip (§8): placed in our content beats.
    const auto [played0, played1] = playedRange();
    const double margin = tempo_.secondsForBeats(tempo_.beatsPerBar() * 4.0);
    const double from = played0 - margin, to = played1 + margin;
    const auto snapshot = session_.snapshot();
    std::vector<model::Note> all = notes_;
    for (const auto& track : snapshot->tracks)
    {
        if (std::any_of(track.clips.begin(), track.clips.end(),
                        [this](const auto& c) { return c.id == clip_->id; }))
            continue; // the clip's own track
        if (options_.othersHidden.contains(track.id))
            continue; // left out (§8)
        for (const auto& c : track.clips)
        {
            if (c.kind != model::ClipInfo::Kind::Midi || c.muted)
                continue;
            for (const auto& lap : model::laps(c, from, to))
                for (const auto& n : c.notes)
                {
                    if (n.muted)
                        continue;
                    const double at = tempo_.secondsForBeats(n.start);
                    if (at < lap.content - 1e-9 || at >= lap.content + (lap.to - lap.from) - 1e-9)
                        continue;
                    auto ghost = n;
                    ghost.start = beatForSeconds(lap.from + (at - lap.content));
                    ghosts_.push_back({ghost, c.colour});
                    all.push_back(ghost);
                }
        }
    }
    if (!ghosts_.empty())
    {
        std::sort(all.begin(), all.end(), model::noteOrder);
        allSpans_ = model::chordSpans(all);
        // Only worth a line where it says more than the clip's own chords.
        const bool same = allSpans_.size() == spans_.size() &&
                          std::equal(allSpans_.begin(), allSpans_.end(), spans_.begin(),
                                     [](const auto& a, const auto& b) { return a.chord == b.chord; });
        if (same)
            allSpans_.clear();
    }
    updateLabels();
    resized();
}

void PianoRoll::frame(double playheadSeconds)
{
    syncGrid(); // set by an assistant, a project opened; the step follows the zoom
    // Over a looped clip's later laps, the playhead shows where in the loop the song is (§6).
    const bool overLoop = clip_ && clip_->loop.on() && playheadSeconds >= clip_->start && playheadSeconds < clip_->end();
    playhead_ = overLoop ? model::clipViewTime(*clip_, playheadSeconds) : playheadSeconds;
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
    {
        session_.setNotes(clip_->id, notes_); // one undo step; the snapshot follows
        notesChanged();
    }
    else
        selectionChanged();
    repaint();
}

void PianoRoll::previewEdit(std::optional<model::NoteEdit> edit)
{
    preview_ = std::move(edit);
    updateRows();
    repaint();
}

void PianoRoll::applyEdit(model::NoteEdit edit)
{
    commit(std::move(edit));
}

// ---- commands ------------------------------------------------------------------------------

void PianoRoll::deleteSelection()
{
    // The controller lane's points when it was used last (§11), else the notes.
    if (clip_ && hasPointSelection())
    {
        keepLane(model::deletePoints(lanePoints_, points_), "Delete points");
        return;
    }
    if (clip_ && !selection_.empty())
        commit(model::deleteNotes(notes_, selection_));
}

void PianoRoll::duplicateSelection()
{
    if (clip_ && timeSpan_)
    {
        // Right after the time selection, which moves along (time-selection.md §14.1): the
        // selected notes, else those starting in it.
        const auto span = *timeSpan_;
        const double a = beatForSeconds(span.start), b = beatForSeconds(span.end);
        auto which = selection_;
        if (which.empty())
            for (std::size_t i = 0; i < notes_.size(); ++i)
                if (notes_[i].start >= a - 1e-9 && notes_[i].start < b - 1e-9)
                    which.insert(i);
        if (!which.empty())
            commit(model::duplicateNotesBy(notes_, which, b - a));
        setTimeSelection(model::TimeSpan{span.end, span.end + span.length()});
        return;
    }
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
    const double at = beatForSeconds(over ? model::clipViewTime(*clip_, seconds) : playedRange().first);
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
    setTimeSelection(std::nullopt); // the notes, not a box's stretch
    std::set<std::size_t> all;
    for (std::size_t i = 0; i < notes_.size(); ++i)
        all.insert(i);
    setSelection(std::move(all));
}

void PianoRoll::transpose(int semitones)
{
    if (clip_ && !selection_.empty())
        commit(model::transposeNotes(notes_, selection_, semitones));
}

void PianoRoll::transposeSteps(int steps)
{
    if (!clip_ || selection_.empty())
        return;
    if (onPads())
        commit(model::transposeInRows(notes_, selection_, rows_, steps)); // a row: the next pad
    else if (inKey())
        commit(model::transposeInKey(notes_, selection_, *key_, steps));
    else
        transpose(steps);
}

void PianoRoll::nudge(int gridSteps)
{
    if (clip_ && !selection_.empty())
        commit(model::nudgeNotes(notes_, selection_, gridSteps * gridStep()));
}

void PianoRoll::quantise()
{
    if (clip_ && !notes_.empty())
    {
        const auto& tool = *model::findNoteTool("quantise");
        commit(model::runNoteTool(tool, notes_, selection_, toolValues(tool), toolContext()).edit);
    }
}

void PianoRoll::fitToKey()
{
    if (clip_ && key_ && !notes_.empty())
        commit(model::fitToKey(notes_, selection_, *key_));
}

void PianoRoll::invertChords(bool up)
{
    if (clip_ && !notes_.empty())
        commit(model::invertChords(notes_, selection_, up));
}

void PianoRoll::voiceChords(music::Voicing voicing)
{
    if (clip_ && !notes_.empty())
        commit(model::voiceChords(notes_, selection_, voicing));
}

void PianoRoll::addBass()
{
    if (clip_ && !notes_.empty())
        commit(model::addBass(notes_, selection_));
}

void PianoRoll::join()
{
    if (clip_ && !notes_.empty())
        commit(model::joinNotes(notes_, selection_));
}

void PianoRoll::splitAtPlayhead()
{
    if (clip_ && !notes_.empty())
        commit(model::splitNotes(notes_, selection_, beatForSeconds(playhead_)));
}

void PianoRoll::splitAtSelection()
{
    if (clip_ && timeSpan_)
        commit(model::splitNotesAt(notes_, selection_, beatForSeconds(timeSpan_->start),
                                   beatForSeconds(timeSpan_->end)));
}

void PianoRoll::legato()
{
    if (clip_ && !notes_.empty())
        commit(model::legatoNotes(notes_, selection_));
}

void PianoRoll::fixedLength()
{
    if (clip_ && !notes_.empty())
        commit(model::fixedLength(notes_, selection_, gridStep()));
}

void PianoRoll::stretch(double factor)
{
    if (!clip_ || notes_.empty())
        return;
    std::set<std::size_t> which = selection_;
    if (which.empty())
        for (std::size_t i = 0; i < notes_.size(); ++i)
            which.insert(i);
    if (const auto span = model::notesSpan(notes_, which))
        commit(model::stretchNotes(notes_, selection_, span->first, factor, true));
}

void PianoRoll::reverse()
{
    if (clip_ && !notes_.empty())
        commit(model::reverseNotes(notes_, selection_));
}

void PianoRoll::mirrorPitches()
{
    if (clip_ && !notes_.empty())
        commit(model::mirrorPitches(notes_, selection_, inKey() ? keyOrNull() : nullptr));
}

void PianoRoll::toggleMute()
{
    if (clip_ && !notes_.empty())
        commit(model::muteNotes(notes_, selection_));
}

bool PianoRoll::allMuted() const
{
    if (notes_.empty())
        return false;
    if (selection_.empty())
        return std::all_of(notes_.begin(), notes_.end(), [](const auto& n) { return n.muted; });
    return std::all_of(selection_.begin(), selection_.end(),
                       [this](auto i) { return i < notes_.size() && notes_[i].muted; });
}

void PianoRoll::selectSamePitch()
{
    setSelection(model::selectSamePitch(notes_, selection_));
}

void PianoRoll::selectOutsideKey()
{
    if (key_)
        setSelection(model::selectOutsideKey(notes_, *key_));
}

void PianoRoll::selectInLoop()
{
    const auto [from, to] = playedRange();
    setSelection(model::selectStartingIn(notes_, beatForSeconds(from), beatForSeconds(to)));
}

void PianoRoll::invertSelection()
{
    setSelection(model::invertSelection(notes_, selection_));
}

// ---- chords --------------------------------------------------------------------------------

PianoRoll::ChordPlace PianoRoll::placeOf(const model::ChordSpan& span)
{
    return {span.start, span.end, {span.notes.begin(), span.notes.end()}};
}

std::optional<music::Chord> PianoRoll::chordAt(const ChordPlace& place) const
{
    std::vector<int> pitches;
    for (auto i : place.replacing)
        if (i < notes_.size())
            pitches.push_back(notes_[i].pitch);
    return music::nameChord(pitches);
}

std::optional<music::Chord> PianoRoll::chordBefore(const ChordPlace& place) const
{
    return model::chordBefore(notes_, place.start, place.replacing);
}

PianoRoll::ChordPlace PianoRoll::placeForSelection() const
{
    if (const auto span = model::notesSpan(notes_, selection_))
        return {span->first, span->second, selection_};
    const auto grid = beatGrid(false);
    const double at = std::max(0.0, grid.floor(beatForSeconds(playhead_)));
    return {at, at + gridStep(), {}};
}

juce::Rectangle<int> PianoRoll::laneAreaFor(const ChordPlace& place) const
{
    // The lane's first line at the place; while the lane isn't shown, the ruler's row there.
    const auto lane = chordLaneArea();
    const auto row = lane.isEmpty() ? rulerArea() : lane.withHeight(ui::Theme::current().shell.rowHeight);
    const auto geo = geometry();
    const int x0 = row.getX() + static_cast<int>(std::floor(geo.xForBeat(place.start)));
    const int x1 = row.getX() + static_cast<int>(std::ceil(geo.xForBeat(place.end)));
    return juce::Rectangle<int>(x0, row.getY(), std::max(1, x1 - x0), row.getHeight()).getIntersection(row);
}

juce::Rectangle<int> PianoRoll::laneAreaWithBefore(const ChordPlace& place) const
{
    auto area = laneAreaFor(place);
    for (auto it = spans_.rbegin(); it != spans_.rend(); ++it)
        if (it->start < place.start - 1e-9)
        {
            if (const auto before = laneAreaFor(placeOf(*it)); !before.isEmpty())
                area = area.getUnion(before);
            break;
        }
    return area;
}

void PianoRoll::setChordTarget(std::optional<ChordPlace> place)
{
    if (place == chordTarget_)
        return;
    chordTarget_ = std::move(place);
    repaint();
}

PianoRoll::ChordPlace PianoRoll::writeChord(const ChordPlace& place, const music::Chord& chord,
                                            const model::ChordStyle& style)
{
    previewOff();
    auto edit =
        model::writeChord(notes_, place.start, place.end, chord, place.replacing, style, drawVelocity_);
    ChordPlace written{place.start, place.end, edit.selection};
    commit(std::move(edit));
    return written;
}

void PianoRoll::previewChord(const ChordPlace& place, const std::optional<music::Chord>& chord,
                             const model::ChordStyle& style)
{
    if (!chord)
    {
        previewOff();
        if (gesture_ == Gesture::None)
            preview_.reset();
        updateRows();
        repaint();
        return;
    }
    auto edit =
        model::writeChord(notes_, place.start, place.end, *chord, place.replacing, style, drawVelocity_);
    std::vector<int> pitches;
    for (auto i : edit.selection)
        pitches.push_back(edit.notes[i].pitch);
    preview_ = std::move(edit);
    updateRows();
    previewChordOn(pitches, drawVelocity_);
    repaint();
}

// ---- hearing notes -------------------------------------------------------------------------

void PianoRoll::audition(const std::vector<int>& pitches)
{
    previewChordOn(pitches, drawVelocity_);
}

void PianoRoll::auditionOff()
{
    previewOff();
}

void PianoRoll::previewOn(int pitch, float velocity)
{
    if (previewPitch_ == pitch && previewChordPitches_.empty())
        return;
    previewOff();
    session_.computerKeyboardNote(pitch, velocity);
    previewPitch_ = pitch;
    repaint(keysArea());
}

void PianoRoll::previewChordOn(const std::vector<int>& pitches, float velocity)
{
    if (pitches == previewChordPitches_)
        return;
    previewOff();
    for (int p : pitches)
        session_.computerKeyboardNote(p, velocity);
    previewChordPitches_ = pitches;
}

void PianoRoll::previewOff()
{
    for (int p : std::exchange(previewChordPitches_, {}))
        session_.computerKeyboardNote(p, 0.0f);
    if (!previewPitch_)
        return;
    session_.computerKeyboardNote(*previewPitch_, 0.0f);
    previewPitch_.reset();
    repaint(keysArea());
}

model::NoteEdit PianoRoll::drawnAt(juce::Point<float> local, bool bypass) const
{
    const auto geo = geometry();
    const auto snapping = beatGrid(bypass);
    const double beat = std::max(0.0, geo.beatForX(local.x));
    const model::Note note{.pitch = std::clamp(geo.pitchForY(local.y), 0, 127),
                           .start = std::max(0.0, snapping.floor(beat)),
                           .length = gridStep(),
                           .velocity = drawVelocity_};
    return options_.draw == model::DrawMode::Note || onPads() // the pads draw hits (§23.4)
               ? model::addNote(notes_, note)
               : model::drawChord(notes_, note, keyOrNull(), options_.chord,
                                  options_.draw == model::DrawMode::ChordUnder);
}

// ---- painting ------------------------------------------------------------------------------

void PianoRoll::setTimeSelection(std::optional<model::TimeSpan> span)
{
    if (span && !(span->end > span->start))
        span.reset();
    if (span == timeSpan_)
        return;
    timeSpan_ = span;
    repaint();
}

std::optional<model::TimeSpan> PianoRoll::boxStretch(const juce::MouseEvent& e) const
{
    // The drag's span on the grid (Alt: free), as the clip view's time.
    const auto grid = beatGrid(e.mods.isAltDown());
    const double a = beatAtX(dragStart_.x), b = beatAtX(e.position.x);
    const double from = std::max(0.0, grid.round(std::min(a, b))), to = grid.round(std::max(a, b));
    if (!(to - from > 1e-9))
        return std::nullopt;
    return model::TimeSpan{secondsForBeat(from), secondsForBeat(to)};
}

void PianoRoll::showTimeSelection(model::TimeSpan span)
{
    setTimeSelection(span);
    if (!clip_ || needsFit_ || (span.start >= timeline_.start() && span.end <= timeline_.end()))
        return; // in view, or the clip's fit, still to come, shows it
    // Scrolled to it at the zoom there is; zoomed out if it's wider than the view.
    const double width = timeline_.end() - timeline_.start(), margin = span.length() * 0.03;
    if (span.length() + 2.0 * margin > width)
        timeline_.setView(std::max(0.0, span.start - margin),
                          timeline_.width() / (span.length() + 2.0 * margin));
    else
        timeline_.setView(std::max(0.0, span.start - margin), timeline_.pixelsPerSecond());
    repaint();
}

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (!clip_)
        return; // the detail panel says what to select
    timeline_.gridLines(tempo_, lines_);
    paintRows(g);
    paintGhosts(g);
    paintNotes(g);
    paintVelocities(g);
    paintKeys(g);
    paintRuler(g);
    paintChordLane(g);

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

    // Rows: the piano's, always: black keys a tone darker, a line under every C (octaves) and
    // between E and F. In key (§21), every row has a line under it, as the rows no longer
    // alternate, and a row outside the key (its notes') is hatched.
    const auto white = t.roles.keyRowLight;
    const auto black = t.roles.keyRowDark;
    const int rowCount = geo.rowCount();
    const int highest = std::min(rowCount - 1, static_cast<int>(std::ceil(topEdge_)) - 1);
    const int lowest = std::max(0, static_cast<int>(std::floor(topEdge_ - 1.0 - area.getHeight() / rowHeight_)));
    const auto x = static_cast<float>(area.getX()), w = static_cast<float>(area.getWidth());
    const auto rh = static_cast<float>(rowHeight_);
    const bool folded = folded_;
    const bool pads = onPads();
    if (folded) // below the last row: nothing
    {
        g.setColour(t.surface0);
        g.fillRect(area);
    }
    for (int r = lowest; r <= highest; ++r)
    {
        const int p = folded ? rows_[static_cast<std::size_t>(r)] : r;
        const float y = static_cast<float>(area.getY() + geo.yForPitch(p));
        // On the pads every row is a pad's, one shade; a pitch with notes and no pad is hatched,
        // as a note outside the key is (§23.2).
        g.setColour(pads || !isBlackKey(p) ? white : black);
        g.fillRect(x, y, w, rh);
        const bool stray =
            pads ? !padNotes_.test(static_cast<std::size_t>(p)) : key_ && !music::contains(*key_, p);
        if (folded && stray) // inside the row, clear of notes' edges
            ui::paintHatch(g, {x, y + 1.0f, w, rh - 2.0f}, t.roles.keyRowHatch);
        if (p % 12 == 0 || p % 12 == 5 || folded)
        {
            g.setColour(p % 12 == 0 ? t.roles.gridOctave : t.roles.gridMinor);
            g.fillRect(x, y + rh - 1.0f, w, 1.0f);
        }
    }

    // Outside the clip (or its loop): notes there are kept but don't play.
    const auto [played0, played1] = playedRange();
    const float clipX0 = static_cast<float>(area.getX() + timeline_.xForTime(played0));
    const float clipX1 = static_cast<float>(area.getX() + timeline_.xForTime(played1));
    g.setColour(t.roles.shade);
    if (clipX0 > x)
        g.fillRect(x, static_cast<float>(area.getY()), clipX0 - x, static_cast<float>(area.getHeight()));
    if (clipX1 < x + w)
        g.fillRect(clipX1, static_cast<float>(area.getY()), x + w - clipX1, static_cast<float>(area.getHeight()));

    time::paintGrid(g, lines_, timeline_, area);
    // The time selection across every row, under the notes (time-selection.md §14.1).
    if (timeSpan_)
        time::paintTimeSelection(g, timeline_, area, timeSpan_->start, timeSpan_->end);

    // An empty kit and clip: nothing to write on yet, and what to do (§23.2).
    if (pads && rows_.empty())
        ui::paintEmptyState(
            g, area, "No pads with sounds yet: drop samples on the Drum Kit's pads, or show the whole roll");
}

void PianoRoll::paintGhosts(juce::Graphics& g)
{
    if (!options_.others || ghosts_.empty())
        return;
    const auto& t = ui::Theme::current();
    const auto area = gridArea();
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    const auto geo = geometry();
    for (const auto& ghost : ghosts_)
    {
        const auto& n = ghost.note;
        if (folded_ && !std::binary_search(rows_.begin(), rows_.end(), n.pitch))
            continue; // not a row shown
        const float x0 = static_cast<float>(area.getX() + geo.xForBeat(n.start));
        const float x1 = static_cast<float>(area.getX() + geo.xForBeat(n.start + n.length));
        const auto r = juce::Rectangle<float>(x0, static_cast<float>(area.getY() + geo.yForPitch(n.pitch)),
                                              std::max(2.0f, x1 - x0), static_cast<float>(rowHeight_))
                           .reduced(0.0f, 2.0f);
        if (!r.intersects(area.toFloat()))
            continue;
        g.setColour(t.ghost(t.clipColour(ghost.colour)));
        g.fillRoundedRectangle(r, 2.0f);
    }
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
    const double clipStart = beatForSeconds(playedRange().first), clipEnd = beatForSeconds(playedRange().second);
    const bool labels = rowHeight_ >= 10.0;
    g.setFont(ui::font(ui::TextStyle::Micro));
    const float microHeight = ui::font(ui::TextStyle::Micro).getHeight();

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
            if (!inside)
                fill = t.faded(fill);
            if (n.muted) // muted (§9.4): hollow, in its colour
            {
                g.setColour(fill);
                g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
            }
            else
            {
                g.setColour(fill);
                g.fillRoundedRectangle(r, 2.0f);
            }
            // Chance (§10): a bar along its top, as long as its chance, where the note's name leaves room
            // for it (a narrow note, or rows tall enough for both); a named note writes its percentage.
            const bool chancy = n.chance < 1.0f && !n.muted && r.getHeight() >= 6.0f;
            const bool named = labels && r.getWidth() > 30.0f;
            constexpr float bar = 2.0f;
            const bool roomy = r.getHeight() >= microHeight + 2.0f * bar + 2.0f; // the bar, a gap, the name
            if (chancy && (!named || roomy))
            {
                g.setColour(t.roles.onClip);
                g.fillRect(r.withHeight(bar).withWidth(r.getWidth() * n.chance).translated(0.0f, 1.0f));
            }
            if (selected)
            {
                g.setColour(t.textPrimary);
                g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
            }
            if (named)
            {
                const auto name = noteName(n.pitch);
                const auto text = chancy && roomy ? r.reduced(4.0f, 0.0f).withTrimmedTop(1.0f + bar)
                                                  : r.reduced(4.0f, 0.0f);
                g.setColour(n.muted ? fill : t.roles.onClip);
                g.drawText(name, text, juce::Justification::centredLeft, false);
                const auto percent = juce::String(juce::roundToInt(n.chance * 100.0f)) + "%";
                const auto micro = ui::TextStyle::Micro;
                if (chancy &&
                    r.getWidth() >
                        static_cast<float>(nameWidth(name, micro) + nameWidth(percent, micro)) + 12.0f)
                    g.drawText(percent, text, juce::Justification::centredRight, false);
            }
        }

    if (gesture_ == Gesture::Box && moved_)
        time::paintBoxSelection(g, juce::Rectangle<float>(dragStart_, lastDrag_));
}

void PianoRoll::paintVelocities(juce::Graphics& g)
{
    // The bottom lane (§11): each note's level as a stem (velocity, chance or release velocity), or
    // a controller's lane, drawn as automation lanes are.
    const auto& t = ui::Theme::current();
    const auto area = velocityArea();
    g.setColour(t.line);
    g.fillRect(getLocalBounds().withY(area.getY() - 1).withHeight(1));
    g.setColour(t.roles.subLane);
    g.fillRect(area);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    time::paintGrid(g, lines_, timeline_, area, false);
    if (showsController())
    {
        paintControllerLane(g);
        return;
    }

    const auto level = laneLevel();
    const auto geo = geometry();
    const auto inner = area.reduced(0, 5).toFloat();
    const auto& notes = shownNotes();
    const auto& selection = shownSelection();
    const auto colour = t.clipColour(clip_->colour);
    const auto yOf = [&inner](float v)
    { return inner.getBottom() - std::clamp(v, 0.0f, 1.0f) * inner.getHeight(); };
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const bool selected = selection.contains(i);
            if (selected != (pass == 1))
                continue;
            const auto& n = notes[i];
            const float x = static_cast<float>(area.getX() + geo.xForBeat(n.start));
            if (x < static_cast<float>(area.getX()) - 4.0f || x > static_cast<float>(area.getRight()) + 4.0f)
                continue;
            // Velocity spread (§10): the range it plays in, behind the stem.
            if (level == model::NoteLevel::Velocity && n.velocitySpread > 0.0f)
            {
                g.setColour(t.ghost(colour));
                const float hi = yOf(n.velocity + n.velocitySpread), lo = yOf(n.velocity - n.velocitySpread);
                g.fillRect(x - 2.0f, hi, 5.0f, lo - hi);
            }
            const float top = yOf(model::levelOf(n, level));
            g.setColour(selected ? t.textPrimary : t.noteFill(colour, n.velocity, false));
            g.fillRect(x, top, 1.0f, inner.getBottom() - top);
            g.fillRect(x - 2.0f, top - 1.0f, 5.0f, 3.0f);
        }

    if (gesture_ == Gesture::VelocityLine && moved_)
    {
        // The line being drawn (§9.3).
        g.setColour(t.textSecondary);
        g.drawLine(juce::Line<float>(dragStart_, lastDrag_), 1.0f);
    }
    if (gesture_ == Gesture::Velocity || gesture_ == Gesture::VelocityOffset ||
        gesture_ == Gesture::VelocityLine)
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
            const float v = model::levelOf(notes[*nearest], level);
            const auto text = level == model::NoteLevel::Chance
                                  ? "Chance " + juce::String(juce::roundToInt(v * 100.0f)) + " %"
                              : level == model::NoteLevel::Release
                                  ? "Release " + juce::String(juce::roundToInt(v * 127.0f))
                                  : "Velocity " + juce::String(juce::roundToInt(v * 127.0f));
            g.setColour(t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::Value));
            g.drawText(text, area.reduced(6, 2).removeFromTop(14), juce::Justification::topRight, false);
        }
    }
}

void PianoRoll::paintControllerLane(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto controller = options_.lane.controller;
    LanePaint paint{.points = lanePreview_ ? &lanePreview_->points
                              : laneKept_  ? &laneKept_->points
                                           : &lanePoints_,
                    .selected = points_,
                    .hovered = hoveredPoint_,
                    .on = true,
                    .colour = t.clipColour(clip_->colour),
                    .current = model::restingValue(controller),
                    .hint = true};
    // The time selection over it too: a drag here selects time and its points (time-selection.md §14.1).
    if (timeSpan_)
        time::paintTimeSelection(g, timeline_, velocityArea(), timeSpan_->start, timeSpan_->end);
    paintAutomationLane(g, laneGeometry(), paint, velocityArea());
    if (gesture_ == Gesture::Controller && laneGesture_.boxing() && moved_)
        time::paintBoxSelection(g, juce::Rectangle<float>(dragStart_, lastDrag_));
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
    const auto x = static_cast<float>(area.getX()), w = static_cast<float>(area.getWidth());
    const auto rh = static_cast<float>(rowHeight_);
    const int rowCount = geo.rowCount();
    const int highest = std::min(rowCount - 1, static_cast<int>(std::ceil(topEdge_)) - 1);
    const int lowest = std::max(0, static_cast<int>(std::floor(topEdge_ - 1.0 - area.getHeight() / rowHeight_)));
    g.setFont(ui::font(ui::TextStyle::Micro));

    if (onPads())
    {
        paintPadKeys(g);
        return;
    }
    if (folded_)
    {
        // In key (§21): each row a key of its own, white or black as on the piano, labelled with
        // its note and, for the key's notes, its step.
        g.setColour(t.surface1);
        g.fillRect(area);
        for (int r = lowest; r <= highest; ++r)
        {
            const int p = rows_[static_cast<std::size_t>(r)];
            const float y = static_cast<float>(area.getY() + geo.yForPitch(p));
            const bool held = previewPitch_ == p ||
                              std::find(previewChordPitches_.begin(), previewChordPitches_.end(), p) !=
                                  previewChordPitches_.end();
            g.setColour(held ? t.active : isBlackKey(p) ? blackKey : whiteKey);
            g.fillRect(x, y, w, rh);
            g.setColour(t.roles.keySeam);
            g.fillRect(x, y + rh - 1.0f, w, 1.0f);
            if (rowHeight_ >= 8.0)
            {
                auto label = noteName(p);
                if (const int step = key_ ? music::scaleStep(*key_, p) : -1; step >= 0)
                    label << juce::String::fromUTF8(" · ") << (step + 1);
                g.setColour(held ? t.onActive : isBlackKey(p) ? whiteKey : t.surface0);
                g.drawText(label, juce::Rectangle<float>(x, y, w - 4.0f, rh - 1.0f),
                           juce::Justification::centredRight, false);
            }
        }
        g.setColour(t.surface0);
        g.fillRect(area.getRight() - 1, area.getY(), 1, area.getHeight());
        return;
    }

    g.setColour(whiteKey);
    g.fillRect(area);
    for (int p = lowest; p <= highest; ++p)
    {
        const float y = static_cast<float>(area.getY() + geo.yForPitch(p));
        const bool held =
            previewPitch_ == p || std::find(previewChordPitches_.begin(), previewChordPitches_.end(), p) !=
                                      previewChordPitches_.end();
        // The whole roll is the piano (§21): the C's labelled, the key shown by nothing else.
        const bool labelled = p % 12 == 0 && rowHeight_ >= 8.0;
        if (isBlackKey(p))
        {
            g.setColour(held ? t.active : blackKey);
            g.fillRect(x, y, w * 0.62f, rh);
            g.setColour(t.roles.keySeam); // where the white keys meet, beside it
            g.fillRect(x + w * 0.62f, y + rh * 0.5f, w * 0.38f, 1.0f);
            if (labelled)
            {
                g.setColour(held ? t.onActive : whiteKey);
                g.drawText(noteName(p), juce::Rectangle<float>(x, y, w * 0.62f - 4.0f, rh),
                           juce::Justification::centredRight, false);
            }
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
        if (labelled)
        {
            g.setColour(held ? t.onActive : t.surface0);
            g.drawText(noteName(p), juce::Rectangle<float>(x, y, w - 4.0f, rh), juce::Justification::centredRight, false);
        }
    }
    g.setColour(t.surface0);
    g.fillRect(area.getRight() - 1, area.getY(), 1, area.getHeight());
}

void PianoRoll::paintPadKeys(juce::Graphics& g)
{
    // Each row its pad's name, as the kit's pads show it (ui::PadGrid): a pad with a sound raised,
    // its name then its note, the whole row faded while the pad is off; a pitch with notes and no
    // pad a well with its note alone, hatched as its row is (§23.2).
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    const auto area = keysArea();
    const auto geo = geometry();
    g.setColour(t.surface1);
    g.fillRect(area);
    const auto x = static_cast<float>(area.getX()), w = static_cast<float>(area.getWidth() - 1);
    const auto rh = static_cast<float>(rowHeight_);
    const int highest = std::min(geo.rowCount() - 1, static_cast<int>(std::ceil(topEdge_)) - 1);
    const int lowest =
        std::max(0, static_cast<int>(std::floor(topEdge_ - 1.0 - area.getHeight() / rowHeight_)));
    // The pads' own styles where the rows are tall enough for them, else the keys' small one.
    const auto fits = [this](ui::TextStyle style)
    { return ui::font(style).getHeight() + 2.0f <= rowHeight_; };
    const auto nameStyle = fits(ui::TextStyle::Control) ? ui::TextStyle::Control : ui::TextStyle::Micro;
    const auto noteStyle = fits(ui::TextStyle::ValueSmall) ? ui::TextStyle::ValueSmall : ui::TextStyle::Micro;
    for (int r = lowest; r <= highest; ++r)
    {
        const int p = rows_[static_cast<std::size_t>(r)];
        const auto cell =
            juce::Rectangle<float>(x, static_cast<float>(area.getY() + geo.yForPitch(p)), w, rh - 1.0f);
        const auto* pad = padOn(p);
        const bool held = previewPitch_ == p;
        // A pad that's off recedes, whole, as on the kit (dimAlpha, never compounded).
        const bool off = pad != nullptr && !pad->on;
        if (off)
            g.beginTransparencyLayer(t.roles.dimAlpha);
        g.setColour(pad != nullptr ? t.lift(t.surface3, held, held) : t.surface0);
        g.fillRect(cell);
        if (pad == nullptr)
            ui::paintHatch(g, cell, t.roles.keyRowHatch);
        if (rowHeight_ >= 8.0)
        {
            auto text = cell.reduced(static_cast<float>(s.listTextInset), 0.0f);
            const auto note = noteName(p);
            g.setColour(t.textTertiary);
            g.setFont(ui::font(noteStyle));
            g.drawText(note, text, juce::Justification::centredRight, false);
            if (pad != nullptr)
            {
                text.removeFromRight(static_cast<float>(nameWidth(note, noteStyle) + s.groupGap));
                g.setColour(held ? t.textPrimary : t.textSecondary);
                g.setFont(ui::font(nameStyle));
                g.drawText(pad->name, text, juce::Justification::centredLeft, true);
            }
        }
        if (off)
            g.endTransparencyLayer();
    }
    g.setColour(t.surface0);
    g.fillRect(area.getRight() - 1, area.getY(), 1, area.getHeight());
}

void PianoRoll::paintRuler(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    g.setColour(t.surface1);
    g.fillRect(
        getLocalBounds().removeFromTop(s.loopStripHeight + s.rulerHeight)); // the corner above the keys too
    // The clip's loop brace: its loop, or (Loop off) what it would loop (clip-loops.md §6).
    time::paintLoopStrip(g, timeline_, loopStripArea(), brace_.preview().value_or(playedRange()), clip_->loop.on());
    time::paintRuler(g, lines_, timeline_, rulerArea(), {.range = playedRange(), .rangeColour = t.clipColour(clip_->colour)});

    // The selection's time handles (§9.2): a bracket over its span, a handle at each end.
    const auto handles = timeHandles();
    const auto& [left, right] = handles;
    if (left.isEmpty())
        return;
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(rulerArea());
    time::paintTimeHandles(g, handles);
    if (gesture_ == Gesture::TimeHandle && moved_)
        ui::paintValueTag(g, (handleAtEnd_ ? right : left).getCentre().toFloat(), getLocalBounds(),
                          factorText(stretchFactor_));
}

std::optional<std::pair<double, double>> PianoRoll::selectionSpan() const
{
    const auto& selection = shownSelection();
    if (selection.size() < 2)
        return std::nullopt;
    return model::notesSpan(shownNotes(), selection);
}

std::pair<juce::Rectangle<int>, juce::Rectangle<int>> PianoRoll::timeHandles() const
{
    const auto span = selectionSpan();
    if (!span || !clip_)
        return {};
    const auto ruler = rulerArea();
    const auto geo = geometry();
    return time::timeHandles(ruler, ruler.getX() + static_cast<int>(std::round(geo.xForBeat(span->first))),
                             ruler.getX() + static_cast<int>(std::round(geo.xForBeat(span->second))));
}

void PianoRoll::paintChordLane(juce::Graphics& g)
{
    const int lines = chordLaneLines();
    if (lines == 0)
        return;
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    const auto lane = chordLaneArea();
    const auto row = getLocalBounds().withY(lane.getY()).withHeight(lane.getHeight());
    g.setColour(t.roles.keyRowDark);
    g.fillRect(row);
    g.setColour(t.line);
    g.fillRect(row.withY(row.getBottom() - 1).withHeight(1));

    const bool own = !spans_.empty() || options_.draw != model::DrawMode::Note;
    auto lineArea = lane.withHeight(s.rowHeight);
    auto labelArea = row.withWidth(keysWidth()).withHeight(s.rowHeight);
    const auto label = [&](const char* text, juce::Rectangle<int> area)
    {
        const int w = static_cast<int>(std::ceil(
            juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), juce::String(text).toUpperCase())));
        ui::paintCapsLabel(g, text, area.withSizeKeepingCentre(w, area.getHeight()).toFloat());
    };
    if (own)
    {
        label("Chords", labelArea);
        // Live while a gesture or the popover's hover changes the notes.
        if (preview_)
        {
            const auto spans = model::chordSpans(preview_->notes);
            paintChordLine(g, lineArea, spans, labelsFor(spans), true);
        }
        else
            paintChordLine(g, lineArea, spans_, spanLabels_, true);
        lineArea.translate(0, s.rowHeight);
        labelArea.translate(0, s.rowHeight);
    }
    if (options_.others && !allSpans_.empty())
    {
        label("All", labelArea);
        paintChordLine(g, lineArea, allSpans_, allLabels_, false);
    }
}

std::vector<PianoRoll::LaneLabel> PianoRoll::labelsFor(const std::vector<model::ChordSpan>& spans) const
{
    const bool steps = key_ && options_.chordLabels != model::ChordLabels::Names;
    const bool names = !key_ || options_.chordLabels != model::ChordLabels::Steps;
    std::vector<LaneLabel> out;
    out.reserve(spans.size());
    for (const auto& span : spans)
    {
        LaneLabel label;
        if (names)
            label.name = utf8(music::chordName(span.chord, keyOrNull()));
        if (steps)
            label.step = (names ? juce::String::fromUTF8(" · ") : juce::String()) +
                         utf8(music::romanNumeral(span.chord, *key_));
        label.nameWidth = nameWidth(label.name);
        label.stepWidth = nameWidth(label.step);
        out.push_back(std::move(label));
    }
    return out;
}

void PianoRoll::updateLabels()
{
    spanLabels_ = labelsFor(spans_);
    allLabels_ = labelsFor(allSpans_);
}

int PianoRoll::nameWidth(const juce::String& text, ui::TextStyle style) const
{
    // Measuring is slow (a font's fallback for ♯ and ♭), and a clip's chords and notes repeat: kept
    // by style and text.
    if (text.isEmpty())
        return 0;
    const auto key = juce::String(static_cast<int>(style)) + "|" + text;
    if (const auto it = widths_.find(key); it != widths_.end())
        return it->second;
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(style), text)));
    if (widths_.size() > 512)
        widths_.clear();
    widths_.emplace(key, w);
    return w;
}

void PianoRoll::paintChordLine(juce::Graphics& g, juce::Rectangle<int> area,
                               const std::vector<model::ChordSpan>& spans,
                               const std::vector<LaneLabel>& labels, bool own)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    const auto geo = geometry();
    const auto& selection = shownSelection();
    const auto nameFont = ui::font(ui::TextStyle::Name);
    g.setFont(nameFont);
    const int least = nameWidth(juce::String::fromUTF8("…")); // the least room worth writing in
    const auto xOf = [&](double beat)
    { return area.getX() + static_cast<int>(std::round(geo.xForBeat(beat))); };

    for (std::size_t k = 0; k < spans.size(); ++k)
    {
        const auto& span = spans[k];
        const int x0 = xOf(span.start), x1 = xOf(span.end);
        const int next = k + 1 < spans.size() ? xOf(spans[k + 1].start) : area.getRight();
        if (x1 < area.getX() || x0 > area.getRight())
            continue;
        const auto spanArea =
            juce::Rectangle<int>(x0, area.getY(), std::max(1, x1 - x0), area.getHeight() - 1);
        if (own && !preview_ &&
            std::all_of(span.notes.begin(), span.notes.end(), [&](auto i) { return selection.contains(i); }))
        {
            g.setColour(t.roles.selectionFill);
            g.fillRect(spanArea);
        }
        else if (own && hoveredSpan_ == k)
        {
            g.setColour(t.roles.hoverWash);
            g.fillRect(spanArea);
        }
        g.setColour(t.line);
        g.fillRect(x0, area.getY(), 1, area.getHeight() - 1);

        // The name, then the step after it; cut short before the next chord, the step going first;
        // nothing where there isn't room for an ellipsis.
        const auto text = juce::Rectangle<int>(x0, area.getY(), next - x0, area.getHeight() - 1)
                              .reduced(s.fieldTextInset, 0);
        if (text.getWidth() < least || k >= labels.size())
            continue;
        const auto& label = labels[k];
        const bool whole = label.nameWidth + label.stepWidth <= text.getWidth();
        if (label.name.isNotEmpty())
        {
            g.setColour(own ? t.textPrimary : t.textSecondary);
            g.drawText(label.name, text, juce::Justification::centredLeft, label.nameWidth > text.getWidth());
        }
        if (label.step.isNotEmpty() && (whole || label.name.isEmpty()))
        {
            g.setColour(own ? t.textSecondary : t.textTertiary);
            g.drawText(label.step, text.withTrimmedLeft(label.nameWidth), juce::Justification::centredLeft,
                       !whole);
        }
    }

    // Where the Chord popover writes (§7.3): marked as a drop is.
    if (own && chordTarget_)
    {
        const auto target = juce::Rectangle<int>(
            xOf(chordTarget_->start), area.getY(),
            std::max(1, xOf(chordTarget_->end) - xOf(chordTarget_->start)), area.getHeight() - 1);
        g.setColour(t.roles.selectionFill);
        g.fillRect(target);
        g.setColour(t.active);
        g.fillRect(target.withTop(target.getBottom() - s.dropLine));
    }
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

std::optional<std::size_t> PianoRoll::spanAtX(float x) const
{
    const double beat = beatAtX(x);
    for (std::size_t k = 0; k < spans_.size(); ++k)
        if (beat >= spans_[k].start && beat < spans_[k].end)
            return k;
    return std::nullopt;
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;
    const auto pos = e.getPosition();
    std::optional<std::size_t> hovered;
    const auto [left, right] = timeHandles();
    const auto grab = static_cast<int>(ui::Theme::current().input.edgeGrab) / 2;
    if (clip_ && keysArea().contains(pos))
        cursor = juce::MouseCursor::PointingHandCursor;
    else if (clip_ && !left.isEmpty() &&
             (left.expanded(grab, 0).contains(pos) || right.expanded(grab, 0).contains(pos)))
        cursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (clip_ && loopStripArea().contains(pos))
    {
        const auto zone = ClipLoopBrace::zoneAt(*clip_, contentStartSeconds(), timeline_, e.position.x - static_cast<float>(gridArea().getX()));
        if (zone == model::BraceZone::Start || zone == model::BraceZone::End)
            cursor = juce::MouseCursor::LeftRightResizeCursor;
    }
    else if (clip_ && chordLaneArea().contains(pos))
    {
        const bool ownLine = (!spans_.empty() || options_.draw != model::DrawMode::Note) &&
                             pos.y < chordLaneArea().getY() + ui::Theme::current().shell.rowHeight;
        if (ownLine)
            hovered = spanAtX(e.position.x);
        if (hovered)
            cursor = juce::MouseCursor::PointingHandCursor;
    }
    if (clip_ && gridArea().contains(pos))
    {
        const auto local = e.position - gridArea().getPosition().toFloat();
        if (const auto hit = model::hitNote(notes_, geometry(), local.x, local.y, ui::Theme::current().input.edgeGrab))
            cursor = hit->zone == model::NoteHit::Zone::Body ? juce::MouseCursor::NormalCursor
                                                              : juce::MouseCursor::LeftRightResizeCursor;
    }
    // A controller lane's points and segments, as an automation lane's (§11).
    std::optional<size_t> hoveredPoint;
    if (clip_ && showsController() && velocityArea().contains(pos))
    {
        const auto hover = LaneGesture::hover(gestureLane(), e.position);
        hoveredPoint = hover.point;
        if (hover.point)
            cursor = juce::MouseCursor::DraggingHandCursor;
        else if (hover.segment)
            cursor = juce::MouseCursor::UpDownResizeCursor;
    }
    if (hoveredPoint != hoveredPoint_)
    {
        hoveredPoint_ = hoveredPoint;
        repaint(velocityArea());
    }
    if (hovered != hoveredSpan_)
    {
        hoveredSpan_ = hovered;
        repaint(chordLaneArea());
    }
    setMouseCursor(cursor);
}

void PianoRoll::mouseExit(const juce::MouseEvent&)
{
    if (std::exchange(hoveredPoint_, std::nullopt))
        repaint(velocityArea());
    if (hoveredSpan_)
    {
        hoveredSpan_.reset();
        repaint(chordLaneArea());
    }
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    if (onActivated)
        onActivated();
    gesture_ = Gesture::None;
    if (std::exchange(preview_, std::nullopt))
        updateRows(); // a tool's preview, given up
    toggleOnClick_.reset();
    if (!clip_)
        return;
    dragStart_ = lastDrag_ = e.position;
    moved_ = false;
    const auto pos = e.getPosition();
    const auto area = gridArea();
    const auto local = e.position - area.getPosition().toFloat();
    const auto geo = geometry();
    const bool shift = e.mods.isShiftDown(), ctrl = e.mods.isCommandDown();

    if (keysArea().contains(pos))
    {
        if (folded_ && rows_.empty())
            return; // an empty kit's: no rows
        gesture_ = Gesture::Keys;
        const int pitch = std::clamp(geo.pitchForY(local.y), 0, 127);
        previewOn(pitch, drawVelocity_);
        // On the pads, a pad's name is its key: it plays it and selects it in the kit, so the
        // chain shows its sound (§23.2).
        if (onPads() && padOn(pitch) != nullptr)
        {
            pressedPad_ = pitch;
            session_.selectPad(kit_, pitch);
        }
        return;
    }
    if (loopStripArea().contains(pos))
    {
        // The clip's loop brace (clip-loops.md §6), as the transport's in the arrangement.
        gesture_ = Gesture::Brace;
        brace_.press(*clip_, contentStartSeconds(), timeline_, local.x);
        return;
    }
    if (const auto [left, right] = timeHandles(); !left.isEmpty() && !e.mods.isPopupMenu())
    {
        const auto grab = static_cast<int>(ui::Theme::current().input.edgeGrab) / 2;
        const bool atStart = left.expanded(grab, 0).contains(pos),
                   atEnd = right.expanded(grab, 0).contains(pos);
        if (atStart || atEnd)
        {
            // A time handle (§9.2): stretches the selection from its other end.
            gesture_ = Gesture::TimeHandle;
            handleAtEnd_ = atEnd && (!atStart || std::abs(pos.x - right.getCentreX()) <
                                                     std::abs(pos.x - left.getCentreX()));
            base_ = {notes_, selection_};
            stretchFactor_ = 1.0;
            return;
        }
    }
    if (rulerArea().contains(pos))
    {
        gesture_ = Gesture::Ruler;
        rulerGesture_.begin(local.x);
        return;
    }
    if (chordLaneArea().contains(pos))
    {
        const auto& s = ui::Theme::current().shell;
        const bool ownLine = (!spans_.empty() || options_.draw != model::DrawMode::Note) &&
                             pos.y < chordLaneArea().getY() + s.rowHeight;
        const auto span = ownLine ? spanAtX(e.position.x) : std::nullopt;
        if (e.mods.isPopupMenu())
        {
            // The lane's menu: what it writes for each chord (§7.1, Display).
            juce::PopupMenu menu;
            const juce::Component::SafePointer<PianoRoll> safe(this);
            for (auto labels :
                 {model::ChordLabels::Both, model::ChordLabels::Names, model::ChordLabels::Steps})
                menu.addItem(juce::PopupMenu::Item(model::chordLabelsName(labels))
                                 .setTicked(options_.chordLabels == labels)
                                 .setEnabled(key_.has_value() || labels == model::ChordLabels::Names)
                                 .setAction(
                                     [safe, labels]
                                     {
                                         if (safe == nullptr)
                                             return;
                                         auto o = safe->options();
                                         o.chordLabels = labels;
                                         safe->setOptions(o);
                                     }));
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
            return;
        }
        if (e.getNumberOfClicks() >= 2)
        {
            // A double-click opens the Chord popover: for the chord there, or for a grid step
            // from the click in an empty part (§7.1).
            ChordPlace place;
            if (span)
                place = placeOf(spans_[*span]);
            else
            {
                const double at = std::max(0.0, beatGrid(e.mods.isAltDown()).floor(beatAtX(e.position.x)));
                place = {at, at + gridStep(), {}};
            }
            if (onChordPopover)
                onChordPopover(place, laneAreaFor(place));
            return;
        }
        if (span)
        {
            // A click selects the chord's notes, so every command applies to it (§5).
            auto selection = shift ? selection_ : std::set<std::size_t>{};
            selection.insert(spans_[*span].notes.begin(), spans_[*span].notes.end());
            setSelection(std::move(selection));
            return;
        }
        gesture_ = Gesture::ChordLane; // a drag sets a longer place for a chord
        return;
    }
    pointsUsed_ = false; // the notes, unless it's the controller lane
    if (velocityArea().contains(pos) && showsController())
    {
        // A controller lane (§11): the automation lanes' gestures; from an empty spot, a drag
        // selects time, and the lane's points in it (time-selection.md §14.1).
        pointsUsed_ = true;
        if (const auto hover = LaneGesture::hover(gestureLane(), e.position);
            e.getNumberOfClicks() == 1 && !hover.point && !hover.segment && !shift && !ctrl &&
            !e.mods.isPopupMenu())
        {
            gesture_ = Gesture::LaneTime;
            points_.clear();
            setTimeSelection(std::nullopt);
            repaint(velocityArea());
            return;
        }
        gesture_ = Gesture::Controller;
        const auto out = laneGesture_.press(e, gestureLane());
        if (out.steps && onMessage)
            onMessage(juce::String::fromUTF8(model::controllerName(options_.lane.controller).c_str()) +
                      " is on or off: it holds each value until the next point, so its lane can't curve");
        if (e.getNumberOfClicks() >= 2 || out.steps)
        {
            gesture_ = Gesture::None;
            if (out.keep)
                keepLane(*out.keep, out.undoName);
            return;
        }
        if (out.select)
        {
            points_ = *out.select;
            repaint(velocityArea());
        }
        return;
    }
    if (velocityArea().contains(pos))
    {
        base_ = {notes_, selection_};
        if (shift)
        {
            gesture_ = Gesture::VelocityLine; // a straight line across the notes (§9.3)
            return;
        }
        if (const auto stem = velocityStemAt(local.x); stem && selection_.contains(*stem))
        {
            gesture_ = Gesture::VelocityOffset; // drag the selection's velocities together
            return;
        }
        gesture_ = Gesture::Velocity; // draw velocities (of the selection, if any)
        const double tolerance = 4.0 / geo.pixelsPerBeat;
        const double beat = geo.beatForX(local.x);
        preview_ = model::NoteEdit{model::paintLevel(notes_, beat - tolerance, beat + tolerance,
                                                     velocityAtY(e.position.y),
                                                     selection_.empty() ? nullptr : &selection_, laneLevel()),
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
            setSelection({hit->index});
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
        if (folded_ && rows_.empty())
            return; // an empty kit's: no rows to draw on (§23.2)
        if (onPads())
        {
            // On the pads, a hit on the row clicked, a grid step long; dragged along the row, a hit
            // on each step it passes (§23.2, D10). Lengthened by its end afterwards, as any note.
            gesture_ = Gesture::Paint;
            paintPitch_ = std::clamp(geo.pitchForY(local.y), 0, 127);
            paintFrom_ = geo.beatForX(local.x);
            preview_ =
                model::paintRow(notes_, paintPitch_, paintFrom_, paintFrom_, quantiseGrid(), drawVelocity_);
            previewOn(paintPitch_, drawVelocity_);
            repaint();
            return;
        }
        // Draw a note (or a chord, §7.2) one grid step long, on the row and in the step under the
        // mouse (Alt: free of the grid); dragging lengthens it.
        base_ = drawnAt(local, e.mods.isAltDown());
        if (base_.selection.empty())
            return;
        preview_ = base_;
        updateRows(); // a chord's notes outside the key show their rows
        gesture_ = Gesture::Draw;
        drag_ = {.kind = model::NoteDragKind::ResizeEnd, .anchor = *base_.selection.begin()};
        std::vector<int> pitches;
        for (auto i : base_.selection)
            pitches.push_back(base_.notes[i].pitch);
        if (pitches.size() > 1)
            previewChordOn(pitches, drawVelocity_);
        else
            previewOn(pitches.front(), drawVelocity_);
        repaint();
        return;
    }
    clickedInSelection_.reset();
    if (hit)
    {
        setTimeSelection(std::nullopt); // a box's stretch: the notes chosen now are these
        auto selection = selection_;
        if (shift)
        {
            // Shift-click: in or out of the selection at once.
            if (selection.contains(hit->index))
                selection.erase(hit->index);
            else
                selection.insert(hit->index);
        }
        else if (ctrl)
        {
            // Ctrl: a click takes it in or out of the selection; a drag copies (§9.1).
            if (selection.contains(hit->index))
                toggleOnClick_ = hit->index;
            else
                selection.insert(hit->index);
        }
        else
        {
            if (selection.size() > 1 && selection.contains(hit->index))
                clickedInSelection_ = hit->index; // stays a group for a drag; just this note for a click
            else if (!selection.contains(hit->index))
                selection = {hit->index};
        }
        setSelection(std::move(selection));
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
    // Empty grid: box selection (Shift or Ctrl adds to the selection), its stretch the time
    // selection (time-selection.md §14.1); a click clears both.
    gesture_ = Gesture::Box;
    const bool additive = shift || ctrl;
    boxBase_ = additive ? selection_ : std::set<std::size_t>{};
    if (!additive && !selection_.empty())
        setSelection({});
    if (!additive)
        setTimeSelection(std::nullopt);
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
    const bool alt = e.mods.isAltDown();

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
    if (gesture_ == Gesture::Brace)
    {
        if (!moved_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
            return;
        moved_ = true;
        const bool free = !isSnapEnabled() || alt;
        brace_.drag(*clip_, timeline_, dragStart_.x - static_cast<float>(area.getX()), local.x,
                    [this, free](double t) { return free ? std::max(0.0, t) : timeline_.snap(t, tempo_); });
        repaint(loopStripArea());
        return;
    }
    if (gesture_ == Gesture::ChordLane)
    {
        moved_ = moved_ || e.getDistanceFromDragStart() >= ui::Theme::current().input.dragThreshold;
        if (moved_)
        {
            const auto grid = beatGrid(alt);
            const double a = beatAtX(dragStart_.x), b = beatAtX(e.position.x);
            const double from = std::max(0.0, grid.round(std::min(a, b))), to = grid.round(std::max(a, b));
            setChordTarget(to > from ? std::optional<ChordPlace>(ChordPlace{from, to, {}}) : std::nullopt);
        }
        return;
    }
    const bool immediate = gesture_ == Gesture::Draw || gesture_ == Gesture::Paint ||
                           gesture_ == Gesture::Velocity || gesture_ == Gesture::VelocityOffset ||
                           gesture_ == Gesture::VelocityLine;
    if (!immediate && !moved_ && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        return; // a click, not a drag
    moved_ = true;
    const double deltaBeats = (e.position.x - dragStart_.x) / geo.pixelsPerBeat;

    switch (gesture_)
    {
    case Gesture::Paint:
        preview_ = model::paintRow(notes_, paintPitch_, paintFrom_, beatAtX(e.position.x), quantiseGrid(),
                                   drawVelocity_);
        break;
    case Gesture::Notes:
    case Gesture::Draw:
        drag_.deltaBeats = deltaBeats;
        if (drag_.kind == model::NoteDragKind::Move)
        {
            // By the rows shown (In key, the key's steps: §21); Ctrl copies (§9.1).
            drag_.deltaPitch = -static_cast<int>(std::lround((e.position.y - dragStart_.y) / rowHeight_));
            drag_.rows = folded_ ? &rows_ : nullptr;
            drag_.copy = gesture_ == Gesture::Notes && e.mods.isCommandDown();
        }
        preview_ = model::dragNotes(base_.notes, base_.selection, drag_, beatGrid(alt));
        if (drag_.kind == model::NoteDragKind::Move)
            previewOn(std::clamp(model::draggedPitch(base_.notes, base_.selection, drag_), 0, 127),
                      base_.notes[drag_.anchor].velocity);
        break;
    case Gesture::Box:
    {
        const double b0 = geo.beatForX(dragStart_.x - static_cast<float>(area.getX()));
        const int p0 = geo.pitchForY(dragStart_.y - static_cast<float>(area.getY()));
        auto selection = boxBase_;
        for (auto i : model::notesInBox(notes_, b0, geo.beatForX(local.x), p0, geo.pitchForY(local.y)))
            selection.insert(i);
        setSelection(std::move(selection));
        setTimeSelection(boxStretch(e));
        break;
    }
    case Gesture::LaneTime:
        if (const auto span = boxStretch(e))
        {
            setTimeSelection(span);
            points_ = model::pointsIn(lanePoints_, span->start, span->end);
        }
        repaint();
        break;
    case Gesture::Velocity:
    {
        // Every note start the mouse passed over since the last event.
        const double tolerance = 4.0 / geo.pixelsPerBeat;
        const double from = geo.beatForX(previous.x - static_cast<float>(area.getX())), to = geo.beatForX(local.x);
        auto notes = model::paintLevel(preview_ ? preview_->notes : notes_, std::min(from, to) - tolerance,
                                       std::max(from, to) + tolerance, velocityAtY(e.position.y),
                                       selection_.empty() ? nullptr : &selection_, laneLevel());
        preview_ = model::NoteEdit{std::move(notes), selection_};
        break;
    }
    case Gesture::VelocityOffset:
    {
        const float delta = -(e.position.y - dragStart_.y) / static_cast<float>(velocityArea().reduced(0, 5).getHeight());
        preview_ = model::NoteEdit{model::offsetLevel(base_.notes, base_.selection, delta, laneLevel()),
                                   base_.selection};
        break;
    }
    case Gesture::VelocityLine:
    {
        const double tolerance = 4.0 / geo.pixelsPerBeat;
        double b0 = beatAtX(dragStart_.x), b1 = beatAtX(e.position.x);
        // The notes under the ends count too, as the pencil's tolerance does.
        b0 += b0 <= b1 ? -tolerance : tolerance;
        b1 += b0 <= b1 ? tolerance : -tolerance;
        preview_ = model::NoteEdit{
            model::lineLevel(base_.notes, b0, velocityAtY(dragStart_.y), b1, velocityAtY(e.position.y),
                             base_.selection.empty() ? nullptr : &base_.selection, laneLevel()),
            base_.selection};
        break;
    }
    case Gesture::Controller:
    {
        const auto out = laneGesture_.drag(e, dragStart_, gestureLane());
        if (out.preview)
            lanePreview_ = *out.preview;
        if (out.select)
            points_ = *out.select;
        break;
    }
    case Gesture::TimeHandle:
    {
        const auto span = model::notesSpan(base_.notes, base_.selection);
        if (!span)
            break;
        const double pivot = handleAtEnd_ ? span->first : span->second;
        const double edge = handleAtEnd_ ? span->second : span->first;
        double to = beatGrid(false).round(beatAtX(e.position.x));
        if (std::abs(to - pivot) < model::minNoteLength)
            to = pivot + (to >= pivot ? model::minNoteLength : -model::minNoteLength);
        stretchFactor_ = (to - pivot) / (edge - pivot);
        // Alt stretches the lengths too (§9.2).
        preview_ = model::stretchNotes(base_.notes, base_.selection, pivot, stretchFactor_, alt);
        break;
    }
    case Gesture::None:
    case Gesture::Keys:
    case Gesture::Ruler:
    case Gesture::Brace:
    case Gesture::ChordLane:
        break; // handled above
    }
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent& e)
{
    if (gesture_ == Gesture::Brace && clip_)
    {
        // A loop drawn or changed (Loop on), or a click on the brace switching it (clip-loops.md §6).
        brace_.release(session_, *clip_, contentStartSeconds(), moved_);
        gesture_ = Gesture::None;
        repaint();
        return;
    }
    if (gesture_ == Gesture::LaneTime)
    {
        gesture_ = Gesture::None; // a click: the lane's points and the stretch cleared at the press
        repaint();
        return;
    }
    if (gesture_ == Gesture::Controller)
    {
        // A controller lane's gesture kept, or a click's selection (§11).
        gesture_ = Gesture::None;
        const auto out = laneGesture_.release(moved_, gestureLane());
        if (out.keep)
            keepLane(*out.keep, out.undoName);
        else
        {
            lanePreview_.reset();
            if (out.select)
                points_ = *out.select;
        }
        repaint();
        return;
    }
    if (gesture_ == Gesture::ChordLane)
    {
        // A place dragged across the lane: the Chord popover for it.
        gesture_ = Gesture::None;
        if (moved_ && chordTarget_ && onChordPopover)
        {
            const auto place = *chordTarget_;
            onChordPopover(place, laneAreaFor(place));
        }
        else
            setChordTarget(std::nullopt);
        return;
    }
    if (gesture_ == Gesture::Ruler && e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
    {
        // A click in the ruler moves the playhead (snapped; Alt for free).
        session_.setPosition(time::rulerTime(timeline_, tempo_,
                                             e.position.x - static_cast<float>(gridArea().getX()),
                                             !isSnapEnabled() || e.mods.isAltDown()));
    }
    if (gesture_ == Gesture::Notes && !moved_)
    {
        if (toggleOnClick_)
        {
            auto selection = selection_;
            selection.erase(*toggleOnClick_);
            setSelection(std::move(selection));
        }
        else if (clickedInSelection_)
            setSelection({*clickedInSelection_});
    }
    clickedInSelection_.reset();
    toggleOnClick_.reset();
    const bool edits = gesture_ == Gesture::Draw || gesture_ == Gesture::Paint ||
                       gesture_ == Gesture::Velocity || gesture_ == Gesture::VelocityOffset ||
                       ((gesture_ == Gesture::Notes || gesture_ == Gesture::VelocityLine ||
                         gesture_ == Gesture::TimeHandle) &&
                        moved_);
    if (edits && preview_)
        commit(std::move(*preview_));
    preview_.reset();
    if (gesture_ == Gesture::Notes || gesture_ == Gesture::Draw || gesture_ == Gesture::Paint ||
        gesture_ == Gesture::Keys)
        previewOff();
    gesture_ = Gesture::None;
    updateRows(); // the rows held still under the gesture: now they follow the notes
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
        // Row height (this view's own axis), keeping the row under the mouse in place.
        const double y = e.position.y - static_cast<float>(area.getY());
        const double dy = wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);
        const double row = topEdge_ - y / rowHeight_;
        rowHeight_ = std::clamp(rowHeight_ * std::pow(2.0, dy), kMinRowHeight, kMaxRowHeight);
        if (!onPads())
            rollRowHeight_ = rowHeight_; // the roll's own; the pads fill the height when opened
        topEdge_ = row + y / rowHeight_;
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
