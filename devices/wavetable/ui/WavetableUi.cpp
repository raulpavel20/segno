#include "WavetableUi.h"

#include "TableFiles.h"

namespace fd::devices::wavetable_ui
{
namespace
{
float plain(const ui::DeviceUiContext& ctx, size_t index)
{
    return ctx.params.getPlain(index);
}

const W* synth(const ui::DeviceUiContext& ctx)
{
    return dynamic_cast<const W*>(ctx.device);
}

/// A parameter set as one gesture.
void set(ui::DeviceUiContext& ctx, size_t index, float value)
{
    ctx.params.beginGesture(index);
    ctx.params.setPlain(index, value);
    ctx.params.endGesture(index);
}

/// A display's well: flat, a step below the module's body.
void paintWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.surface0);
    g.fillRoundedRectangle(r, t.radiusControl);
}

float inset()
{
    return static_cast<float>(ui::Theme::current().deviceGrid.displayInset);
}

/// A point dragged on a display: filled `surface3`, ringed in the value colour; `active` while
/// held or hovered (device-panels.md, "Display handle").
void paintHandle(juce::Graphics& g, juce::Point<float> at, bool hot)
{
    const auto& t = ui::Theme::current();
    const auto size = static_cast<float>(t.deviceGrid.displayHandle);
    const auto r = juce::Rectangle<float>(size, size).withCentre(at);
    g.setColour(hot ? t.active : t.surface3);
    g.fillEllipse(r);
    g.setColour(hot ? t.active : t.value);
    g.drawEllipse(r.reduced(0.5f), 1.0f);
}

/// The cycle warps of oscillator n at their amounts (FM and the other warps from another source,
/// and the spectral ones, leave the cycle alone), with the Remap curves they read.
std::array<dsp::CycleRenderer::WarpSetting, 2> cycleWarps(const ui::DeviceUiContext& ctx, int n,
                                                          std::array<dsp::CurveTable, 2>& remaps)
{
    std::array<dsp::CycleRenderer::WarpSetting, 2> warps{};
    for (int k = 0; k < 2; ++k)
    {
        const auto warp = static_cast<dsp::Warp>(
            static_cast<int>(plain(ctx, W::osc(n, k == 0 ? W::OscWarp1 : W::OscWarp2))));
        if (const auto* s = synth(ctx))
            remaps[static_cast<size_t>(k)] = s->remapCurve(n, k).table();
        warps[static_cast<size_t>(k)] = {dsp::isCycleWarp(warp) ? warp : dsp::Warp::Off,
                                         plain(ctx, W::osc(n, k == 0 ? W::OscWarp1Amt : W::OscWarp2Amt)) *
                                             0.01f,
                                         &remaps[static_cast<size_t>(k)]};
    }
    return warps;
}

/// A cycle's samples as a line across `r`, its middle at the centre.
juce::Path cyclePath(const float* cycle, int size, juce::Rectangle<float> r, int points)
{
    juce::Path p;
    for (int i = 0; i <= points; ++i)
    {
        const auto at = std::min(size - 1, static_cast<int>(static_cast<double>(i) / points * size));
        const juce::Point<float> pt(r.getX() +
                                        r.getWidth() * static_cast<float>(i) / static_cast<float>(points),
                                    r.getCentreY() - cycle[at] * r.getHeight() * 0.5f);
        i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
    }
    return p;
}

/// A changed set of values (what a display draws from): true when any moved.
bool changed(std::vector<float>& last, const std::vector<float>& now)
{
    if (now == last)
        return false;
    last = now;
    return true;
}
} // namespace

// ---- Modulation ----------------------------------------------------------------------------------

std::vector<ui::ModulationModel::Route> Modulation::routes() const
{
    std::vector<Route> out;
    for (int n = 0; n < W::numRoutes; ++n)
    {
        const auto source = static_cast<int>(plain(ctx_, W::route(n, W::RouteSource)));
        const auto target = static_cast<int>(plain(ctx_, W::route(n, W::RouteTarget)));
        if (source <= 0 || target <= 0)
            continue;
        if (const auto param = W::targetParam(target))
            out.push_back(
                {n, source, *param, plain(ctx_, W::route(n, W::RouteAmount)) * 0.01f, bipolar(source)});
    }
    return out;
}

juce::String Modulation::sourceName(int source) const
{
    const auto& choices = ctx_.info.params[W::route(0, W::RouteSource)].choices;
    return source >= 0 && static_cast<size_t>(source) < choices.size()
               ? juce::String(std::string(choices[static_cast<size_t>(source)]))
               : juce::String();
}

int Modulation::freeSlot() const
{
    for (int n = 0; n < W::numRoutes; ++n)
        if (plain(ctx_, W::route(n, W::RouteSource)) < 0.5f)
            return n;
    return -1;
}

bool Modulation::isFull() const
{
    return freeSlot() < 0;
}

bool Modulation::addRoute(int source, size_t param)
{
    const int n = freeSlot();
    const auto target = W::targetOf(param);
    if (n < 0 || !target)
        return false;
    const auto& spec = ctx_.info.params[param];
    ctx_.params.beginEdit("Modulate " + juce::String(std::string(spec.name)) + " with " + sourceName(source));
    set(ctx_, W::route(n, W::RouteSource), static_cast<float>(source));
    set(ctx_, W::route(n, W::RouteTarget), static_cast<float>(*target));
    set(ctx_, W::route(n, W::RouteAmount), 50.0f);
    set(ctx_, W::route(n, W::RouteCurve), 0.0f);
    set(ctx_, W::route(n, W::RouteScale), 0.0f);
    return true;
}

bool Modulation::addFromMatrix()
{
    const int n = freeSlot();
    if (n < 0)
        return false;
    const int source = lastSource > 0 ? lastSource : static_cast<int>(W::Source::Lfo1);
    ctx_.params.beginEdit("Add a modulation route");
    set(ctx_, W::route(n, W::RouteSource), static_cast<float>(source));
    set(ctx_, W::route(n, W::RouteTarget), static_cast<float>(W::TargetNone));
    set(ctx_, W::route(n, W::RouteAmount), 50.0f);
    set(ctx_, W::route(n, W::RouteCurve), 0.0f);
    set(ctx_, W::route(n, W::RouteScale), 0.0f);
    return true;
}

void Modulation::removeRoute(int index)
{
    ctx_.params.beginEdit("Remove a modulation route");
    for (const auto p : {W::RouteSource, W::RouteTarget, W::RouteAmount, W::RouteCurve, W::RouteScale})
        set(ctx_, W::route(index, p), ctx_.info.params[W::route(index, p)].def);
}

void Modulation::beginAmount(int index)
{
    ctx_.params.beginGesture(W::route(index, W::RouteAmount));
}

void Modulation::setAmount(int index, float amount)
{
    ctx_.params.setPlain(W::route(index, W::RouteAmount), std::clamp(amount, -1.0f, 1.0f) * 100.0f);
}

void Modulation::endAmount(int index)
{
    ctx_.params.endGesture(W::route(index, W::RouteAmount));
}

std::optional<int> Modulation::routeOfParam(size_t param) const
{
    const size_t first = W::route(0, W::RouteSource), end = W::route(W::numRoutes, W::RouteSource);
    if (param < first || param >= end)
        return std::nullopt;
    return static_cast<int>((param - first) / W::RouteParamCount);
}

std::vector<ui::ModulationModel::SourceInfo> Modulation::sources() const
{
    // In the matrix's order, by group; a macro by its name.
    using S = W::Source;
    const auto* s = synth(ctx_);
    std::vector<SourceInfo> out;
    for (int id = 1; id < static_cast<int>(S::Count); ++id)
    {
        const auto source = static_cast<S>(id);
        const char* group = source < S::Lfo1       ? "Envelopes"
                            : source < S::Random1  ? "LFOs"
                            : source < S::Macro1   ? "Random"
                            : source < S::Velocity ? "Macros"
                                                   : "Playing";
        auto name = sourceName(id);
        if (source >= S::Macro1 && source < S::Velocity && s != nullptr)
            name = juce::String(s->macroName(id - static_cast<int>(S::Macro1)));
        out.push_back({id, name, group});
    }
    return out;
}

std::vector<int> Modulation::listed() const
{
    std::vector<int> out;
    for (int n = 0; n < W::numRoutes; ++n)
        if (plain(ctx_, W::route(n, W::RouteSource)) >= 0.5f)
            out.push_back(n);
    return out;
}

bool Modulation::bipolar(int source) noexcept
{
    using S = W::Source;
    const auto s = static_cast<S>(source);
    return (s >= S::Lfo1 && s < S::Macro1) || s == S::Note || s == S::PitchBend || s == S::NoteRandom;
}

// ---- TableDisplay --------------------------------------------------------------------------------

TableDisplay::TableDisplay(ui::DeviceUiContext& context) : ctx_(context)
{
    renderer_.prepare();
    cycle_.resize(dsp::CycleRenderer::bufferSize);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setTooltip("The oscillator's table, front to back: drag up and down to move through it (Position)");
}

void TableDisplay::setOscillator(int n)
{
    osc_ = n;
    frames_.clear();
    builtFor_ = nullptr;
    last_.clear();
    refresh();
    repaint();
}

const dsp::Wavetable* TableDisplay::table() const
{
    const auto* s = synth(ctx_);
    return s != nullptr ? s->table(osc_).get() : nullptr; // the synth keeps it while it's its table
}

void TableDisplay::refresh()
{
    // Where it plays: the newest note's position while one sounds, else Position.
    float position = plain(ctx_, W::osc(osc_, W::OscPos)) * 0.01f;
    if (ctx_.analysis != nullptr && ctx_.analysis->read(W::ActiveVoices) > 0.0f)
        position = ctx_.analysis->read(W::Osc1Position + osc_);
    std::vector<float> now{position};
    for (const auto p : {W::OscWarp1, W::OscWarp1Amt, W::OscWarp2, W::OscWarp2Amt})
        now.push_back(plain(ctx_, W::osc(osc_, p)));
    if (table() != builtFor_)
        frames_.clear();
    if (changed(last_, now) || frames_.empty())
    {
        shown_ = position;
        repaint();
    }
}

void TableDisplay::buildFrames(juce::Rectangle<float> area)
{
    frames_.clear();
    const auto* t = table();
    builtFor_ = t;
    if (t == nullptr)
        return;
    // Up to 24 frames, evenly through the table, the front one first; each a step up and to the
    // right of the one before.
    const int count = std::min(t->frames(), 24);
    depth_ = count > 1 ? juce::Point<float>(area.getWidth() * 0.3f, -area.getHeight() * 0.45f)
                       : juce::Point<float>();
    const auto frame = juce::Rectangle<float>(area.getX(), area.getY() - depth_.y, area.getWidth() - depth_.x,
                                              area.getHeight() + depth_.y);
    const int points = std::max(16, static_cast<int>(frame.getWidth() / 2.0f));
    for (int k = count; k-- > 0;) // back to front
    {
        const int f = count > 1
                          ? juce::roundToInt(static_cast<float>(k) * static_cast<float>(t->frames() - 1) /
                                             static_cast<float>(count - 1))
                          : 0;
        const float along = count > 1 ? static_cast<float>(k) / static_cast<float>(count - 1) : 0.0f;
        frames_.push_back(cyclePath(t->samples(f), dsp::Wavetable::frameSize,
                                    frame.translated(depth_.x * along, depth_.y * along).reduced(0.0f, 1.0f),
                                    points));
    }
}

void TableDisplay::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto well = getLocalBounds().toFloat();
    paintWell(g, well);
    const auto area = well.reduced(inset());
    const auto* table = this->table();
    if (table == nullptr)
        return;
    if (frames_.empty())
        buildFrames(area);

    // The frames, back to front, the farther the fainter.
    for (size_t i = 0; i < frames_.size(); ++i)
    {
        const float near =
            frames_.size() > 1 ? static_cast<float>(i) / static_cast<float>(frames_.size() - 1) : 1.0f;
        g.setColour(t.atDepth(t.textTertiary, near));
        g.strokePath(frames_[i], juce::PathStrokeType(1.0f));
    }

    // The cycle it plays, where it is: the table at the position, its warps applied.
    std::array<dsp::CurveTable, 2> remaps{};
    const auto warps = cycleWarps(ctx_, osc_, remaps);
    const float position = std::clamp(shown_, 0.0f, 1.0f);
    const auto frame = juce::Rectangle<float>(area.getX(), area.getY() - depth_.y, area.getWidth() - depth_.x,
                                              area.getHeight() + depth_.y)
                           .translated(depth_.x * position, depth_.y * position)
                           .reduced(0.0f, 1.0f);
    const int harmonics = std::clamp(static_cast<int>(frame.getWidth() / 2.0f), 16, 256);
    const int size = renderer_.render(*table, position, warps, harmonics, cycle_.data());
    const auto line = cyclePath(dsp::CycleRenderer::cycle(cycle_.data()), size, frame,
                                std::max(16, static_cast<int>(frame.getWidth())));
    auto fill = line;
    fill.lineTo(frame.getRight(), frame.getCentreY());
    fill.lineTo(frame.getX(), frame.getCentreY());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

void TableDisplay::mouseDown(const juce::MouseEvent&)
{
    dragStart_ = plain(ctx_, W::osc(osc_, W::OscPos));
    ctx_.params.beginGesture(W::osc(osc_, W::OscPos));
}

void TableDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const auto& input = ui::Theme::current().input;
    const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    const auto moved = static_cast<float>(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
    ctx_.params.setPlain(W::osc(osc_, W::OscPos),
                         std::clamp(dragStart_ + moved / input.valueTravel * 100.0f * fine, 0.0f, 100.0f));
    refresh();
}

void TableDisplay::mouseUp(const juce::MouseEvent&)
{
    ctx_.params.endGesture(W::osc(osc_, W::OscPos));
}

void TableDisplay::mouseDoubleClick(const juce::MouseEvent&)
{
    set(ctx_, W::osc(osc_, W::OscPos), ctx_.info.params[W::osc(osc_, W::OscPos)].def);
}

// ---- TablePicker ---------------------------------------------------------------------------------

namespace
{
constexpr int kImportItem = 10000;
}

TablePicker::TablePicker(ui::DeviceUiContext& context) : ctx_(context)
{
    // The factory tables by category, then Import.
    const auto tables = wavetable::factoryTables();
    std::string_view category;
    for (size_t i = 0; i < tables.size(); ++i)
    {
        if (tables[i].category != category)
        {
            category = tables[i].category;
            field_.addSectionHeading(juce::String(std::string(category)));
        }
        field_.addItem(juce::String(std::string(tables[i].name)), static_cast<int>(i) + 1);
    }
    field_.addSeparator();
    field_.addItem(juce::String::fromUTF8("Import a file…"), kImportItem);
    field_.setTooltip("The oscillator's table: a factory one, or a file of your own (WAV, .wt)");
    field_.onChange = [this]
    {
        const int id = field_.getSelectedId();
        if (id == kImportItem)
            importFile();
        else if (id > 0)
            choose(wavetable::factoryTables()[static_cast<size_t>(id - 1)].id);
    };
    previous_.setTooltip("Previous table");
    next_.setTooltip("Next table");
    previous_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(+1); };
    addAndMakeVisible(previous_);
    addAndMakeVisible(field_);
    addAndMakeVisible(next_);
}

void TablePicker::setOscillator(int n)
{
    osc_ = n;
    shownId_.clear();
    refresh();
}

void TablePicker::refresh()
{
    const auto* s = synth(ctx_);
    if (s == nullptr || field_.isPopupActive())
        return;
    const auto& id = s->tableId(osc_);
    const auto& name = s->tableName(osc_);
    if (id == shownId_ && name == shownName_)
        return;
    shownId_ = id;
    shownName_ = name;
    const auto tables = wavetable::factoryTables();
    for (size_t i = 0; i < tables.size(); ++i)
        if (tables[i].id == id)
        {
            field_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
            return;
        }
    field_.setText(juce::String(name), juce::dontSendNotification); // a table of your own
}

void TablePicker::resized()
{
    // As the presets' field: previous, the name, next (presets.md §6).
    const auto& t = ui::Theme::current();
    const int button = t.deviceGrid.headerButton, gap = t.shell.itemGap, row = t.deviceGrid.row;
    auto r = getLocalBounds();
    previous_.setBounds(r.removeFromLeft(button).withSizeKeepingCentre(button, button));
    r.removeFromLeft(gap);
    next_.setBounds(r.removeFromRight(button).withSizeKeepingCentre(button, button));
    r.removeFromRight(gap);
    field_.setBounds(r.withSizeKeepingCentre(r.getWidth(), row));
}

void TablePicker::step(int delta)
{
    const auto tables = wavetable::factoryTables();
    const auto n = static_cast<int>(tables.size());
    int at = -1;
    for (int i = 0; i < n; ++i)
        if (tables[static_cast<size_t>(i)].id == shownId_)
            at = i;
    const int next = at < 0 ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;
    choose(tables[static_cast<size_t>(next)].id);
}

void TablePicker::choose(std::string_view id)
{
    ctx_.params.beginEdit("Choose a table");
    ctx_.params.applyExtraState(W::factoryTablePatch(osc_, id));
    refresh();
}

void TablePicker::importFile()
{
    refresh(); // the field shows the table it plays until a file is chosen
    shownId_.clear();
    refresh();
    chooser_ = std::make_unique<juce::FileChooser>("Import a wavetable", juce::File(), "*.wav;*.wt");
    juce::Component::SafePointer<TablePicker> self(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self](const juce::FileChooser& chooser)
        {
            if (self == nullptr || chooser.getResult() == juce::File())
                return;
            std::string error;
            const auto file = chooser.getResult();
            const auto read = wavetable::importTable(file.getFullPathName().toStdString(), 0, &error);
            if (!read)
            {
                juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                                 .withIconType(juce::MessageBoxIconType::WarningIcon)
                                                 .withTitle("Couldn't import the table")
                                                 .withMessage(file.getFileName() + ": " + juce::String(error))
                                                 .withButton("OK"),
                                             nullptr);
                return;
            }
            const auto table =
                dsp::Wavetable::fromCycles(read->samples.data(), read->frameLength, read->frames, true);
            self->ctx_.params.beginEdit("Import a table");
            self->ctx_.params.applyExtraState(W::userTablePatch(
                self->osc_,
                read->name.empty() ? file.getFileNameWithoutExtension().toStdString() : read->name, table));
            self->refresh();
        });
}

// ---- curves (Remap's, an LFO's) ------------------------------------------------------------------

namespace
{
/// A named shape as points to start editing from (the bends near its own curve).
std::vector<std::array<float, 3>> pointsOf(const W::Curve& c)
{
    if (!c.points.empty())
        return c.points;
    if (c.shape == "triangle")
        return {{0.0f, 0.5f, 0.0f}, {0.25f, 1.0f, 0.0f}, {0.75f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.0f}};
    if (c.shape == "saw-up")
        return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    if (c.shape == "saw-down")
        return {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    if (c.shape == "square")
        return {{0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    if (c.shape == "steps")
        return {{0.0f, 0.0f, 0.0f},        {0.25f, 0.0f, 0.0f},       {0.25f, 1.0f / 3.0f, 0.0f},
                {0.5f, 1.0f / 3.0f, 0.0f}, {0.5f, 2.0f / 3.0f, 0.0f}, {0.75f, 2.0f / 3.0f, 0.0f},
                {0.75f, 1.0f, 0.0f},       {1.0f, 1.0f, 0.0f}};
    // The sine: quarters bent towards its own roundness.
    return {{0.0f, 0.5f, 0.3f},
            {0.25f, 1.0f, -0.3f},
            {0.5f, 0.5f, 0.3f},
            {0.75f, 0.0f, -0.3f},
            {1.0f, 0.5f, 0.0f}};
}

/// A curve's line across `r` (0 at the bottom, 1 at the top), from the table the audio plays.
juce::Path curveLine(const W::Curve& c, juce::Rectangle<float> r)
{
    const auto table = c.table();
    juce::Path p;
    const auto n = table.size() - 1;
    for (size_t i = 0; i <= n; ++i)
    {
        const juce::Point<float> pt(r.getX() + r.getWidth() * static_cast<float>(i) / static_cast<float>(n),
                                    r.getBottom() - table[i] * r.getHeight());
        i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
    }
    return p;
}

juce::Point<float> pointAt(const std::array<float, 3>& p, juce::Rectangle<float> r)
{
    return {r.getX() + p[0] * r.getWidth(), r.getBottom() - p[1] * r.getHeight()};
}

/// The point under `at` (within a handle's reach), or -1.
int pointNear(const std::vector<std::array<float, 3>>& points, juce::Rectangle<float> r,
              juce::Point<float> at)
{
    const float reach = static_cast<float>(ui::Theme::current().deviceGrid.displayHandle);
    int best = -1;
    float nearest = reach;
    for (size_t i = 0; i < points.size(); ++i)
        if (const float d = pointAt(points[i], r).getDistanceFrom(at); d <= nearest)
        {
            nearest = d;
            best = static_cast<int>(i);
        }
    return best;
}

/// Moves point `i` to `at`, between its neighbours (the first and last stay at the ends).
void movePoint(std::vector<std::array<float, 3>>& points, size_t i, juce::Rectangle<float> r,
               juce::Point<float> at)
{
    float x = (at.x - r.getX()) / r.getWidth();
    const float lo = i == 0 ? 0.0f : points[i - 1][0], hi = i + 1 == points.size() ? 1.0f : points[i + 1][0];
    x = i == 0 ? 0.0f : (i + 1 == points.size() ? 1.0f : std::clamp(x, lo, hi));
    points[i][0] = x;
    points[i][1] = std::clamp((r.getBottom() - at.y) / r.getHeight(), 0.0f, 1.0f);
}

/// Adds a point on the curve at `at`'s x, or removes the one there (keeping two).
void addOrRemove(W::Curve& c, juce::Rectangle<float> r, juce::Point<float> at)
{
    c.points = pointsOf(c);
    if (const int i = pointNear(c.points, r, at); i > 0 && static_cast<size_t>(i) + 1 < c.points.size())
    {
        c.points.erase(c.points.begin() + i);
        return;
    }
    const float x = std::clamp((at.x - r.getX()) / r.getWidth(), 0.0f, 1.0f);
    const auto table = c.table();
    const float y = dsp::lookup(table, x);
    const auto it = std::upper_bound(c.points.begin(), c.points.end(), x,
                                     [](float v, const std::array<float, 3>& p) { return v < p[0]; });
    c.points.insert(it, {x, y, 0.0f});
}

/// The curve's state keys, for an LFO ("lfo3") or a Remap ("osc1.remap2").
nlohmann::json curvePatch(const std::string& key, const W::Curve& c)
{
    nlohmann::json patch;
    if (c.points.empty())
    {
        patch[key + ".points"] = nullptr;
        patch[key + ".shape"] = c.shape;
    }
    else
    {
        patch[key + ".points"] = c.points;
        patch[key + ".shape"] = nullptr;
    }
    return patch;
}

void paintCurve(juce::Graphics& g, const W::Curve& c, juce::Rectangle<float> r, bool handles, int hot)
{
    const auto& t = ui::Theme::current();
    const auto line = curveLine(c, r);
    auto fill = line;
    fill.lineTo(r.getBottomRight());
    fill.lineTo(r.getBottomLeft());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    if (handles)
        for (size_t i = 0; i < c.points.size(); ++i)
            paintHandle(g, pointAt(c.points[i], r), static_cast<int>(i) == hot);
}
} // namespace

// ---- CycleDisplay --------------------------------------------------------------------------------

CycleDisplay::CycleDisplay(ui::DeviceUiContext& context) : ctx_(context)
{
    renderer_.prepare();
    cycle_.resize(dsp::CycleRenderer::bufferSize);
}

void CycleDisplay::setOscillator(int n)
{
    osc_ = n;
    last_.clear();
    refresh();
    repaint();
}

int CycleDisplay::remapWarp() const
{
    for (int k = 0; k < 2; ++k)
        if (static_cast<int>(plain(ctx_, W::osc(osc_, k == 0 ? W::OscWarp1 : W::OscWarp2))) ==
            static_cast<int>(dsp::Warp::Remap))
            return k;
    return -1;
}

void CycleDisplay::refresh()
{
    std::vector<float> now{plain(ctx_, W::osc(osc_, W::OscPos))};
    for (const auto p : {W::OscWarp1, W::OscWarp1Amt, W::OscWarp2, W::OscWarp2Amt})
        now.push_back(plain(ctx_, W::osc(osc_, p)));
    if (const auto* s = synth(ctx_))
    {
        now.push_back(static_cast<float>(reinterpret_cast<std::uintptr_t>(s->table(osc_).get()) & 0xFFFFFF));
        for (int k = 0; k < 2; ++k)
            for (const auto& p : s->remapCurve(osc_, k).points)
                now.insert(now.end(), p.begin(), p.end());
    }
    const bool remap = remapWarp() >= 0;
    setMouseCursor(remap ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    setTooltip(remap ? "The Remap curve: where in the cycle each moment reads. Drag a point; double-click to "
                       "add or remove one"
                     : "The cycle the warps make");
    if (changed(last_, now))
        repaint();
}

juce::Rectangle<float> CycleDisplay::plot() const
{
    // The Remap curve's handles whole inside the well.
    return getLocalBounds().toFloat().reduced(
        inset() + static_cast<float>(ui::Theme::current().deviceGrid.displayHandle) * 0.5f);
}

W::Curve CycleDisplay::curve() const
{
    const auto* s = synth(ctx_);
    const int k = remapWarp();
    return s != nullptr && k >= 0 ? s->remapCurve(osc_, k) : W::Curve{"saw-up", {}};
}

void CycleDisplay::setCurve(const W::Curve& c)
{
    const int k = remapWarp();
    if (k < 0)
        return;
    ctx_.params.beginEdit("Draw the Remap curve");
    ctx_.params.applyExtraState(
        curvePatch("osc" + std::to_string(osc_ + 1) + ".remap" + std::to_string(k + 1), c));
}

void CycleDisplay::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto well = getLocalBounds().toFloat();
    paintWell(g, well);
    const auto r = plot();
    const auto* s = synth(ctx_);
    if (s == nullptr || s->table(osc_) == nullptr)
        return;
    // The cycle the warps make, from the renderer the audio uses.
    std::array<dsp::CurveTable, 2> remaps{};
    const auto warps = cycleWarps(ctx_, osc_, remaps);
    const int harmonics = std::clamp(static_cast<int>(r.getWidth() / 2.0f), 16, 256);
    const int size = renderer_.render(*s->table(osc_), plain(ctx_, W::osc(osc_, W::OscPos)) * 0.01f, warps,
                                      harmonics, cycle_.data());
    const auto line = cyclePath(dsp::CycleRenderer::cycle(cycle_.data()), size, r,
                                std::max(16, static_cast<int>(r.getWidth())));
    const bool remap = remapWarp() >= 0;
    g.setColour(remap ? t.textTertiary : t.value);
    g.strokePath(line, juce::PathStrokeType(remap ? 1.0f : 1.5f, juce::PathStrokeType::curved));
    if (remap)
    {
        paintCurve(g, dragged_ >= 0 ? editing_ : W::Curve{"saw-up", pointsOf(curve())}, r, true, dragged_);
    }
}

void CycleDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (remapWarp() < 0)
        return;
    editing_ = W::Curve{"saw-up", pointsOf(curve())};
    dragged_ = pointNear(editing_.points, plot(), e.position);
}

void CycleDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged_ < 0)
        return;
    movePoint(editing_.points, static_cast<size_t>(dragged_), plot(), e.position);
    repaint();
}

void CycleDisplay::mouseUp(const juce::MouseEvent& e)
{
    if (dragged_ >= 0 && e.mouseWasDraggedSinceMouseDown())
        setCurve(editing_); // one undo step for the whole drag
    dragged_ = -1;
    repaint();
}

void CycleDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (remapWarp() < 0)
        return;
    auto c = curve();
    addOrRemove(c, plot(), e.position);
    setCurve(c);
}

// ---- SourceWave ----------------------------------------------------------------------------------

SourceWave::SourceWave(ui::DeviceUiContext& context, bool noise)
    : MiniDisplay(context, {},
                  noise ? std::vector<size_t>{W::NoiseType, W::NoiseColor}
                        : std::vector<size_t>{W::SubShape}),
      noise_(noise)
{
}

void SourceWave::drawContent(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = ui::Theme::current();
    const int n = std::max(16, static_cast<int>(area.getWidth()));
    juce::Path p;
    if (noise_)
    {
        // A moment of the noise itself, seeded so it holds still.
        dsp::ColouredNoise noise;
        noise.reset(1);
        const auto type = static_cast<dsp::ColouredNoise::Type>(static_cast<int>(param(W::NoiseType)));
        const float colour = param(W::NoiseColor) * 0.01f;
        std::vector<float> x(static_cast<size_t>(n) + 1);
        float peak = 1e-6f;
        for (auto& v : x)
        {
            v = noise.next(type, colour, 48000.0);
            peak = std::max(peak, std::abs(v));
        }
        for (int i = 0; i <= n; ++i)
        {
            const juce::Point<float> pt(
                area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(n),
                area.getCentreY() - x[static_cast<size_t>(i)] / peak * area.getHeight() * 0.45f);
            i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
        }
        g.setColour(t.value);
        g.strokePath(p, juce::PathStrokeType(1.0f));
        return;
    }
    // Two cycles of the sub's wave, as the audio path makes it.
    const auto mix = W::subShape(static_cast<int>(param(W::SubShape)));
    for (int i = 0; i <= n; ++i)
    {
        const double phase = std::fmod(2.0 * i / n, 1.0);
        const auto y = static_cast<float>(dsp::Oscillator::naiveAt(mix, std::min(phase, 0.99999)));
        const juce::Point<float> pt(area.getX() +
                                        area.getWidth() * static_cast<float>(i) / static_cast<float>(n),
                                    area.getCentreY() - y * area.getHeight() * 0.45f);
        i == 0 ? p.startNewSubPath(pt) : p.lineTo(pt);
    }
    g.setColour(t.value);
    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

// ---- FilterResponse ------------------------------------------------------------------------------

namespace
{
constexpr double kDisplayRate = 48000.0;
}

FilterResponse::FilterResponse(ui::DeviceUiContext& context) : ctx_(context)
{
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    setTooltip("Drag sideways for the cutoff, up and down for the resonance (Shift: finely)");
}

void FilterResponse::setFilter(int n)
{
    filter_ = n;
    last_.clear();
    refresh();
    repaint();
}

void FilterResponse::refresh()
{
    std::vector<float> now;
    for (int n = 0; n < W::numFilters; ++n)
        for (const auto p : {W::FilterOn, W::FilterType, W::FilterCutoff, W::FilterRes, W::FilterShape})
            now.push_back(plain(ctx_, W::filter(n, p)));
    if (changed(last_, now))
        repaint();
}

ui::SpectrumPlot FilterResponse::plot() const
{
    return {getLocalBounds().toFloat().reduced(inset()), 18.0f, -30.0f};
}

void FilterResponse::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    paintWell(g, getLocalBounds().toFloat());
    const auto p = plot();
    g.setColour(t.surface3);
    g.drawHorizontalLine(juce::roundToInt(p.yOf(0.0f)), p.area.getX(), p.area.getRight());
    // The other filter faint behind, the selected one in the value colour; one that's off dimmed.
    for (const int n : {1 - filter_, filter_})
    {
        const bool selected = n == filter_, on = plain(ctx_, W::filter(n, W::FilterOn)) >= 0.5f;
        if (!selected && !on)
            continue;
        const auto type =
            static_cast<dsp::VoiceFilter::Type>(static_cast<int>(plain(ctx_, W::filter(n, W::FilterType))));
        const double cutoff = plain(ctx_, W::filter(n, W::FilterCutoff)),
                     res = plain(ctx_, W::filter(n, W::FilterRes)) * 0.01,
                     shape = plain(ctx_, W::filter(n, W::FilterShape)) * 0.01;
        const int points = std::max(32, static_cast<int>(p.area.getWidth()));
        juce::Path line;
        for (int i = 0; i <= points; ++i)
        {
            const double hz =
                ui::SpectrumPlot::minHz *
                std::pow(ui::SpectrumPlot::maxHz / ui::SpectrumPlot::minHz, static_cast<double>(i) / points);
            const double m = dsp::VoiceFilter::magnitude(type, hz, cutoff, res, shape, kDisplayRate);
            const juce::Point<float> pt(p.xOf(hz),
                                        p.yOf(static_cast<float>(20.0 * std::log10(std::max(m, 1e-6)))));
            i == 0 ? line.startNewSubPath(pt) : line.lineTo(pt);
        }
        const auto colour = selected && on ? t.value : t.textTertiary;
        if (selected)
        {
            auto fill = line;
            fill.lineTo(p.area.getBottomRight());
            fill.lineTo(p.area.getBottomLeft());
            fill.closeSubPath();
            g.setColour(t.fill(colour));
            g.fillPath(fill);
        }
        g.setColour(colour);
        g.strokePath(line, juce::PathStrokeType(selected ? 1.5f : 1.0f, juce::PathStrokeType::curved));
        if (selected)
        {
            const double m = dsp::VoiceFilter::magnitude(type, cutoff, cutoff, res, shape, kDisplayRate);
            paintHandle(g, {p.xOf(cutoff), p.yOf(static_cast<float>(20.0 * std::log10(std::max(m, 1e-6))))},
                        isMouseButtonDown());
        }
    }
}

void FilterResponse::mouseDown(const juce::MouseEvent&)
{
    startCutoff_ = plain(ctx_, W::filter(filter_, W::FilterCutoff));
    startRes_ = plain(ctx_, W::filter(filter_, W::FilterRes));
    ctx_.params.beginGesture(W::filter(filter_, W::FilterCutoff));
    ctx_.params.beginGesture(W::filter(filter_, W::FilterRes));
    repaint();
}

void FilterResponse::mouseDrag(const juce::MouseEvent& e)
{
    const auto& input = ui::Theme::current().input;
    const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    const auto p = plot();
    const double octaves =
        static_cast<double>(e.getDistanceFromDragStartX()) * fine / p.area.getWidth() * std::log2(1000.0);
    const auto& cutoff = ctx_.info.params[W::filter(filter_, W::FilterCutoff)];
    const auto& res = ctx_.info.params[W::filter(filter_, W::FilterRes)];
    ctx_.params.setPlain(
        W::filter(filter_, W::FilterCutoff),
        std::clamp(static_cast<float>(startCutoff_ * std::exp2(octaves)), cutoff.min, cutoff.max));
    ctx_.params.setPlain(W::filter(filter_, W::FilterRes),
                         std::clamp(startRes_ - static_cast<float>(e.getDistanceFromDragStartY()) * fine /
                                                    input.valueTravel * 100.0f,
                                    res.min, res.max));
    refresh();
}

void FilterResponse::mouseUp(const juce::MouseEvent&)
{
    ctx_.params.endGesture(W::filter(filter_, W::FilterCutoff));
    ctx_.params.endGesture(W::filter(filter_, W::FilterRes));
    repaint();
}

// ---- EnvelopeEditor ------------------------------------------------------------------------------

EnvelopeEditor::EnvelopeEditor(ui::DeviceUiContext& context, Modulation& modulation)
    : ctx_(context), handle_(modulation, static_cast<int>(W::Source::AmpEnv))
{
    addAndMakeVisible(handle_);
    setTooltip("Drag the points for the times and the sustain; drag a segment up or down to bend it");
}

void EnvelopeEditor::setEnvelope(int n)
{
    env_ = n;
    handle_.setSource(static_cast<int>(W::Source::AmpEnv) + n);
    resized(); // the chip's name changed
    last_.clear();
    refresh();
    repaint();
}

void EnvelopeEditor::resized()
{
    // The source's handle, a chip, in the top-right corner, inside the well.
    const int size = ui::Theme::current().deviceGrid.sourceHandle;
    handle_.setBounds(getLocalBounds()
                          .reduced(static_cast<int>(inset()))
                          .removeFromTop(size)
                          .removeFromRight(handle_.preferredWidth()));
}

float EnvelopeEditor::param(W::EnvParam p) const
{
    return plain(ctx_, W::env(env_, p));
}

void EnvelopeEditor::refresh()
{
    std::vector<float> now;
    for (size_t p = 0; p < W::EnvParamCount; ++p)
        now.push_back(param(static_cast<W::EnvParam>(p)));
    if (changed(last_, now))
        repaint();
}

EnvelopeEditor::Layout EnvelopeEditor::layout() const
{
    // Below the handle's row; the segments as wide as the log of their times, the sustain a fixed
    // share (as the shared envelope display draws them).
    // The points' handles sit whole inside the well.
    Layout l;
    const auto& grid = ui::Theme::current().deviceGrid;
    l.area = getLocalBounds()
                 .toFloat()
                 .reduced(inset() + static_cast<float>(grid.displayHandle) * 0.5f)
                 .withTrimmedTop(static_cast<float>(grid.sourceHandle));
    const auto w = [](float ms) { return std::log1p(std::max(0.0f, ms) / 5.0f); };
    const float a = w(param(W::EnvAttack)), h = w(param(W::EnvHold)), d = w(param(W::EnvDecay)),
                r = w(param(W::EnvRelease));
    const float sustain = std::max(0.15f * (a + h + d + r), 0.5f);
    const float total = std::max(1e-3f, a + h + d + sustain + r);
    const float x0 = l.area.getX(), width = l.area.getWidth();
    l.attackEnd = x0 + width * a / total;
    l.holdEnd = l.attackEnd + width * h / total;
    l.decayEnd = l.holdEnd + width * d / total;
    l.sustainEnd = l.decayEnd + width * sustain / total;
    l.releaseEnd = x0 + width;
    return l;
}

juce::Point<float> EnvelopeEditor::pointOf(int part, const Layout& l) const
{
    const float sustainY = l.area.getBottom() - param(W::EnvSustain) * 0.01f * l.area.getHeight();
    switch (part)
    {
    case 0:
        return {l.attackEnd, l.area.getY()};
    case 1:
        return {l.decayEnd, sustainY};
    default:
        return {l.releaseEnd, l.area.getBottom()};
    }
}

int EnvelopeEditor::partAt(juce::Point<float> p) const
{
    const auto l = layout();
    const float reach = static_cast<float>(ui::Theme::current().deviceGrid.displayHandle);
    for (int part = 0; part < 3; ++part)
        if (pointOf(part, l).getDistanceFrom(p) <= reach)
            return part;
    if (!l.area.expanded(0.0f, reach).contains(p))
        return -1;
    if (p.x < l.attackEnd)
        return 3;
    if (p.x >= l.holdEnd && p.x < l.decayEnd)
        return 4;
    if (p.x >= l.sustainEnd)
        return 5;
    return -1;
}

void EnvelopeEditor::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    paintWell(g, getLocalBounds().toFloat());
    const auto l = layout();
    const auto& a = l.area;
    const float sustain = param(W::EnvSustain) * 0.01f;
    juce::Path line;
    line.startNewSubPath(a.getX(), a.getBottom());
    // Each segment from the envelope's own shape: rising, falling to the sustain, falling to zero.
    const auto segment = [&](float x0, float x1, float from, float to, float curve)
    {
        const int n = std::max(2, static_cast<int>(x1 - x0));
        for (int i = 1; i <= n; ++i)
        {
            const double u = static_cast<double>(i) / n;
            const auto y = static_cast<float>(from + (to - from) * dsp::CurvedEnvelope::shape(u, curve));
            line.lineTo(x0 + (x1 - x0) * static_cast<float>(u), a.getBottom() - y * a.getHeight());
        }
    };
    segment(a.getX(), l.attackEnd, 0.0f, 1.0f, param(W::EnvAttackCurve) * 0.01f);
    line.lineTo(l.holdEnd, a.getY());
    segment(l.holdEnd, l.decayEnd, 1.0f, sustain, param(W::EnvDecayCurve) * 0.01f);
    line.lineTo(l.sustainEnd, a.getBottom() - sustain * a.getHeight());
    segment(l.sustainEnd, l.releaseEnd, sustain, 0.0f, param(W::EnvReleaseCurve) * 0.01f);
    auto fill = line;
    fill.lineTo(a.getBottomRight());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    for (int part = 0; part < 3; ++part)
        paintHandle(g, pointOf(part, l), dragged_ == part);
}

void EnvelopeEditor::mouseMove(const juce::MouseEvent& e)
{
    const int part = partAt(e.position);
    setMouseCursor(part == 1               ? juce::MouseCursor::UpDownLeftRightResizeCursor
                   : part >= 0 && part < 3 ? juce::MouseCursor::LeftRightResizeCursor
                   : part >= 3             ? juce::MouseCursor::UpDownResizeCursor
                                           : juce::MouseCursor::NormalCursor);
}

namespace
{
/// The parameters a part of an envelope drags.
std::vector<W::EnvParam> partParams(int part)
{
    switch (part)
    {
    case 0:
        return {W::EnvAttack};
    case 1:
        return {W::EnvDecay, W::EnvSustain};
    case 2:
        return {W::EnvRelease};
    case 3:
        return {W::EnvAttackCurve};
    case 4:
        return {W::EnvDecayCurve};
    case 5:
        return {W::EnvReleaseCurve};
    default:
        return {};
    }
}
} // namespace

void EnvelopeEditor::mouseDown(const juce::MouseEvent& e)
{
    dragged_ = partAt(e.position);
    const auto params = partParams(dragged_);
    for (size_t i = 0; i < params.size(); ++i)
    {
        startValues_[i] = param(params[i]);
        ctx_.params.beginGesture(W::env(env_, params[i]));
    }
    repaint();
}

void EnvelopeEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged_ < 0)
        return;
    const auto& input = ui::Theme::current().input;
    const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    const auto dx = static_cast<float>(e.getDistanceFromDragStartX()) * fine,
               dy = static_cast<float>(e.getDistanceFromDragStartY()) * fine;
    const auto setTo = [&](W::EnvParam p, float v)
    {
        const auto& spec = ctx_.info.params[W::env(env_, p)];
        ctx_.params.setPlain(W::env(env_, p), std::clamp(v, spec.min, spec.max));
    };
    // Times double for every bend travel dragged; sustain and curves follow the pointer's height.
    const auto time = [&](float start) { return std::max(start, 1.0f) * std::exp2(dx / input.bendTravel); };
    const float height = layout().area.getHeight();
    switch (dragged_)
    {
    case 0:
        setTo(W::EnvAttack, time(startValues_[0]));
        break;
    case 1:
        setTo(W::EnvDecay, time(startValues_[0]));
        setTo(W::EnvSustain, startValues_[1] - dy / std::max(1.0f, height) * 100.0f);
        break;
    case 2:
        setTo(W::EnvRelease, time(startValues_[0]));
        break;
    default:
        // A falling segment bends the other way to a rising one under the same drag.
        setTo(partParams(dragged_)[0],
              startValues_[0] + (dragged_ == 3 ? -dy : dy) / input.bendTravel * 100.0f);
        break;
    }
    refresh();
}

void EnvelopeEditor::mouseUp(const juce::MouseEvent&)
{
    for (const auto p : partParams(dragged_))
        ctx_.params.endGesture(W::env(env_, p));
    dragged_ = -1;
    repaint();
}

// ---- ShapeEditor ---------------------------------------------------------------------------------

namespace
{
/// A named shape's name in its menu: "saw-up" reads "Saw up".
juce::String shapeName(std::string_view id)
{
    auto name = juce::String(std::string(id)).replaceCharacter('-', ' ');
    return name.substring(0, 1).toUpperCase() + name.substring(1);
}
} // namespace

ShapeEditor::ShapeEditor(ui::DeviceUiContext& context, Modulation& modulation)
    : ctx_(context), handle_(modulation, static_cast<int>(W::Source::Lfo1))
{
    shapes_.setTooltip("A shape to start from");
    shapes_.onClick = [this] { showShapes(); };
    addAndMakeVisible(handle_);
    addAndMakeVisible(shapes_);
    setSource(0);
}

void ShapeEditor::setSource(int n)
{
    source_ = n;
    handle_.setSource(isLfo() ? static_cast<int>(W::Source::Lfo1) + n
                              : static_cast<int>(W::Source::Random1) + n - W::numLfos);
    shapes_.setVisible(isLfo());
    resized(); // the chip's name changed
    setTooltip(isLfo()
                   ? "Drag a point; double-click to add or remove one; drag a segment up or down to bend it"
                   : "How the random source moves");
    setMouseCursor(isLfo() ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    lastState_.clear();
    refresh();
    repaint();
}

void ShapeEditor::resized()
{
    // The handle, a chip, in the top-right corner; the shapes' menu at the left.
    const int size = ui::Theme::current().deviceGrid.sourceHandle;
    auto top = getLocalBounds().reduced(static_cast<int>(inset())).removeFromTop(size);
    handle_.setBounds(top.removeFromRight(handle_.preferredWidth()));
    shapes_.setBounds(top.removeFromLeft(size));
}

juce::Rectangle<float> ShapeEditor::plot() const
{
    // Below the handle's row; the points' handles whole inside the well.
    const auto& grid = ui::Theme::current().deviceGrid;
    return getLocalBounds()
        .toFloat()
        .reduced(inset() + static_cast<float>(grid.displayHandle) * 0.5f)
        .withTrimmedTop(static_cast<float>(grid.sourceHandle));
}

W::Curve ShapeEditor::curve() const
{
    const auto* s = synth(ctx_);
    return s != nullptr && isLfo() ? s->lfoShape(source_) : W::Curve{};
}

void ShapeEditor::setCurve(const W::Curve& c)
{
    if (!isLfo())
        return;
    ctx_.params.beginEdit("Draw LFO " + juce::String(source_ + 1));
    ctx_.params.applyExtraState(curvePatch("lfo" + std::to_string(source_ + 1), c));
    lastState_.clear();
    refresh();
}

void ShapeEditor::refresh()
{
    // What it draws: the curve or the random source's mode, and the LFO's phase while it plays.
    std::string state = std::to_string(source_);
    if (isLfo())
    {
        const auto c = curve();
        state += c.shape;
        for (const auto& p : c.points)
            for (const float v : p)
                state += "," + std::to_string(v);
        phase_ = -1.0f;
        if (ctx_.analysis != nullptr && ctx_.analysis->read(W::ActiveVoices) > 0.0f)
            phase_ = ctx_.analysis->read(W::Lfo1Phase + source_);
        state += "@" + std::to_string(juce::roundToInt(phase_ * 500.0f));
    }
    else
        state += "m" + std::to_string(
                           static_cast<int>(plain(ctx_, W::random(source_ - W::numLfos, W::RandomMode))));
    if (state != lastState_)
    {
        lastState_ = state;
        repaint();
    }
}

void ShapeEditor::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    paintWell(g, getLocalBounds().toFloat());
    const auto r = plot();
    if (!isLfo())
    {
        // The random source itself, seeded, a few of its cycles across.
        dsp::RandomSource random;
        random.reset(7);
        const auto mode = static_cast<dsp::RandomSource::Mode>(
            static_cast<int>(plain(ctx_, W::random(source_ - W::numLfos, W::RandomMode))));
        const int n = std::max(16, static_cast<int>(r.getWidth()));
        const double perPoint = 6.0 / n;
        juce::Path line;
        for (int i = 0; i <= n; ++i)
        {
            if (mode == dsp::RandomSource::Mode::PerNote && i % (n / 3 + 1) == 0)
                random.noteOn();
            const float v = random.advance(mode, 1, perPoint);
            const juce::Point<float> pt(r.getX() +
                                            r.getWidth() * static_cast<float>(i) / static_cast<float>(n),
                                        r.getCentreY() - v * r.getHeight() * 0.5f);
            i == 0 ? line.startNewSubPath(pt) : line.lineTo(pt);
        }
        g.setColour(t.value);
        g.strokePath(line, juce::PathStrokeType(1.5f));
        return;
    }
    const bool editing = dragged_ >= 0 || bending_ >= 0;
    const auto c = editing ? editing_ : curve();
    paintCurve(g, c, r, !c.points.empty(), dragged_);
    if (phase_ >= 0.0f)
    {
        // Where it is now: a line through the curve, and a dot on it.
        const float x = r.getX() + phase_ * r.getWidth();
        g.setColour(t.textSecondary);
        g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        const float y = r.getBottom() - dsp::lookup(c.table(), phase_) * r.getHeight();
        const float dot = static_cast<float>(t.deviceGrid.displayHandle) * 0.5f;
        g.setColour(t.textPrimary);
        g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre({x, y}));
    }
}

int ShapeEditor::pointAt(juce::Point<float> p) const
{
    return pointNear(editing_.points, plot(), p);
}

void ShapeEditor::mouseDown(const juce::MouseEvent& e)
{
    if (!isLfo())
        return;
    editing_ = W::Curve{curve().shape, pointsOf(curve())};
    dragged_ = pointAt(e.position);
    bending_ = -1;
    if (dragged_ < 0)
    {
        // Between two points: bend that segment.
        const float x = (e.position.x - plot().getX()) / plot().getWidth();
        for (size_t k = 0; k + 1 < editing_.points.size(); ++k)
            if (x >= editing_.points[k][0] && x <= editing_.points[k + 1][0])
            {
                bending_ = static_cast<int>(k);
                bendStart_ = editing_.points[k][2];
                bendY_ = e.position.y;
                break;
            }
    }
    repaint();
}

void ShapeEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragged_ >= 0)
        movePoint(editing_.points, static_cast<size_t>(dragged_), plot(), e.position);
    else if (bending_ >= 0)
    {
        // Up bulges the segment up, whichever way it goes.
        const auto& p = editing_.points;
        const auto k = static_cast<size_t>(bending_);
        const float way = p[k + 1][1] >= p[k][1] ? 1.0f : -1.0f;
        editing_.points[k][2] = std::clamp(
            bendStart_ + way * (bendY_ - e.position.y) / ui::Theme::current().input.bendTravel, -1.0f, 1.0f);
    }
    repaint();
}

void ShapeEditor::mouseUp(const juce::MouseEvent& e)
{
    if ((dragged_ >= 0 || bending_ >= 0) && e.mouseWasDraggedSinceMouseDown())
        setCurve(editing_); // one undo step for the whole drag
    dragged_ = bending_ = -1;
    repaint();
}

void ShapeEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!isLfo())
        return;
    auto c = curve();
    addOrRemove(c, plot(), e.position);
    setCurve(c);
}

void ShapeEditor::showShapes()
{
    juce::PopupMenu menu;
    const auto named = W::namedShapes();
    const auto current = curve();
    for (size_t i = 0; i < named.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1, shapeName(named[i]), true,
                     current.points.empty() && current.shape == named[i]);
    juce::Component::SafePointer<ShapeEditor> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&shapes_),
                       [self](int chosen)
                       {
                           if (self == nullptr || chosen <= 0)
                               return;
                           self->setCurve(
                               W::Curve{std::string(W::namedShapes()[static_cast<size_t>(chosen - 1)]), {}});
                       });
}

// ---- MatrixList ----------------------------------------------------------------------------------

namespace
{
constexpr int kMatrixRows = 5; // routes showing at once, below the captions
struct MatrixColumn
{
    const char* caption;
    W::RouteParam param;
    int column, columns;
};
constexpr MatrixColumn kMatrixColumns[] = {{"Source", W::RouteSource, 0, 2},
                                           {"Target", W::RouteTarget, 2, 3},
                                           {"Amount", W::RouteAmount, 5, 2},
                                           {"Curve", W::RouteCurve, 7, 2},
                                           {"Scale", W::RouteScale, 9, 2}};
} // namespace

MatrixList::MatrixList(ui::DeviceUiContext& context, Modulation& modulation)
    : ctx_(context), modulation_(modulation)
{
    add_.setTooltip("Add a modulation route (or drag a source's handle onto a knob)");
    add_.onClick = [this]
    {
        if (!modulation_.addFromMatrix())
            add_.setTooltip("The matrix is full: remove a route first");
        refresh();
    };
    addAndMakeVisible(add_);
    rebuild();
}

MatrixList::~MatrixList() = default;

void MatrixList::rebuild()
{
    for (auto& c : controls_)
        removeChildComponent(c.get());
    controls_.clear();
    listed_ = modulation_.listed();
    first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(listed_.size()) - kMatrixRows));
    for (int row = 0; row < kMatrixRows && first_ + row < static_cast<int>(listed_.size()); ++row)
    {
        const int n = listed_[static_cast<size_t>(first_ + row)];
        for (const auto& col : kMatrixColumns)
        {
            const auto index = W::route(n, col.param);
            std::unique_ptr<juce::Component> control;
            if (col.param == W::RouteAmount || col.param == W::RouteCurve)
            {
                auto bar = std::make_unique<ui::ParamKnob>(ctx_, index);
                bar->setStyle(ui::Knob::Style::Bar);
                bar->setName({}); // the caption names the column
                control = std::move(bar);
            }
            else
                control = std::make_unique<ui::ParamComboBox>(ctx_, index);
            control->setBounds(ui::PanelSection::cellBounds(col.column, row + 1, col.columns, 1));
            addAndMakeVisible(*control);
            controls_.push_back(std::move(control));
        }
    }
    add_.setEnabled(!modulation_.isFull());
    // Which routes show, when there are more than fit: in the section's title.
    if (auto* section = findParentComponentOfClass<ui::PanelSection>())
        section->setSectionTitle(listed_.size() > static_cast<size_t>(kMatrixRows)
                                     ? "Matrix: " + juce::String(first_ + 1) +
                                           juce::String::fromUTF8("\u2013") +
                                           juce::String(first_ + kMatrixRows) + " of " +
                                           juce::String(static_cast<int>(listed_.size()))
                                     : juce::String("Matrix"));
    repaint();
}

void MatrixList::refresh()
{
    if (modulation_.listed() != listed_)
    {
        rebuild();
        return;
    }
    for (auto& c : controls_)
        if (auto* pc = dynamic_cast<ui::ParamControl*>(c.get()))
            pc->refresh();
    add_.setEnabled(!modulation_.isFull());
}

void MatrixList::resized()
{
    // Add at the end of the caption row.
    const int size = ui::Theme::current().deviceGrid.headerButton;
    add_.setBounds(
        ui::PanelSection::cellBounds(10, 0, 1, 1).removeFromRight(size).withSizeKeepingCentre(size, size));
}

void MatrixList::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int was = first_;
    first_ += wheel.deltaY < 0.0f ? 1 : (wheel.deltaY > 0.0f ? -1 : 0);
    first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(listed_.size()) - kMatrixRows));
    if (first_ != was)
        rebuild();
}

void MatrixList::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    for (const auto& col : kMatrixColumns)
        ui::paintCapsLabel(g, col.caption,
                           ui::PanelSection::cellBounds(col.column, 0, col.columns, 1).toFloat());
    if (listed_.empty())
    {
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(juce::String::fromUTF8("Drag a source\u2019s chip onto a knob, right-click a knob: "
                                          "Modulate with, or add a route with +"),
                   ui::PanelSection::cellBounds(0, 1, 11, kMatrixRows), juce::Justification::centred, false);
    }
}
} // namespace fd::devices::wavetable_ui
