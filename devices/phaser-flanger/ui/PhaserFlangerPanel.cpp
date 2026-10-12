#include "PhaserFlangerPanel.h"

#include "PhaserFlanger.h"

#include <functional>
#include <utility>

namespace fd::devices
{
namespace
{
using P = PhaserFlanger;

/// The response doesn't depend on the sample rate where the display draws it.
constexpr double kDisplayRate = 48000.0;
constexpr const char* kTitles[] = {"Phaser", "Flanger", "Doubler"};

P::ModeKind modeOf(const ui::DeviceUiContext& ctx)
{
    return static_cast<P::ModeKind>(static_cast<int>(ctx.params.getPlain(P::Mode)));
}

/// The sweep's settings for `mode`, as the audio path takes them (the envelope only while on).
P::Sweep sweepOf(const ui::DeviceUiContext& ctx, P::ModeKind mode)
{
    const auto& p = ctx.params;
    return {.mode = mode,
            .centerHz = p.getPlain(P::PhaserCenter),
            .flangerMs = p.getPlain(P::FlangerTime),
            .doublerMs = p.getPlain(P::DoublerTime),
            .amount = p.getPlain(P::Amount) * 0.01,
            .envAmount = p.getPlain(P::Env) >= 0.5f ? p.getPlain(P::EnvAmount) * 0.01 : 0.0};
}

/// The parameter that sets where `mode` sits: Center or its Time.
size_t placeOf(P::ModeKind mode)
{
    return mode == P::ModeKind::Phaser    ? P::PhaserCenter
           : mode == P::ModeKind::Flanger ? P::FlangerTime
                                          : P::DoublerTime;
}

/// "244–369 Hz", "1.2–3.4 kHz" or "283 Hz–2.3 kHz": the unit once where both ends share it.
juce::String hzRange(double lo, double hi)
{
    const auto number = [](double hz)
    { return hz < 1000.0 ? juce::String(juce::roundToInt(hz)) : juce::String(hz / 1000.0, 1); };
    const auto unit = [](double hz) { return juce::String(hz < 1000.0 ? " Hz" : " kHz"); };
    const auto dash = juce::String::fromUTF8("\xE2\x80\x93");
    if ((lo < 1000.0) == (hi < 1000.0))
        return number(lo) + dash + number(hi) + unit(hi);
    return number(lo) + unit(lo) + dash + number(hi) + unit(hi);
}

/// Where the sweep reaches, the LFO at either end and the envelope (while on) at rest or full.
std::pair<double, double> reach(const P::Sweep& s)
{
    double lo = 1e9, hi = -1e9;
    for (const double lfo : {-1.0, 1.0})
        for (const double env : {0.0, 1.0})
        {
            const double at = P::sweep(s, lfo, env);
            lo = std::min(lo, at);
            hi = std::max(hi, at);
        }
    return {lo, hi};
}

/// "0.4–2.0 ms": both ends to the same places, two under a millisecond.
juce::String msRange(double lo, double hi)
{
    const int places = hi < 1.0 ? 2 : 1;
    return juce::String(lo, places) + juce::String::fromUTF8("\xE2\x80\x93") + juce::String(hi, places) +
           " ms";
}

/// The response (SPEC: UI layout): for the Phaser and the Flanger, the notches or the comb where
/// each side's sweep is now, over a band the sweep covers; for the Doubler, its take's movement
/// over two cycles, as tall as Amount. Its line reads the range the sweep covers. Drag sideways
/// for Center or Time, up and down for Amount.
class ResponseDisplay final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit ResponseDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, {},
                      {P::Mode, P::PhaserCenter, P::PhaserNotches, P::PhaserSpread, P::FlangerTime,
                       P::FlangerZero, P::DoublerTime, P::Feedback, P::Invert, P::Amount, P::Mix, P::Shape,
                       P::Stereo, P::Offset, P::Spin, P::Env, P::EnvAmount}),
          ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip("Drag sideways to move the effect, up or down for how far it sweeps (Shift: finely)");
        setTitle("Response");
        update();
    }

    /// Called when the mode changes (the section takes its name).
    std::function<void(P::ModeKind)> onMode;

    void refresh() override
    {
        MiniDisplay::refresh();
        if (ctx_.analysis != nullptr)
        {
            const std::array<float, 4> now{ctx_.analysis->read(P::LfoLeft), ctx_.analysis->read(P::LfoRight),
                                           ctx_.analysis->read(P::Envelope),
                                           ctx_.analysis->read(P::LfoPosition)};
            if (now != now_)
            {
                now_ = now;
                repaint();
            }
        }
        update();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        placeParam_ = placeOf(modeOf(ctx_));
        start_ = e.position;
        startPlace_ = spec(placeParam_).toNormalized(ctx_.params.getPlain(placeParam_));
        startAmount_ = spec(P::Amount).toNormalized(ctx_.params.getPlain(P::Amount));
        ctx_.params.beginGesture(placeParam_);
        ctx_.params.beginGesture(P::Amount);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& input = ui::Theme::current().input;
        const float scale = (e.mods.isShiftDown() ? input.fineFactor : 1.0f) / input.valueTravel;
        const auto to = [this](size_t param, float normalized)
        { ctx_.params.setPlain(param, spec(param).fromNormalized(std::clamp(normalized, 0.0f, 1.0f))); };
        // Right is higher: the Phaser's notches up, the Flanger's comb up (a shorter delay).
        const float sideways = (e.position.x - start_.x) * scale;
        to(placeParam_, startPlace_ + (placeParam_ == P::FlangerTime ? -sideways : sideways));
        to(P::Amount, startAmount_ + (start_.y - e.position.y) * scale);
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ctx_.params.endGesture(placeParam_);
        ctx_.params.endGesture(P::Amount);
    }

private:
    [[nodiscard]] const ParamSpec& spec(size_t param) const { return ctx_.info.params[param]; }

    /// The mode (for the section's name) and the readout: the range the sweep covers.
    void update()
    {
        const auto mode = modeOf(ctx_);
        if (mode != mode_)
        {
            mode_ = mode;
            if (onMode)
                onMode(mode);
        }
        const auto [lo, hi] = reach(sweepOf(ctx_, mode));
        std::pair<juce::String, juce::String> text;
        if (mode == P::ModeKind::Phaser)
            text.first = hzRange(lo, hi);
        else
        {
            text.first = msRange(lo, hi);
            if (mode == P::ModeKind::Flanger && ctx_.params.getPlain(P::FlangerZero) >= 0.5f &&
                lo < P::zeroMs)
                text.second = "through zero";
        }
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
        const auto plot = area.withTrimmedTop(inset / 2.0f).reduced(0.0f, inset / 2.0f);

        const auto mode = modeOf(ctx_);
        if (mode == P::ModeKind::Doubler)
            drawMovement(g, plot);
        else
            drawResponse(g, plot, mode);
    }

    void drawResponse(juce::Graphics& g, juce::Rectangle<float> plot, P::ModeKind mode)
    {
        const auto& t = ui::Theme::current();
        const auto& p = ctx_.params;
        ui::SpectrumPlot axis{.area = plot};
        const auto s = sweepOf(ctx_, mode);
        // Where the sweep reaches: the notches' frequency, or the comb's first notch.
        const auto firstNotch = [&](double at)
        { return mode == P::ModeKind::Phaser ? at : 1000.0 / (2.0 * std::max(at, 0.025)); };
        const auto [lo, hi] = reach(s);
        const double a = firstNotch(lo), b = firstNotch(hi);
        const float x0 = axis.xOf(std::clamp(std::min(a, b), 20.0, 20000.0));
        const float x1 = axis.xOf(std::clamp(std::max(a, b), 20.0, 20000.0));
        g.setColour(t.fill(t.value));
        g.fillRect(juce::Rectangle<float>(x0, plot.getY(), std::max(1.0f, x1 - x0), plot.getHeight()));

        const P::Response r{.mode = mode,
                            .notches = static_cast<int>(p.getPlain(P::PhaserNotches)),
                            .spread = p.getPlain(P::PhaserSpread) * 0.01,
                            .feedback = p.getPlain(P::Feedback) * 0.01,
                            .invert = p.getPlain(P::Invert) >= 0.5f,
                            .mix = p.getPlain(P::Mix) * 0.01,
                            .zero = p.getPlain(P::FlangerZero) >= 0.5f};
        constexpr double topDb = 12.0, bottomDb = -30.0;
        const auto yOf = [&](double level)
        {
            const double d = std::clamp(20.0 * std::log10(std::max(level, 1e-6)), bottomDb, topDb);
            return plot.getY() + static_cast<float>((topDb - d) / (topDb - bottomDb)) * plot.getHeight();
        };
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(yOf(1.0)), plot.getX(), plot.getRight());
        const int n = std::max(16, static_cast<int>(plot.getWidth()));
        for (const int side : {1, 0})
        {
            // At rest where nothing plays it (the snapshots, the gallery).
            const double lfo = ctx_.analysis != nullptr ? now_[static_cast<size_t>(side)] : 0.0;
            const double env = ctx_.analysis != nullptr ? now_[2] : 0.0;
            const double at = P::sweep(s, lfo, env);
            juce::Path path;
            for (int i = 0; i <= n; ++i)
            {
                const double hz =
                    axis.hzAt(plot.getX() + static_cast<float>(i) / static_cast<float>(n) * plot.getWidth());
                const juce::Point<float> pt(plot.getX() + static_cast<float>(i) / static_cast<float>(n) *
                                                              plot.getWidth(),
                                            yOf(P::response(r, at, hz, kDisplayRate)));
                i == 0 ? path.startNewSubPath(pt) : path.lineTo(pt);
            }
            g.setColour(side == 0 ? t.value : t.textSecondary);
            g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
        }
    }

    void drawMovement(juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto& t = ui::Theme::current();
        const auto& p = ctx_.params;
        static constexpr dsp::ModulationLfo::Shape kShapes[] = {
            dsp::ModulationLfo::Shape::Sine,   dsp::ModulationLfo::Shape::Triangle,
            dsp::ModulationLfo::Shape::SawUp,  dsp::ModulationLfo::Shape::SawDown,
            dsp::ModulationLfo::Shape::Square, dsp::ModulationLfo::Shape::SampleAndHold,
            dsp::ModulationLfo::Shape::Wander};
        const auto shape = kShapes[std::clamp(static_cast<int>(p.getPlain(P::Shape)), 0, 6)];
        const bool spin = static_cast<int>(p.getPlain(P::Stereo)) == 1;
        const double offset = p.getPlain(P::Offset) / 360.0, ratio = 1.0 + p.getPlain(P::Spin) * 0.01;
        const auto half = static_cast<float>(p.getPlain(P::Amount) * 0.01) * plot.getHeight() * 0.5f;
        const int n = std::max(16, static_cast<int>(plot.getWidth()));
        for (const int side : {1, 0})
        {
            juce::Path path;
            for (int i = 0; i <= n; ++i)
            {
                const double position = 2.0 * i / n;
                const double at = side == 0 ? position : spin ? position * ratio : position + offset;
                const juce::Point<float> pt(plot.getX() + static_cast<float>(i) / static_cast<float>(n) *
                                                              plot.getWidth(),
                                            plot.getCentreY() - dsp::ModulationLfo::value(shape, at) * half);
                i == 0 ? path.startNewSubPath(pt) : path.lineTo(pt);
            }
            g.setColour(side == 0 ? t.value : t.textSecondary);
            g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
        }
        if (ctx_.analysis != nullptr)
        {
            g.setColour(t.textTertiary);
            g.drawVerticalLine(juce::roundToInt(plot.getX() + now_[3] * 0.5f * plot.getWidth()), plot.getY(),
                               plot.getBottom());
        }
    }

    ui::DeviceUiContext& ctx_;
    std::pair<juce::String, juce::String> readout_;
    std::array<float, 4> now_{}; // the LFO left and right, the envelope, the LFO's position
    P::ModeKind mode_ = P::ModeKind::Phaser;
    size_t placeParam_ = P::PhaserCenter;
    juce::Point<float> start_;
    float startPlace_ = 0.0f, startAmount_ = 0.0f;
};

/// Sections per devices/phaser-flanger/SPEC.md, on the module grid (docs/design/device-panels.md).
class PhaserFlangerPanel final : public ui::DevicePanel
{
public:
    explicit PhaserFlangerPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& effect = addSection("effect", kTitles[static_cast<int>(modeOf(ctx))], 6);
        auto& response = own(std::make_unique<ResponseDisplay>(ctx));
        effect.place(response, 0, 0, 4, 3);
        response.onMode = [&effect](P::ModeKind mode)
        { effect.setSectionTitle(kTitles[static_cast<int>(mode)]); };
        effect.segmented(P::Mode, 4, 0, 2, 3);
        auto& place = effect.knob(P::PhaserCenter, 0, 3);
        auto& notches = effect.knob(P::PhaserNotches, 1, 3);
        auto& spread = effect.knob(P::PhaserSpread, 2, 3);
        effect.knob(P::Warmth, 3, 3);
        effect.bar(P::Feedback, 4, 3, 2);
        effect.toggle(P::Invert, 4, 4, 2);
        auto& zero = effect.toggle(P::FlangerZero, 4, 5, 2);
        // Center, or the mode's Time.
        place.follow([&ctx] { return placeOf(modeOf(ctx)); });

        auto& modulation = addSection("modulation", "Modulation", 3);
        auto& rate = modulation.knob(P::Rate, 0, 0);
        modulation.knob(P::Amount, 1, 0);
        auto& stereo = modulation.knob(P::Offset, 2, 0);
        modulation.combo(P::Shape, 0, 3, 2);
        modulation.toggle(P::Sync, 2, 3);
        modulation.segmented(P::Stereo, 0, 4, 2, 1);
        // The rate in Hz, or as a note value while Sync is on; Offset, or Spin.
        rate.follow([&ctx] { return ctx.params.getPlain(P::Sync) >= 0.5f ? P::Div : P::Rate; });
        stereo.follow(
            [&ctx] { return static_cast<int>(ctx.params.getPlain(P::Stereo)) == 1 ? P::Spin : P::Offset; });

        auto& envelope = addSection("envelope", "Envelope", 3, P::Env);
        envelope.knob(P::EnvAmount, 0, 0);
        envelope.knob(P::EnvAttack, 1, 0);
        envelope.knob(P::EnvRelease, 2, 0);

        auto& output = addSection("output", "Output", 2);
        output.knob(P::Mix, 0, 0);
        output.knob(P::Output, 1, 0);
        output.toggle(P::LowCut, 0, 3, 2);
        auto& freq = output.bar(P::LowCutFreq, 0, 4, 2);

        dimWhen({&notches, &spread}, [&ctx] { return modeOf(ctx) != P::ModeKind::Phaser; });
        dimWhen({&zero}, [&ctx] { return modeOf(ctx) != P::ModeKind::Flanger; });
        dimWhen({&freq}, [&ctx] { return ctx.params.getPlain(P::LowCut) < 0.5f; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? P::PeakInL : P::PeakInR); });
            meterOut_.setLevelSource([bus](int c)
                                     { return bus->takePeak(c == 0 ? P::PeakOutL : P::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& phaserFlangerPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "phaser-flanger",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<PhaserFlangerPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
