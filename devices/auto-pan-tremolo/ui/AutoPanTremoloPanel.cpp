#include "AutoPanTremoloPanel.h"

#include "AutoPanTremolo.h"

#include <array>
#include <functional>
#include <utility>

namespace fd::devices
{
namespace
{
using A = AutoPanTremolo;

constexpr const char* kTitles[] = {"Panning", "Tremolo"};

A::ModeKind modeOf(const ui::DeviceUiContext& ctx)
{
    return static_cast<A::ModeKind>(static_cast<int>(ctx.params.getPlain(A::Mode)));
}

A::Wave waveOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    return {.shape = static_cast<int>(p.getPlain(A::Shape)),
            .edge = p.getPlain(A::Edge) * 0.01,
            .phase = p.getPlain(A::Phase) / 360.0,
            .invert = p.getPlain(A::Invert) >= 0.5f};
}

/// The rate it moves at. Synced, it's what the device last played at (it knows the tempo);
/// where it hasn't played (snapshots), 120 BPM in 4/4.
double rateOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    if (p.getPlain(A::Sync) < 0.5f)
        return p.getPlain(A::Rate);
    if (ctx.analysis != nullptr)
        if (const float r = ctx.analysis->read(A::RateHz); r > 0.0f)
            return r;
    return 2.0 / dsp::lfoNoteBeats(static_cast<int>(p.getPlain(A::Div)), 4.0);
}

/// The movement (SPEC: UI layout): two cycles of each side's level (Tremolo, silence at the bottom)
/// or of the sound's place (Panning, left at the top), and a marker where it is now. Its line reads
/// the depth or the swing, and the rate. Drag sideways for Rate, up and down for Amount.
class MovementDisplay final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit MovementDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, {},
                      {A::Mode, A::Rate, A::Sync, A::Div, A::Shape, A::Amount, A::Edge, A::Phase, A::Invert,
                       A::Stereo, A::Offset, A::Spin, A::Harmonic, A::Vintage}),
          ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip("The movement: drag sideways for Rate, up or down for Amount (Shift: finely)");
        setTitle("Movement");
        update();
    }

    /// Called when the mode changes (the section takes its name).
    std::function<void(A::ModeKind)> onMode;

    void refresh() override
    {
        MiniDisplay::refresh();
        if (ctx_.analysis != nullptr)
            if (const float position = ctx_.analysis->read(A::LfoPosition); position != position_)
            {
                position_ = position;
                repaint();
            }
        update();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        rateParam_ = ctx_.params.getPlain(A::Sync) >= 0.5f ? A::Div : A::Rate;
        start_ = e.position;
        startRate_ = spec(rateParam_).toNormalized(ctx_.params.getPlain(rateParam_));
        startAmount_ = spec(A::Amount).toNormalized(ctx_.params.getPlain(A::Amount));
        ctx_.params.beginGesture(rateParam_);
        ctx_.params.beginGesture(A::Amount);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& input = ui::Theme::current().input;
        const float scale = (e.mods.isShiftDown() ? input.fineFactor : 1.0f) / input.valueTravel;
        const auto to = [this](size_t param, float normalized)
        { ctx_.params.setPlain(param, spec(param).fromNormalized(std::clamp(normalized, 0.0f, 1.0f))); };
        to(rateParam_, startRate_ + (e.position.x - start_.x) * scale);
        to(A::Amount, startAmount_ + (start_.y - e.position.y) * scale);
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ctx_.params.endGesture(rateParam_);
        ctx_.params.endGesture(A::Amount);
    }

private:
    [[nodiscard]] const ParamSpec& spec(size_t param) const { return ctx_.info.params[param]; }

    /// The mode (for the section's name) and the readout: how deep or how wide, and how fast.
    void update()
    {
        const auto mode = modeOf(ctx_);
        if (mode != mode_)
        {
            mode_ = mode;
            if (onMode)
                onMode(mode);
        }
        const double amount = ctx_.params.getPlain(A::Amount) * 0.01;
        std::pair<juce::String, juce::String> text;
        if (mode == A::ModeKind::Tremolo)
        {
            const double dip = A::level(amount, -1.0, ctx_.params.getPlain(A::Vintage) >= 0.5f);
            text.first = dip <= 1e-6 ? juce::String("dips to silence")
                                     : "dips to " + juce::String(20.0 * std::log10(dip), 1) + " dB";
        }
        else
        {
            const int swing = juce::roundToInt(amount * 100.0);
            text.first = swing == 0 ? juce::String("C")
                                    : juce::String(swing) + "L" + juce::String::fromUTF8("\xE2\x80\x93") +
                                          juce::String(swing) + "R";
        }
        text.second = juce::String(rateOf(ctx_), 2) + " Hz";
        if (text != readout_)
        {
            readout_ = std::move(text);
            setDescription(readout_.first + ", " + readout_.second);
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
        auto plot = area.withTrimmedTop(inset / 2.0f).reduced(0.0f, inset / 2.0f);

        const auto& p = ctx_.params;
        const auto mode = modeOf(ctx_);
        const auto w = waveOf(ctx_);
        const double amount = p.getPlain(A::Amount) * 0.01;
        if (mode == A::ModeKind::Panning)
        {
            // Left above, right below, named in a gutter.
            const float gutter = juce::GlyphArrangement::getStringWidth(ui::capsLabelFont(), "M") +
                                 static_cast<float>(t.deviceGrid.displayInset);
            const auto full = plot;
            plot = plot.withTrimmedLeft(gutter);
            ui::paintCapsLabel(g, "L", full.withHeight(full.getHeight() * 0.5f),
                               juce::Justification::centredLeft);
            ui::paintCapsLabel(g, "R", full.withTrimmedTop(full.getHeight() * 0.5f),
                               juce::Justification::centredLeft);
            g.setColour(t.surface3);
            g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());
            stroke(g, plot, t.value,
                   [&](double position) { return 0.5 + 0.5 * amount * A::wave(w, position); });
        }
        else
        {
            const bool spin = static_cast<int>(p.getPlain(A::Stereo)) == 1;
            const double offset = p.getPlain(A::Offset) / 360.0, ratio = 1.0 + p.getPlain(A::Spin) * 0.01;
            const bool vintage = p.getPlain(A::Vintage) >= 0.5f;
            if (p.getPlain(A::Harmonic) >= 0.5f)
                stroke(g, plot, t.faded(t.value), [&](double position)
                       { return 1.0 - A::mirrorLevel(amount, A::wave(w, position), vintage); });
            stroke(g, plot, t.textSecondary,
                   [&](double position)
                   {
                       const double right = spin ? position * ratio : position + offset;
                       return 1.0 - A::level(amount, A::wave(w, right), vintage);
                   });
            stroke(g, plot, t.value,
                   [&](double position) { return 1.0 - A::level(amount, A::wave(w, position), vintage); });
        }
        if (ctx_.analysis != nullptr)
        {
            g.setColour(t.textTertiary);
            g.drawVerticalLine(juce::roundToInt(plot.getX() + position_ * 0.5f * plot.getWidth()),
                               plot.getY(), plot.getBottom());
        }
    }

    /// Two cycles of `down` (0 at the top of `plot`, 1 at the bottom) as a line.
    template <typename Down>
    static void stroke(juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour, Down down)
    {
        const int n = std::max(16, static_cast<int>(plot.getWidth()));
        juce::Path path;
        for (int i = 0; i <= n; ++i)
        {
            const double position = 2.0 * i / n;
            const juce::Point<float> pt(plot.getX() +
                                            static_cast<float>(i) / static_cast<float>(n) * plot.getWidth(),
                                        plot.getY() + static_cast<float>(down(position)) * plot.getHeight());
            i == 0 ? path.startNewSubPath(pt) : path.lineTo(pt);
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }

    ui::DeviceUiContext& ctx_;
    std::pair<juce::String, juce::String> readout_;
    float position_ = 0.0f;
    A::ModeKind mode_ = A::ModeKind::Panning;
    size_t rateParam_ = A::Rate;
    juce::Point<float> start_;
    float startRate_ = 0.0f, startAmount_ = 0.0f;
};

/// Sections per devices/auto-pan-tremolo/SPEC.md, on the module grid (docs/design/device-panels.md).
class AutoPanTremoloPanel final : public ui::DevicePanel
{
public:
    explicit AutoPanTremoloPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& effect = addSection("effect", kTitles[static_cast<int>(modeOf(ctx))], 6);
        auto& movement = own(std::make_unique<MovementDisplay>(ctx));
        effect.place(movement, 0, 0, 4, 3);
        movement.onMode = [&effect](A::ModeKind mode)
        { effect.setSectionTitle(kTitles[static_cast<int>(mode)]); };
        effect.segmented(A::Mode, 4, 0, 2, 2);
        effect.combo(A::Shape, 4, 2, 2);
        auto& rate = effect.knob(A::Rate, 0, 3);
        effect.knob(A::Amount, 1, 3);
        effect.knob(A::Edge, 2, 3);
        auto& phase = effect.knob(A::Phase, 3, 3);
        effect.knob(A::Attack, 4, 3);
        effect.toggle(A::Sync, 5, 3);
        effect.toggle(A::Invert, 5, 4);
        // The rate in Hz, or as a note value while Sync is on.
        rate.follow([&ctx] { return ctx.params.getPlain(A::Sync) >= 0.5f ? A::Div : A::Rate; });

        auto& tremolo = addSection("tremolo", "Stereo & Tone", 3);
        auto& stereoKnob = tremolo.knob(A::Offset, 0, 0);
        auto& stereo = tremolo.segmented(A::Stereo, 1, 0, 2, 1);
        auto& harmonic = tremolo.toggle(A::Harmonic, 1, 1, 2);
        auto& vintage = tremolo.toggle(A::Vintage, 1, 2, 2);
        // Offset, or Spin.
        stereoKnob.follow(
            [&ctx] { return static_cast<int>(ctx.params.getPlain(A::Stereo)) == 1 ? A::Spin : A::Offset; });

        auto& output = addSection("output", "Output", 2);
        output.knob(A::Output, 0, 0);
        output.toggle(A::LowCut, 0, 3, 2);
        auto& freq = output.bar(A::LowCutFreq, 0, 4, 2);

        // The TREMOLO section in Panning; Phase while free; Freq while Low Cut is off.
        dimWhen({&stereoKnob, &stereo, &harmonic, &vintage},
                [&ctx] { return modeOf(ctx) == A::ModeKind::Panning; });
        dimWhen({&phase}, [&ctx] { return ctx.params.getPlain(A::Sync) < 0.5f; });
        dimWhen({&freq}, [&ctx] { return ctx.params.getPlain(A::LowCut) < 0.5f; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? A::PeakInL : A::PeakInR); });
            meterOut_.setLevelSource([bus](int c)
                                     { return bus->takePeak(c == 0 ? A::PeakOutL : A::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& autoPanTremoloPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "auto-pan-tremolo",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<AutoPanTremoloPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
