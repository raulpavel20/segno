#include "SpectrumPanel.h"

#include "Spectrum.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>

namespace fd::devices
{
namespace
{
using S = Spectrum;
using Dash = ui::SpectrumPlot::Dash;
using dsp::SpectrumChannel;

juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

juce::String ellipsis()
{
    return juce::String::fromUTF8("\xE2\x80\xA6");
}

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;w64;aif;aiff;flac;mp3");
}

/// Peak labels that hold still (SPEC): a new set of peaks shows once it has lasted half a second.
class PeakLabels
{
public:
    struct Label
    {
        int band = 0;
        juce::String text;
    };

    /// `bands`: the peaks now, loudest first; `refine` gives a band's peak frequency.
    void update(const std::vector<int>& bands, double now, const std::function<double(int)>& refine)
    {
        std::vector<int> shown;
        for (const auto& l : shown_)
            shown.push_back(l.band);
        if (same(bands, shown))
        {
            pending_.reset();
            return;
        }
        if (!pending_ || !same(bands, pending_->bands))
        {
            pending_ = Pending{bands, now};
            return;
        }
        if (now - pending_->since < 0.5)
            return;
        shown_.clear();
        for (const int b : bands)
            shown_.push_back({b, utf8(peakLabel(refine(b)))});
        pending_.reset();
    }

    void clear()
    {
        shown_.clear();
        pending_.reset();
    }

    [[nodiscard]] const std::vector<Label>& shown() const noexcept { return shown_; }

private:
    /// The same peaks, in any order, give or take a band.
    static bool same(std::vector<int> a, std::vector<int> b)
    {
        if (a.size() != b.size())
            return false;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        for (size_t i = 0; i < a.size(); ++i)
            if (std::abs(a[i] - b[i]) > 1)
                return false;
        return true;
    }

    struct Pending
    {
        std::vector<int> bands;
        double since = 0.0;
    };
    std::vector<Label> shown_;
    std::optional<Pending> pending_;
};

/// The display (devices/spectrum/SPEC.md, "UI layout"): the input's spectrum and its peak hold,
/// the compared track with collisions shaded, the side, the snapshot and the reference, peak
/// labels, a legend, and the levels under the pointer. A dropped audio file becomes the reference.
class SpectrumDisplay final : public juce::Component,
                              public juce::SettableTooltipClient,
                              public juce::FileDragAndDropTarget,
                              public ui::ParamControl
{
public:
    explicit SpectrumDisplay(ui::DeviceUiContext& ctx)
        : ctx_(ctx), device_(dynamic_cast<const S*>(ctx.device))
    {
        setTooltip("Drop an audio file here to show its average spectrum as a reference");
        configure();
    }

    std::function<void(const juce::String& path)> onFileDropped;

    /// Parameter and state changes (the panel's refresh tick).
    void refresh() override
    {
        configure();
        auto snapshot = device_ != nullptr ? device_->snapshot() : std::nullopt;
        auto reference = device_ != nullptr ? device_->reference() : std::nullopt;
        const bool snapshotChanged =
            snapshot.has_value() != snapshot_.has_value() ||
            (snapshot && (snapshot->curve != snapshot_->curve || snapshot->channel != snapshot_->channel));
        const bool referenceChanged =
            reference.has_value() != reference_.has_value() ||
            (reference && (reference->curves != reference_->curves || reference->name != reference_->name));
        if (snapshotChanged)
            snapshot_ = std::move(snapshot);
        if (referenceChanged)
        {
            reference_ = std::move(reference);
            matched_ = false; // match the new one at once
        }
        const auto key = std::make_tuple(param(S::Slope), param(S::Range), param(S::Labels), param(S::Hold),
                                         param(S::Side));
        if (snapshotChanged || referenceChanged || key != last_)
        {
            last_ = key;
            repaint();
        }
        // The frame clock drives it on screen; this keeps it going where there's none (offscreen).
        tick(juce::Time::getMillisecondCounterHiRes() / 1000.0);
    }

    /// Every frame: takes what the taps carried since.
    void tick(double now)
    {
        const double elapsed = lastTick_ > 0.0 ? std::min(now - lastTick_, 0.5) : 0.0;
        lastTick_ = now;
        if (ctx_.analysis == nullptr)
            return;
        auto& bus = *ctx_.analysis;
        const bool side = sideApplies();
        bool fresh = drain(bus, S::InputLeft, S::InputRight,
                           [&](const float* l, const float* r, size_t n)
                           {
                               if (side)
                                   side_.push(l, r, n);
                               return main_.push(l, r, n);
                           });
        if (!side && side_.hasData())
            side_.reset();

        // The compared track: while one is chosen and its sound arrives.
        size_t compared = 0;
        fresh |= drain(bus, S::CompareLeft, S::CompareRight,
                       [&](const float* l, const float* r, size_t n)
                       {
                           compared += n;
                           return compare_.push(l, r, n);
                       });
        if (compared > 0)
            lastCompared_ = now;
        const bool comparing = compareChosen() && compare_.hasData() && now - lastCompared_ < 0.5;
        if (comparing != comparing_)
        {
            comparing_ = comparing;
            if (!comparing)
                compare_.reset();
            fresh = true;
        }
        if (!fresh)
            return;

        bands_ = main_.bands();
        if (comparing_)
            compareBands_ = compare_.bands();
        if (side_.hasData())
            sideBands_ = side_.bands();
        if (const auto hold = S::holdSeconds(param(S::Hold)))
            hold_.update(bands_, now - lastHold_, *hold);
        lastHold_ = now;
        const int count = S::labelCount(param(S::Labels));
        if (count > 0 && main_.hasData())
            labels_.update(dsp::spectrumPeaks(bands_, slope(), 2 * count, -S::rangeDb(param(S::Range))), now,
                           [this](int band) { return main_.peakNear(dsp::spectrum::bandHz(band)).hz; });
        else
            labels_.clear();
        // The reference follows what plays, slowly (over about 3 s), so it doesn't jump.
        if (reference_ && main_.hasData() && hasSignal())
        {
            const float target =
                dsp::spectrumMatchOffset(reference_->curves[static_cast<size_t>(channel())], bands_);
            offset_ = matched_
                          ? offset_ + (target - offset_) * static_cast<float>(1.0 - std::exp(-elapsed / 3.0))
                          : target;
            matched_ = true;
        }
        repaint();
    }

    /// The main curve now, for Freeze; nothing before any sound arrived.
    [[nodiscard]] std::optional<dsp::SpectrumCurve> current() const
    {
        return main_.hasData() ? std::optional(bands_) : std::nullopt;
    }

    [[nodiscard]] bool hasSignal() const
    {
        return main_.hasData() &&
               std::any_of(bands_.begin(), bands_.end(), [](float v) { return v > dsp::spectrum::floorDb; });
    }

    [[nodiscard]] SpectrumChannel channel() const
    {
        return static_cast<SpectrumChannel>(
            std::clamp(static_cast<int>(param(S::Channel)), 0, dsp::numSpectrumChannels - 1));
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(dragOver_ ? t.roles.dropWell : t.surface0);
        g.fillRoundedRectangle(bounds, t.radiusControl);
        const auto plot = plotArea();
        plot.paintGrid(g, S::rangeDb(param(S::Range)) / 5.0f);

        const auto curves = shownCurves();
        if (curves.size() == 1 && !main_.hasData())
        {
            ui::paintEmptyState(g, getLocalBounds(),
                                ctx_.analysis != nullptr ? "Play to see the spectrum" : "No signal here");
            return;
        }
        if (comparing_ && main_.hasData())
            paintCollisions(g, plot);
        for (const auto& c : curves)
        {
            if (c.kind == Kind::Main)
                continue;
            ui::SpectrumPlot::stroke(g, plot.line(*c.curve, c.offset), c.colour, c.dash,
                                     c.kind == Kind::Compare ? 1.5f : 1.0f);
        }
        if (main_.hasData())
        {
            const auto line = plot.line(bands_);
            g.setColour(t.roles.spectrumFill);
            g.fillPath(plot.fill(line));
            ui::SpectrumPlot::stroke(g, line, t.roles.spectrumLine);
            if (S::holdSeconds(param(S::Hold)) && hold_.hasData())
                ui::SpectrumPlot::stroke(g, plot.line(hold_.curve()), t.roles.spectrumPre);
        }
        if (comparing_) // on top: the track it's compared with
            ui::SpectrumPlot::stroke(g, plot.line(compareBands_), t.roles.spectrumCompare, Dash::Solid, 1.5f);
        paintLabels(g, plot, curves.size() > 1 || hover_.has_value());
        if (hover_)
            paintHover(g, plot, curves);
        else if (curves.size() > 1)
            paintLegend(g, curves);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        hover_ = e.position.x;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover_.reset();
        repaint();
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return files.size() == 1 && isAudioFile(files[0]);
    }

    void fileDragEnter(const juce::StringArray&, int, int) override { setDragOver(true); }
    void fileDragExit(const juce::StringArray&) override { setDragOver(false); }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        setDragOver(false);
        if (onFileDropped && !files.isEmpty())
            onFileDropped(files[0]);
    }

private:
    enum class Kind
    {
        Main,
        Compare,
        Side,
        Snapshot,
        Reference
    };

    struct Curve
    {
        Kind kind;
        juce::String name;
        const dsp::SpectrumCurve* curve;
        juce::Colour colour;
        Dash dash;
        float offset = 0.0f;
    };

    [[nodiscard]] float param(size_t index) const { return ctx_.params.getPlain(index); }
    [[nodiscard]] float slope() const { return S::slopeDb(param(S::Slope)); }

    [[nodiscard]] bool sideApplies() const
    {
        const auto c = channel();
        return param(S::Side) >= 0.5f && (c == SpectrumChannel::Both || c == SpectrumChannel::Mid);
    }

    [[nodiscard]] bool compareChosen() const
    {
        return ctx_.sidechain != nullptr && ctx_.sidechain->selected() > 0;
    }

    /// The compared track's name: its own in the DAW; "Sidechain" in a plugin.
    [[nodiscard]] juce::String compareName() const
    {
        if (ctx_.sidechain == nullptr)
            return "Sidechain";
        const auto sources = ctx_.sidechain->sources();
        const auto i = static_cast<size_t>(ctx_.sidechain->selected());
        return i < sources.size() && sources[i] != "External" ? utf8(sources[i]) : juce::String("Sidechain");
    }

    void configure()
    {
        const double rate = ctx_.analysis != nullptr ? ctx_.analysis->sampleRate() : 48000.0;
        const int order = S::fftOrder(param(S::Resolution));
        if (order != main_.order() || rate != main_.sampleRate())
        {
            for (auto* a : {&main_, &side_, &compare_})
                a->prepare(order, rate);
            hold_.reset();
            labels_.clear();
        }
        if (channel() != main_.channel())
        {
            hold_.reset();
            labels_.clear();
            matched_ = false;
        }
        main_.setChannel(channel());
        compare_.setChannel(channel());
        side_.setChannel(SpectrumChannel::Side);
        for (auto* a : {&main_, &side_, &compare_})
            a->setAveraging(param(S::Average));
    }

    /// Takes everything the pair of FIFOs holds, in step, into `feed`. Whether it transformed.
    template <typename Feed> bool drain(AnalysisBus& bus, int leftFifo, int rightFifo, Feed&& feed)
    {
        auto& l = bus.fifo(leftFifo);
        auto& r = bus.fifo(rightFifo);
        bool transformed = false;
        size_t n = std::min(l.available(), r.available());
        while (n > 0)
        {
            const size_t chunk = std::min(n, chunkL_.size());
            l.pop(chunkL_.data(), chunk);
            r.pop(chunkR_.data(), chunk);
            transformed |= feed(chunkL_.data(), chunkR_.data(), chunk);
            n -= chunk;
        }
        return transformed;
    }

    [[nodiscard]] ui::SpectrumPlot plotArea() const
    {
        return {.area = getLocalBounds().toFloat(),
                .topDb = 0.0f,
                .bottomDb = -S::rangeDb(param(S::Range)),
                .slope = slope()};
    }

    /// Back to front, every curve shown besides the input's own (the legend's order).
    [[nodiscard]] std::vector<Curve> shownCurves() const
    {
        const auto& r = ui::Theme::current().roles;
        std::vector<Curve> out{{Kind::Main, "Input", &bands_, r.spectrumLine, Dash::Solid}};
        if (comparing_)
            out.push_back({Kind::Compare, compareName(), &compareBands_, r.spectrumCompare, Dash::Solid});
        if (sideApplies() && side_.hasData())
            out.push_back({Kind::Side, "Side", &sideBands_, r.spectrumSide, Dash::Dotted});
        if (snapshot_)
        {
            const bool other = snapshot_->channel != static_cast<int>(channel());
            const auto name =
                "Snapshot" + (other ? " (" +
                                          juce::String(std::string(
                                              S::staticInfo().params[S::Channel].choices[static_cast<size_t>(
                                                  snapshot_->channel)])) +
                                          ")"
                                    : juce::String());
            out.push_back({Kind::Snapshot, name, &snapshot_->curve, r.spectrumSnapshot, Dash::Dashed});
        }
        if (reference_)
            out.push_back({Kind::Reference, utf8("Reference \xC2\xB7 " + reference_->name),
                           &reference_->curves[static_cast<size_t>(channel())], r.spectrumReference,
                           Dash::LongDashed, offset_});
        return out;
    }

    void paintCollisions(juce::Graphics& g, const ui::SpectrumPlot& plot) const
    {
        const auto collide = dsp::spectrumCollisions(bands_, compareBands_, slope());
        const double half = std::exp2(0.5 / dsp::spectrum::bandsPerOctave);
        g.setColour(ui::Theme::current().roles.spectrumCollision);
        for (int b = 0; b < dsp::spectrum::numBands;)
        {
            if (!collide[static_cast<size_t>(b)])
            {
                ++b;
                continue;
            }
            const int from = b;
            while (b < dsp::spectrum::numBands && collide[static_cast<size_t>(b)])
                ++b;
            const float x0 = plot.xOf(dsp::spectrum::bandHz(from) / half),
                        x1 = plot.xOf(dsp::spectrum::bandHz(b - 1) * half);
            g.fillRect(juce::Rectangle<float>(x0, plot.area.getY(), x1 - x0, plot.area.getHeight()));
        }
    }

    /// Frequency and note above the labelled peaks: louder ones first, up to the count, none on
    /// top of another or of the top line (`topLine`: the legend or the readout is there).
    void paintLabels(juce::Graphics& g, const ui::SpectrumPlot& plot, bool topLine) const
    {
        const auto count = static_cast<size_t>(S::labelCount(param(S::Labels)));
        if (!main_.hasData() || labels_.shown().empty() || count == 0)
            return;
        const auto& t = ui::Theme::current();
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        const auto height = static_cast<float>(t.deviceGrid.displayTextHeight);
        const auto font = ui::font(ui::TextStyle::ValueSmall);
        const float top = plot.area.getY() + (topLine ? height + inset : inset / 2.0f);
        std::vector<juce::Rectangle<float>> placed;
        g.setFont(font);
        for (const auto& label : labels_.shown())
        {
            if (placed.size() >= count)
                break;
            const auto pt = plot.pointOf(bands_, label.band);
            const float width = juce::GlyphArrangement::getStringWidth(font, label.text);
            auto box =
                juce::Rectangle<float>(width, height).withCentre({pt.x, pt.y - height / 2.0f - inset / 2.0f});
            box = box.withX(std::clamp(box.getX(), plot.area.getX() + inset,
                                       plot.area.getRight() - inset - width))
                      .withY(std::max(box.getY(), top));
            if (std::any_of(placed.begin(), placed.end(),
                            [&box, inset](const auto& p) { return p.expanded(inset, 0.0f).intersects(box); }))
                continue;
            placed.push_back(box);
            g.setColour(t.roles.shade); // readable over any curve behind it
            g.fillRoundedRectangle(box.expanded(inset / 2.0f, 0.0f), t.radiusControl);
            g.setColour(t.textSecondary);
            g.drawText(label.text, box, juce::Justification::centred, false);
        }
    }

    /// One entry per curve: a short line in its colour and dash, and its name.
    void paintLegend(juce::Graphics& g, const std::vector<Curve>& curves) const
    {
        const auto& t = ui::Theme::current();
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        const auto height = static_cast<float>(t.deviceGrid.displayTextHeight);
        const auto font = ui::font(ui::TextStyle::Caption);
        g.setFont(font);
        float x = inset;
        const float y = inset / 2.0f;
        for (const auto& c : curves)
        {
            juce::Path sample;
            sample.startNewSubPath(x, y + height / 2.0f);
            sample.lineTo(x + height, y + height / 2.0f);
            ui::SpectrumPlot::stroke(g, sample, c.kind == Kind::Main ? t.textTertiary : c.colour, c.dash,
                                     c.kind == Kind::Compare ? 1.5f : 1.0f);
            x += height + inset / 2.0f;
            const float width = juce::GlyphArrangement::getStringWidth(font, c.name);
            if (x + width > static_cast<float>(getWidth()) - inset)
                break; // what doesn't fit is left out, never squeezed
            g.setColour(t.textSecondary);
            g.drawText(c.name, juce::Rectangle<float>(x, y, width, height), juce::Justification::centredLeft,
                       false);
            x += width + 2.0f * inset;
        }
    }

    /// Under the pointer: a line, the frequency and note, and each curve's level there.
    void paintHover(juce::Graphics& g, const ui::SpectrumPlot& plot, const std::vector<Curve>& curves) const
    {
        const auto& t = ui::Theme::current();
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        const auto height = static_cast<float>(t.deviceGrid.displayTextHeight);
        g.setColour(t.roles.gridMinor);
        g.fillRect(std::round(*hover_), plot.area.getY(), 1.0f, plot.area.getHeight());
        const double hz = plot.hzAt(*hover_);
        const double at =
            std::clamp(dsp::spectrum::bandAt(hz), 0.0, static_cast<double>(dsp::spectrum::numBands - 1));
        auto levelOf = [&](const Curve& c)
        {
            const auto lo = static_cast<size_t>(at);
            const auto hi = std::min(lo + 1, static_cast<size_t>(dsp::spectrum::numBands - 1));
            const float a = (*c.curve)[lo], b = (*c.curve)[hi];
            if (a <= dsp::spectrum::floorDb || b <= dsp::spectrum::floorDb)
                return juce::String("-inf");
            const float v = a + (b - a) * static_cast<float>(at - static_cast<double>(lo)) + c.offset +
                            dsp::spectrum::tiltDb(hz, plot.slope);
            return juce::String(juce::roundToInt(v)) + " dB";
        };
        juce::String text = utf8(frequencyText(hz) + " \xC2\xB7 " + noteText(hz));
        for (const auto& c : curves)
            if (c.kind != Kind::Main || main_.hasData())
                text << "   " << (c.kind == Kind::Reference ? juce::String("Reference") : c.name) << " "
                     << levelOf(c);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.setColour(t.textSecondary);
        g.drawText(text,
                   juce::Rectangle<float>(inset, inset / 2.0f, static_cast<float>(getWidth()) - 2.0f * inset,
                                          height),
                   juce::Justification::centredLeft, true);
    }

    void setDragOver(bool over)
    {
        dragOver_ = over;
        repaint();
    }

    ui::DeviceUiContext& ctx_;
    const S* device_;
    dsp::SpectrumAnalyser main_, side_, compare_;
    dsp::SpectrumPeakHold hold_;
    dsp::SpectrumCurve bands_{}, compareBands_{}, sideBands_{};
    PeakLabels labels_;
    std::optional<S::Snapshot> snapshot_;
    std::optional<S::Reference> reference_;
    float offset_ = 0.0f;
    bool matched_ = false, comparing_ = false, dragOver_ = false;
    double lastTick_ = 0.0, lastHold_ = 0.0, lastCompared_ = -1.0;
    std::optional<float> hover_;
    std::tuple<float, float, float, float, float> last_{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    std::array<float, 4096> chunkL_{}, chunkR_{};
};

/// A line of text in the CURVES section: the reference's name, or what reading a file came to.
class StatusLine final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    /// The text now, and whether it's a problem.
    std::function<std::pair<juce::String, bool>()> source;

    void refresh() override
    {
        if (!source)
            return;
        const auto [text, problem] = source();
        if (text == text_ && problem == problem_)
            return;
        text_ = text;
        problem_ = problem;
        setTooltip(text); // the whole of a long name
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.setColour(problem_ ? t.roles.warning : t.textSecondary);
        g.drawText(text_, getLocalBounds(), juce::Justification::centredLeft, true);
    }

private:
    juce::String text_;
    bool problem_ = false;
};

/// Sections per devices/spectrum/SPEC.md, on the module grid (docs/design/device-panels.md).
class SpectrumPanel final : public ui::DevicePanel
{
public:
    explicit SpectrumPanel(ui::DeviceUiContext& ctx)
        : DevicePanel(ctx), device_(dynamic_cast<const S*>(ctx.device))
    {
        setSidechainCaption("Compare", "Compare: another track's spectrum, drawn with this one");

        auto& spectrum = addSection("spectrum", "Spectrum", 12);
        display_ = &own(std::make_unique<SpectrumDisplay>(ctx));
        spectrum.place(*display_, 0, 0, 12, 6);
        display_->onFileDropped = [this](const juce::String& path) { loadReference(path); };

        auto& curves = addSection("curves", "Curves", 4);
        auto& side = curves.toggle(S::Side, 0, 0, 2);
        auto& freeze =
            button(curves, "Freeze", "Freeze the curve as it is now, to compare before and after", 0, 1);
        auto& clearSnapshot = button(curves, "Clear", "Clear the snapshot", 2, 1);
        auto& load =
            button(curves, "Reference" + ellipsis(),
                   "Show a song's average spectrum, level-matched (or drop a file on the display)", 0, 2);
        auto& clearReference = button(curves, "Clear", "Clear the reference", 2, 2);
        status_ = &own(std::make_unique<StatusLine>());
        status_->source = [this]() -> std::pair<juce::String, bool>
        {
            if (reading_.isNotEmpty())
                return {reading_, false};
            if (problem_.isNotEmpty())
                return {problem_, true};
            const auto r = device_ != nullptr ? device_->reference() : std::nullopt;
            return {r ? utf8(r->name) : juce::String(), false};
        };
        curves.place(*status_, 0, 3, 4, 1, ui::PanelSection::Kind::Line);
        freeze.onClick = [this, &ctx]
        {
            if (const auto curve = display_->current())
                ctx.params.applyExtraState(
                    S::snapshotPatch(S::Snapshot{*curve, static_cast<int>(display_->channel())}));
        };
        clearSnapshot.onClick = [&ctx] { ctx.params.applyExtraState(S::snapshotPatch(std::nullopt)); };
        load.onClick = [this] { browse(); };
        clearReference.onClick = [this, &ctx]
        {
            cancelJob();
            problem_.clear();
            ctx.params.applyExtraState(S::referencePatch(std::nullopt));
        };

        auto& settings = addSection("settings", "Settings", 5);
        settings.label("Channel", 0, 0, 2);
        settings.combo(S::Channel, 2, 0, 2);
        settings.label("Resolution", 0, 1, 2);
        settings.combo(S::Resolution, 2, 1, 2);
        settings.label("Peak hold", 0, 2, 2);
        settings.combo(S::Hold, 2, 2, 2);
        settings.label("Range", 0, 3, 2);
        settings.combo(S::Range, 2, 3, 2);
        settings.label("Peak labels", 0, 4, 2);
        settings.combo(S::Labels, 2, 4, 2);
        settings.label("Slope", 0, 5, 2);
        settings.combo(S::Slope, 2, 5, 2);
        settings.knob(S::Average, 4, 0);

        // What can't act now recedes (ui-principles §3).
        dimWhen({&freeze}, [this] { return !display_->hasSignal(); });
        dimWhen({&clearSnapshot}, [this] { return device_ == nullptr || !device_->snapshot(); });
        dimWhen({&clearReference}, [this] { return device_ == nullptr || !device_->reference(); });
        dimWhen({&side},
                [this]
                {
                    const auto c = display_->channel();
                    return c != SpectrumChannel::Both && c != SpectrumChannel::Mid;
                });
    }

    ~SpectrumPanel() override { cancelJob(); }

private:
    ui::Toggle& button(ui::PanelSection& section, const juce::String& text, const juce::String& tip,
                       int column, int row)
    {
        auto& b = own(std::make_unique<ui::Toggle>(text));
        b.setClickingTogglesState(false);
        b.setTooltip(tip);
        section.place(b, column, row, 2, 1, ui::PanelSection::Kind::Line);
        return b;
    }

    void tick()
    {
        if (isShowing())
            display_->tick(juce::Time::getMillisecondCounterHiRes() / 1000.0);
    }

    void browse()
    {
        chooser_ = std::make_unique<juce::FileChooser>("Choose a reference", juce::File(),
                                                       "*.wav;*.wave;*.w64;*.aif;*.aiff;*.flac;*.mp3");
        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<SpectrumPanel>(this)](const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult().existsAsFile())
                    safe->loadReference(fc.getResult().getFullPathName());
            });
    }

    /// Reads the file's average spectrum off the message thread, then hands it to the host
    /// binding (ADR 0017): one undo step in the DAW.
    void loadReference(const juce::String& path)
    {
        cancelJob();
        const auto name = juce::File(path).getFileName();
        reading_ = "Reading " + name + ellipsis();
        problem_.clear();
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        cancel_ = cancel;
        job_ = std::thread(
            [cancel, file = path.toStdString(), title = name.toStdString(),
             safe = juce::Component::SafePointer<SpectrumPanel>(this)]
            {
                std::string error;
                auto curves = analyseFile(file, &error, cancel.get());
                if (cancel->load())
                    return;
                juce::MessageManager::callAsync(
                    [safe, curves, error, file, title]
                    {
                        if (safe == nullptr)
                            return;
                        safe->reading_.clear();
                        if (curves)
                            safe->context().params.applyExtraState(
                                S::referencePatch(S::Reference{title, file, *curves}));
                        else
                            safe->problem_ = "Couldn't read " + utf8(title) +
                                             (error.empty() ? juce::String() : ": " + utf8(error));
                    });
            });
    }

    void cancelJob()
    {
        if (cancel_ != nullptr)
            cancel_->store(true);
        if (job_.joinable())
            job_.join();
        cancel_.reset();
        reading_.clear();
    }

    const S* device_;
    SpectrumDisplay* display_ = nullptr;
    StatusLine* status_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::thread job_;
    juce::String reading_, problem_;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};
} // namespace

const ui::PanelFactory& spectrumPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "spectrum",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<SpectrumPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
