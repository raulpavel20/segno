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
                              public ui::ParamControl,
                              public SpectrogramView
{
public:
    explicit SpectrumDisplay(ui::DeviceUiContext& ctx)
        : ctx_(ctx), device_(dynamic_cast<const S*>(ctx.device))
    {
        setTooltip("Drop an audio file here to show its average spectrum as a reference");
        main_.onTransform = [this] { spectrogramLine(); };
        configure();
    }

    void resized() override { prepareHistory(); }

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
                                         param(S::Side), param(S::View));
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
        playing_ = bus.read(S::PlayingSlot) >= 0.5f;
        // The compared track first: the spectrogram's lines read its last transform for collisions.
        size_t compared = 0;
        bool fresh = drain(bus, S::CompareLeft, S::CompareRight,
                           [&](const float* l, const float* r, size_t n)
                           {
                               compared += n;
                               return compare_.push(l, r, n);
                           });
        const bool side = sideApplies();
        const auto drawn = history_.version();
        fresh |= drain(bus, S::InputLeft, S::InputRight,
                       [&](const float* l, const float* r, size_t n)
                       {
                           if (side)
                               side_.push(l, r, n);
                           return main_.push(l, r, n);
                       });
        if (!side && side_.hasData())
            side_.reset();
        if (spectrogramShown() && history_.version() != drawn)
            repaint();
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
        if (spectrogramShown())
        {
            paintSpectrogram(g);
            return;
        }
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
        hoverY_ = e.position.y;
        repaint();
    }

    /// What hovering the spectrogram at (`x`, `y`) reads: "2.60 kHz · E6   1.2 s ago   −24 dB"
    /// (tests too).
    [[nodiscard]] juce::String spectrogramReadout(float x, float y) const override
    {
        const auto plot = plotArea();
        const double hz = plot.hzAt(x);
        const int age = std::clamp(static_cast<int>(y), 0, std::max(0, history_.rows() - 1));
        const auto column =
            static_cast<size_t>(std::clamp(static_cast<int>(x), 0, std::max(0, history_.width() - 1)));
        const double ago = age * history_.secondsPerRow();
        juce::String text = utf8(frequencyText(hz) + " \xC2\xB7 " + noteText(hz));
        text << "   " << juce::String(ago, 1) << " s ago";
        const auto row = history_.levels(age);
        if (age < history_.filled() && column < row.size())
        {
            // Below the range shown, it says so: the window's own floor means nothing.
            const float level = row[column] + dsp::spectrum::tiltDb(hz, slope());
            const float range = S::rangeDb(param(S::Range));
            text << "   "
                 << (level < -range ? "< " + juce::String(-juce::roundToInt(range))
                                    : juce::String(juce::roundToInt(level)))
                 << " dB";
        }
        return text;
    }

    [[nodiscard]] const dsp::SpectrogramHistory& history() const noexcept override { return history_; }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover_.reset();
        hoverY_.reset();
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
        // The spectrogram: a line about every 10 ms whatever the size (SPEC.md, stage 2).
        const bool spectrogram = spectrogramShown();
        const int hop = spectrogram ? static_cast<int>(std::lround(rate * 0.01)) : 0;
        for (auto* a : {&main_, &compare_})
            a->setHop(hop);
        if (spectrogram != spectrogramWas_ || S::lengthSeconds(param(S::Length)) != lengthWas_)
        {
            spectrogramWas_ = spectrogram;
            lengthWas_ = S::lengthSeconds(param(S::Length));
            prepareHistory();
            repaint();
        }
    }

public:
    /// Whether the display shows the spectrogram (View).
    [[nodiscard]] bool spectrogramShown() const override { return param(S::View) >= 0.5f; }

private:
    /// The spectrogram's rows, a pixel each, over Length; a span of frequencies per pixel across,
    /// on the spectrum's own axis. Starts it again.
    void prepareHistory()
    {
        // The rows end above a strip for the frequency labels, which never sit on the picture.
        const int width = std::max(0, getWidth());
        const int rows = std::max(0, getHeight() - ui::Theme::current().deviceGrid.displayTextHeight);
        history_.prepare(width, rows, rows > 0 ? S::lengthSeconds(param(S::Length)) / rows : 0.01);
        const auto plot = plotArea();
        edges_.resize(static_cast<size_t>(width) + 1);
        for (int x = 0; x <= width; ++x)
            edges_[static_cast<size_t>(x)] = plot.hzAt(static_cast<float>(x));
        line_.assign(static_cast<size_t>(width), dsp::spectrum::floorDb);
        collide_.assign(static_cast<size_t>(width), 0);
        bandOf_.resize(static_cast<size_t>(width));
        for (int x = 0; x < width; ++x)
            bandOf_[static_cast<size_t>(x)] = static_cast<int>(
                std::lround(std::clamp(dsp::spectrum::bandAt(plot.hzAt(static_cast<float>(x) + 0.5f)), 0.0,
                                       static_cast<double>(dsp::spectrum::numBands - 1))));
        drawnVersion_ = 0;
    }

    /// Each transform while the spectrogram shows: a line of what's heard now, kept while the
    /// song plays or sound comes in; held otherwise (SPEC.md, stage 2).
    void spectrogramLine()
    {
        if (!spectrogramShown() || history_.width() == 0)
            return;
        main_.lineAt(edges_, line_);
        const bool sound = std::any_of(line_.begin(), line_.end(), [](float v) { return v > kSoundFloorDb; });
        if (!playing_ && !sound)
            return;
        std::span<const std::uint8_t> collides;
        if (comparing_ && compare_.hasData())
        {
            // Where both are loud at this moment: the spectrum's collision rule, in time.
            const auto bands =
                dsp::spectrumCollisions(main_.currentBands(), compare_.currentBands(), slope());
            for (size_t x = 0; x < collide_.size(); ++x)
                collide_[x] = bands[static_cast<size_t>(bandOf_[x])] ? 1 : 0;
            collides = collide_;
        }
        history_.add(line_, collides, main_.hop() / main_.sampleRate());
    }

private:
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

    /// The spectrogram (SPEC.md, stage 2): its rows as an image, the newest at the top, the
    /// frequency grid over it, a second's mark at the right, and the readout under the pointer.
    void paintSpectrogram(juce::Graphics& g)
    {
        const auto& t = ui::Theme::current();
        const auto plot = plotArea();
        if (history_.filled() == 0)
        {
            plot.paintGrid(g);
            ui::paintEmptyState(g, getLocalBounds(),
                                ctx_.analysis != nullptr ? "Play to see the spectrogram" : "No signal here");
            return;
        }
        renderImage();
        {
            juce::Graphics::ScopedSaveState saved(g);
            juce::Path clip;
            clip.addRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
            g.reduceClipRegion(clip);
            g.drawImageAt(image_, 0, 0);
        }
        plot.paintGrid(g);
        // A mark every second (every five past 10 s), labelled at the right on a backing, as peak
        // labels are: the picture behind can be anything.
        const double length = S::lengthSeconds(param(S::Length));
        const double step = length > 10.0 ? 5.0 : 1.0;
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        const auto textHeight = static_cast<float>(t.deviceGrid.displayTextHeight);
        g.setFont(ui::font(ui::TextStyle::Caption));
        for (double s = step; s < length; s += step)
        {
            const float y = std::round(static_cast<float>(s / history_.secondsPerRow()));
            g.setColour(t.roles.gridMinor);
            g.fillRect(0.0f, y, static_cast<float>(getWidth()), 1.0f);
            const auto text = juce::String(juce::roundToInt(s)) + " s";
            const float textWidth =
                std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text));
            const auto box = juce::Rectangle<float>(static_cast<float>(getWidth()) - inset - textWidth,
                                                    y - textHeight, textWidth, textHeight);
            g.setColour(t.roles.shade);
            g.fillRoundedRectangle(box.expanded(inset / 2.0f, 0.0f), t.radiusControl);
            g.setColour(t.textTertiary);
            g.drawText(text, box, juce::Justification::centredRight, false);
        }
        if (hover_ && hoverY_ && *hoverY_ < static_cast<float>(history_.rows()))
        {
            g.setColour(t.roles.gridMinor);
            g.fillRect(std::round(*hover_), 0.0f, 1.0f, static_cast<float>(history_.rows()));
            g.fillRect(0.0f, std::round(*hoverY_), static_cast<float>(getWidth()), 1.0f);
            // The readout on a backing, at the top left, as the spectrum's is.
            const auto readout = spectrogramReadout(*hover_, *hoverY_);
            const float width =
                std::min(static_cast<float>(getWidth()) - 2.0f * inset,
                         std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), readout)));
            const auto box = juce::Rectangle<float>(inset, inset / 2.0f, width, textHeight);
            g.setColour(t.roles.shade);
            g.fillRoundedRectangle(box.expanded(inset / 2.0f, 0.0f), t.radiusControl);
            g.setColour(t.textSecondary);
            g.drawText(readout, box, juce::Justification::centredLeft, true);
        }
    }

    /// The image from the history, written again when it changed: each point's level after the
    /// Slope, over the Range, through the theme's spectrogram colours.
    void renderImage()
    {
        const auto& t = ui::Theme::current();
        const int width = history_.width(), rows = history_.rows();
        const float range = S::rangeDb(param(S::Range)), tilt = slope();
        const auto key = std::make_tuple(history_.version(), range, tilt, &t);
        if (image_.isValid() && image_.getWidth() == width && image_.getHeight() == rows && key == drawnKey_)
            return;
        drawnKey_ = key;
        if (!image_.isValid() || image_.getWidth() != width || image_.getHeight() != rows)
            image_ = juce::Image(juce::Image::ARGB, std::max(1, width), std::max(1, rows), true);
        if (lutTheme_ != &t) // the colours, 256 steps each, read from the theme once
        {
            lutTheme_ = &t;
            for (int i = 0; i < 256; ++i)
            {
                lut_[static_cast<size_t>(i)] = t.spectrogram(static_cast<float>(i) / 255.0f, false);
                lutHot_[static_cast<size_t>(i)] = t.spectrogram(static_cast<float>(i) / 255.0f, true);
            }
        }
        if (tiltAt_.size() != static_cast<size_t>(width) || tiltWas_ != tilt)
        {
            tiltWas_ = tilt;
            tiltAt_.resize(static_cast<size_t>(width));
            const auto plot = plotArea();
            for (int x = 0; x < width; ++x)
                tiltAt_[static_cast<size_t>(x)] =
                    dsp::spectrum::tiltDb(plot.hzAt(static_cast<float>(x) + 0.5f), tilt);
        }
        const juce::Image::BitmapData data(image_, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < rows; ++y)
        {
            const auto levels = history_.levels(y);
            const auto hot = history_.collisions(y);
            const bool kept = y < history_.filled();
            for (int x = 0; x < width; ++x)
            {
                const auto i = static_cast<size_t>(x);
                const float level = kept ? (levels[i] + tiltAt_[i] + range) / range : 0.0f;
                const auto step = static_cast<size_t>(std::clamp(juce::roundToInt(level * 255.0f), 0, 255));
                data.setPixelColour(x, y, (kept && hot[i] != 0 ? lutHot_ : lut_)[step]);
            }
        }
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
    std::optional<float> hover_, hoverY_;
    // The spectrogram (SPEC.md, stage 2).
    static constexpr float kSoundFloorDb = -150.0f; // a line louder than this is sound coming in
    dsp::SpectrogramHistory history_;
    std::vector<double> edges_;
    std::vector<float> line_, tiltAt_;
    std::vector<std::uint8_t> collide_;
    std::vector<int> bandOf_;
    juce::Image image_;
    std::array<juce::Colour, 256> lut_{}, lutHot_{};
    const ui::Theme* lutTheme_ = nullptr;
    float tiltWas_ = -1.0f;
    std::tuple<std::uint64_t, float, float, const ui::Theme*> drawnKey_{};
    std::uint64_t drawnVersion_ = 0;
    bool playing_ = false, spectrogramWas_ = false;
    double lengthWas_ = 0.0;
    std::tuple<float, float, float, float, float, float> last_{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
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
        // The view, and the spectrogram's length while it shows (SPEC.md, stage 2).
        addHeaderCombo(S::View);
        auto& length = addHeaderCombo(S::Length);
        showHeaderWhen(length, [this] { return display_ != nullptr && display_->spectrogramShown(); });

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
        auto& hold = settings.combo(S::Hold, 2, 2, 2);
        settings.label("Range", 0, 3, 2);
        settings.combo(S::Range, 2, 3, 2);
        settings.label("Peak labels", 0, 4, 2);
        auto& labels = settings.combo(S::Labels, 2, 4, 2);
        settings.label("Slope", 0, 5, 2);
        settings.combo(S::Slope, 2, 5, 2);
        auto& average = settings.knob(S::Average, 4, 0);

        // What can't act now recedes (ui-principles §3); in the spectrogram, what belongs to the
        // curve: kept for when the view turns back (SPEC.md, stage 2).
        const auto curveHidden = [this] { return display_->spectrogramShown(); };
        dimWhen({&freeze}, [this, curveHidden] { return curveHidden() || !display_->hasSignal(); });
        dimWhen({&clearSnapshot},
                [this, curveHidden] { return curveHidden() || device_ == nullptr || !device_->snapshot(); });
        dimWhen({&clearReference},
                [this, curveHidden] { return curveHidden() || device_ == nullptr || !device_->reference(); });
        dimWhen({&side},
                [this, curveHidden]
                {
                    const auto c = display_->channel();
                    return curveHidden() || (c != SpectrumChannel::Both && c != SpectrumChannel::Mid);
                });
        dimWhen({&load, status_, &hold, &labels, &average}, curveHidden);
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
