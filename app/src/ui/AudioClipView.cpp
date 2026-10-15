#include "ui/AudioClipView.h"

#include "model/AudioToolkit.h"
#include "model/ClipLoops.h"
#include "model/Fades.h"
#include "ui/WaveformTiles.h"

#include <cmath>

namespace fd::app
{
namespace
{
/// The range's gain handle, dragged: dB per pixel (Shift: a tenth of it).
constexpr double kGainPerPixel = 0.1;
/// The level column, dragged: pixels for each doubling of the rows' height.
constexpr float kZoomPixels = 48.0f;
constexpr float kMaxZoom = 64.0f;

juce::String dbText(double db)
{
    const auto text = juce::String(std::abs(db), 1) + " dB";
    return db < -0.05 ? juce::String::fromUTF8("\xE2\x88\x92") + text : (db > 0.05 ? "+" + text : text);
}

model::FadeClip fadeClipOf(const model::ClipInfo& c)
{
    return {.id = c.id,
            .start = c.start,
            .length = c.length,
            .offset = c.offset,
            .sourceLength = c.sourceLength,
            .loop = c.loop,
            .source = c.sourcePath,
            .muted = c.muted,
            .reversed = c.sound.reversed,
            .in = c.fadeIn,
            .out = c.fadeOut,
            .crossOut = c.crossfadeOut};
}
} // namespace

AudioClipView::AudioClipView(engine::Session& session) : session_(session)
{
    syncGrid();
    setWantsKeyboardFocus(false);
    waveforms_.onReady = [this] { repaint(); };
    // The lane's head (§4): what the lane shows, the gain line; pitch comes later (§17).
    laneHead_.setValue("GAIN");
    laneHead_.setTooltip("The gain line: the clip's level over time, on top of its gain. Double-click the "
                         "lane to add a point");
    laneHead_.onClick = [this] { laneHead_.showMenu(laneMenu()); };
    addChildComponent(laneHead_);
}

AudioClipView::~AudioClipView() = default;

juce::PopupMenu AudioClipView::laneMenu() const
{
    juce::PopupMenu m;
    m.addItem(1, "Gain", clip_.has_value(), true); // the only one until pitch (§17)
    return m;
}

juce::String AudioClipView::gridStepName() const
{
    return timeline_.gridStepName(tempo_);
}

// ---- geometry ------------------------------------------------------------------------------

juce::Rectangle<int> AudioClipView::loopStripArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedLeft(s.keyboardWidth).removeFromTop(s.loopStripHeight);
}

juce::Rectangle<int> AudioClipView::rulerArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds()
        .withTrimmedLeft(s.keyboardWidth)
        .withTrimmedTop(s.loopStripHeight)
        .removeFromTop(s.rulerHeight);
}

juce::Rectangle<int> AudioClipView::laneArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedLeft(s.keyboardWidth).removeFromBottom(s.velocityHeight);
}

juce::Rectangle<int> AudioClipView::waveformArea() const
{
    const auto& s = ui::Theme::current().shell;
    // Between the ruler and the lane, a line above the lane.
    return getLocalBounds()
        .withTrimmedLeft(s.keyboardWidth)
        .withTrimmedTop(s.loopStripHeight + s.rulerHeight)
        .withTrimmedBottom(s.velocityHeight + 1);
}

juce::Rectangle<int> AudioClipView::levelArea() const
{
    const auto& s = ui::Theme::current().shell;
    return getLocalBounds().withTrimmedTop(s.loopStripHeight + s.rulerHeight).removeFromLeft(s.keyboardWidth);
}

int AudioClipView::rowCount() const
{
    if (!clip_ || clip_->fileChannels < 2)
        return 1;
    using M = model::ChannelMode;
    const auto mode = clip_->sound.channels;
    return mode == M::Stereo || mode == M::Swapped ? 2 : 1;
}

juce::Rectangle<int> AudioClipView::rowArea(int row) const
{
    // Rows of equal height, a line between them.
    const auto area = waveformArea();
    const int n = rowCount();
    const int h = (area.getHeight() - (n - 1)) / n;
    return {area.getX(), area.getY() + row * (h + 1), area.getWidth(),
            row == n - 1 ? area.getBottom() - (area.getY() + row * (h + 1)) : h};
}

juce::Rectangle<float> AudioClipView::rangeHandle() const
{
    if (!range_ || !clip_)
        return {};
    const auto& s = ui::Theme::current().shell;
    const auto row = rowArea(0);
    const float x0 = static_cast<float>(row.getX() + timeline_.xForTime(range_->first));
    const float x1 = static_cast<float>(row.getX() + timeline_.xForTime(range_->second));
    const float size = static_cast<float>(s.fadeHandleSize);
    // It hangs from the top of the first row, as a fade's square from the top of a clip's body.
    return juce::Rectangle<float>(size, size)
        .withCentre({(x0 + x1) * 0.5f, 0.0f})
        .withY(static_cast<float>(row.getY() + s.itemGap));
}

double AudioClipView::origin() const
{
    return clip_ ? model::clipViewOrigin(*clip_) : 0.0;
}

std::pair<double, double> AudioClipView::playedRange() const
{
    return clip_ ? ClipLoopBrace::rangeOf(*clip_, origin()) : std::pair{0.0, 0.0};
}

void AudioClipView::fitToClip()
{
    const auto area = waveformArea();
    if (!clip_ || area.getWidth() <= 1)
        return; // once laid out
    needsFit_ = false;
    timeline_.setWidth(area.getWidth());
    const auto [from, to] = playedRange();
    const double length = std::max(to - from, tempo_.secondsForBeats(tempo_.beatsPerBar()));
    const double margin = length * 0.03;
    timeline_.setView(std::max(0.0, from - margin), area.getWidth() / (length + 2.0 * margin));
}

void AudioClipView::resized()
{
    const auto& s = ui::Theme::current().shell;
    laneHead_.setBounds(getLocalBounds().removeFromBottom(s.velocityHeight).removeFromLeft(s.keyboardWidth));
    timeline_.setWidth(waveformArea().getWidth());
    if (needsFit_)
        fitToClip();
}

// ---- state ---------------------------------------------------------------------------------

void AudioClipView::syncGrid()
{
    // The clip view's grid: the piano roll's (ADR 0047), which the Session keeps.
    if (const auto g = session_.grid(engine::Session::GridEditor::PianoRoll); g != timeline_.grid())
    {
        timeline_.setGrid(g);
        repaint();
    }
}

void AudioClipView::setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo)
{
    tempo_ = tempo;
    if (!clip || clip->kind != model::ClipInfo::Kind::Audio)
    {
        clip_.reset();
        gesture_ = Gesture::None;
        laneHead_.setVisible(false);
        setRange(std::nullopt);
        repaint();
        return;
    }
    const bool another = !clip_ || clip_->id != clip->id;
    clip_ = std::move(clip);
    waveforms_.request(clip_->sourcePath);
    lineView_ = model::lineToView(clip_->sound.line, origin(), clip_->sourceLength, clip_->sound.reversed);
    laneKept_.reset(); // the snapshot has it now
    laneHead_.setVisible(true);
    if (another)
    {
        needsFit_ = true;
        fitToClip();
        points_.clear();
        previewGains_.clear();
        setRange(std::nullopt);
    }
    else if (range_)
    {
        // Kept within the clip as it is now (a trim, an undo).
        const double a = std::max(range_->first, clip_->start), b = std::min(range_->second, clip_->end());
        setRange(b > a ? std::optional(std::pair{a, b}) : std::nullopt);
    }
    if (points_.size() > lineView_.size())
        points_.clear();
    repaint();
}

void AudioClipView::frame(double playheadSeconds)
{
    syncGrid();
    // Over a looped clip's later laps, the playhead shows where in the loop the song is (§6).
    const bool overLoop = clip_ && clip_->loop.on() && playheadSeconds >= clip_->start && playheadSeconds < clip_->end();
    playhead_ = overLoop ? model::clipViewTime(*clip_, playheadSeconds) : playheadSeconds;
    playheadTracker_.update(*this, timeline_, waveformArea().getUnion(laneArea()), playhead_);
}

void AudioClipView::setRange(std::optional<std::pair<double, double>> range)
{
    if (range == range_)
        return;
    range_ = range;
    if (onRangeChanged)
        onRangeChanged();
    repaint();
}

void AudioClipView::selectAll()
{
    if (clip_)
        setRange(std::pair{clip_->start, clip_->end()});
}

void AudioClipView::setLevelZoom(float zoom)
{
    zoom = std::clamp(zoom, 1.0f, kMaxZoom);
    if (zoom == zoom_)
        return;
    zoom_ = zoom;
    repaint();
}

std::string AudioClipView::rangeRefusal(engine::Session::RangeAction action) const
{
    if (!clip_)
        return "Show an audio clip in the clip view.";
    if (!range_)
        return "Select a range of the waveform first: drag across it.";
    return session_.rangeRefusal(clip_->id, range_->first, range_->second, action);
}

bool AudioClipView::editRange(engine::Session::RangeAction action, double db)
{
    using A = engine::Session::RangeAction;
    if (const auto why = rangeRefusal(action); !why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(why.c_str()));
        return false;
    }
    const auto r = session_.editRange(clip_->id, range_->first, range_->second, action, db);
    if (!r.why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(r.why.c_str()));
        return false;
    }
    // A command that made a clip of the range shows it; one that took the range away shows what's left.
    if (action == A::Split || action == A::Crop || action == A::Delete)
    {
        setRange(std::nullopt);
        if (onSelectClips && !r.clips.empty())
            onSelectClips({r.clips.front()});
    }
    return true;
}

// ---- the gain line ---------------------------------------------------------------------------

void AudioClipView::deletePoints()
{
    if (!clip_ || points_.empty())
        return;
    model::PointEdit edit;
    const auto& line = laneKept_ ? laneKept_->points : lineView_;
    for (size_t i = 0; i < line.size(); ++i)
        if (!points_.contains(i))
            edit.points.push_back(line[i]);
    keepLine(edit, "Delete points");
}

model::AudioSound AudioClipView::shownSound() const
{
    auto sound = clip_->sound;
    if (linePreview_)
        sound.line = *linePreview_;
    else if (const auto& shown = lanePreview_ ? lanePreview_ : laneKept_)
        sound.line = model::lineFromView(shown->points, origin(), clip_->sourceLength, sound.reversed);
    if (const auto it = previewGains_.find(clip_->id); it != previewGains_.end())
        sound.gainDb = it->second;
    return sound;
}

const model::AutomationPoints& AudioClipView::shownLine() const
{
    if (lanePreview_)
        return lanePreview_->points;
    if (laneKept_)
        return laneKept_->points;
    if (linePreview_)
    {
        lineShown_ = model::lineToView(*linePreview_, origin(), clip_->sourceLength, clip_->sound.reversed);
        return lineShown_;
    }
    return lineView_;
}

LaneGeometry AudioClipView::laneGeometry() const
{
    const auto lane = laneArea();
    return {.area = lane.toFloat(), .laneX0 = static_cast<float>(lane.getX()), .timeline = &timeline_};
}

LaneGesture::Lane AudioClipView::gestureLane() const
{
    // The last edit kept, until the snapshot has it: a gesture right after another builds on it.
    return {.points = laneKept_ ? &laneKept_->points : &lineView_,
            .rules = {},
            .geometry = laneGeometry(),
            .selection = points_,
            .snap = [this](double seconds, bool free) { return snapped(seconds, free); },
            .drawName = "Draw gain line"};
}

void AudioClipView::keepLine(const model::PointEdit& edit, const std::string& undoName)
{
    if (!clip_)
        return;
    lanePreview_.reset();
    laneKept_ = edit; // until the snapshot has it
    points_ = edit.selection;
    session_.setGainLine(
        clip_->id, model::lineFromView(edit.points, origin(), clip_->sourceLength, clip_->sound.reversed),
        undoName);
    repaint();
}

double AudioClipView::snapped(double seconds, bool free) const
{
    return free || !timeline_.grid().snap ? std::max(0.0, seconds) : timeline_.snap(seconds, tempo_);
}

// ---- the audio's tools (§11) ---------------------------------------------------------------

std::vector<std::uint64_t> AudioClipView::toolClips() const
{
    std::vector<std::uint64_t> out = selectedClips ? selectedClips() : std::vector<std::uint64_t>{};
    if (out.empty() && clip_)
        out.push_back(clip_->id);
    return out;
}

model::ToolValues AudioClipView::toolValues(const model::ToolSpec& tool) const
{
    return rememberedValues ? rememberedValues(tool) : model::toolValues(tool, {});
}

void AudioClipView::setToolValues(const model::ToolSpec& tool, const model::ToolValues& values)
{
    if (rememberValues)
        rememberValues(tool, values);
}

void AudioClipView::preview(const model::ToolSpec& tool, const model::ToolValues& values)
{
    previewGains_.clear();
    if (tool.id == "normalise")
    {
        const auto choice = model::normaliseChoice(values);
        for (const auto& [id, gain] :
             session_.normaliseGains(toolClips(), {.to = choice.loudness ? model::NormaliseTo::Loudness
                                                                         : model::NormaliseTo::Peak,
                                                   .targetDb = choice.targetDb,
                                                   .together = choice.together}))
            if (gain)
                previewGains_[id] = *gain;
    }
    repaint();
}

void AudioClipView::cancel()
{
    if (previewGains_.empty())
        return;
    previewGains_.clear();
    repaint();
}

void AudioClipView::apply(const model::ToolSpec& tool, const model::ToolValues& values)
{
    previewGains_.clear();
    if (tool.id == "normalise")
    {
        const auto choice = model::normaliseChoice(values);
        session_.normalise(toolClips(),
                           {.to = choice.loudness ? model::NormaliseTo::Loudness : model::NormaliseTo::Peak,
                            .targetDb = choice.targetDb,
                            .together = choice.together});
    }
    repaint();
}

juce::String AudioClipView::scope(const model::ToolSpec&) const
{
    const auto n = static_cast<int>(toolClips().size());
    return juce::String(n) + (n == 1 ? " clip" : " clips");
}

juce::String AudioClipView::unavailable(const model::ToolSpec&) const
{
    return clip_ ? juce::String() : juce::String("Show an audio clip in the clip view");
}

std::uint64_t AudioClipView::version() const
{
    // What the tools act on: the clips, and each one's gain and line as the snapshot has them.
    std::uint64_t v = 1469598103934665603ull;
    const auto mix = [&v](std::uint64_t x) { v = (v ^ x) * 1099511628211ull; };
    for (auto id : toolClips())
        mix(id);
    if (clip_)
    {
        mix(static_cast<std::uint64_t>(std::llround(clip_->sound.gainDb * 1000.0f)));
        mix(clip_->sound.line.size());
    }
    return v;
}

// ---- painting ------------------------------------------------------------------------------

const model::Peaks* AudioClipView::rowPeaks(int row)
{
    const auto channels = waveforms_.channels(clip_->sourcePath);
    if (channels.empty())
        return nullptr;
    if (channels.size() == 1)
        return channels.front();
    using M = model::ChannelMode;
    switch (clip_->sound.channels)
    {
    case M::Stereo:
        return channels[static_cast<size_t>(std::min(row, 1))];
    case M::Swapped:
        return channels[static_cast<size_t>(row == 0 ? 1 : 0)];
    case M::Left:
        return channels[0];
    case M::Right:
        return channels[1];
    case M::Mono:
    {
        auto& mean = mean_[clip_->sourcePath];
        if (mean == nullptr)
            mean = model::meanPeaks(*channels[0], *channels[1]);
        return mean.get();
    }
    }
    return channels.front();
}

void AudioClipView::paintRows(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto colour = t.clipColour(clip_->colour);
    const auto [from, to] = playedRange();
    const auto sound = shownSound();
    const double o = origin(), length = clip_->sourceLength;
    const auto fade = fadeClipOf(*clip_);
    // The waveform as heard (§3.4): through the clip's gain and line; where it plays, its fades too.
    const auto soundAt = [&](double viewTime)
    { return zoom_ * model::soundGainAt(sound, model::fileTimeOf(viewTime - o, length, sound.reversed)); };
    const auto playedAt = [&](double viewTime)
    {
        float gain = soundAt(viewTime);
        if (viewTime >= clip_->start && viewTime <= clip_->end())
            gain *= model::playbackGain(fade, clip_->playback, viewTime);
        return gain;
    };
    const model::ClipInfo file{.kind = model::ClipInfo::Kind::Audio, .start = o, .length = length};
    const auto xOf = [&](double seconds)
    { return static_cast<float>(waveformArea().getX() + timeline_.xForTime(seconds)); };
    for (int r = 0; r < rowCount(); ++r)
    {
        const auto row = rowArea(r);
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(row);
        g.setColour(t.roles.shade);
        g.fillRect(row);
        const auto played = juce::Rectangle<float>(xOf(from), static_cast<float>(row.getY()),
                                                   xOf(to) - xOf(from), static_cast<float>(row.getHeight()));
        g.setColour(t.clipBody(colour, false));
        g.fillRect(played);
        time::paintGrid(g, lines_, timeline_, row);
        if (const auto* peaks = rowPeaks(r))
        {
            // The file from the content's zero (nothing past its ends): in full where it plays,
            // faded either side. Each part reads only its own columns.
            const auto fileArea = row.getIntersection(
                juce::Rectangle<float>(xOf(o), static_cast<float>(row.getY()), xOf(o + length) - xOf(o),
                                       static_cast<float>(row.getHeight()))
                    .getSmallestIntegerContainer());
            const auto playedArea = fileArea.getIntersection(played.getSmallestIntegerContainer());
            const auto faded = t.faded(t.clipContent(colour));
            for (const auto& side : {fileArea.withRight(std::max(fileArea.getX(), playedArea.getX())),
                                     fileArea.withLeft(std::min(fileArea.getRight(), playedArea.getRight()))})
                if (!side.isEmpty())
                {
                    juce::Graphics::ScopedSaveState part(g);
                    g.reduceClipRegion(side);
                    WaveformTiles::drawDirect(g, *peaks, file, row.toFloat(), side, timeline_, row.getX(),
                                              faded, soundAt);
                }
            if (!playedArea.isEmpty())
            {
                juce::Graphics::ScopedSaveState part(g);
                g.reduceClipRegion(played.getSmallestIntegerContainer());
                WaveformTiles::drawDirect(g, *peaks, file, row.toFloat(), playedArea, timeline_, row.getX(),
                                          t.clipContent(colour), playedAt);
            }
        }
    }
    // A line between rows, and above the lane.
    g.setColour(t.line);
    for (int r = 1; r < rowCount(); ++r)
        g.fillRect(rowArea(r).withY(rowArea(r).getY() - 1).withHeight(1));
    g.fillRect(waveformArea().withY(waveformArea().getBottom()).withHeight(1));
}

void AudioClipView::paintRange(juce::Graphics& g) const
{
    if (!range_)
        return;
    const auto& t = ui::Theme::current();
    const auto area = waveformArea();
    const float x0 = static_cast<float>(area.getX() + timeline_.xForTime(range_->first));
    const float x1 = static_cast<float>(area.getX() + timeline_.xForTime(range_->second));
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area);
    const auto box = juce::Rectangle<float>(x0, static_cast<float>(area.getY()), x1 - x0,
                                            static_cast<float>(area.getHeight()));
    g.setColour(t.roles.selectionFill);
    g.fillRect(box);
    g.setColour(t.roles.selectionStroke);
    g.fillRect(box.withWidth(1.0f));
    g.fillRect(box.withX(x1 - 1.0f).withWidth(1.0f));
    // Its gain handle (§5), the fade squares' look.
    const auto handle = rangeHandle();
    g.setColour(t.textPrimary);
    g.fillRect(handle);
    g.setColour(t.surface0);
    g.drawRect(handle, 1.0f);
    if (gesture_ == Gesture::RangeGain && moved_)
        ui::paintValueTag(g, handle.getCentre(), getLocalBounds(), "Range " + dbText(rangeGainDb_));
}

void AudioClipView::paintLane(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto lane = laneArea();
    g.setColour(t.roles.subLane);
    g.fillRect(lane);
    time::paintGrid(g, lines_, timeline_, lane, false);
    // The line exists where the file does; the rest is shaded.
    const double o = origin();
    const float x0 = static_cast<float>(lane.getX() + timeline_.xForTime(o));
    const float x1 = static_cast<float>(lane.getX() + timeline_.xForTime(o + clip_->sourceLength));
    const auto file = lane.toFloat()
                          .withLeft(std::max(x0, static_cast<float>(lane.getX())))
                          .withRight(std::min(x1, static_cast<float>(lane.getRight())));
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.excludeClipRegion(file.getSmallestIntegerContainer());
        g.setColour(t.roles.shade);
        g.fillRect(lane);
    }
    if (file.getWidth() <= 0.0f)
        return;
    const auto& line = shownLine();
    LanePaint paint{.points = &line,
                    .selected = points_,
                    .hovered = hoveredPoint_,
                    .on = true,
                    .colour = t.clipColour(clip_->colour),
                    .current = line.empty() ? std::optional(model::lineValueForDb(0.0)) : std::nullopt,
                    .assigned = true,
                    .hint = true};
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(file.getSmallestIntegerContainer());
    paintAutomationLane(g, laneGeometry(), paint, file.getSmallestIntegerContainer());
}

void AudioClipView::paintLevels(juce::Graphics& g) const
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    const auto column = levelArea();
    g.setColour(t.surface1);
    g.fillRect(column);
    g.setColour(t.line);
    g.fillRect(column.withX(column.getRight() - 1).withWidth(1));
    // Each row's level scale (§4): the level at its top edge (0 dB at full scale), −∞ in its middle,
    // and the steps between where they fit; each with its tick, mirrored below the middle.
    const auto labelFont = ui::font(ui::TextStyle::ValueSmall);
    const float labelHeight = labelFont.getHeight();
    const float spacing = labelHeight + static_cast<float>(s.itemGap); // from one label's middle to the next
    const float tickX = static_cast<float>(column.getRight() - s.itemGap - 1);
    const int textRight = column.getRight() - s.itemGap - s.itemGap;
    for (int r = 0; r < rowCount(); ++r)
    {
        const auto row = rowArea(r).withX(column.getX()).withWidth(column.getWidth());
        const float top = static_cast<float>(row.getY()), mid = static_cast<float>(row.getCentreY());
        const float half = static_cast<float>(row.getHeight()) * 0.5f;
        const auto mark = [&](float y, float labelY, const juce::String& text)
        {
            g.setColour(t.roles.gridBar);
            g.fillRect(juce::Rectangle<float>(tickX, y, static_cast<float>(s.itemGap), 1.0f));
            g.fillRect(juce::Rectangle<float>(
                tickX, std::min(2.0f * mid - y, static_cast<float>(row.getBottom()) - 1.0f),
                static_cast<float>(s.itemGap), 1.0f));
            g.setColour(t.textTertiary);
            g.setFont(labelFont);
            g.drawText(text,
                       juce::Rectangle<float>(static_cast<float>(column.getX()), labelY - labelHeight * 0.5f,
                                              static_cast<float>(textRight - column.getX()), labelHeight),
                       juce::Justification::centredRight, false);
        };
        if (half >= spacing) // else too short for a scale
        {
            float last = top - spacing; // the middle of the label above the next one
            for (const double db : {0.0, -3.0, -6.0, -12.0, -18.0, -24.0, -36.0, -48.0})
            {
                const float y = mid - half * zoom_ * static_cast<float>(std::pow(10.0, db / 20.0));
                if (y < top - 0.5f)
                    continue;                                               // above the row: zoomed in
                const float labelY = std::max(y, top + labelHeight * 0.5f); // inside the row at its top
                if (labelY - last < spacing || mid - labelY < spacing)
                    continue;
                last = labelY;
                mark(y, labelY,
                     db == 0.0
                         ? juce::String("0")
                         : juce::String::fromUTF8("\xE2\x88\x92") + juce::String(static_cast<int>(-db)));
            }
            mark(mid, mid, juce::String::fromUTF8("\xE2\x88\x92\xE2\x88\x9E"));
        }
        // The rows' sides, while there are two (as the clip plays them: §6.3).
        if (rowCount() == 2)
        {
            g.setColour(t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Micro));
            g.drawText(r == 0 ? "L" : "R",
                       row.withTrimmedLeft(s.itemGap + s.itemGap)
                           .withHeight(static_cast<int>(labelHeight) + s.itemGap),
                       juce::Justification::centredLeft, false);
        }
    }
}

void AudioClipView::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (!clip_)
        return; // the detail panel says what to select
    timeline_.gridLines(tempo_, lines_);
    paintRows(g);
    paintRange(g);
    paintLane(g);
    // The loop strip with the clip's brace, then the ruler with what it plays in its colour.
    const auto [from, to] = playedRange();
    time::paintLoopStrip(g, timeline_, loopStripArea(), brace_.preview().value_or(std::pair{from, to}),
                         clip_->loop.on());
    time::paintRuler(g, lines_, timeline_, rulerArea(),
                     {.range = std::pair{from, to}, .rangeColour = t.clipColour(clip_->colour)});
    paintLevels(g);
    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(waveformArea().getUnion(laneArea()));
    time::paintPlayhead(g, timeline_, waveformArea().getUnion(laneArea()), playhead_);
}

// ---- input ---------------------------------------------------------------------------------

void AudioClipView::mouseMove(const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;
    std::optional<size_t> hovered;
    if (clip_)
    {
        const float x = e.position.x - static_cast<float>(waveformArea().getX());
        if (loopStripArea().contains(e.getPosition()))
        {
            const auto zone = ClipLoopBrace::zoneAt(*clip_, origin(), timeline_, x);
            if (zone == model::BraceZone::Start || zone == model::BraceZone::End)
                cursor = juce::MouseCursor::LeftRightResizeCursor;
        }
        else if (rangeHandle().expanded(ui::Theme::current().input.edgeGrab * 0.5f).contains(e.position))
            cursor = juce::MouseCursor::UpDownResizeCursor;
        else if (laneArea().contains(e.getPosition()))
        {
            const auto hover = LaneGesture::hover(gestureLane(), e.position);
            hovered = hover.point;
            if (hover.point)
                cursor = juce::MouseCursor::DraggingHandCursor;
            else if (hover.segment)
                cursor = juce::MouseCursor::UpDownResizeCursor;
        }
        else if (levelArea().contains(e.getPosition()) && !laneHead_.getBounds().contains(e.getPosition()))
            cursor = juce::MouseCursor::UpDownResizeCursor;
    }
    if (hovered != hoveredPoint_)
    {
        hoveredPoint_ = hovered;
        repaint(laneArea());
    }
    setMouseCursor(cursor);
}

void AudioClipView::mouseDown(const juce::MouseEvent& e)
{
    gesture_ = Gesture::None;
    if (!clip_)
        return;
    if (onUsed)
        onUsed();
    dragStart_ = lastDrag_ = e.position;
    moved_ = false;
    const float x = e.position.x - static_cast<float>(waveformArea().getX());
    if (e.mods.isPopupMenu())
    {
        // The Audio menu where you work (ADR 0040).
        if (menu && (waveformArea().contains(e.getPosition()) || laneArea().contains(e.getPosition())))
            menu().showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1)));
        return;
    }
    if (loopStripArea().contains(e.getPosition()))
    {
        // The clip's loop brace, as the piano roll's and the transport's (clip-loops.md §6).
        gesture_ = Gesture::Brace;
        brace_.press(*clip_, origin(), timeline_, x);
        return;
    }
    if (rulerArea().contains(e.getPosition()))
    {
        gesture_ = Gesture::Ruler;
        rulerGesture_.begin(x);
        return;
    }
    if (rangeHandle().expanded(ui::Theme::current().input.edgeGrab * 0.5f).contains(e.position))
    {
        gesture_ = Gesture::RangeGain;
        rangeGainDb_ = 0.0;
        return;
    }
    if (laneArea().contains(e.getPosition()))
    {
        // The gain line (§6.2): the automation lanes' gestures.
        gesture_ = Gesture::Line;
        const auto out = laneGesture_.press(e, gestureLane());
        if (e.getNumberOfClicks() >= 2)
        {
            gesture_ = Gesture::None;
            if (out.keep)
                keepLine(*out.keep, out.undoName);
            return;
        }
        if (out.select)
            points_ = *out.select;
        if (out.preview)
            lanePreview_ = *out.preview;
        repaint(laneArea());
        return;
    }
    if (levelArea().contains(e.getPosition()))
    {
        // Dragged, the rows' height; double-clicked, back to full scale filling a row.
        if (e.getNumberOfClicks() >= 2)
            setLevelZoom(1.0f);
        else
        {
            gesture_ = Gesture::Level;
            zoomBase_ = zoom_;
        }
        return;
    }
    if (waveformArea().contains(e.getPosition()))
    {
        gesture_ = Gesture::Range;
        rangeAnchor_ = snapped(timeline_.timeForX(x), e.mods.isAltDown());
    }
}

void AudioClipView::mouseDrag(const juce::MouseEvent& e)
{
    if (!clip_)
        return;
    const auto previous = lastDrag_;
    lastDrag_ = e.position;
    const float x = e.position.x - static_cast<float>(waveformArea().getX());
    const bool past = e.getDistanceFromDragStart() >= ui::Theme::current().input.dragThreshold;
    switch (gesture_)
    {
    case Gesture::Ruler:
        rulerGesture_.drag(timeline_, e.position - previous, x);
        repaint();
        return;
    case Gesture::Brace:
    {
        if (!moved_ && !past)
            return;
        moved_ = true;
        const bool free = !timeline_.grid().snap || e.mods.isAltDown();
        brace_.drag(*clip_, timeline_, dragStart_.x - static_cast<float>(waveformArea().getX()), x,
                    [this, free](double t) { return free ? std::max(0.0, t) : timeline_.snap(t, tempo_); });
        repaint(loopStripArea());
        return;
    }
    case Gesture::Range:
    {
        if (!moved_ && !past)
            return;
        moved_ = true;
        const double t = snapped(timeline_.timeForX(x), e.mods.isAltDown());
        const double a = std::max(std::min(rangeAnchor_, t), clip_->start);
        const double b = std::min(std::max(rangeAnchor_, t), clip_->end());
        setRange(b > a ? std::optional(std::pair{a, b}) : std::nullopt);
        return;
    }
    case Gesture::RangeGain:
    {
        moved_ = moved_ || past;
        if (!moved_ || !range_)
            return;
        const double perPixel = e.mods.isShiftDown() ? kGainPerPixel * 0.1 : kGainPerPixel;
        rangeGainDb_ = std::round((dragStart_.y - e.position.y) * perPixel * 10.0) / 10.0;
        auto line = clip_->sound.line;
        for (const auto& span :
             model::fileSpans(*clip_, clip_->sound.reversed, range_->first, range_->second))
            line = model::lineWithGain(line, span, rangeGainDb_);
        linePreview_ = std::move(line);
        repaint();
        return;
    }
    case Gesture::Line:
    {
        if (!moved_ && !past)
            return;
        moved_ = true;
        const auto out = laneGesture_.drag(e, dragStart_, gestureLane());
        if (out.preview)
            lanePreview_ = *out.preview;
        if (out.select)
            points_ = *out.select;
        repaint();
        return;
    }
    case Gesture::Level:
        setLevelZoom(zoomBase_ * std::exp2((dragStart_.y - e.position.y) / kZoomPixels));
        return;
    case Gesture::None:
        return;
    }
}

void AudioClipView::mouseUp(const juce::MouseEvent& e)
{
    if (!clip_)
    {
        gesture_ = Gesture::None;
        return;
    }
    switch (gesture_)
    {
    case Gesture::Brace:
        brace_.release(session_, *clip_, origin(), moved_);
        break;
    case Gesture::Ruler:
        if (e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
            // A click in the ruler moves the playhead (snapped; Alt for free).
            session_.setPosition(time::rulerTime(timeline_, tempo_,
                                                 e.position.x - static_cast<float>(waveformArea().getX()),
                                                 !timeline_.grid().snap || e.mods.isAltDown()));
        break;
    case Gesture::Range:
        if (!moved_)
            setRange(std::nullopt); // a click lets the range go
        break;
    case Gesture::RangeGain:
        linePreview_.reset();
        if (moved_ && rangeGainDb_ != 0.0)
            editRange(engine::Session::RangeAction::Gain, rangeGainDb_);
        break;
    case Gesture::Line:
    {
        const auto out = laneGesture_.release(moved_, gestureLane());
        if (out.keep)
            keepLine(*out.keep, out.undoName);
        else
        {
            lanePreview_.reset();
            if (out.select)
                points_ = *out.select;
        }
        break;
    }
    case Gesture::Level:
    case Gesture::None:
        break;
    }
    gesture_ = Gesture::None;
    repaint();
}

void AudioClipView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!clip_)
        return;
    time::applyWheel(timeline_, e.position.x - static_cast<float>(waveformArea().getX()), e.mods, wheel, [](double) {});
    repaint();
}

void AudioClipView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    time::applyMagnify(timeline_, e.position.x - static_cast<float>(waveformArea().getX()), scaleFactor);
    repaint();
}
} // namespace fd::app
