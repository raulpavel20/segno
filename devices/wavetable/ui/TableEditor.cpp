#include "TableEditor.h"

#include "FactoryTables.h"
#include "TableFiles.h"
#include "WavetableUi.h"

namespace fd::devices::wavetable_ui
{
using namespace look;

namespace
{
using Recipe = wavetable::Recipe;
using K = Recipe::SourceKind;
using C = Recipe::ChangeKind;
constexpr int N = Recipe::frameSize;
constexpr int kPreviewNote = 48;    // what Preview holds: C3
constexpr double kMostAudio = 60.0; // seconds of audio the way chooser reads, at most
constexpr float kPreviewVelocity = 0.8f;

const W* synthOf(const ui::DeviceUiContext& ctx)
{
    return dynamic_cast<const W*>(ctx.device);
}

juce::String kindName(K kind)
{
    switch (kind)
    {
    case K::Draw:
        return "Draw";
    case K::Harmonics:
        return "Harmonics";
    case K::Formula:
        return "Formula";
    case K::Audio:
        return "Audio";
    case K::Image:
        return "Image";
    case K::Table:
        return "Table";
    }
    return {};
}

juce::String factoryName(const std::string& id)
{
    for (const auto& t : wavetable::factoryTables())
        if (t.id == id)
            return juce::String(std::string(t.name));
    return {};
}

/// A layer's name in the list: its kind, and a table's name.
juce::String layerName(const Recipe::Layer& l)
{
    if (l.kind == K::Table && !l.tableId.empty())
        return factoryName(l.tableId);
    return kindName(l.kind);
}

bool keyframed(const Recipe::Layer* l)
{
    return l != nullptr && (l->kind == K::Draw || l->kind == K::Harmonics);
}

/// The keyframe of `l` at `frame`, or -1.
int keyframeIndex(const Recipe::Layer& l, int frame, int count)
{
    for (size_t i = 0; i < l.keyframes.size(); ++i)
        if (std::clamp(l.keyframes[i].frame, 0, count - 1) == frame)
            return static_cast<int>(i);
    return -1;
}

void sortKeyframes(Recipe::Layer& l)
{
    std::stable_sort(l.keyframes.begin(), l.keyframes.end(),
                     [](const auto& a, const auto& b) { return a.frame < b.frame; });
}

/// The fill a layer of `kind` uses: none for a formula (it makes every frame itself).
bool fills(K kind)
{
    return kind != K::Formula;
}

void warn(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}

/// A bar for a value the editor keeps: shown while it's dragged, kept when it's let go (or at
/// once, from the keys).
void wireBar(ui::Knob& bar, std::function<void(float value, bool keep)> apply)
{
    auto held = std::make_shared<bool>(false);
    bar.onGestureStart = [held] { *held = true; };
    bar.onValueChange = [held, apply](float v) { apply(v, !*held); };
    bar.onGestureEnd = [held, apply, &bar]
    {
        *held = false;
        apply(bar.getValue(), true);
    };
}

juce::String percent(float v)
{
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
}

std::optional<float> percentFromText(const juce::String& text)
{
    const auto t = text.retainCharacters("0123456789.-");
    if (t.isEmpty())
        return std::nullopt;
    return std::clamp(t.getFloatValue() / 100.0f, 0.0f, 1.0f);
}

/// The frame count's bar: 1 to 256, doubling over equal distances.
float framesToBar(int frames)
{
    return static_cast<float>(std::log2(std::clamp(frames, 1, Recipe::maxFrames)) / 8.0);
}

int framesFromBar(float v)
{
    return std::clamp(static_cast<int>(std::lround(std::exp2(std::clamp(v, 0.0f, 1.0f) * 8.0f))), 1,
                      Recipe::maxFrames);
}

/// A drawn point's place in the 2D view: x across, y from -1 (bottom) to 1 (top).
juce::Point<float> drawnPoint(const std::array<float, 3>& p, juce::Rectangle<float> r)
{
    return {r.getX() + p[0] * r.getWidth(), r.getCentreY() - p[1] * r.getHeight() * 0.5f};
}

int drawnPointNear(const std::vector<std::array<float, 3>>& points, juce::Rectangle<float> r,
                   juce::Point<float> at)
{
    const float reach = static_cast<float>(ui::Theme::current().deviceGrid.displayHandle);
    int best = -1;
    float nearest = reach;
    for (size_t i = 0; i < points.size(); ++i)
        if (const float d = drawnPoint(points[i], r).getDistanceFrom(at); d <= nearest)
        {
            nearest = d;
            best = static_cast<int>(i);
        }
    return best;
}

float snap(float x, int grid)
{
    return grid > 0 ? std::round(x * static_cast<float>(grid)) / static_cast<float>(grid) : x;
}
} // namespace

// ---- LayerView -----------------------------------------------------------------------------------

LayerView::LayerView(TableEditor& editor) : editor_(editor)
{
    setOpaque(false);
}

void LayerView::update()
{
    const auto* l = editor_.layer();
    const auto& r = editor_.recipe();
    frame_ = l != nullptr ? wavetable::layerFrame(*l, editor_.shownFrame(), r.frames)
                          : std::vector<float>(static_cast<size_t>(N), 0.0f);
    harmonics_ = wavetable::frameHarmonics(frame_, Recipe::numHarmonicBars);
    repaint();
}

bool LayerView::editsWave() const
{
    const auto* l = editor_.layer();
    return l != nullptr && l->kind == K::Draw;
}

bool LayerView::editsHarmonics() const
{
    const auto* l = editor_.layer();
    return l != nullptr && l->kind == K::Harmonics;
}

juce::Rectangle<float> LayerView::waveWell() const
{
    // Two wells side by side, the module grid's column gap between them.
    const auto r = getLocalBounds().toFloat();
    const float gap = static_cast<float>(ui::Theme::current().deviceGrid.columnGap);
    return r.withWidth((r.getWidth() - gap) * 0.5f);
}

juce::Rectangle<float> LayerView::harmonicsWell() const
{
    const auto wave = waveWell();
    return getLocalBounds().toFloat().withLeft(getLocalBounds().toFloat().getRight() - wave.getWidth());
}

juce::Rectangle<float> LayerView::wavePlot() const
{
    // Room above for the well's caption, and all round for a point's handle at the edge.
    const auto& grid = ui::Theme::current().deviceGrid;
    const float handle = static_cast<float>(grid.displayHandle) * 0.5f;
    return waveWell()
        .reduced(inset())
        .withTrimmedTop(static_cast<float>(grid.displayTextHeight))
        .reduced(handle);
}

juce::Rectangle<float> LayerView::barsPlot() const
{
    const auto& grid = ui::Theme::current().deviceGrid;
    auto r = harmonicsWell().reduced(inset()).withTrimmedTop(static_cast<float>(grid.displayTextHeight));
    return r.withTrimmedBottom(r.getHeight() * 0.25f + inset());
}

juce::Rectangle<float> LayerView::phasesPlot() const
{
    auto r = harmonicsWell().reduced(inset());
    return r.withTop(barsPlot().getBottom() + inset());
}

void LayerView::paint(juce::Graphics& g)
{
    paintWell(g, waveWell());
    paintWell(g, harmonicsWell());
    if (editor_.layer() == nullptr)
        return;
    paintWave(g);
    paintHarmonics(g);
}

namespace
{
/// A well's caption in its top left corner: what it shows, and whether it's edited there.
void paintCaption(juce::Graphics& g, juce::Rectangle<float> well, const juce::String& text)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.textTertiary);
    g.setFont(ui::font(ui::TextStyle::Caption));
    g.drawText(text, well.reduced(inset()).withHeight(static_cast<float>(t.deviceGrid.displayTextHeight)),
               juce::Justification::topLeft, true);
}
} // namespace

void LayerView::paintWave(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto area = wavePlot();
    paintCaption(g, waveWell(), editsWave() ? "Wave: draw here" : "Wave");
    g.setColour(t.roles.gridMinor);
    g.fillRect(area.getX(), area.getCentreY(), area.getWidth(), 1.0f);
    // The drawing's points, on its keyframe (or the one a drag is adding).
    const auto* l = editor_.layer();
    const Recipe::Keyframe* key = nullptr;
    if (editsWave())
    {
        const auto& layer = dragging_ ? editing_.layers[static_cast<size_t>(editor_.selectedLayer())] : *l;
        if (const int k = keyframeIndex(layer, editor_.shownFrame(), editor_.recipe().frames); k >= 0)
            key = &layer.keyframes[static_cast<size_t>(k)];
        if (editor_.grid() > 0)
            for (int i = 1; i < editor_.grid(); ++i)
                g.fillRect(area.getX() +
                               area.getWidth() * static_cast<float>(i) / static_cast<float>(editor_.grid()),
                           area.getY(), 1.0f, area.getHeight());
    }
    // The layer's frame, from the function the bake uses.
    const auto line = cyclePath(frame_.data(), N, area, std::max(16, static_cast<int>(area.getWidth())));
    auto fill = line;
    fill.lineTo(area.getRight(), area.getCentreY());
    fill.lineTo(area.getX(), area.getCentreY());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    if (key != nullptr && editor_.drawTool() == TableEditor::DrawTool::Points)
        for (size_t i = 0; i < key->points.size(); ++i)
            paintHandle(g, drawnPoint(key->points[i], area), static_cast<int>(i) == dragged_);
}

void LayerView::paintHarmonics(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    paintCaption(g, harmonicsWell(), editsHarmonics() ? "Harmonics: set here" : "Harmonics");
    const auto bars = barsPlot(), phases = phasesPlot();
    const int count = Recipe::numHarmonicBars;
    const float pitch = bars.getWidth() / static_cast<float>(count);
    const float gap = std::min(1.0f, pitch * 0.25f);
    // A keyframe's own levels and phases while it's edited; else the frame's, read from it.
    const auto* l = editor_.layer();
    std::vector<float> levels = harmonics_.levels, phase = harmonics_.phases;
    if (editsHarmonics())
    {
        const auto& layer = dragging_ ? editing_.layers[static_cast<size_t>(editor_.selectedLayer())] : *l;
        if (const int k = keyframeIndex(layer, editor_.shownFrame(), editor_.recipe().frames); k >= 0)
        {
            levels = layer.keyframes[static_cast<size_t>(k)].levels;
            phase = layer.keyframes[static_cast<size_t>(k)].phases;
        }
    }
    g.setColour(t.roles.gridMinor);
    g.fillRect(phases.getX(), phases.getCentreY(), phases.getWidth(), 1.0f);
    g.setColour(t.textTertiary);
    g.setFont(ui::font(ui::TextStyle::Caption));
    g.drawText("Phases", phases.withHeight(static_cast<float>(t.deviceGrid.displayTextHeight)),
               juce::Justification::topRight, false);
    for (int h = 0; h < count && h < static_cast<int>(levels.size()); ++h)
    {
        const float x = bars.getX() + pitch * static_cast<float>(h);
        const float level = std::clamp(levels[static_cast<size_t>(h)], 0.0f, 1.0f);
        g.setColour(t.value);
        g.fillRect(juce::Rectangle<float>(x, bars.getBottom() - level * bars.getHeight(), pitch - gap,
                                          level * bars.getHeight()));
        if (h < static_cast<int>(phase.size()) && level >= 0.01f) // a phase only where there's a harmonic
        {
            // A phase from the middle: up for the first half turn, down for the second.
            const float p = phase[static_cast<size_t>(h)];
            const float y = phases.getCentreY() - (p <= 0.5f ? p : p - 1.0f) * phases.getHeight();
            g.setColour(t.textSecondary);
            g.fillRect(juce::Rectangle<float>(x, std::min(y, phases.getCentreY()), pitch - gap,
                                              std::max(1.0f, std::abs(y - phases.getCentreY()))));
        }
    }
}

void LayerView::mouseMove(const juce::MouseEvent& e)
{
    // Says where the layer is edited, and that the other side is there to look at.
    const auto* l = editor_.layer();
    const bool onWave = waveWell().contains(e.position);
    juce::String tip;
    if (l == nullptr)
        tip = {};
    else if (onWave && editsWave())
        tip = editor_.drawTool() == TableEditor::DrawTool::Freehand
                  ? "Draw the layer's wave freehand, a point at every step of the grid"
                  : "Drag a point to move it, a line up or down to bend it; double-click to add or remove a "
                    "point. "
                    "Between keyframes, editing adds one here";
    else if (!onWave && editsHarmonics())
        tip = "Drag across the bars to set the harmonics' levels, across the strip under them for their "
              "phases. "
              "Between keyframes, editing adds one here";
    else if (editsWave() || editsHarmonics())
        tip = onWave ? "The layer's wave at this frame: its harmonics, on the right, are where it's set"
                     : "The layer's harmonics at this frame: it's drawn on its wave, on the left";
    else
        tip = "The layer at this frame, before the changes: set it on the left";
    if (tip != getTooltip())
        setTooltip(tip);
}

Recipe::Keyframe& LayerView::keyframeToEdit()
{
    auto& layer = editing_.layers[static_cast<size_t>(editor_.selectedLayer())];
    int k = keyframeIndex(layer, editor_.shownFrame(), editing_.frames);
    if (k < 0)
    {
        layer.keyframes.push_back(wavetable::newKeyframe(layer, editor_.shownFrame(), editing_.frames));
        std::stable_sort(layer.keyframes.begin(), layer.keyframes.end(),
                         [](const auto& a, const auto& b) { return a.frame < b.frame; });
        k = keyframeIndex(layer, editor_.shownFrame(), editing_.frames);
        added_ = true;
    }
    return layer.keyframes[static_cast<size_t>(k)];
}

void LayerView::mouseDown(const juce::MouseEvent& e)
{
    dragging_ = added_ = false;
    dragged_ = bending_ = -1;
    last_ = e.position;
    const bool onWave = waveWell().contains(e.position);
    if (e.mods.isPopupMenu() || !((onWave && editsWave()) || (!onWave && editsHarmonics())))
        return;
    editing_ = editor_.recipe();
    dragging_ = true;
    auto& k = keyframeToEdit();
    if (editsHarmonics())
    {
        phasesDrag_ = phasesPlot().contains(e.position);
        setBar(e.position, phasesDrag_);
        return;
    }
    if (editor_.drawTool() == TableEditor::DrawTool::Freehand)
    {
        drawFreehand(e.position, e.position);
        return;
    }
    const auto area = wavePlot();
    dragged_ = drawnPointNear(k.points, area, e.position);
    if (dragged_ < 0)
    {
        // Between two points: bend that segment.
        const float x = (e.position.x - area.getX()) / area.getWidth();
        for (size_t i = 0; i + 1 < k.points.size(); ++i)
            if (x >= k.points[i][0] && x <= k.points[i + 1][0])
            {
                bending_ = static_cast<int>(i);
                bendStart_ = k.points[i][2];
                bendY_ = e.position.y;
                break;
            }
    }
    if (added_)
        editor_.preview(editing_); // the keyframe added shows at once
    repaint();
}

void LayerView::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    auto& k = keyframeToEdit();
    if (editsHarmonics())
        setBar(e.position, phasesDrag_);
    else if (editor_.drawTool() == TableEditor::DrawTool::Freehand)
        drawFreehand(last_, e.position);
    else if (dragged_ >= 0)
    {
        // Between its neighbours; the first and last stay at the ends, snapping to the grid.
        const auto r = wavePlot();
        auto& p = k.points;
        const auto i = static_cast<size_t>(dragged_);
        const float x = snap((e.position.x - r.getX()) / r.getWidth(), editor_.grid());
        const float lo = i == 0 ? 0.0f : p[i - 1][0], hi = i + 1 == p.size() ? 1.0f : p[i + 1][0];
        p[i][0] = i == 0 ? 0.0f : (i + 1 == p.size() ? 1.0f : std::clamp(x, lo, hi));
        p[i][1] = std::clamp((r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f), -1.0f, 1.0f);
    }
    else if (bending_ >= 0)
    {
        // Up bulges the segment up, whichever way it goes (as the LFO's editor).
        auto& p = k.points;
        const auto i = static_cast<size_t>(bending_);
        const float way = p[i + 1][1] >= p[i][1] ? 1.0f : -1.0f;
        p[i][2] = std::clamp(
            bendStart_ + way * (bendY_ - e.position.y) / ui::Theme::current().input.bendTravel, -1.0f, 1.0f);
    }
    last_ = e.position;
    editor_.preview(editing_);
}

void LayerView::mouseUp(const juce::MouseEvent& e)
{
    if (dragging_ && (e.mouseWasDraggedSinceMouseDown() || added_ || editsHarmonics() ||
                      editor_.drawTool() == TableEditor::DrawTool::Freehand))
        editor_.keep(editing_, editsHarmonics()
                                   ? (phasesDrag_ ? "Set the harmonics' phases" : "Set the harmonics")
                                   : "Draw a wave"); // one undo step, the keyframe added in it
    dragging_ = added_ = false;
    dragged_ = bending_ = -1;
    repaint();
}

void LayerView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!editsWave() || editor_.drawTool() != TableEditor::DrawTool::Points ||
        !waveWell().contains(e.position))
        return;
    editing_ = editor_.recipe();
    auto& points = keyframeToEdit().points;
    const auto area = wavePlot();
    if (const int i = drawnPointNear(points, area, e.position);
        i > 0 && static_cast<size_t>(i) + 1 < points.size())
    {
        points.erase(points.begin() + i);
        editor_.keep(std::move(editing_), "Remove a point");
        return;
    }
    // A point on the drawing at the pointer's x.
    const float x =
        std::clamp(snap((e.position.x - area.getX()) / area.getWidth(), editor_.grid()), 0.0f, 1.0f);
    const auto y = static_cast<float>(wavetable::drawnAt(points, x));
    const auto it = std::upper_bound(points.begin(), points.end(), x,
                                     [](float v, const std::array<float, 3>& p) { return v < p[0]; });
    points.insert(it, {x, y, 0.0f});
    editor_.keep(std::move(editing_), "Add a point");
}

void LayerView::drawFreehand(juce::Point<float> from, juce::Point<float> to)
{
    // Every grid step the stroke passes gets a point at the stroke's height there (64 steps
    // without a grid); the ends stay where they are.
    auto& points = keyframeToEdit().points;
    const auto r = wavePlot();
    const int steps = editor_.grid() > 0 ? editor_.grid() : 64;
    const auto xOf = [&](juce::Point<float> p)
    { return std::clamp((p.x - r.getX()) / r.getWidth(), 0.0f, 1.0f); };
    const auto yOf = [&](juce::Point<float> p)
    { return std::clamp((r.getCentreY() - p.y) / (r.getHeight() * 0.5f), -1.0f, 1.0f); };
    const float x0 = std::min(xOf(from), xOf(to)), x1 = std::max(xOf(from), xOf(to));
    const int s0 = static_cast<int>(std::round(x0 * static_cast<float>(steps)));
    const int s1 = static_cast<int>(std::round(x1 * static_cast<float>(steps)));
    std::vector<std::array<float, 3>> kept;
    for (const auto& p : points)
        if (p[0] < static_cast<float>(s0) / static_cast<float>(steps) - 1e-6f ||
            p[0] > static_cast<float>(s1) / static_cast<float>(steps) + 1e-6f)
            kept.push_back(p);
    for (int s = s0; s <= s1; ++s)
    {
        const float x = static_cast<float>(s) / static_cast<float>(steps);
        const float t =
            std::abs(xOf(to) - xOf(from)) > 1e-6f ? (x - xOf(from)) / (xOf(to) - xOf(from)) : 1.0f;
        kept.push_back({x, yOf(from) + std::clamp(t, 0.0f, 1.0f) * (yOf(to) - yOf(from)), 0.0f});
    }
    std::sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
    points = std::move(kept);
    editor_.preview(editing_);
}

void LayerView::setBar(juce::Point<float> at, bool phases)
{
    auto& k = keyframeToEdit();
    const auto bars = barsPlot();
    const int count = Recipe::numHarmonicBars;
    k.levels.resize(static_cast<size_t>(count), 0.0f);
    k.phases.resize(static_cast<size_t>(count), 0.0f);
    // Every bar between the last point and this one, so a quick drag leaves no gaps.
    const auto barOf = [&](float x)
    {
        return std::clamp(static_cast<int>((x - bars.getX()) / bars.getWidth() * static_cast<float>(count)),
                          0, count - 1);
    };
    const int b0 = barOf(last_.x), b1 = barOf(at.x);
    for (int b = std::min(b0, b1); b <= std::max(b0, b1); ++b)
    {
        const float t = b0 == b1 ? 1.0f : static_cast<float>(b - b0) / static_cast<float>(b1 - b0);
        const float y = last_.y + t * (at.y - last_.y);
        if (phases)
        {
            const auto area = phasesPlot();
            const float p = std::clamp((area.getCentreY() - y) / area.getHeight(), -0.5f, 0.5f);
            k.phases[static_cast<size_t>(b)] = p < 0.0f ? p + 1.0f : p;
        }
        else
            k.levels[static_cast<size_t>(b)] =
                std::clamp((bars.getBottom() - y) / bars.getHeight(), 0.0f, 1.0f);
    }
    last_ = at;
    editor_.preview(editing_);
}

// ---- ResultView ----------------------------------------------------------------------------------

ResultView::ResultView(TableEditor& editor) : editor_(editor)
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setTooltip("The result: the whole table as the module plays it, every layer summed and then the changes. "
               "Click a frame to show it, or drag up and down to go through them");
}

void ResultView::update()
{
    frames_.clear();
    repaint();
}

juce::Rectangle<float> ResultView::plot() const
{
    return getLocalBounds().toFloat().reduced(inset());
}

namespace
{
/// The 3D view's step from the front frame to the back one, in `area`.
juce::Point<float> depthOf(juce::Rectangle<float> area, int frames)
{
    return frames > 1 ? juce::Point<float>(area.getWidth() * 0.25f, -area.getHeight() * 0.4f)
                      : juce::Point<float>();
}
} // namespace

juce::Rectangle<float> ResultView::frameArea(float along) const
{
    const auto area = plot();
    const auto depth = depthOf(area, editor_.baked().frames());
    const auto frame = juce::Rectangle<float>(area.getX(), area.getY() - depth.y, area.getWidth() - depth.x,
                                              area.getHeight() + depth.y);
    return frame.translated(depth.x * along, depth.y * along).reduced(0.0f, 1.0f);
}

int ResultView::frameAt(float y) const
{
    const int frames = editor_.baked().frames();
    if (frames <= 1)
        return 0;
    const auto front = frameArea(0.0f), back = frameArea(1.0f);
    const float along = (y - front.getCentreY()) / (back.getCentreY() - front.getCentreY());
    return std::clamp(juce::roundToInt(along * static_cast<float>(frames - 1)), 0, frames - 1);
}

void ResultView::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    paintWell(g, getLocalBounds().toFloat());
    const auto& table = editor_.baked();
    if (table.frames() < 1)
        return;
    // Up to 48 frames, evenly through the table, each a step up and right of the one before (as
    // the panel's display draws it).
    const int count = std::min(table.frames(), 48);
    if (frames_.empty() || builtFor_ != &table)
    {
        frames_.clear();
        builtFor_ = &table;
        const int points = std::max(16, static_cast<int>(frameArea(0.0f).getWidth() / 2.0f));
        for (int k = count; k-- > 0;)
        {
            const int f =
                count > 1 ? juce::roundToInt(static_cast<float>(k) * static_cast<float>(table.frames() - 1) /
                                             static_cast<float>(count - 1))
                          : 0;
            const float along = count > 1 ? static_cast<float>(k) / static_cast<float>(count - 1) : 0.0f;
            frames_.push_back(cyclePath(table.samples(f), N, frameArea(along), points));
        }
    }
    for (size_t i = 0; i < frames_.size(); ++i)
    {
        const float near =
            frames_.size() > 1 ? static_cast<float>(i) / static_cast<float>(frames_.size() - 1) : 1.0f;
        g.setColour(t.atDepth(t.textTertiary, near));
        g.strokePath(frames_[i], juce::PathStrokeType(1.0f));
    }
    // The frame shown, where it is.
    const int shown = std::clamp(editor_.shownFrame(), 0, table.frames() - 1);
    const float along =
        table.frames() > 1 ? static_cast<float>(shown) / static_cast<float>(table.frames() - 1) : 0.0f;
    const auto r = frameArea(along);
    const auto line = cyclePath(table.samples(shown), N, r, std::max(16, static_cast<int>(r.getWidth())));
    auto fill = line;
    fill.lineTo(r.getRight(), r.getCentreY());
    fill.lineTo(r.getX(), r.getCentreY());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

void ResultView::mouseDown(const juce::MouseEvent& e)
{
    editor_.showFrame(frameAt(e.position.y));
    startFrame_ = editor_.shownFrame();
}

void ResultView::mouseDrag(const juce::MouseEvent& e)
{
    // Up goes back through the table, as the panel's display does with Position.
    const auto& input = ui::Theme::current().input;
    const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    const auto moved = static_cast<float>(-e.getDistanceFromDragStartY());
    const int frames = editor_.recipe().frames;
    editor_.showFrame(std::clamp(
        startFrame_ + juce::roundToInt(moved / input.valueTravel * fine * static_cast<float>(frames)), 0,
        frames - 1));
}

// ---- KeyframeStrip -------------------------------------------------------------------------------

KeyframeStrip::KeyframeStrip(TableEditor& editor) : editor_(editor)
{
    setTooltip(
        "The table's frames, left to right: click one to see it; double-click to add a keyframe there, "
        "drag one to move it, right-click it to remove it");
}

juce::Rectangle<float> KeyframeStrip::track() const
{
    const float half = static_cast<float>(ui::Theme::current().deviceGrid.displayHandle) * 0.5f;
    return getLocalBounds().toFloat().reduced(half, 0.0f);
}

float KeyframeStrip::xOf(int frame) const
{
    const auto r = track();
    const int frames = editor_.recipe().frames;
    return frames > 1 ? r.getX() + r.getWidth() * static_cast<float>(std::clamp(frame, 0, frames - 1)) /
                                       static_cast<float>(frames - 1)
                      : r.getCentreX();
}

int KeyframeStrip::frameAt(float x) const
{
    const auto r = track();
    const int frames = editor_.recipe().frames;
    return frames > 1
               ? std::clamp(juce::roundToInt((x - r.getX()) / r.getWidth() * static_cast<float>(frames - 1)),
                            0, frames - 1)
               : 0;
}

int KeyframeStrip::keyframeAt(float x) const
{
    const auto* l = editor_.layer();
    if (!keyframed(l))
        return -1;
    const float reach = static_cast<float>(ui::Theme::current().deviceGrid.displayHandle) * 0.5f;
    int best = -1;
    float nearest = reach + 0.5f;
    for (size_t i = 0; i < l->keyframes.size(); ++i)
        if (const float d = std::abs(xOf(l->keyframes[i].frame) - x); d <= nearest)
        {
            nearest = d;
            best = static_cast<int>(i);
        }
    return best;
}

void KeyframeStrip::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto r = track();
    const float y = r.getCentreY();
    // The frames as ticks along a line, the shown one in the value colour.
    g.setColour(t.surface3);
    g.fillRect(r.getX(), y - 0.5f, r.getWidth(), 1.0f);
    const int frames = editor_.recipe().frames;
    const float spacing = frames > 1 ? r.getWidth() / static_cast<float>(frames - 1) : r.getWidth();
    const int every = std::max(1, static_cast<int>(std::ceil(4.0f / spacing))); // ticks at least 4 px apart
    for (int f = 0; f < frames; f += every)
        g.fillRect(xOf(f) - 0.5f, y - 3.0f, 1.0f, 6.0f);
    g.setColour(t.value);
    g.fillRect(xOf(editor_.shownFrame()) - 1.0f, r.getY(), 2.0f, r.getHeight());
    // The selected layer's keyframes.
    const auto* l = editor_.layer();
    if (!keyframed(l))
        return;
    const float size = static_cast<float>(t.deviceGrid.displayHandle);
    for (size_t i = 0; i < l->keyframes.size(); ++i)
    {
        const int frame = static_cast<int>(i) == dragged_ ? dragFrame_ : l->keyframes[i].frame;
        juce::Path diamond;
        const juce::Point<float> c(xOf(frame), y);
        diamond.addQuadrilateral(c.x, c.y - size * 0.5f, c.x + size * 0.5f, c.y, c.x, c.y + size * 0.5f,
                                 c.x - size * 0.5f, c.y);
        const bool hot = static_cast<int>(i) == dragged_ || frame == editor_.shownFrame();
        g.setColour(hot ? t.active : t.surface3);
        g.fillPath(diamond);
        g.setColour(hot ? t.active : t.value);
        g.strokePath(diamond, juce::PathStrokeType(1.0f));
    }
}

void KeyframeStrip::mouseDown(const juce::MouseEvent& e)
{
    dragged_ = keyframeAt(e.position.x);
    if (e.mods.isPopupMenu())
    {
        const int k = dragged_;
        dragged_ = -1;
        if (k < 0)
            return;
        const auto* l = editor_.layer();
        juce::PopupMenu menu;
        menu.addItem("Remove Keyframe", l != nullptr && l->keyframes.size() > 1, false,
                     [safe = juce::Component::SafePointer<KeyframeStrip>(this), k]
                     {
                         if (safe != nullptr)
                             safe->editor_.removeKeyframe(k);
                     });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition());
        return;
    }
    if (dragged_ >= 0)
    {
        dragFrame_ = editor_.layer()->keyframes[static_cast<size_t>(dragged_)].frame;
        editor_.showFrame(dragFrame_);
    }
    else
        editor_.showFrame(frameAt(e.position.x));
}

void KeyframeStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged_ < 0)
    {
        editor_.showFrame(frameAt(e.position.x));
        return;
    }
    dragFrame_ = frameAt(e.position.x);
    editor_.showFrame(dragFrame_);
    repaint();
}

void KeyframeStrip::mouseUp(const juce::MouseEvent& e)
{
    if (dragged_ >= 0 && e.mouseWasDraggedSinceMouseDown())
    {
        const int k = dragged_;
        dragged_ = -1;
        editor_.moveKeyframe(k, dragFrame_);
    }
    dragged_ = -1;
    repaint();
}

void KeyframeStrip::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (keyframeAt(e.position.x) < 0)
        editor_.addKeyframe(frameAt(e.position.x));
}

// ---- TableEditor ---------------------------------------------------------------------------------

TableEditor::TableEditor(ui::DeviceUiContext& context, int oscillator)
    : ctx_(context), osc_(std::clamp(oscillator, 0, W::numOscs - 1)),
      frameReadout_([this] { return juce::String(frame_ + 1) + " / " + juce::String(recipe_.frames); },
                    "256 / 256")
{
    const auto& grid = ui::Theme::current().deviceGrid;
    setMinimumRows(kRows);
    // Left to right as the sound goes (§11): the layers, the selected one, the result, the
    // changes that shape it.
    layersPane_ = addPane("Layers", 4);
    layerPane_ = addPane("Layer", 0, 5);
    resultPane_ = addPane("Result", 4);
    changesPane_ = addPane("Changes", 4);

    preview_.setTooltip("Hear the module hold a C3 through the table as you shape it; click again to let go");
    preview_.setWantsKeyboardFocus(false);
    preview_.setSize(ui::Toggle::widthFor(preview_.getButtonText()), ui::Theme::current().shell.rowHeight);
    preview_.onClick = [this] { togglePreview(); };
    preview_.setEnabled(static_cast<bool>(ctx_.playNote));
    addCommand(preview_);

    addLayer_.setTooltip(
        "Add a layer: a drawing, harmonics, a formula, audio, an image or a table (up to four)");
    addLayer_.setWantsKeyboardFocus(false);
    addLayer_.onClick = [this] { showAddLayerMenu(); };
    placeInTitle(layersPane_, addLayer_, grid.titleHeight, true);
    addChange_.setTooltip("Add a change: each shapes every frame, in order from the top");
    addChange_.setWantsKeyboardFocus(false);
    addChange_.onClick = [this] { showAddChangeMenu(); };
    placeInTitle(changesPane_, addChange_, grid.titleHeight, true);

    // The selected layer: where the frame shown is in it, its wave and harmonics over its
    // keyframes, and the buttons that add and remove one.
    status_.setJustification(juce::Justification::centredRight);
    placeInTitle(layerPane_, status_, kStatusWidth, true);
    place(layerPane_, layerView_, 0, 0, 0, -2);
    place(layerPane_, strip_, 0, -2, 0, 1);
    for (auto* b : {&addKey_, &removeKey_})
    {
        b->setClickingTogglesState(false);
        b->setWantsKeyboardFocus(false);
    }
    addKey_.setTooltip("Add a keyframe to the layer at the frame shown, made of what the layer has there "
                       "(also a double-click on the strip, or editing the frame)");
    addKey_.onClick = [this] { addKeyframe(frame_); };
    removeKey_.setTooltip("Remove the layer's keyframe at the frame shown (also its menu on the strip)");
    removeKey_.onClick = [this]
    {
        if (const auto* l = layer(); keyframed(l))
            removeKeyframe(keyframeIndex(*l, frame_, recipe_.frames));
    };
    // Each as wide as its name needs: five columns, the layer view's least.
    place(layerPane_, addKey_, 0, -1, 2, 1, Kind::Line);
    place(layerPane_, removeKey_, 2, -1, 3, 1, Kind::Line);

    // The result: the whole table, the frame shown, its frame count.
    frameReadout_.setTooltip(
        "The frame shown, in the result and the layer: click another in the result or on the "
        "layer's strip");
    placeInTitle(resultPane_, frameReadout_, frameReadout_.preferredWidth(), true);
    place(resultPane_, result_, 0, 0, 0, -1);
    frames_.setStyle(ui::Knob::Style::Bar);
    frames_.setName("Frames");
    frames_.setTooltip("How many frames the table has (1 to 256): its sources are spread over them");
    frames_.setDefaultValue(framesToBar(64));
    frames_.textFromValue = [](float v) { return juce::String(framesFromBar(v)); };
    frames_.valueFromText = [](const juce::String& text) -> std::optional<float>
    {
        const int n = text.getIntValue();
        if (n < 1)
            return std::nullopt;
        return framesToBar(n);
    };
    wireBar(frames_,
            [this](float v, bool keep)
            {
                if (keep)
                    setFrames(framesFromBar(v));
            });
    place(resultPane_, frames_, 0, -1, 3, 1, Kind::Line);

    load();
    rebuildAll();
    shownTitle_ = title();
}

TableEditor::~TableEditor()
{
    closing();
}

void TableEditor::closing()
{
    if (previewNote_ >= 0 && ctx_.playNote)
        ctx_.playNote(previewNote_, 0.0f);
    previewNote_ = -1;
    preview_.setToggleState(false, juce::dontSendNotification);
}

juce::String TableEditor::title() const
{
    const auto* s = synthOf(ctx_);
    return "Osc " + juce::String(osc_ + 1) + ", " +
           juce::String(s != nullptr ? s->tableName(osc_) : recipe_.name);
}

void TableEditor::setOscillator(int n)
{
    n = std::clamp(n, 0, W::numOscs - 1);
    if (n == osc_)
        return;
    if (audio_)
        useAudio();
    osc_ = n;
    load();
    rebuildAll();
    updateTitle();
}

const Recipe::Layer* TableEditor::layer() const noexcept
{
    return layer_ >= 0 && layer_ < static_cast<int>(recipe_.layers.size())
               ? &recipe_.layers[static_cast<size_t>(layer_)]
               : nullptr;
}

// ---- following the module ------------------------------------------------------------------------

void TableEditor::load()
{
    const auto* s = synthOf(ctx_);
    if (s == nullptr)
        return;
    std::optional<Recipe> r;
    if (const auto& kept = s->recipe(osc_); kept.is_object())
        r = wavetable::recipeFromJson(kept);
    if (!r)
    {
        // A recipe of the table it plays; a factory table becomes one of yours once edited.
        const auto& id = s->tableId(osc_);
        const bool factory = id != "user";
        r = wavetable::recipeFromTable(*s->table(osc_), factory ? id : std::string());
        r->name = s->tableName(osc_) + (factory ? " (edited)" : "");
    }
    recipe_ = std::move(*r);
    kept_ = wavetable::toJson(recipe_);
    seen_ = s->table(osc_).get();
    seenRecipe_ = s->recipe(osc_);
    layer_ = std::clamp(layer_, 0, std::max(0, static_cast<int>(recipe_.layers.size()) - 1));
    frame_ = std::clamp(frame_, 0, recipe_.frames - 1);
    bakeNow();
}

void TableEditor::follow()
{
    const auto* s = synthOf(ctx_);
    if (s == nullptr)
        return;
    const auto* table = s->table(osc_).get();
    const auto& recipe = s->recipe(osc_);
    if (table == seen_ && recipe == seenRecipe_)
        return;
    seen_ = table;
    seenRecipe_ = recipe;
    if (recipe.is_object() && recipe == kept_)
        return; // what this editor kept
    // Undo, an assistant or the table picker changed it: show what the module has now.
    if (audio_)
        audio_.reset();
    load();
    rebuildAll();
}

void TableEditor::frame()
{
    // While a drag edits, the layer follows at once and the result every 120 ms at most: a bake
    // takes a while on large tables (more in a debug build).
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (rebake_ && (!editing_ || now - lastBake_ >= 120.0))
        bakeNow();
    if (!editing_)
        follow();
    frameReadout_.refresh();
    updateTitle();
}

void TableEditor::bakeNow()
{
    baked_ = wavetable::bake(recipe_);
    rebake_ = false;
    lastBake_ = juce::Time::getMillisecondCounterHiRes();
    result_.update();
    layerChanged();
}

void TableEditor::layerChanged()
{
    layerView_.update();
    strip_.repaint();
    frameReadout_.refresh();
    updateLayerPane();
}

void TableEditor::updateLayerPane()
{
    const auto* l = layer();
    setPaneTitle(layerPane_, l != nullptr ? "Layer " + juce::String(layer_ + 1) +
                                                juce::String::fromUTF8(" · ") + kindName(l->kind)
                                          : juce::String("Layer"));
    status_.setText(layerStatus());
    status_.setTooltip(keyframed(l) && keyframeIndex(*l, frame_, recipe_.frames) < 0
                           ? "Between keyframes: editing the layer here adds one"
                           : juce::String());
    const bool keys = keyframed(l);
    const bool onKey = keys && keyframeIndex(*l, frame_, recipe_.frames) >= 0;
    addKey_.setVisible(keys);
    removeKey_.setVisible(keys);
    addKey_.setEnabled(keys && !onKey && !audio_);
    removeKey_.setEnabled(onKey && l->keyframes.size() > 1 && !audio_);
    // What can't be done here recedes (ui-principles §3).
    const float dim = ui::Theme::current().roles.dimAlpha;
    addKey_.setAlpha(addKey_.isEnabled() ? 1.0f : dim);
    removeKey_.setAlpha(removeKey_.isEnabled() ? 1.0f : dim);
}

juce::String TableEditor::layerStatus() const
{
    const auto* l = layer();
    if (l == nullptr)
        return {};
    const auto frameText = [](int f) { return juce::String(f + 1); };
    switch (l->kind)
    {
    case K::Draw:
    case K::Harmonics:
    {
        if (keyframeIndex(*l, frame_, recipe_.frames) >= 0)
            return "keyframe at " + frameText(frame_);
        int before = -1, after = -1;
        for (const auto& k : l->keyframes)
        {
            const int at = std::clamp(k.frame, 0, recipe_.frames - 1);
            if (at < frame_ && at > before)
                before = at;
            if (at > frame_ && (after < 0 || at < after))
                after = at;
        }
        if (before >= 0 && after >= 0)
            return "between " + frameText(before) + " and " + frameText(after);
        return "held from " + frameText(before >= 0 ? before : after);
    }
    case K::Formula:
        return "made at each frame";
    case K::Table:
        if (!l->tableId.empty())
            if (const auto table = wavetable::factoryTable(l->tableId))
                return juce::String(table->frames()) + " frames, filled";
        [[fallthrough]];
    case K::Audio:
    case K::Image:
        return juce::String(l->sourceFrames()) + " frames, filled";
    }
    return {};
}

void TableEditor::updateTitle()
{
    if (const auto t = title(); t != shownTitle_)
    {
        shownTitle_ = t;
        if (onTitleChanged)
            onTitleChanged();
    }
}

// ---- edits ---------------------------------------------------------------------------------------

void TableEditor::preview(Recipe recipe)
{
    recipe_ = std::move(recipe);
    editing_ = true;
    rebake_ = true;
    layerChanged();
}

void TableEditor::keep(Recipe recipe, const juce::String& what)
{
    recipe_ = std::move(recipe);
    editing_ = false;
    if (wavetable::toJson(recipe_) == kept_)
    {
        bakeNow(); // nothing changed: no undo step
        return;
    }
    commit(what);
}

void TableEditor::commit(const juce::String& what, bool newStep)
{
    bakeNow();
    kept_ = wavetable::toJson(recipe_);
    if (newStep)
        ctx_.params.beginEdit(what);
    ctx_.params.applyExtraState(W::recipePatch(osc_, recipe_, baked_));
    // What the module has now, so an undo back to what it had before is seen (a factory table
    // is the same table again).
    if (const auto* s = synthOf(ctx_))
    {
        seen_ = s->table(osc_).get();
        seenRecipe_ = s->recipe(osc_);
    }
    updateTitle();
}

void TableEditor::selectLayer(int index)
{
    layer_ = std::clamp(index, 0, std::max(0, static_cast<int>(recipe_.layers.size()) - 1));
    rebuildLeft();
    layerChanged();
}

void TableEditor::showFrame(int frame)
{
    frame = std::clamp(frame, 0, std::max(0, recipe_.frames - 1));
    if (frame == frame_)
        return;
    frame_ = frame;
    result_.repaint();
    layerChanged();
    // The changes' amounts at this frame.
    for (size_t i = 0; i < changeBars_.size(); ++i)
        if (const auto c = firstChange_ + static_cast<int>(i); c < static_cast<int>(recipe_.changes.size()))
            changeBars_[i]->setValue(recipe_.changes[static_cast<size_t>(c)].amountAt(frame_),
                                     juce::dontSendNotification);
}

void TableEditor::addLayer(Recipe::Layer layer)
{
    if (static_cast<int>(recipe_.layers.size()) >= Recipe::maxLayers)
        return;
    const auto kind = layer.kind;
    auto r = recipe_;
    r.layers.push_back(std::move(layer));
    layer_ = static_cast<int>(r.layers.size()) - 1;
    keep(std::move(r), "Add a layer");
    // On its keyframe, where it's edited.
    if (kind == K::Draw || kind == K::Harmonics)
        frame_ = std::clamp(recipe_.layers.back().keyframes.front().frame, 0, recipe_.frames - 1);
    rebuildLeft();
    layerChanged();
}

void TableEditor::removeLayer(int index)
{
    if (index < 0 || index >= static_cast<int>(recipe_.layers.size()) || recipe_.layers.size() <= 1)
        return;
    auto r = recipe_;
    r.layers.erase(r.layers.begin() + index);
    layer_ =
        std::min(layer_ >= index && layer_ > 0 ? layer_ - 1 : layer_, static_cast<int>(r.layers.size()) - 1);
    keep(std::move(r), "Remove a layer");
    rebuildLeft();
}

void TableEditor::moveLayer(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || to < 0 || index >= static_cast<int>(recipe_.layers.size()) ||
        to >= static_cast<int>(recipe_.layers.size()))
        return;
    auto r = recipe_;
    std::swap(r.layers[static_cast<size_t>(index)], r.layers[static_cast<size_t>(to)]);
    layer_ = to;
    keep(std::move(r), "Move a layer");
    rebuildLeft();
}

void TableEditor::setLayerOn(int index, bool on)
{
    if (index < 0 || index >= static_cast<int>(recipe_.layers.size()))
        return;
    auto r = recipe_;
    r.layers[static_cast<size_t>(index)].on = on;
    keep(std::move(r), on ? "Turn a layer on" : "Turn a layer off");
    rebuildLeft();
}

void TableEditor::addKeyframe(int frame)
{
    if (!keyframed(layer()))
        return;
    frame = std::clamp(frame, 0, recipe_.frames - 1);
    if (keyframeIndex(*layer(), frame, recipe_.frames) < 0)
    {
        auto r = recipe_;
        auto& l = r.layers[static_cast<size_t>(layer_)];
        l.keyframes.push_back(wavetable::newKeyframe(l, frame, r.frames));
        sortKeyframes(l);
        keep(std::move(r), "Add a keyframe");
    }
    frame_ = -1;
    showFrame(frame);
}

void TableEditor::moveKeyframe(int index, int frame)
{
    const auto* l = layer();
    if (!keyframed(l) || index < 0 || index >= static_cast<int>(l->keyframes.size()))
        return;
    frame = std::clamp(frame, 0, recipe_.frames - 1);
    const int there = keyframeIndex(*l, frame, recipe_.frames);
    if (there >= 0 && there != index)
        return; // one keyframe a frame
    auto r = recipe_;
    auto& moved = r.layers[static_cast<size_t>(layer_)];
    moved.keyframes[static_cast<size_t>(index)].frame = frame;
    sortKeyframes(moved);
    keep(std::move(r), "Move a keyframe");
    frame_ = -1;
    showFrame(frame);
}

void TableEditor::removeKeyframe(int index)
{
    const auto* l = layer();
    if (!keyframed(l) || index < 0 || index >= static_cast<int>(l->keyframes.size()) ||
        l->keyframes.size() <= 1)
        return;
    auto r = recipe_;
    auto& from = r.layers[static_cast<size_t>(layer_)].keyframes;
    from.erase(from.begin() + index);
    keep(std::move(r), "Remove a keyframe");
    layerChanged();
}

void TableEditor::addChange(Recipe::Change change)
{
    auto r = recipe_;
    r.changes.push_back(std::move(change));
    keep(std::move(r), "Add a change");
    // The new one in view.
    firstChange_ = std::max(0, static_cast<int>(recipe_.changes.size()) - rows());
    rebuildChanges();
}

void TableEditor::removeChange(int index)
{
    if (index < 0 || index >= static_cast<int>(recipe_.changes.size()))
        return;
    auto r = recipe_;
    r.changes.erase(r.changes.begin() + index);
    keep(std::move(r), "Remove a change");
    rebuildChanges();
}

void TableEditor::moveChange(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || to < 0 || index >= static_cast<int>(recipe_.changes.size()) ||
        to >= static_cast<int>(recipe_.changes.size()))
        return;
    auto r = recipe_;
    std::swap(r.changes[static_cast<size_t>(index)], r.changes[static_cast<size_t>(to)]);
    keep(std::move(r), "Move a change");
    rebuildChanges();
}

void TableEditor::setChangeOn(int index, bool on)
{
    if (index < 0 || index >= static_cast<int>(recipe_.changes.size()))
        return;
    auto r = recipe_;
    r.changes[static_cast<size_t>(index)].on = on;
    keep(std::move(r), on ? "Turn a change on" : "Turn a change off");
    rebuildChanges();
}

namespace
{
/// `r` with change `index`'s amount set: at `frame` while it varies, else for the whole table.
Recipe withAmount(Recipe r, int index, float amount, int frame)
{
    auto& a = r.changes[static_cast<size_t>(index)].amounts;
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (a.size() > 1)
    {
        const auto it =
            std::lower_bound(a.begin(), a.end(), frame, [](const auto& p, int f) { return p.first < f; });
        if (it != a.end() && it->first == frame)
            it->second = amount;
        else
            a.insert(it, {frame, amount});
    }
    else
        a = {{0, amount}};
    return r;
}
} // namespace

void TableEditor::setChangeAmount(int index, float amount)
{
    if (index < 0 || index >= static_cast<int>(recipe_.changes.size()))
        return;
    keep(withAmount(recipe_, index, amount, frame_), "Set a change's amount");
}

void TableEditor::setChangeVaries(int index, bool varies)
{
    if (index < 0 || index >= static_cast<int>(recipe_.changes.size()))
        return;
    auto r = recipe_;
    auto& c = r.changes[static_cast<size_t>(index)];
    if (varies && c.amounts.size() <= 1)
        c.amounts = {{0, c.amountAt(0)}, {r.frames - 1, c.amountAt(r.frames - 1)}};
    else if (!varies)
        c.amounts = {{0, c.amountAt(frame_)}};
    keep(std::move(r), varies ? "Vary a change's amount" : "Hold a change's amount");
    rebuildChanges();
}

void TableEditor::setFrames(int count)
{
    count = std::clamp(count, 1, Recipe::maxFrames);
    if (count == recipe_.frames)
        return;
    auto r = recipe_;
    // Keyframes and amounts keep their place in the table.
    const auto rescale = [&](int f)
    {
        return recipe_.frames > 1 ? juce::roundToInt(static_cast<float>(f) * static_cast<float>(count - 1) /
                                                     static_cast<float>(recipe_.frames - 1))
                                  : 0;
    };
    for (auto& l : r.layers)
    {
        for (auto& k : l.keyframes)
            k.frame = rescale(k.frame);
        // Two keyframes on one frame: the first stays.
        l.keyframes.erase(std::unique(l.keyframes.begin(), l.keyframes.end(),
                                      [](const auto& a, const auto& b) { return a.frame == b.frame; }),
                          l.keyframes.end());
    }
    for (auto& c : r.changes)
    {
        for (auto& [f, a] : c.amounts)
            f = c.amounts.size() > 1 ? rescale(f) : 0;
        c.amounts.erase(std::unique(c.amounts.begin(), c.amounts.end(),
                                    [](const auto& a, const auto& b) { return a.first == b.first; }),
                        c.amounts.end());
    }
    frame_ = std::clamp(rescale(frame_), 0, count - 1);
    r.frames = count;
    keep(std::move(r), "Set the frame count");
    frames_.setValue(framesToBar(recipe_.frames), juce::dontSendNotification);
    layerChanged();
}

// ---- the panes -----------------------------------------------------------------------------------

void TableEditor::rebuildAll()
{
    frames_.setValue(framesToBar(recipe_.frames), juce::dontSendNotification);
    rebuildLeft();
    rebuildChanges();
    layerChanged();
}

void TableEditor::rebuildLeft()
{
    clearPane(layersPane_);
    leftControls_.clear();
    formula_ = nullptr;
    const auto& grid = ui::Theme::current().deviceGrid;
    juce::ignoreUnused(grid);
    const int count = static_cast<int>(recipe_.layers.size());
    addLayer_.setEnabled(count < Recipe::maxLayers && !audio_);
    // While a new table is taken from audio, its frame count is the chooser's.
    frames_.setVisible(!audio_ || audio_->asLayer);

    // A row a layer: its On, its name (selecting it).
    for (int i = 0; i < count; ++i)
    {
        auto& dot = make(leftControls_, std::make_unique<ui::Toggle>());
        dot.setToggleState(recipe_.layers[static_cast<size_t>(i)].on, juce::dontSendNotification);
        dot.setTooltip("Turn the layer on or off");
        dot.onClick = [this, i, &dot] { setLayerOn(i, dot.getToggleState()); };
        place(layersPane_, dot, 0, i, 1, 1, Kind::Line);
    }
    if (count > 0)
    {
        juce::StringArray names;
        for (const auto& l : recipe_.layers)
            names.add(layerName(l));
        auto& list = make(leftControls_, std::make_unique<ui::Selector>(names, 1));
        list.setSelected(layer_);
        list.onSelect = [this](int i) { selectLayer(i); };
        for (int i = 0; i < count; ++i)
        {
            list.buttons()[i]->setTooltip("Select the layer to edit it; right-click it to move or remove it");
            list.buttons()[i]->onPopupMenu = [this, i] { showLayerMenu(i); };
        }
        place(layersPane_, list, 1, 0, 3, count, Kind::Line);
    }

    // The way chooser while audio arrives (§5).
    if (audio_)
    {
        label(layersPane_, "From audio", 0, 4, 4);
        auto& ways =
            make(leftControls_,
                 std::make_unique<ui::Selector>(juce::StringArray{"Pitched", "Spectral", "Slices"}, 3));
        ways.setSelected(static_cast<int>(audio_->way));
        ways.onSelect = [this](int i) { chooseWay(static_cast<wavetable::AudioWay>(i)); };
        ways.buttons()[0]->setTooltip("Single cycles where the pitch is, phase-aligned: for notes, voices, "
                                      "instruments");
        ways.buttons()[1]->setTooltip(
            "The harmonics' levels at the sound's pitch over time: its colour without "
            "its noise, for pads and chorused sounds");
        ways.buttons()[2]->setTooltip("Windows of the sound as they are: for anything, a loop's texture");
        place(layersPane_, ways, 0, 5, 4, 1, Kind::Line);
        auto& taken = make(leftControls_, std::make_unique<ui::Knob>());
        taken.setStyle(ui::Knob::Style::Bar);
        taken.setName("Frames");
        taken.setTooltip("How many frames to take, spread through the sound (8 to 256)");
        taken.setDefaultValue(framesToBar(64));
        taken.setValue(framesToBar(audio_->frames), juce::dontSendNotification);
        taken.textFromValue = [](float v) { return juce::String(std::max(8, framesFromBar(v))); };
        taken.valueFromText = [](const juce::String& text) -> std::optional<float>
        { return text.getIntValue() >= 1 ? std::optional(framesToBar(text.getIntValue())) : std::nullopt; };
        wireBar(taken,
                [this](float v, bool keep)
                {
                    if (keep)
                        chooseAudioFrames(std::max(8, framesFromBar(v)));
                });
        place(layersPane_, taken, 0, 6, 4, 1, Kind::Line);
        auto& note = make(leftControls_, std::make_unique<NoteText>());
        note.setText(audio_->message, audio_->failed);
        place(layersPane_, note, 0, 7, 4, 2);
        auto& use = make(leftControls_, std::make_unique<ui::Toggle>("Use"));
        use.setClickingTogglesState(false);
        use.setTooltip("Keep the table made from the audio");
        use.setEnabled(!audio_->failed);
        use.setAlpha(use.isEnabled() ? 1.0f : ui::Theme::current().roles.dimAlpha);
        use.onClick = [this] { useAudio(); };
        place(layersPane_, use, 0, kRows - 1, 2, 1, Kind::Line);
        auto& cancel = make(leftControls_, std::make_unique<ui::Toggle>("Cancel"));
        cancel.setClickingTogglesState(false);
        cancel.setTooltip("Put back the table from before");
        cancel.onClick = [this] { cancelAudio(); };
        place(layersPane_, cancel, 2, kRows - 1, 2, 1, Kind::Line);
        return;
    }

    const auto* l = layer();
    if (l == nullptr)
        return;
    const int index = layer_;
    label(layersPane_, kindName(l->kind), 0, 4, 4);

    auto& level = make(leftControls_, std::make_unique<ui::Knob>());
    level.setStyle(ui::Knob::Style::Bar);
    level.setName("Level");
    level.setTooltip("How much of the layer is in the table: the layers are summed");
    level.setDefaultValue(1.0f);
    level.setValue(l->level, juce::dontSendNotification);
    level.textFromValue = percent;
    level.valueFromText = percentFromText;
    wireBar(level,
            [this, index](float v, bool keep)
            {
                auto r = recipe_;
                r.layers[static_cast<size_t>(index)].level = v;
                keep ? this->keep(std::move(r), "Set a layer's level") : preview(std::move(r));
            });
    place(layersPane_, level, 0, 5, 4, 1, Kind::Line);

    if (fills(l->kind))
    {
        auto& fill = make(leftControls_, std::make_unique<ui::ChoiceBox>());
        fill.addItem("Crossfade", 1);
        fill.addItem("Spectral", 2);
        fill.setInlineName("Fill");
        fill.setSelectedId(l->fill == Recipe::Fill::Spectral ? 2 : 1, juce::dontSendNotification);
        fill.setTooltip(keyframed(l)
                            ? "How the frames between keyframes are filled: the waves mixed, or each "
                              "harmonic moved from one to the next"
                            : "How the frames between the source's own are filled: the waves mixed, or "
                              "each harmonic moved from one to the next");
        fill.onChange = [this, index, &fill]
        {
            auto r = recipe_;
            r.layers[static_cast<size_t>(index)].fill =
                fill.getSelectedId() == 2 ? Recipe::Fill::Spectral : Recipe::Fill::Crossfade;
            keep(std::move(r), "Set a layer's fill");
        };
        place(layersPane_, fill, 0, 6, 4, 1, Kind::Line);
    }

    switch (l->kind)
    {
    case K::Draw:
    {
        auto& tool =
            make(leftControls_, std::make_unique<ui::Selector>(juce::StringArray{"Points", "Freehand"}, 2));
        tool.setSelected(tool_ == DrawTool::Freehand ? 1 : 0);
        tool.buttons()[0]->setTooltip("Drag points and bend lines, as in an LFO's shape");
        tool.buttons()[1]->setTooltip("Draw the wave as you drag, a point at every step of the grid");
        tool.onSelect = [this](int i)
        {
            tool_ = i == 1 ? DrawTool::Freehand : DrawTool::Points;
            layerView_.update();
        };
        place(layersPane_, tool, 0, 7, 4, 1, Kind::Line);
        auto& snapTo = make(leftControls_, std::make_unique<ui::ChoiceBox>());
        const int steps[] = {0, 8, 16, 32, 64};
        for (int i = 0; i < 5; ++i)
            snapTo.addItem(steps[i] == 0 ? juce::String("Off") : juce::String(steps[i]), i + 1);
        snapTo.setInlineName("Grid");
        for (int i = 0; i < 5; ++i)
            if (steps[i] == grid_)
                snapTo.setSelectedId(i + 1, juce::dontSendNotification);
        snapTo.setTooltip("The steps across the cycle that points snap to, and that freehand drawing fills");
        snapTo.onChange = [this, &snapTo]
        {
            const int s[] = {0, 8, 16, 32, 64};
            grid_ = s[std::clamp(snapTo.getSelectedId() - 1, 0, 4)];
            layerView_.repaint();
        };
        place(layersPane_, snapTo, 0, 8, 4, 1, Kind::Line);
        break;
    }
    case K::Formula:
    {
        auto& field = make(leftControls_, std::make_unique<juce::TextEditor>());
        const auto& t = ui::Theme::current();
        field.setMultiLine(true, true);
        field.setReturnKeyStartsNewLine(false);
        field.setFont(ui::font(ui::TextStyle::Value));
        field.setIndents(t.shell.fieldTextInset, t.shell.fieldTextInset);
        field.setColour(juce::TextEditor::backgroundColourId, t.surface3);
        field.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        field.setText(juce::String(l->formula), false);
        field.setTooltip("One line in x (the phase, 0 to 1) and t (where in the table, 0 to 1): + - * / ^, "
                         "sin, cos, tan, abs, sqrt, exp, log, min, max, pow, clamp, floor, frac, and the "
                         "waves tri, saw, sq, rand. Return keeps it");
        auto& message = make(leftControls_, std::make_unique<NoteText>());
        const auto check = [&field, &message]
        {
            const auto error = wavetable::formulaError(field.getText().toStdString());
            message.setText(error.empty() ? juce::String("x: the phase, t: where in the table")
                                          : juce::String(error),
                            !error.empty());
        };
        check();
        field.onTextChange = check;
        const auto apply = [this, index, &field]
        {
            const auto text = field.getText().trim().toStdString();
            if (!wavetable::formulaError(text).empty() ||
                text == recipe_.layers[static_cast<size_t>(index)].formula)
                return;
            auto r = recipe_;
            r.layers[static_cast<size_t>(index)].formula = text;
            keep(std::move(r), "Edit a formula");
        };
        field.onReturnKey = apply;
        field.onFocusLost = apply;
        formula_ = &field;
        place(layersPane_, field, 0, 6, 4, 2);
        place(layersPane_, message, 0, 8, 4, 1);
        break;
    }
    case K::Image:
    {
        auto& harmonics = make(leftControls_, std::make_unique<ui::Toggle>("As harmonics"));
        harmonics.setToggleState(l->imageAsHarmonics, juce::dontSendNotification);
        harmonics.setTooltip("Each row's brightness as the levels of a frame's harmonics, left to right, "
                             "instead of as its wave");
        harmonics.onClick = [this, index, &harmonics]
        {
            auto r = recipe_;
            r.layers[static_cast<size_t>(index)].imageAsHarmonics = harmonics.getToggleState();
            keep(std::move(r), "Read an image as harmonics");
        };
        place(layersPane_, harmonics, 0, 7, 4, 1, Kind::Line);
        auto& note = make(leftControls_, std::make_unique<NoteText>());
        note.setText(juce::String(l->note) + (l->note.empty() ? "" : ", ") + juce::String(l->sourceFrames()) +
                     " rows");
        place(layersPane_, note, 0, 8, 4, 1);
        break;
    }
    case K::Table:
    {
        auto& pick = make(leftControls_, std::make_unique<TableField>());
        const auto tables = wavetable::factoryTables();
        for (size_t i = 0; i < tables.size(); ++i)
        {
            pick.addItem(juce::String(std::string(tables[i].name)), static_cast<int>(i) + 1);
            if (tables[i].id == l->tableId)
                pick.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        pick.setInlineName("Table");
        if (l->tableId.empty())
            pick.setText("Its own frames", juce::dontSendNotification);
        pick.setTooltip("The factory table the layer starts from");
        pick.buildMenu = [this, index](juce::PopupMenu& menu)
        {
            menu = factoryTableMenu(recipe_.layers[static_cast<size_t>(index)].tableId,
                                    [this, index](std::string_view id)
                                    {
                                        auto r = recipe_;
                                        auto& t = r.layers[static_cast<size_t>(index)];
                                        t.tableId = std::string(id);
                                        t.frames.clear();
                                        if (const auto table = wavetable::factoryTable(id))
                                            t.note = table->name;
                                        keep(std::move(r), "Choose a layer's table");
                                        rebuildLeft();
                                        layerChanged();
                                    });
        };
        place(layersPane_, pick, 0, 7, 4, 1, Kind::Line);
        break;
    }
    case K::Audio:
    {
        auto& note = make(leftControls_, std::make_unique<NoteText>());
        note.setText(juce::String(l->note));
        place(layersPane_, note, 0, 7, 4, 2);
        break;
    }
    case K::Harmonics:
        break;
    }
}

juce::String TableEditor::changeName(const Recipe::Change& change) const
{
    switch (change.kind)
    {
    case C::Normalise:
        return "Normalise";
    case C::RemoveDc:
        return "Remove DC";
    case C::PhaseAlign:
        return "Phase Align";
    case C::Smooth:
        return "Smooth";
    case C::Fold:
        return "Fold";
    case C::Filter:
        return change.filterType == 1 ? "High Pass" : change.filterType == 2 ? "Band Pass" : "Low Pass";
    case C::Reverse:
        return "Reverse";
    case C::Mirror:
        return "Mirror";
    case C::Warp:
    {
        // The oscillator's own names for its warps.
        const auto& choices = ctx_.info.params[W::osc(0, W::OscWarp1)].choices;
        const auto i = static_cast<size_t>(change.warp);
        return i < choices.size() ? juce::String(std::string(choices[i])) : juce::String("Warp");
    }
    }
    return {};
}

void TableEditor::rebuildChanges()
{
    clearPane(changesPane_);
    changeControls_.clear();
    changeBars_.clear();
    const int count = static_cast<int>(recipe_.changes.size());
    const int showing = std::max(1, rows());
    firstChange_ = std::clamp(firstChange_, 0, std::max(0, count - showing));
    setPaneTitle(changesPane_, count > showing ? "Changes " + juce::String(firstChange_ + 1) +
                                                     juce::String::fromUTF8("–") +
                                                     juce::String(std::min(count, firstChange_ + showing)) +
                                                     " of " + juce::String(count)
                                               : juce::String("Changes"));
    if (count == 0)
    {
        auto& hint = make(changeControls_, std::make_unique<NoteText>());
        hint.setText("None: + adds one. Each shapes the result, in order from the top.");
        place(changesPane_, hint, 0, 0, 4, 2);
        return;
    }
    for (int row = 0; row < showing && firstChange_ + row < count; ++row)
    {
        const int i = firstChange_ + row;
        const auto& c = recipe_.changes[static_cast<size_t>(i)];
        auto& dot = make(changeControls_, std::make_unique<ui::Toggle>());
        dot.setToggleState(c.on, juce::dontSendNotification);
        dot.setTooltip("Turn the change on or off");
        dot.onClick = [this, i, &dot] { setChangeOn(i, dot.getToggleState()); };
        place(changesPane_, dot, 0, row, 1, 1, Kind::Line);
        auto& bar = make(changeControls_, std::make_unique<ui::Knob>());
        bar.setStyle(ui::Knob::Style::Bar);
        bar.setName(changeName(c));
        bar.setDefaultValue(1.0f);
        bar.setValue(c.amountAt(frame_), juce::dontSendNotification);
        bar.textFromValue = percent;
        bar.valueFromText = percentFromText;
        bar.setTooltip(
            c.amounts.size() > 1
                ? "How much it changes the frame shown: set at each frame you show, in between from "
                  "frame to frame. Right-click for more"
                : "How much it changes every frame. Right-click to vary it from frame to frame, move "
                  "or remove it");
        bar.onPopupMenu = [this, i] { showChangeMenu(i); };
        wireBar(bar,
                [this, i](float v, bool keep)
                {
                    auto r = withAmount(recipe_, i, v, frame_);
                    keep ? this->keep(std::move(r), "Set a change's amount") : preview(std::move(r));
                });
        place(changesPane_, bar, 1, row, 3, 1, Kind::Line);
        changeBars_.push_back(&bar);
    }
}

std::vector<ui::Knob*> TableEditor::changeBars() const
{
    return changeBars_;
}

void TableEditor::resized()
{
    const int before = rows();
    ModuleEditor::resized();
    if (rows() != before)
        rebuildChanges();
}

void TableEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Over the changes: scroll them.
    const auto at = e.getEventRelativeTo(this).position.toInt();
    if (!paneBounds(changesPane_).contains(at))
        return;
    const int count = static_cast<int>(recipe_.changes.size());
    if (count <= rows())
        return;
    const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    firstChange_ = std::clamp(firstChange_ + (delta < 0.0f ? 1 : -1), 0, count - rows());
    rebuildChanges();
}

// ---- menus ---------------------------------------------------------------------------------------

void TableEditor::showAddLayerMenu()
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<TableEditor> self(this);
    const auto add = [self](K kind)
    {
        return [self, kind]
        {
            if (self != nullptr)
                self->addLayer(wavetable::newLayer(kind));
        };
    };
    menu.addItem("Draw", add(K::Draw));
    menu.addItem("Harmonics", add(K::Harmonics));
    menu.addItem("Formula", add(K::Formula));
    menu.addSeparator();
    menu.addItem(juce::String::fromUTF8("Audio…"),
                 [self]
                 {
                     if (self != nullptr)
                         self->startImport(true);
                 });
    menu.addItem(juce::String::fromUTF8("Image…"),
                 [self]
                 {
                     if (self != nullptr)
                         self->importImage(false);
                 });
    // The factory tables by category, as every table's menu lists them.
    const auto tables = factoryTableMenu({},
                                         [self](std::string_view id)
                                         {
                                             if (self == nullptr)
                                                 return;
                                             auto layer = wavetable::newLayer(K::Table);
                                             layer.tableId = std::string(id);
                                             self->addLayer(std::move(layer));
                                         });
    menu.addSubMenu("Table", tables);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addLayer_));
}

void TableEditor::showAddChangeMenu()
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<TableEditor> self(this);
    const auto add = [self](C kind, int filter = 0, dsp::Warp warp = dsp::Warp::FormantShift)
    {
        return [self, kind, filter, warp]
        {
            if (self == nullptr)
                return;
            Recipe::Change c;
            c.kind = kind;
            c.filterType = filter;
            c.warp = warp;
            c.amounts = {{0, kind == C::Filter || kind == C::Smooth || kind == C::Fold || kind == C::Warp
                                 ? 0.5f
                                 : 1.0f}};
            self->addChange(std::move(c));
        };
    };
    menu.addItem("Normalise", add(C::Normalise));
    menu.addItem("Remove DC", add(C::RemoveDc));
    menu.addItem("Phase Align", add(C::PhaseAlign));
    menu.addItem("Smooth", add(C::Smooth));
    menu.addItem("Fold", add(C::Fold));
    juce::PopupMenu filters;
    filters.addItem("Low Pass", add(C::Filter, 0));
    filters.addItem("High Pass", add(C::Filter, 1));
    filters.addItem("Band Pass", add(C::Filter, 2));
    menu.addSubMenu("Filter", filters);
    menu.addItem("Reverse", add(C::Reverse));
    menu.addItem("Mirror", add(C::Mirror));
    juce::PopupMenu warps;
    for (int w = static_cast<int>(dsp::Warp::FormantShift); w <= static_cast<int>(dsp::Warp::Shepard); ++w)
    {
        Recipe::Change probe;
        probe.kind = C::Warp;
        probe.warp = static_cast<dsp::Warp>(w);
        warps.addItem(changeName(probe), add(C::Warp, 0, static_cast<dsp::Warp>(w)));
    }
    menu.addSubMenu("Spectral", warps);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addChange_));
}

void TableEditor::showLayerMenu(int index)
{
    const int count = static_cast<int>(recipe_.layers.size());
    juce::Component::SafePointer<TableEditor> self(this);
    juce::PopupMenu menu;
    menu.addItem("Move Up", index > 0, false,
                 [self, index]
                 {
                     if (self != nullptr)
                         self->moveLayer(index, -1);
                 });
    menu.addItem("Move Down", index + 1 < count, false,
                 [self, index]
                 {
                     if (self != nullptr)
                         self->moveLayer(index, +1);
                 });
    menu.addSeparator();
    menu.addItem("Remove", count > 1, false,
                 [self, index]
                 {
                     if (self != nullptr)
                         self->removeLayer(index);
                 });
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

void TableEditor::showChangeMenu(int index)
{
    const int count = static_cast<int>(recipe_.changes.size());
    if (index < 0 || index >= count)
        return;
    const bool varies = recipe_.changes[static_cast<size_t>(index)].amounts.size() > 1;
    juce::Component::SafePointer<TableEditor> self(this);
    juce::PopupMenu menu;
    menu.addItem("Vary From Frame to Frame", true, varies,
                 [self, index, varies]
                 {
                     if (self != nullptr)
                         self->setChangeVaries(index, !varies);
                 });
    menu.addSeparator();
    menu.addItem("Move Up", index > 0, false,
                 [self, index]
                 {
                     if (self != nullptr)
                         self->moveChange(index, -1);
                 });
    menu.addItem("Move Down", index + 1 < count, false,
                 [self, index]
                 {
                     if (self != nullptr)
                         self->moveChange(index, +1);
                 });
    menu.addSeparator();
    menu.addItem("Remove",
                 [self, index]
                 {
                     if (self != nullptr)
                         self->removeChange(index);
                 });
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
}

// ---- Preview -------------------------------------------------------------------------------------

void TableEditor::togglePreview()
{
    if (!ctx_.playNote)
        return;
    if (previewNote_ >= 0)
    {
        ctx_.playNote(previewNote_, 0.0f);
        previewNote_ = -1;
    }
    else
    {
        previewNote_ = kPreviewNote;
        ctx_.playNote(previewNote_, kPreviewVelocity);
    }
    preview_.setToggleState(previewNote_ >= 0, juce::dontSendNotification);
}

// ---- audio to wavetable --------------------------------------------------------------------------

wavetable::AudioWay TableEditor::audioWay() const noexcept
{
    return audio_ ? audio_->way : wavetable::AudioWay::Pitched;
}

void TableEditor::takeAudio(std::vector<float> mono, double sampleRate, juce::String name, bool asLayer)
{
    if (audio_)
        useAudio();
    if (mono.size() < 2 || sampleRate <= 0.0)
        return;
    // A minute at most: enough to find a table's frames in.
    mono.resize(std::min(mono.size(), static_cast<size_t>(kMostAudio * sampleRate)));
    PendingAudio a;
    a.samples = std::move(mono);
    a.sampleRate = sampleRate;
    a.name = name.toStdString();
    a.asLayer = asLayer && static_cast<int>(recipe_.layers.size()) < Recipe::maxLayers;
    a.frames = a.asLayer ? std::max(8, recipe_.frames) : 64;
    a.before = recipe_;
    // Pitched where there's a pitch; Slices where there isn't.
    a.way = wavetable::findPitch(a.samples.data(), static_cast<int>(a.samples.size()), sampleRate)
                ? wavetable::AudioWay::Pitched
                : wavetable::AudioWay::Slices;
    audio_ = std::move(a);
    runAudio();
    rebuildLeft();
    requestShow();
}

void TableEditor::chooseWay(wavetable::AudioWay way)
{
    if (!audio_ || audio_->way == way)
        return;
    audio_->way = way;
    runAudio();
    rebuildLeft();
}

void TableEditor::chooseAudioFrames(int count)
{
    if (!audio_)
        return;
    count = std::clamp(count, 8, Recipe::maxFrames);
    if (count == audio_->frames)
        return;
    audio_->frames = count;
    runAudio();
    rebuildLeft();
}

void TableEditor::runAudio()
{
    auto& a = *audio_;
    std::string error;
    const auto frames = wavetable::framesFromAudio(a.samples.data(), static_cast<int>(a.samples.size()),
                                                   a.sampleRate, a.way, a.frames, &error);
    const auto seconds = juce::String(static_cast<double>(a.samples.size()) / a.sampleRate, 1) + " s";
    if (!frames)
    {
        a.failed = true;
        a.message = error;
        if (a.started) // what it played before, back
        {
            recipe_ = a.before;
            commit("Make a wavetable from audio", false);
        }
        return;
    }
    a.failed = false;
    a.message = (a.way == wavetable::AudioWay::Slices
                     ? seconds + " of audio, " + juce::String(frames->count) + " slices"
                     : juce::String(frames->pitch, 1) + " Hz, " + seconds + " of audio")
                    .toStdString();
    Recipe r = a.asLayer ? a.before : Recipe{};
    if (a.asLayer)
    {
        r.layers.push_back(wavetable::audioLayer(*frames, a.way, a.name));
        layer_ = static_cast<int>(r.layers.size()) - 1;
    }
    else
    {
        r = wavetable::recipeFromAudio(*frames, a.way, a.name);
        layer_ = 0;
    }
    recipe_ = std::move(r);
    frame_ = std::clamp(frame_, 0, recipe_.frames - 1);
    // Heard at once: one undo step for the whole choosing.
    commit("Make a wavetable from audio", !a.started);
    a.started = true;
    frames_.setValue(framesToBar(recipe_.frames), juce::dontSendNotification);
}

void TableEditor::useAudio()
{
    if (!audio_)
        return;
    const bool failed = audio_->failed;
    if (failed)
    {
        cancelAudio();
        return;
    }
    audio_.reset();
    rebuildLeft();
}

void TableEditor::cancelAudio()
{
    if (!audio_)
        return;
    if (audio_->started)
    {
        recipe_ = audio_->before;
        commit("Make a wavetable from audio", false);
    }
    audio_.reset();
    layer_ = std::clamp(layer_, 0, std::max(0, static_cast<int>(recipe_.layers.size()) - 1));
    frame_ = std::clamp(frame_, 0, recipe_.frames - 1);
    rebuildAll();
}

// ---- files ---------------------------------------------------------------------------------------

void TableEditor::openFile(const juce::File& file, bool asLayer)
{
    const auto path = file.getFullPathName().toStdString();
    std::string error;
    if (wavetable::isTableFile(path))
    {
        const auto read = wavetable::importTable(path, 0, &error);
        if (!read)
        {
            warn("Couldn't import the table", file.getFileName() + ": " + juce::String(error));
            return;
        }
        const auto table =
            dsp::Wavetable::fromCycles(read->samples.data(), read->frameLength, read->frames, true);
        const auto name = read->name.empty() ? file.getFileNameWithoutExtension().toStdString() : read->name;
        if (asLayer && static_cast<int>(recipe_.layers.size()) < Recipe::maxLayers)
        {
            Recipe::Layer layer;
            layer.kind = K::Table;
            // A factory table's file: the layer takes the table by its ID.
            if (const auto id = wavetable::factoryIdOf(path);
                !id.empty() && wavetable::factoryTable(id) != nullptr)
            {
                layer.tableId = id;
                layer.note = name;
                addLayer(std::move(layer));
                return;
            }
            for (int f = 0; f < table.frames(); ++f)
                layer.frames.insert(layer.frames.end(), table.samples(f), table.samples(f) + N);
            layer.note = name;
            addLayer(std::move(layer));
            return;
        }
        if (audio_)
            useAudio();
        if (loadTableFile(ctx_, osc_, file))
            follow(); // the module's new table, a recipe of it
        requestShow();
        return;
    }
    const auto audio = wavetable::readMono(path, &error);
    if (!audio)
    {
        warn("Couldn't read the audio", file.getFileName() + ": " + juce::String(error));
        return;
    }
    takeAudio(audio->samples, audio->sampleRate, file.getFileNameWithoutExtension(), asLayer);
}

void TableEditor::startImport(bool asLayer)
{
    chooser_ = std::make_unique<juce::FileChooser>(asLayer ? "Add audio or a table as a layer"
                                                           : "Import a wavetable",
                                                   wavetablesFolder(ctx_), tableAndAudioFiles);
    juce::Component::SafePointer<TableEditor> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self, asLayer](const juce::FileChooser& chooser)
                          {
                              if (self != nullptr && chooser.getResult() != juce::File())
                                  self->openFile(chooser.getResult(), asLayer);
                          });
}

void TableEditor::startExport()
{
    exportTable(*this, ctx_, osc_, chooser_);
}

void TableEditor::importImage(bool asHarmonics)
{
    chooser_ = std::make_unique<juce::FileChooser>("Add an image as a layer", juce::File(),
                                                   "*.png;*.jpg;*.jpeg;*.gif;*.bmp");
    juce::Component::SafePointer<TableEditor> self(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self, asHarmonics](const juce::FileChooser& chooser)
        {
            if (self == nullptr || chooser.getResult() == juce::File())
                return;
            const auto file = chooser.getResult();
            const auto image = juce::ImageFileFormat::loadFrom(file);
            if (!image.isValid())
            {
                warn("Couldn't read the image", file.getFileName());
                return;
            }
            // Its brightness, row by row from the top.
            const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
            std::vector<float> brightness(static_cast<size_t>(image.getWidth()) *
                                          static_cast<size_t>(image.getHeight()));
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    brightness[static_cast<size_t>(y) * static_cast<size_t>(image.getWidth()) +
                               static_cast<size_t>(x)] = pixels.getPixelColour(x, y).getPerceivedBrightness();
            self->addLayer(wavetable::imageLayer(brightness.data(), image.getWidth(), image.getHeight(),
                                                 asHarmonics,
                                                 file.getFileNameWithoutExtension().toStdString()));
        });
}

bool TableEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;wt;aif;aiff;flac;mp3");
}

void TableEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    // On the layers: added as a layer; anywhere else: a new table.
    const bool onLayers = paneBounds(layersPane_).contains(x, y);
    if (!files.isEmpty())
        openFile(juce::File(files[0]), onLayers);
}
} // namespace fd::devices::wavetable_ui
