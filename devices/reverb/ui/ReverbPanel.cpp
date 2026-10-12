#include "ReverbPanel.h"

#include "Reverb.h"

#include <utility>

namespace fd::devices
{
namespace
{
using R = Reverb;

/// The decay hardly depends on the sample rate where the display draws it.
constexpr double kDisplayRate = 48000.0;
constexpr double kShortest = 0.1, kLongest = 60.0; // the display's time scale (seconds, log)

R::ModeKind modeOf(const ui::DeviceUiContext& ctx)
{
    return static_cast<R::ModeKind>(static_cast<int>(ctx.params.getPlain(R::Mode)));
}

R::Bands bandsOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    return {.decay = p.getPlain(R::Decay),
            .lowMult = p.getPlain(R::LowMult) * 0.01,
            .lowHz = p.getPlain(R::LowFreq),
            .highMult = p.getPlain(R::HighMult) * 0.01,
            .highHz = p.getPlain(R::HighFreq)};
}

/// A line of the network as long as the mean, with the all-passes' delay: where the display
/// takes the decay from (the lines differ only around the crossovers).
double meanLoop(const ui::DeviceUiContext& ctx)
{
    return R::meanLine(modeOf(ctx), ctx.params.getPlain(R::Size) * 0.01) + 0.0007;
}

/// How long the reverb rings at each frequency (SPEC: UI layout), from `Reverb::decayAt`, with
/// the crossovers marked. Drag up and down to change the band under the pointer (Low Decay, Decay
/// or High Decay); sideways near a crossover to move it.
class DecayDisplay final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit DecayDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(
              ctx, {},
              {R::Mode, R::Decay, R::Size, R::LowMult, R::LowFreq, R::HighMult, R::HighFreq, R::Freeze}),
          ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setTooltip(
            "How long each frequency rings: drag up or down to change a band, sideways at a crossover to "
            "move it (Shift: finely)");
        setTitle("Decay");
        update();
    }

    void refresh() override
    {
        MiniDisplay::refresh();
        update();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(crossoverAt(e.position.x) != nullptr ? juce::MouseCursor::LeftRightResizeCursor
                                                            : juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        start_ = e.position;
        if (const auto* crossover = crossoverAt(e.position.x))
        {
            param_ = *crossover;
            sideways_ = true;
        }
        else
        {
            const double hz = axis_.hzAt(e.position.x);
            const auto b = bandsOf(ctx_);
            param_ = hz < b.lowHz ? R::LowMult : hz > b.highHz ? R::HighMult : R::Decay;
            sideways_ = false;
        }
        startValue_ = spec(param_).toNormalized(ctx_.params.getPlain(param_));
        ctx_.params.beginGesture(param_);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& input = ui::Theme::current().input;
        const auto& s = spec(param_);
        if (sideways_)
        {
            const double hz = axis_.hzAt(e.position.x);
            ctx_.params.setPlain(param_, std::clamp(static_cast<float>(hz), s.min, s.max));
        }
        else
        {
            const float scale = (e.mods.isShiftDown() ? input.fineFactor : 1.0f) / input.valueTravel;
            const float normalized = std::clamp(startValue_ + (start_.y - e.position.y) * scale, 0.0f, 1.0f);
            ctx_.params.setPlain(param_, s.fromNormalized(normalized));
        }
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override { ctx_.params.endGesture(param_); }

private:
    [[nodiscard]] const ParamSpec& spec(size_t param) const { return ctx_.info.params[param]; }

    /// The crossover under `x` (within the edge grab), if any.
    [[nodiscard]] const size_t* crossoverAt(float x) const
    {
        static constexpr size_t kLow = R::LowFreq, kHigh = R::HighFreq;
        const auto grab = ui::Theme::current().input.edgeGrab;
        if (std::abs(x - axis_.xOf(ctx_.params.getPlain(R::LowFreq))) <= grab)
            return &kLow;
        if (std::abs(x - axis_.xOf(ctx_.params.getPlain(R::HighFreq))) <= grab)
            return &kHigh;
        return nullptr;
    }

    /// The readout: the middle's decay, and the size of the space (not for a plate).
    void update()
    {
        const auto& p = ctx_.params;
        std::pair<juce::String, juce::String> text;
        if (p.getPlain(R::Freeze) >= 0.5f)
            text.first = "frozen";
        else
            text.first = juce::String(p.getPlain(R::Decay), 2) + " s";
        if (const double m = R::metres(modeOf(ctx_), p.getPlain(R::Size) * 0.01); m > 0.0)
            text.second = juce::String(juce::roundToInt(m)) + " m";
        if (text != readout_)
        {
            readout_ = std::move(text);
            setDescription(readout_.second.isEmpty() ? readout_.first
                                                     : readout_.first + ", " + readout_.second);
            repaint();
        }
    }

    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto& t = ui::Theme::current();
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        const auto text = static_cast<float>(t.deviceGrid.displayTextHeight);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.setColour(t.textSecondary);
        auto line = area.removeFromTop(text);
        g.drawText(readout_.first, line, juce::Justification::centredLeft, false);
        g.drawText(readout_.second, line, juce::Justification::centredRight, false);
        // The time scale's labels in a gutter at the left, clear of the curve.
        const auto full = area.withTrimmedTop(inset / 2.0f).reduced(0.0f, inset / 2.0f);
        const auto valueFont = ui::font(ui::TextStyle::ValueSmall);
        const float gutter = juce::GlyphArrangement::getStringWidth(valueFont, "10 s") + inset;
        const auto plot = full.withTrimmedLeft(gutter);
        axis_ = ui::SpectrumPlot{.area = plot};

        const auto yOf = [&](double seconds)
        {
            const double share = std::log(std::clamp(seconds, kShortest, kLongest) / kShortest) /
                                 std::log(kLongest / kShortest);
            return plot.getBottom() - static_cast<float>(share) * plot.getHeight();
        };
        // A second and ten seconds, faint.
        for (const double s : {1.0, 10.0})
        {
            const float y = std::round(yOf(s));
            g.setColour(t.roles.gridMinor);
            g.fillRect(plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour(t.textTertiary);
            g.drawText(juce::String(juce::roundToInt(s)) + " s",
                       juce::Rectangle<float>(full.getX(), y - text / 2.0f, gutter - inset, text),
                       juce::Justification::centredRight, false);
        }
        // The crossovers.
        const auto b = bandsOf(ctx_);
        g.setColour(t.line);
        for (const double hz : {b.lowHz, b.highHz})
            g.fillRect(std::round(axis_.xOf(hz)), plot.getY(), 1.0f, plot.getHeight());

        // The decay at each frequency: frozen, it rings on forever (off the top).
        const bool frozen = ctx_.params.getPlain(R::Freeze) >= 0.5f;
        const double loop = meanLoop(ctx_);
        const int n = std::max(16, static_cast<int>(plot.getWidth()));
        juce::Path curve;
        for (int i = 0; i <= n; ++i)
        {
            const float x = plot.getX() + static_cast<float>(i) / static_cast<float>(n) * plot.getWidth();
            const double seconds = frozen ? kLongest : R::decayAt(b, axis_.hzAt(x), loop, kDisplayRate);
            const juce::Point<float> pt(x, yOf(seconds));
            i == 0 ? curve.startNewSubPath(pt) : curve.lineTo(pt);
        }
        auto fill = curve;
        fill.lineTo(plot.getBottomRight());
        fill.lineTo(plot.getBottomLeft());
        fill.closeSubPath();
        g.setColour(t.fill(t.value));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }

    ui::DeviceUiContext& ctx_;
    std::pair<juce::String, juce::String> readout_;
    ui::SpectrumPlot axis_;
    size_t param_ = R::Decay;
    bool sideways_ = false;
    juce::Point<float> start_;
    float startValue_ = 0.0f;
};

/// Sections per devices/reverb/SPEC.md, on the module grid (docs/design/device-panels.md).
class ReverbPanel final : public ui::DevicePanel
{
public:
    explicit ReverbPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& reverb = addSection("reverb", "Reverb", 7);
        reverb.place(own(std::make_unique<DecayDisplay>(ctx)), 0, 0, 5, 3);
        reverb.segmented(R::Mode, 5, 0, 2, 2);
        reverb.toggle(R::Freeze, 5, 2, 2);
        reverb.knob(R::Decay, 0, 3);
        reverb.knob(R::Size, 1, 3);
        reverb.knob(R::Predelay, 2, 3);
        reverb.knob(R::Diffusion, 3, 3);
        auto& early = reverb.knob(R::Early, 4, 3);
        reverb.knob(R::Motion, 5, 3);
        reverb.knob(R::Width, 6, 3);

        auto& bands = addSection("bands", "Decay Bands", 2);
        bands.knob(R::LowMult, 0, 0);
        bands.knob(R::HighMult, 1, 0);
        bands.knob(R::LowFreq, 0, 3);
        bands.knob(R::HighFreq, 1, 3);

        auto& filter = addSection("filter", "Filter", 2, R::Filter);
        filter.place(own(std::make_unique<ui::CutFilterCurve>(ctx, R::Filter, R::FilterLow, R::FilterHigh)),
                     0, 0, 2, 3);
        filter.knob(R::FilterLow, 0, 3);
        filter.knob(R::FilterHigh, 1, 3);

        auto& ducking = addSection("ducking", "Ducking", 1);
        ducking.knob(R::Duck, 0, 0);
        ducking.knob(R::DuckRelease, 0, 3);

        auto& output = addSection("output", "Output", 1);
        output.knob(R::Mix, 0, 0);
        output.knob(R::Output, 0, 3);

        // A plate has no early reflections.
        dimWhen({&early}, [&ctx] { return modeOf(ctx) == R::ModeKind::Plate; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? R::PeakInL : R::PeakInR); });
            meterOut_.setLevelSource([bus](int c)
                                     { return bus->takePeak(c == 0 ? R::PeakOutL : R::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& reverbPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "reverb",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<ReverbPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
