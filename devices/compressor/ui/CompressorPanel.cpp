#include "CompressorPanel.h"

#include "Compressor.h"
#include "fd/dsp/Math.h"

#include <array>

namespace fd::devices
{
namespace
{
using C = Compressor;

constexpr float kFloorDb = -60.0f; // both displays show -60 .. 0 dBFS

/// The static curve from the same function the audio path uses, with the detector's level as a
/// moving dot. Drag the threshold point (or left of it) to move the threshold; drag above the
/// curve right of it, up or down, to change the ratio.
class CurveDisplay final : public juce::Component, public juce::SettableTooltipClient, public ui::ParamControl
{
public:
    explicit CurveDisplay(ui::DeviceUiContext& ctx) : ctx_(ctx)
    {
        setTooltip("Drag the point for Threshold; drag right of it, up or down, for Ratio. Double-click resets.");
    }

    void refresh() override
    {
        const auto key = std::make_tuple(get(C::Threshold), get(C::Ratio), get(C::Knee));
        if (key != last_)
        {
            last_ = key;
            repaint();
        }
    }

    /// Every frame: follow the detector's level (falling at most 30 dB/s, so the dot glides).
    void tick()
    {
        if (ctx_.analysis == nullptr)
            return;
        const float db = dsp::gainToDb(ctx_.analysis->takePeak(C::DetectorLevel));
        const float next = std::max(db, level_ - 0.5f);
        if (std::abs(next - level_) > 0.05f)
        {
            level_ = next;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);

        g.setColour(t.line);
        for (float db = -12.0f; db > kFloorDb; db -= 12.0f)
        {
            g.fillRect(xOf(db), plot().getY(), 1.0f, plot().getHeight());
            g.fillRect(plot().getX(), yOf(db), plot().getWidth(), 1.0f);
        }
        g.setColour(t.surface3);
        g.drawLine(xOf(kFloorDb), yOf(kFloorDb), xOf(0.0f), yOf(0.0f), 1.0f); // unity

        const float threshold = get(C::Threshold), ratio = ratioValue(), knee = get(C::Knee);
        juce::Path curve;
        constexpr int steps = 120;
        for (int i = 0; i <= steps; ++i)
        {
            const float in = kFloorDb * (1.0f - static_cast<float>(i) / steps);
            const auto pt = juce::Point<float>(xOf(in), yOf(C::curve(in, threshold, ratio, knee)));
            i == 0 ? curve.startNewSubPath(pt) : curve.lineTo(pt);
        }
        juce::Path fill(curve);
        fill.lineTo(xOf(0.0f), plot().getBottom());
        fill.lineTo(xOf(kFloorDb), plot().getBottom());
        fill.closeSubPath();
        g.setColour(t.roles.curveFill);
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(curve, juce::PathStrokeType(1.5f));

        // The detector's level on the curve.
        if (level_ > kFloorDb)
        {
            const auto dot = juce::Point<float>(xOf(level_), yOf(C::curve(level_, threshold, ratio, knee)));
            g.setColour(t.textPrimary);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(dot));
        }

        // The threshold point.
        const auto box = juce::Rectangle<float>(11.0f, 11.0f).withCentre(thresholdPoint());
        const bool lit = drag_ == Drag::Threshold || hoverPoint_;
        g.setColour(lit ? t.active : t.surface3);
        g.fillEllipse(box);
        if (!lit)
        {
            g.setColour(t.value);
            g.drawEllipse(box.reduced(0.5f), 1.2f);
        }

        if (!readout_.isEmpty())
        {
            g.setColour(t.textSecondary);
            g.setFont(ui::font(ui::TextStyle::ValueSmall));
            g.drawText(readout_, r.reduced(6.0f, 4.0f), juce::Justification::topLeft, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        drag_ = e.position.getDistanceFrom(thresholdPoint()) <= 9.0f || e.position.x < thresholdPoint().x
                    ? Drag::Threshold
                    : Drag::Ratio;
        const auto p = param();
        ctx_.params.beginGesture(p);
        startPos_ = e.position;
        startValue_ = drag_ == Drag::Threshold ? get(C::Threshold) : spec(C::Ratio).toNormalized(get(C::Ratio));
        showReadout();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (drag_ == Drag::None)
            return;
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const auto d = (e.position - startPos_) * fine;
        if (drag_ == Drag::Threshold)
        {
            // Along the diagonal: right and up both raise it.
            const float dbPerPx = -kFloorDb / plot().getWidth();
            set(C::Threshold, juce::jlimit(kFloorDb, 0.0f, startValue_ + 0.5f * (d.x - d.y) * dbPerPx));
        }
        else
        {
            // Down compresses harder, like the curve bending down.
            set(C::Ratio, spec(C::Ratio).fromNormalized(juce::jlimit(0.0f, 1.0f, startValue_ + d.y / 150.0f)));
        }
        showReadout();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag_ == Drag::None)
            return;
        ctx_.params.endGesture(param());
        drag_ = Drag::None;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto p = e.position.x <= thresholdPoint().x + 9.0f ? C::Threshold : C::Ratio;
        ctx_.params.beginGesture(p);
        set(p, spec(p).def);
        ctx_.params.endGesture(p);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool over = e.position.getDistanceFrom(thresholdPoint()) <= 9.0f;
        if (over != hoverPoint_)
        {
            hoverPoint_ = over;
            repaint();
        }
        drag_ = e.position.x < thresholdPoint().x || over ? Drag::Threshold : Drag::Ratio;
        showReadout();
        drag_ = Drag::None;
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverPoint_ = false;
        readout_.clear();
        repaint();
    }

private:
    enum class Drag { None, Threshold, Ratio };

    [[nodiscard]] float get(size_t p) const { return ctx_.params.getPlain(p); }
    [[nodiscard]] const ParamSpec& spec(size_t p) const { return ctx_.info.params[p]; }
    [[nodiscard]] size_t param() const { return drag_ == Drag::Threshold ? C::Threshold : C::Ratio; }
    /// The ratio as the audio path reads it: its top value is infinity.
    [[nodiscard]] float ratioValue() const
    {
        const float r = get(C::Ratio);
        return r >= C::infiniteRatio ? std::numeric_limits<float>::infinity() : r;
    }

    void set(size_t p, float value)
    {
        ctx_.params.setPlain(p, value);
        refresh();
    }

    void showReadout()
    {
        const auto p = param();
        readout_ = juce::String(std::string(spec(p).name)) + " " + juce::String(spec(p).toText(get(p)));
        repaint();
    }

    [[nodiscard]] juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(6.0f); }
    [[nodiscard]] float xOf(float db) const { return plot().getX() + (db - kFloorDb) / -kFloorDb * plot().getWidth(); }
    [[nodiscard]] float yOf(float db) const
    {
        return plot().getBottom() - juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb) * plot().getHeight();
    }
    [[nodiscard]] juce::Point<float> thresholdPoint() const
    {
        const float threshold = get(C::Threshold);
        return {xOf(threshold), yOf(C::curve(threshold, threshold, ratioValue(), get(C::Knee)))};
    }

    ui::DeviceUiContext& ctx_;
    std::tuple<float, float, float> last_{1.0f, 0.0f, -1.0f};
    float level_ = -200.0f;
    Drag drag_ = Drag::None;
    bool hoverPoint_ = false;
    juce::Point<float> startPos_;
    float startValue_ = 0.0f;
    juce::String readout_;
};

/// About four seconds of input level (dim, from the bottom), output level (a line) and gain
/// reduction (from the top, in the value colour), scrolling right to left. Frames come from the
/// device's history FIFO, one every 10 ms.
class HistoryDisplay final : public juce::Component, public ui::ParamControl
{
public:
    explicit HistoryDisplay(ui::DeviceUiContext& ctx) : ctx_(ctx), threshold_(ctx.params.getPlain(C::Threshold)) {}

    void refresh() override
    {
        if (const float threshold = ctx_.params.getPlain(C::Threshold); threshold != threshold_)
        {
            threshold_ = threshold;
            repaint();
        }
    }

    /// Every frame: take the new history frames. `visible` false only drains the FIFO.
    void tick(bool visible)
    {
        if (ctx_.analysis == nullptr)
            return;
        auto& fifo = ctx_.analysis->fifo(C::historyFifo);
        bool any = false;
        float frame[3];
        while (fifo.pop(frame, 3) == 3)
        {
            frames_[static_cast<size_t>(write_)] = {frame[0], frame[1], frame[2]};
            write_ = (write_ + 1) % kFrames;
            any = true;
        }
        if (any && visible)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);
        const auto area = r.reduced(1.0f);

        g.setColour(t.line);
        for (float db = -12.0f; db > kFloorDb; db -= 12.0f)
            g.fillRect(area.getX(), yOfLevel(area, db), area.getWidth(), 1.0f);

        // The threshold, for reading the input against it.
        g.setColour(t.textTertiary);
        const float ty = yOfLevel(area, threshold_);
        const float dashes[] = {3.0f, 3.0f};
        g.drawDashedLine({area.getX(), ty, area.getRight(), ty}, dashes, 2, 1.0f);

        // One column per pixel: the loudest frame it covers. The output and reduction lines
        // break where there is nothing to show, so silence draws nothing at the edges.
        const int width = static_cast<int>(area.getWidth());
        juce::Path input, output, reduction, reductionLine;
        input.startNewSubPath(area.getX(), area.getBottom());
        reduction.startNewSubPath(area.getX(), area.getY());
        bool outputOn = false, reductionOn = false;
        float currentReduction = 0.0f;
        for (int x = 0; x < width; ++x)
        {
            const int from = x * kFrames / width, to = std::max(from + 1, (x + 1) * kFrames / width);
            float in = kFloorDb, out = kFloorDb, gr = 0.0f;
            for (int f = from; f < to; ++f)
            {
                const auto& frame = frames_[static_cast<size_t>((write_ + f) % kFrames)];
                in = std::max(in, frame[0]);
                out = std::max(out, frame[1]);
                gr = std::max(gr, frame[2]);
            }
            const float px = area.getX() + static_cast<float>(x);
            input.lineTo(px, yOfLevel(area, in));
            const auto o = juce::Point<float>(px, yOfLevel(area, out));
            const bool hasOutput = out > kFloorDb;
            if (hasOutput)
                outputOn ? output.lineTo(o) : output.startNewSubPath(o);
            outputOn = hasOutput;
            const float grY = area.getY() + std::min(gr, -kFloorDb) / -kFloorDb * area.getHeight();
            const auto rp = juce::Point<float>(px, grY);
            reduction.lineTo(rp);
            const bool hasReduction = gr >= 0.05f;
            if (hasReduction)
                reductionOn ? reductionLine.lineTo(rp) : reductionLine.startNewSubPath(rp);
            reductionOn = hasReduction;
            currentReduction = gr;
        }
        input.lineTo(area.getRight(), area.getBottom());
        input.closeSubPath();
        g.setColour(t.roles.spectrumFill);
        g.fillPath(input);
        g.setColour(t.textSecondary);
        g.strokePath(output, juce::PathStrokeType(1.0f));

        reduction.lineTo(area.getRight(), area.getY());
        reduction.closeSubPath();
        g.setColour(t.roles.curveFill);
        g.fillPath(reduction);
        g.setColour(t.value);
        g.strokePath(reductionLine, juce::PathStrokeType(1.5f));

        g.setColour(t.textSecondary);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        const auto text =
            currentReduction >= 0.05f ? "-" + juce::String(currentReduction, 1) + " dB" : juce::String("0.0 dB");
        g.drawText(text, r.reduced(6.0f, 4.0f), juce::Justification::bottomRight, false);
    }

private:
    static constexpr int kFrames = 400; // 4 s at one frame per 10 ms

    [[nodiscard]] static float yOfLevel(juce::Rectangle<float> area, float db)
    {
        return area.getBottom() - juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb) * area.getHeight();
    }

    ui::DeviceUiContext& ctx_;
    std::array<std::array<float, 3>, kFrames> frames_ = [] {
        std::array<std::array<float, 3>, kFrames> a{};
        for (auto& f : a)
            f = {kFloorDb, kFloorDb, 0.0f};
        return a;
    }();
    int write_ = 0;
    float threshold_;
};

/// Sections per devices/compressor/SPEC.md, on the module grid (docs/design/device-panels.md).
class CompressorPanel final : public ui::DevicePanel
{
public:
    explicit CompressorPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& curve = addSection("curve", "Curve", 3);
        curve_ = &own(std::make_unique<CurveDisplay>(ctx));
        curve.place(*curve_, 0, 0, 3, 6);

        auto& history = addSection("history", "History", 4);
        history_ = &own(std::make_unique<HistoryDisplay>(ctx));
        history.place(*history_, 0, 0, 4, 6);

        auto& comp = addSection("comp", "Compression", 3);
        comp.knob(C::Threshold, 0, 0);
        comp.knob(C::Ratio, 1, 0);
        comp.knob(C::Knee, 2, 0);
        auto& makeup = comp.knob(C::Makeup, 0, 3);
        comp.toggle(C::MakeupAuto, 1, 3, 2);
        comp.segmented(C::Character, 1, 4, 2, 1);

        auto& time = addSection("time", "Time", 2);
        time.knob(C::Attack, 0, 0);
        time.knob(C::Release, 1, 0);
        time.toggle(C::ReleaseAuto, 0, 3, 2);
        time.label("Lookahead", 0, 4, 2);
        time.combo(C::Lookahead, 0, 5, 2);

        auto& detector = addSection("detector", "Detector", 2);
        detector.segmented(C::Detector, 0, 0, 2, 1);
        detector.bar(C::Link, 0, 1, 2);

        auto& sidechain = addSection("sidechain", "Sidechain", 3);
        sidechain.label("Filter", 0, 0, 1);
        sidechain.combo(C::ScFilter, 1, 0, 2);
        sidechain.toggle(C::ScListen, 1, 1, 2);
        auto& freq = sidechain.knob(C::ScFreq, 0, 3);
        auto& q = sidechain.knob(C::ScQ, 1, 3);
        auto& gain = sidechain.knob(C::ScGain, 2, 3);

        auto& output = addSection("output", "Output", 1);
        output.knob(C::Output, 0, 0);
        output.knob(C::Mix, 0, 3);

        // Settings the current switches ignore recede (ui-principles §3).
        dimWhen({&makeup}, [&ctx] { return ctx.params.getPlain(C::MakeupAuto) >= 0.5f; });
        auto filter = [&ctx] {
            return static_cast<C::ScFilterType>(static_cast<int>(ctx.params.getPlain(C::ScFilter)));
        };
        dimWhen({&freq, &q}, [filter] { return filter() == C::ScFilterType::Off; });
        dimWhen({&gain}, [filter] { return filter() != C::ScFilterType::Bell; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? C::PeakInL : C::PeakInR); });
            meterOut_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? C::PeakOutL : C::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    void tick()
    {
        if (!isShowing())
            return;
        if (!findSection("curve")->isCollapsed())
            curve_->tick();
        history_->tick(!findSection("history")->isCollapsed());
    }

    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
    CurveDisplay* curve_ = nullptr;
    HistoryDisplay* history_ = nullptr;
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& compressorPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "compressor",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<CompressorPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
