#include "WavetableUi.h"

#include "TableFiles.h"

namespace fd::devices::wavetable_ui
{
namespace look
{
/// A display's well (the kit's).
void paintWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    ui::paintDisplayWell(g, r);
}

float inset()
{
    return static_cast<float>(ui::Theme::current().deviceGrid.displayInset);
}

/// A point dragged on a display (the kit's).
void paintHandle(juce::Graphics& g, juce::Point<float> at, bool hot)
{
    ui::paintDisplayHandle(g, at, hot);
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
} // namespace look

using namespace look;

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

/// The warps that shape oscillator n's cycle (in the cycle, and on its harmonics) at their
/// amounts, with the Remap curves they read; FM and the other warps from another source leave the
/// cycle alone.
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
        warps[static_cast<size_t>(k)] = {
            dsp::isCycleWarp(warp) || dsp::isSpectralWarp(warp) ? warp : dsp::Warp::Off,
            plain(ctx, W::osc(n, k == 0 ? W::OscWarp1Amt : W::OscWarp2Amt)) * 0.01f,
            &remaps[static_cast<size_t>(k)]};
    }
    return warps;
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
    if (onEdit)
        onEdit();
}

bool TableDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    return onFile && files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;wt;aif;aiff;flac;mp3");
}

void TableDisplay::filesDropped(const juce::StringArray& files, int, int)
{
    if (onFile && !files.isEmpty())
        onFile(juce::File(files[0]));
}

// ---- files ---------------------------------------------------------------------------------------

juce::File wavetablesFolder(const ui::DeviceUiContext& ctx)
{
    const auto root = ctx.segnoFolder ? ctx.segnoFolder() : ui::defaultPresetsFolder().getParentDirectory();
    return root.getChildFile("Wavetables");
}

namespace
{
void warn(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}
} // namespace

bool loadTableFile(ui::DeviceUiContext& ctx, int n, const juce::File& file)
{
    // A factory table's file (the Library's, ADR 0055): that factory table, by its ID.
    if (const auto id = wavetable::factoryIdOf(file.getFullPathName().toStdString());
        !id.empty() && wavetable::factoryTable(id) != nullptr)
    {
        ctx.params.beginEdit("Choose a table");
        ctx.params.applyExtraState(W::factoryTablePatch(n, id));
        return true;
    }
    std::string error;
    const auto read = wavetable::importTable(file.getFullPathName().toStdString(), 0, &error);
    if (!read)
    {
        warn("Couldn't import the table", file.getFileName() + ": " + juce::String(error));
        return false;
    }
    const auto table =
        dsp::Wavetable::fromCycles(read->samples.data(), read->frameLength, read->frames, true);
    ctx.params.beginEdit("Import a table");
    ctx.params.applyExtraState(W::userTablePatch(
        n, read->name.empty() ? file.getFileNameWithoutExtension().toStdString() : read->name, table));
    return true;
}

void exportTable(juce::Component& owner, ui::DeviceUiContext& ctx, int n,
                 std::unique_ptr<juce::FileChooser>& chooser)
{
    const auto* s = synth(ctx);
    if (s == nullptr)
        return;
    auto folder = wavetablesFolder(ctx);
    folder.createDirectory();
    const auto name = juce::File::createLegalFileName(juce::String(s->tableName(n)));
    chooser = std::make_unique<juce::FileChooser>("Export the wavetable", folder.getChildFile(name + ".wav"),
                                                  "*.wav;*.wt");
    juce::Component::SafePointer<juce::Component> alive(&owner);
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [alive, &ctx, n](const juce::FileChooser& c)
        {
            if (alive == nullptr || c.getResult() == juce::File())
                return;
            auto file = c.getResult();
            if (!file.hasFileExtension("wav;wt"))
                file = file.withFileExtension("wav");
            const auto* synth = dynamic_cast<const W*>(ctx.device);
            const auto table = synth != nullptr ? synth->table(n) : nullptr;
            std::string error = "the module has no table";
            const auto path = file.getFullPathName().toStdString();
            const bool ok = table != nullptr &&
                            (file.hasFileExtension("wt") ? wavetable::exportWt(*table, path, &error)
                                                         : wavetable::exportWav(*table, path, &error));
            if (!ok)
                warn("Couldn't export the wavetable", file.getFileName() + ": " + juce::String(error));
        });
}

// ---- TablePicker ---------------------------------------------------------------------------------

juce::PopupMenu factoryTableMenu(std::string_view current, std::function<void(std::string_view id)> chosen)
{
    juce::PopupMenu menu, sub;
    std::string_view category;
    const auto close = [&]
    {
        if (!category.empty())
            menu.addSubMenu(juce::String(std::string(category)), sub, true, nullptr,
                            [&]
                            {
                                for (const auto& t : wavetable::factoryTables())
                                    if (t.category == category && t.id == current)
                                        return true;
                                return false;
                            }());
        sub = {};
    };
    for (const auto& t : wavetable::factoryTables())
    {
        if (t.category != category)
        {
            close();
            category = t.category;
        }
        sub.addItem(juce::String(std::string(t.name)), true, t.id == current,
                    [chosen, id = std::string(t.id)] { chosen(id); });
    }
    close();
    return menu;
}

void TableField::showPopup()
{
    juce::PopupMenu menu;
    if (buildMenu)
        buildMenu(menu);
    menu.setLookAndFeel(&getLookAndFeel());
    juce::Component::SafePointer<TableField> self(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                       [self](int)
                       {
                           // Until hidePopup(), the box thinks its menu is still open.
                           if (self != nullptr)
                               self->hidePopup();
                       });
}

TablePicker::TablePicker(ui::DeviceUiContext& context) : ctx_(context)
{
    rebuildMenu();
    field_.setTooltip(
        "The oscillator's table: a factory one, or one of your own (a file, audio, or made in the "
        "editor)");
    field_.buildMenu = [this](juce::PopupMenu& menu)
    {
        menu = factoryTableMenu(shownId_, [this](std::string_view id) { choose(id); });
        menu.addSeparator();
        if (editable_)
            menu.addItem(juce::String::fromUTF8("Edit…"),
                         [this]
                         {
                             if (onEdit)
                                 onEdit();
                         });
        menu.addItem(juce::String::fromUTF8("Import…"), [this] { onImport ? onImport() : importFile(); });
    };
    previous_.setTooltip("Previous table");
    next_.setTooltip("Next table");
    previous_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(+1); };
    addAndMakeVisible(previous_);
    addAndMakeVisible(field_);
    addAndMakeVisible(next_);
}

void TablePicker::rebuildMenu()
{
    // The names the field shows; its menu is built when it opens.
    field_.clear(juce::dontSendNotification);
    const auto tables = wavetable::factoryTables();
    for (size_t i = 0; i < tables.size(); ++i)
        field_.addItem(juce::String(std::string(tables[i].name)), static_cast<int>(i) + 1);
    shownId_.clear();
    shownName_.clear();
}

void TablePicker::setEditable(bool editable)
{
    if (editable == editable_)
        return;
    editable_ = editable;
    rebuildMenu();
    refresh();
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
    shownId_.clear(); // the field shows the table it plays until a file is chosen
    refresh();
    chooser_ =
        std::make_unique<juce::FileChooser>("Import a wavetable", wavetablesFolder(ctx_), "*.wav;*.wt");
    juce::Component::SafePointer<TablePicker> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self](const juce::FileChooser& chooser)
                          {
                              if (self == nullptr || chooser.getResult() == juce::File())
                                  return;
                              loadTableFile(self->ctx_, self->osc_, chooser.getResult());
                              self->refresh();
                          });
}

// ---- curves (Remap's, an LFO's) ------------------------------------------------------------------

namespace
{
/// The kit's (shared with the LFO modulator, modulators.md §12.1).
std::vector<std::array<float, 3>> pointsOf(const W::Curve& c)
{
    return dsp::editablePoints(c);
}

using ui::curves::addOrRemove;
using ui::curves::movePoint;
using ui::curves::pointNear;

/// The curve's state keys, for an LFO ("lfo3") or a Remap ("osc1.remap2"): the kit's.
using ui::synth::curvePatch;

void paintCurve(juce::Graphics& g, const W::Curve& c, juce::Rectangle<float> r, bool handles, int hot)
{
    ui::paintDrawnCurve(g, c, r, handles, hot);
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

} // namespace fd::devices::wavetable_ui
