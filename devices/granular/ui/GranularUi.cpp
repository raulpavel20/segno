#include "GranularUi.h"

namespace fd::devices::granular_ui
{
namespace
{
float plain(const ui::DeviceUiContext& ctx, size_t index)
{
    return ctx.params.getPlain(index);
}

const G* granular(const ui::DeviceUiContext& ctx)
{
    return dynamic_cast<const G*>(ctx.device);
}

bool changed(std::vector<float>& last, const std::vector<float>& now)
{
    if (now == last)
        return false;
    last = now;
    return true;
}

/// The semitones a grain's pitch is drawn across, up and down from the middle.
constexpr float kPitchSpan = 24.0f;

/// A note's name with its octave (middle C, 60, is C3, as the parameters write notes).
juce::String noteName(double note)
{
    static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const long n = std::lround(note);
    return juce::String(names[((n % 12) + 12) % 12]) +
           juce::String(static_cast<int>(std::floor(n / 12.0)) - 2);
}

juce::String fileName(const std::string& path)
{
    return juce::File::createFileWithoutCheckingPath(juce::String::fromUTF8(path.c_str()))
        .getFileNameWithoutExtension();
}
} // namespace

void loadSourceFile(ui::DeviceUiContext& ctx, int n, const juce::File& file)
{
    const std::string key = "src" + std::to_string(n + 1) + ".";
    ctx.params.beginEdit("Load " + file.getFileNameWithoutExtension() + " into Source " +
                         juce::String(n + 1));
    ctx.params.applyExtraState({{key + "path", file.getFullPathName().toStdString()},
                                {key + "name", file.getFileNameWithoutExtension().toStdString()}});
}

// ---- SourceDisplay -------------------------------------------------------------------------------

SourceDisplay::SourceDisplay(ui::DeviceUiContext& context) : ctx_(context)
{
    setTooltip(
        "Drag for the position; drag S and E for the part grains come from; drop a sample, a clip or a "
        "Library instrument");
}

void SourceDisplay::setSource(int n)
{
    source_ = n;
    last_.clear();
    refresh();
    repaint();
}

bool SourceDisplay::live() const
{
    return plain(ctx_, G::source(source_, G::SrcInput)) >= 0.5f;
}

std::shared_ptr<const GrainSource> SourceDisplay::shown() const
{
    const auto* g = granular(ctx_);
    if (g == nullptr)
        return nullptr;
    return live() ? g->liveView() : g->sourceData(source_);
}

void SourceDisplay::refresh()
{
    std::vector<float> now;
    for (const auto p : {G::SrcInput, G::SrcStart, G::SrcEnd, G::SrcOn})
        now.push_back(plain(ctx_, G::source(source_, p)));
    for (const auto p : {G::Pos, G::PosSpray, G::PosDist, G::SrcMix})
        now.push_back(plain(ctx_, p));
    if (ctx_.analysis != nullptr)
        now.push_back(ctx_.analysis->read(G::Scan1 + source_));
    bool dirty = changed(last_, now);
    if (auto data = shown(); data != lastData_)
    {
        lastData_ = std::move(data);
        dirty = true;
    }
    if (const auto* g = granular(ctx_))
    {
        auto grains = g->grains();
        if (grains.size() != grains_.size() ||
            !std::equal(grains.begin(), grains.end(), grains_.begin(), [](const auto& a, const auto& b)
                        { return a.position == b.position && a.pitch == b.pitch && a.level == b.level; }))
        {
            grains_ = std::move(grains);
            dirty = true;
        }
    }
    if (dirty)
        repaint();
}

juce::Rectangle<int> SourceDisplay::textLine() const
{
    const auto& m = ui::Theme::current().deviceGrid;
    return getLocalBounds().reduced(m.displayInset, m.displayInset / 2).removeFromTop(m.displayTextHeight);
}

juce::Rectangle<float> SourceDisplay::waveArea() const
{
    const auto& m = ui::Theme::current().deviceGrid;
    return getLocalBounds()
        .toFloat()
        .reduced(static_cast<float>(m.displayInset), static_cast<float>(m.displayInset) * 0.5f)
        .withTrimmedTop(static_cast<float>(m.displayTextHeight));
}

float SourceDisplay::xOf(double place) const
{
    const auto w = waveArea();
    return w.getX() + static_cast<float>(std::clamp(place, 0.0, 1.0)) * w.getWidth();
}

double SourceDisplay::placeAt(float x) const
{
    const auto w = waveArea();
    return std::clamp(static_cast<double>((x - w.getX()) / std::max(1.0f, w.getWidth())), 0.0, 1.0);
}

double SourceDisplay::partStart() const
{
    return plain(ctx_, G::source(source_, G::SrcStart)) * 0.01;
}

double SourceDisplay::partEnd() const
{
    return std::max(partStart(), static_cast<double>(plain(ctx_, G::source(source_, G::SrcEnd))) * 0.01);
}

double SourceDisplay::position() const
{
    // Across the part; in the live buffer, back from its newest moment at the right.
    const double pos = plain(ctx_, G::Pos) * 0.01;
    if (live())
        return 1.0 - pos;
    return partStart() + pos * (partEnd() - partStart());
}

std::optional<size_t> SourceDisplay::partAt(float x) const
{
    if (shown() == nullptr)
        return std::nullopt;
    const float near = static_cast<float>(ui::Theme::current().deviceGrid.displayHandle) * 0.5f;
    if (!live())
    {
        if (std::abs(x - xOf(partStart())) <= near)
            return G::source(source_, G::SrcStart);
        if (std::abs(x - xOf(partEnd())) <= near)
            return G::source(source_, G::SrcEnd);
    }
    return G::Pos;
}

void SourceDisplay::paintWave(juce::Graphics& g, const GrainSource& s, juce::Rectangle<float> lane,
                              int channel, juce::Colour inside) const
{
    const auto& t = ui::Theme::current();
    const auto& lo = s.overviewMin[static_cast<size_t>(channel)];
    const auto& hi = s.overviewMax[static_cast<size_t>(channel)];
    if (lo.empty())
        return;
    const float mid = lane.getCentreY(), half = lane.getHeight() * 0.5f;
    const double from = live() ? 0.0 : partStart(), to = live() ? 1.0 : partEnd();
    const int width = static_cast<int>(lane.getWidth());
    for (int x = 0; x < width; ++x)
    {
        const double p0 = static_cast<double>(x) / width, p1 = static_cast<double>(x + 1) / width;
        const auto b0 = static_cast<size_t>(p0 * static_cast<double>(lo.size()));
        const auto b1 = std::max(b0 + 1, static_cast<size_t>(p1 * static_cast<double>(lo.size())));
        float mn = 0.0f, mx = 0.0f;
        for (size_t b = b0; b < std::min(b1, lo.size()); ++b)
        {
            mn = std::min(mn, lo[b]);
            mx = std::max(mx, hi[b]);
        }
        g.setColour(p1 > from && p0 < to ? inside : t.roles.outside);
        g.fillRect(lane.getX() + static_cast<float>(x), mid - mx * half, 1.0f,
                   std::max(1.0f, (mx - mn) * half));
    }
}

void SourceDisplay::paintMarker(juce::Graphics& g, double place, const char* tag, bool right) const
{
    // As the Sampler's Start and End: a line, and its letter on a tag at the top.
    const auto& t = ui::Theme::current();
    const auto w = waveArea();
    const float x = xOf(place), size = static_cast<float>(t.deviceGrid.displayHandle);
    g.setColour(t.textPrimary);
    g.fillRect(x - 0.5f, w.getY(), 1.0f, w.getHeight());
    const auto box = juce::Rectangle<float>(size, size).withPosition(right ? x - size : x, w.getY());
    g.fillRect(box);
    g.setColour(t.surface0);
    g.setFont(ui::font(ui::TextStyle::Label));
    g.drawText(tag, box, juce::Justification::centred, false);
}

void SourceDisplay::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto r = getLocalBounds().toFloat();
    g.setColour(dragOver_ ? t.roles.dropWell : t.surface0);
    g.fillRoundedRectangle(r, t.radiusControl);
    g.setFont(ui::font(ui::TextStyle::Label));
    const auto data = shown();
    if (data == nullptr)
    {
        g.setColour(t.textSecondary);
        g.drawFittedText(
            live() ? juce::String("No input yet: choose a track in the header's Input")
                   : juce::String("Drop a sample, a clip or a Library instrument, or click to browse"),
            getLocalBounds().reduced(t.deviceGrid.displayInset), juce::Justification::centred, 3);
        return;
    }

    // The waveform, both channels, behind the grains; dimmer outside the part.
    const auto w = waveArea();
    const auto colour = source_ == 0 ? t.value : t.roles.secondSource;
    if (data->numChannels > 1)
    {
        auto lanes = w;
        const auto top = lanes.removeFromTop(lanes.getHeight() * 0.5f);
        paintWave(g, *data, top, 0, t.textTertiary);
        paintWave(g, *data, lanes, 1, t.textTertiary);
    }
    else
        paintWave(g, *data, w, 0, t.textTertiary);

    // The spray, Position, the playhead.
    const double pos = position();
    const double spray = plain(ctx_, G::PosSpray) * 0.01 * (live() ? 1.0 : partEnd() - partStart());
    const bool ahead = static_cast<int>(plain(ctx_, G::PosDist)) == 2;
    const double sprayFrom = ahead ? pos : pos - spray * 0.5,
                 sprayTo = ahead ? pos + spray : pos + spray * 0.5;
    if (spray > 0.0)
    {
        g.setColour(t.fill(colour));
        g.fillRect(
            juce::Rectangle<float>(xOf(sprayFrom), w.getY(), xOf(sprayTo) - xOf(sprayFrom), w.getHeight()));
    }
    g.setColour(colour);
    g.fillRect(xOf(pos) - 0.75f, w.getY(), 1.5f, w.getHeight());
    if (ctx_.analysis != nullptr)
        if (const float scan = ctx_.analysis->read(G::Scan1 + source_); scan >= 0.0f)
        {
            g.setColour(t.roles.marker);
            g.fillRect(xOf(partStart() + scan * (partEnd() - partStart())), w.getY(), 1.0f, w.getHeight());
        }

    // Every grain playing: where it reads across, its pitch up and down, its level as its
    // brightness, its source as its colour.
    const float dot = static_cast<float>(t.deviceGrid.displayHandle) * 0.5f;
    for (const auto& grain : grains_)
    {
        if (grain.source != source_)
            continue; // the other source's grains show on its own view
        const float y =
            w.getCentreY() - std::clamp(grain.pitch / kPitchSpan, -1.0f, 1.0f) * (w.getHeight() - dot) * 0.5f;
        const int step = grain.level > 0.6f ? 0 : grain.level > 0.3f ? 1 : 2;
        g.setColour(t.echo(grain.source == 0 ? t.value : t.roles.secondSource, step));
        const float size = dot * (0.6f + 0.4f * std::clamp(grain.level, 0.0f, 1.0f));
        g.fillEllipse(xOf(grain.position) - size * 0.5f, y - size * 0.5f, size, size);
    }

    if (!live())
    {
        paintMarker(g, partStart(), "S", false);
        paintMarker(g, partEnd(), "E", true);
    }

    // What it is: its length and root (the name is under it), or the live buffer's length.
    g.setColour(t.textSecondary);
    g.setFont(ui::font(ui::TextStyle::Label));
    const auto dotSep = juce::String::fromUTF8("  \xC2\xB7  ");
    juce::String text = live() ? "Live" + dotSep + juce::String(plain(ctx_, G::LiveLength), 1) + " s"
                               : juce::String(data->seconds(), 1) + " s";
    if (!live())
        text += dotSep + (data->root ? "Root " + noteName(*data->root) + (data->rootFound ? " found" : "")
                                     : juce::String("No root yet"));
    g.drawText(text, textLine(), juce::Justification::centredLeft, true);
}

void SourceDisplay::mouseMove(const juce::MouseEvent& e)
{
    const auto part = partAt(e.position.x);
    setMouseCursor(part && *part != G::Pos ? juce::MouseCursor::LeftRightResizeCursor
                                           : juce::MouseCursor::NormalCursor);
}

void SourceDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (shown() == nullptr)
    {
        if (onBrowse && !live())
            onBrowse();
        return;
    }
    dragging_ = partAt(e.position.x);
    if (dragging_)
    {
        ctx_.params.beginGesture(*dragging_);
        mouseDrag(e);
    }
}

void SourceDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    const double place = placeAt(e.position.x);
    float value = 0.0f;
    if (*dragging_ == G::Pos)
        value = static_cast<float>(live() ? (1.0 - place) * 100.0
                                          : (place - partStart()) / std::max(1e-6, partEnd() - partStart()) *
                                                100.0);
    else
        value = static_cast<float>(place * 100.0);
    ctx_.params.setPlain(*dragging_, std::clamp(value, 0.0f, 100.0f));
    refresh();
}

void SourceDisplay::mouseUp(const juce::MouseEvent&)
{
    if (dragging_)
        ctx_.params.endGesture(*dragging_);
    dragging_.reset();
}

bool SourceDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension(audioFiles);
}

void SourceDisplay::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver_ = true;
    repaint();
}

void SourceDisplay::fileDragExit(const juce::StringArray&)
{
    dragOver_ = false;
    repaint();
}

void SourceDisplay::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver_ = false;
    loadSourceFile(ctx_, source_, juce::File(files[0]));
    repaint();
}

// ---- SourcePicker --------------------------------------------------------------------------------

SourcePicker::SourcePicker(ui::DeviceUiContext& context) : ctx_(context)
{
    previous_.setTooltip("The previous sample in its folder");
    next_.setTooltip("The next sample in its folder");
    previous_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(1); };
    field_.setTooltip("The source: load another, or clear it");
    field_.onChange = [this]
    {
        const int id = field_.getSelectedId();
        if (id == 2)
            browse();
        else if (id == 3)
        {
            ctx_.params.beginEdit("Clear Source " + juce::String(source_ + 1));
            const std::string key = "src" + std::to_string(source_ + 1) + ".";
            ctx_.params.applyExtraState({{key + "path", nullptr}, {key + "name", nullptr}});
        }
        refresh();
    };
    for (auto* c : std::initializer_list<juce::Component*>{&previous_, &field_, &next_})
        addAndMakeVisible(*c);
    refresh();
}

void SourcePicker::setSource(int n)
{
    source_ = n;
    shownName_ = {};
    refresh();
}

void SourcePicker::refresh()
{
    const auto* g = granular(ctx_);
    const auto data = g != nullptr ? g->sourceData(source_) : nullptr;
    const auto name =
        data != nullptr ? juce::String::fromUTF8(data->name.c_str()) : juce::String("No sample");
    if (name == shownName_)
        return;
    shownName_ = name;
    field_.clear(juce::dontSendNotification);
    field_.addItem(name, 1);
    field_.addItem(juce::String::fromUTF8("Load…"), 2);
    field_.addItem("Clear", 3);
    field_.setItemEnabled(3, data != nullptr);
    field_.setSelectedId(1, juce::dontSendNotification);
    previous_.setEnabled(data != nullptr);
    next_.setEnabled(data != nullptr);
}

void SourcePicker::resized()
{
    // Previous, the name, next: the Wavetable's table picker's arrangement.
    auto r = getLocalBounds();
    const int button = r.getHeight();
    previous_.setBounds(r.removeFromLeft(button));
    next_.setBounds(r.removeFromRight(button));
    const int gap = ui::Theme::current().deviceGrid.columnGap;
    field_.setBounds(r.reduced(gap, 0));
}

void SourcePicker::browse()
{
    chooser_ = std::make_unique<juce::FileChooser>("Load a sample into Source " + juce::String(source_ + 1),
                                                   juce::File(), audioFiles);
    juce::Component::SafePointer<SourcePicker> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self](const juce::FileChooser& chooser)
                          {
                              if (self != nullptr && chooser.getResult() != juce::File())
                                  loadSourceFile(self->ctx_, self->source_, chooser.getResult());
                          });
}

void SourcePicker::step(int delta)
{
    const auto* g = granular(ctx_);
    const auto data = g != nullptr ? g->sourceData(source_) : nullptr;
    if (data == nullptr || data->path.empty())
        return;
    const auto file = juce::File(juce::String::fromUTF8(data->path.c_str()));
    auto files = file.getParentDirectory().findChildFiles(juce::File::findFiles, false, audioFiles);
    files.sort();
    const int at = files.indexOf(file);
    if (files.isEmpty() || at < 0)
        return;
    loadSourceFile(ctx_, source_, files[(at + delta + files.size()) % files.size()]);
}

// ---- WindowDisplay -------------------------------------------------------------------------------

WindowDisplay::WindowDisplay(ui::DeviceUiContext& context) : ctx_(context)
{
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    setTooltip("The grains' window: drag sideways for its skew, up and down for its flatness");
}

granular::GrainWindow WindowDisplay::window() const
{
    granular::GrainWindow w;
    w.shape = static_cast<granular::Window>(static_cast<int>(plain(ctx_, G::GrainWindow)));
    w.skew = plain(ctx_, G::GrainSkew) * 0.01f;
    w.flatness = plain(ctx_, G::GrainFlat) * 0.01f;
    // Square's fades (and the sharp windows' sharp side) are a millisecond of the grain.
    w.fade = std::clamp(1.0f / std::max(1.0f, plain(ctx_, G::GrainSize)), 0.002f, 0.5f);
    if (const auto* g = granular(ctx_))
        if (w.shape == granular::Window::Drawn)
        {
            drawn_ = g->drawnWindow().table();
            w.drawn = &drawn_;
        }
    return w;
}

void WindowDisplay::refresh()
{
    std::vector<float> now;
    for (const auto p : {G::GrainWindow, G::GrainSkew, G::GrainFlat, G::GrainSize})
        now.push_back(plain(ctx_, p));
    if (changed(last_, now))
        repaint();
}

void WindowDisplay::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    ui::paintDisplayWell(g, getLocalBounds().toFloat());
    const auto area = getLocalBounds().toFloat().reduced(static_cast<float>(t.deviceGrid.displayInset));
    const auto w = window();
    const int points = std::max(16, static_cast<int>(area.getWidth()));
    juce::Path line;
    for (int i = 0; i <= points; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(points);
        const juce::Point<float> pt(area.getX() + phase * area.getWidth(),
                                    area.getBottom() - w.at(phase) * area.getHeight());
        i == 0 ? line.startNewSubPath(pt) : line.lineTo(pt);
    }
    auto fill = line;
    fill.lineTo(area.getBottomRight());
    fill.lineTo(area.getBottomLeft());
    fill.closeSubPath();
    g.setColour(t.fill(t.value));
    g.fillPath(fill);
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
}

void WindowDisplay::mouseDown(const juce::MouseEvent&)
{
    startSkew_ = plain(ctx_, G::GrainSkew);
    startFlat_ = plain(ctx_, G::GrainFlat);
    ctx_.params.beginGesture(G::GrainSkew);
    ctx_.params.beginGesture(G::GrainFlat);
}

void WindowDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const auto& input = ui::Theme::current().input;
    const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    ctx_.params.setPlain(G::GrainSkew,
                         std::clamp(startSkew_ + static_cast<float>(e.getDistanceFromDragStartX()) * fine /
                                                     input.valueTravel * 200.0f,
                                    -100.0f, 100.0f));
    ctx_.params.setPlain(G::GrainFlat,
                         std::clamp(startFlat_ - static_cast<float>(e.getDistanceFromDragStartY()) * fine /
                                                     input.valueTravel * 100.0f,
                                    0.0f, 100.0f));
    refresh();
}

void WindowDisplay::mouseUp(const juce::MouseEvent&)
{
    ctx_.params.endGesture(G::GrainSkew);
    ctx_.params.endGesture(G::GrainFlat);
}

// ---- GrainFilterResponse -------------------------------------------------------------------------

GrainFilterResponse::GrainFilterResponse(ui::DeviceUiContext& context) : ctx_(context)
{
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    setTooltip("Each grain's filter, its cutoff's spray as a band: drag sideways for the cutoff, up and down "
               "for the "
               "resonance (Shift: finely)");
}

double GrainFilterResponse::magnitude(int type, double hz, double cutoff, double res) noexcept
{
    // Exactly as each grain's filter has it (the type's choice, 0 LP … 3 Resonator).
    return granular::grainFilterMagnitude(type + 1, hz, cutoff, res, 48000.0);
}

void GrainFilterResponse::refresh()
{
    std::vector<float> now;
    for (const auto p : {G::GFilterOn, G::GFilterType, G::GFilterCutoff, G::GFilterRes, G::GFilterSpray})
        now.push_back(plain(ctx_, p));
    if (changed(last_, now))
        repaint();
}

ui::SpectrumPlot GrainFilterResponse::plot() const
{
    return {
        getLocalBounds().toFloat().reduced(static_cast<float>(ui::Theme::current().deviceGrid.displayInset)),
        18.0f, -30.0f};
}

void GrainFilterResponse::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    ui::paintDisplayWell(g, getLocalBounds().toFloat());
    const auto p = plot();
    g.setColour(t.surface3);
    g.drawHorizontalLine(juce::roundToInt(p.yOf(0.0f)), p.area.getX(), p.area.getRight());
    const int type = static_cast<int>(plain(ctx_, G::GFilterType));
    const double cutoff = plain(ctx_, G::GFilterCutoff), res = plain(ctx_, G::GFilterRes) * 0.01,
                 spray = plain(ctx_, G::GFilterSpray);
    // The spray: where the grains' cutoffs land, a band either side.
    if (spray > 0.0)
    {
        const double lo = std::max(ui::SpectrumPlot::minHz, cutoff * std::exp2(-spray * 0.5)),
                     hi = std::min(ui::SpectrumPlot::maxHz, cutoff * std::exp2(spray * 0.5));
        g.setColour(t.fill(t.value));
        g.fillRect(
            juce::Rectangle<float>(p.xOf(lo), p.area.getY(), p.xOf(hi) - p.xOf(lo), p.area.getHeight()));
    }
    const int points = std::max(32, static_cast<int>(p.area.getWidth()));
    juce::Path line;
    for (int i = 0; i <= points; ++i)
    {
        const double hz =
            ui::SpectrumPlot::minHz *
            std::pow(ui::SpectrumPlot::maxHz / ui::SpectrumPlot::minHz, static_cast<double>(i) / points);
        const double m = magnitude(type, hz, cutoff, res);
        const juce::Point<float> pt(p.xOf(hz),
                                    p.yOf(static_cast<float>(20.0 * std::log10(std::max(m, 1e-6)))));
        i == 0 ? line.startNewSubPath(pt) : line.lineTo(pt);
    }
    g.setColour(t.value);
    g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    const double m = magnitude(type, cutoff, cutoff, res);
    ui::paintDisplayHandle(g,
                           {p.xOf(cutoff), p.yOf(static_cast<float>(20.0 * std::log10(std::max(m, 1e-6))))},
                           isMouseButtonDown());
}

void GrainFilterResponse::mouseDown(const juce::MouseEvent&)
{
    startCutoff_ = plain(ctx_, G::GFilterCutoff);
    startRes_ = plain(ctx_, G::GFilterRes);
    ctx_.params.beginGesture(G::GFilterCutoff);
    ctx_.params.beginGesture(G::GFilterRes);
    repaint();
}

void GrainFilterResponse::mouseDrag(const juce::MouseEvent& e)
{
    const auto& input = ui::Theme::current().input;
    const float fine = e.mods.isShiftDown() ? input.fineFactor : 1.0f;
    const double octaves = static_cast<double>(e.getDistanceFromDragStartX()) * fine /
                           plot().area.getWidth() * std::log2(1000.0);
    const auto& cutoff = ctx_.info.params[G::GFilterCutoff];
    ctx_.params.setPlain(G::GFilterCutoff, std::clamp(static_cast<float>(startCutoff_ * std::exp2(octaves)),
                                                      cutoff.min, cutoff.max));
    ctx_.params.setPlain(G::GFilterRes,
                         std::clamp(startRes_ - static_cast<float>(e.getDistanceFromDragStartY()) * fine /
                                                    input.valueTravel * 100.0f,
                                    0.0f, 100.0f));
    refresh();
}

void GrainFilterResponse::mouseUp(const juce::MouseEvent&)
{
    ctx_.params.endGesture(G::GFilterCutoff);
    ctx_.params.endGesture(G::GFilterRes);
    repaint();
}
} // namespace fd::devices::granular_ui
