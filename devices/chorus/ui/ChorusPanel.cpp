#include "ChorusPanel.h"

#include "Chorus.h"

#include <utility>

namespace fd::devices
{
namespace
{
using C = Chorus;

/// The voices' delays hardly depend on the sample rate (only Vibrato's shortest does, by a sample).
constexpr double kDisplayRate = 48000.0;

C::Voicing voicingOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    return {.mode = static_cast<C::ModeKind>(static_cast<int>(p.getPlain(C::Mode))),
            .depth = p.getPlain(C::Depth) * 0.01,
            .delayMs = p.getPlain(C::DelayTime),
            .shape = p.getPlain(C::Shape) * 0.01,
            .offset = p.getPlain(C::Offset) / 360.0};
}

/// The rate the voices move at. Synced, it's what the device last played at (it knows the
/// tempo); where it hasn't played (snapshots), 120 BPM in 4/4.
double rateOf(const ui::DeviceUiContext& ctx)
{
    const auto& p = ctx.params;
    if (p.getPlain(C::Sync) < 0.5f)
        return p.getPlain(C::Rate);
    if (ctx.analysis != nullptr)
        if (const float r = ctx.analysis->read(C::RateHz); r > 0.0f)
            return r;
    return 2.0 / C::divisionBeats(static_cast<int>(p.getPlain(C::Div)), 4.0);
}

/// The voices drawn (SPEC: UI layout): each voice's movement over two cycles, as tall as Depth,
/// the left side's voices over the right's. Its line reads how far they bend the pitch and the
/// delays they sweep. Drag sideways for Rate (note values while synced), up or down for Depth.
class VoicesDisplay final : public ui::MiniDisplay, public juce::SettableTooltipClient
{
public:
    explicit VoicesDisplay(ui::DeviceUiContext& ctx)
        : MiniDisplay(ctx, {},
                      {C::Mode, C::Rate, C::Sync, C::Div, C::Depth, C::Shape, C::Offset, C::DelayTime}),
          ctx_(ctx)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip("The voices' movement: drag sideways for Rate, up or down for Depth (Shift: finely)");
        setTitle("Voices");
        updateReadout();
    }

    void refresh() override
    {
        MiniDisplay::refresh();
        const float position = ctx_.analysis != nullptr ? ctx_.analysis->read(C::LfoPosition) : 0.0f;
        const double rate = rateOf(ctx_);
        if (position != position_ || rate != rate_)
        {
            position_ = position;
            repaint();
        }
        updateReadout();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        rateParam_ = ctx_.params.getPlain(C::Sync) >= 0.5f ? C::Div : C::Rate;
        start_ = e.position;
        startRate_ = spec(rateParam_).toNormalized(ctx_.params.getPlain(rateParam_));
        startDepth_ = spec(C::Depth).toNormalized(ctx_.params.getPlain(C::Depth));
        ctx_.params.beginGesture(rateParam_);
        ctx_.params.beginGesture(C::Depth);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& input = ui::Theme::current().input;
        const float scale = (e.mods.isShiftDown() ? input.fineFactor : 1.0f) / input.valueTravel;
        const auto to = [this](size_t param, float normalized)
        {
            const auto& s = spec(param);
            ctx_.params.setPlain(param, s.fromNormalized(std::clamp(normalized, 0.0f, 1.0f)));
        };
        to(rateParam_, startRate_ + (e.position.x - start_.x) * scale);
        to(C::Depth, startDepth_ + (start_.y - e.position.y) * scale);
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ctx_.params.endGesture(rateParam_);
        ctx_.params.endGesture(C::Depth);
    }

private:
    [[nodiscard]] const ParamSpec& spec(size_t param) const { return ctx_.info.params[param]; }

    /// How far the voices bend the pitch ("±12 cents") and the delays they sweep ("6.0–8.0 ms"),
    /// drawn and as the display's description.
    void updateReadout()
    {
        const auto v = voicingOf(ctx_);
        rate_ = rateOf(ctx_);
        const double cents = C::pitchSwing(v, rate_, kDisplayRate);
        const double shortest = C::baseMs(v.mode, v.delayMs, kDisplayRate);
        std::pair<juce::String, juce::String> text{
            juce::String::fromUTF8("\xC2\xB1") + juce::String(juce::roundToInt(cents)) + " cents",
            juce::String(shortest, 1) + juce::String::fromUTF8("\xE2\x80\x93") +
                juce::String(shortest + C::sweepMs(v.mode, v.depth), 1) + " ms"};
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

        // The readout, then the plot below it.
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.setColour(t.textSecondary);
        auto line = area.removeFromTop(text);
        g.drawText(readout_.first, line, juce::Justification::centredLeft, false);
        g.drawText(readout_.second, line, juce::Justification::centredRight, false);
        // Room for the strokes at the top and bottom.
        const auto plot = area.withTrimmedTop(inset / 2.0f).reduced(0.0f, inset / 2.0f);

        // Each voice's movement, as tall as Depth: what moves its delay (the readout gives the
        // delays themselves).
        const auto v = voicingOf(ctx_);
        const auto half = static_cast<float>(v.depth) * plot.getHeight() * 0.5f;
        const int n = std::max(16, static_cast<int>(plot.getWidth()));
        const int voices = C::voiceCount(v.mode);
        for (const int side : {1, 0})
        {
            g.setColour(side == 0 ? t.value : t.textSecondary);
            for (int k = 0; k < voices; ++k)
            {
                juce::Path path;
                for (int i = 0; i <= n; ++i)
                {
                    const double position = 2.0 * i / n;
                    const juce::Point<float> pt(
                        plot.getX() + static_cast<float>(i) / static_cast<float>(n) * plot.getWidth(),
                        plot.getCentreY() - static_cast<float>(C::voiceValue(v, side, k, position)) * half);
                    i == 0 ? path.startNewSubPath(pt) : path.lineTo(pt);
                }
                g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
            }
        }
        // Where the movement is now, in the first cycle, while it plays.
        if (ctx_.analysis != nullptr)
        {
            g.setColour(t.textTertiary);
            g.drawVerticalLine(juce::roundToInt(plot.getX() + position_ * 0.5f * plot.getWidth()),
                               plot.getY(), plot.getBottom());
        }
    }

    ui::DeviceUiContext& ctx_;
    std::pair<juce::String, juce::String> readout_;
    float position_ = 0.0f;
    double rate_ = 0.0;
    size_t rateParam_ = C::Rate;
    juce::Point<float> start_;
    float startRate_ = 0.0f, startDepth_ = 0.0f;
};

/// Sections per devices/chorus/SPEC.md, on the module grid (docs/design/device-panels.md).
class ChorusPanel final : public ui::DevicePanel
{
public:
    explicit ChorusPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& modulation = addSection("modulation", "Modulation", 6);
        modulation.place(own(std::make_unique<VoicesDisplay>(ctx)), 0, 0, 4, 3);
        modulation.segmented(C::Mode, 4, 0, 2, 3);
        auto& rate = modulation.knob(C::Rate, 0, 3);
        modulation.knob(C::Depth, 1, 3);
        modulation.knob(C::Shape, 2, 3);
        modulation.knob(C::Offset, 3, 3);
        modulation.toggle(C::Sync, 4, 3, 2);
        // The rate in Hz, or as a note value while Sync is on.
        rate.follow([&ctx] { return ctx.params.getPlain(C::Sync) >= 0.5f ? C::Div : C::Rate; });

        auto& voices = addSection("voices", "Voices", 3);
        auto& delay = voices.knob(C::DelayTime, 0, 0);
        auto& width = voices.knob(C::Width, 1, 0);
        voices.knob(C::Warmth, 2, 0);
        auto& feedback = voices.bar(C::Feedback, 0, 3, 2);
        auto& invert = voices.toggle(C::Invert, 2, 3);
        voices.toggle(C::LowCut, 0, 4, 2);
        auto& freq = voices.bar(C::LowCutFreq, 0, 5, 2);

        auto& output = addSection("output", "Output", 1);
        auto& mix = output.knob(C::Mix, 0, 0);
        output.knob(C::Output, 0, 3);

        // Vibrato is the voices alone, without feedback.
        dimWhen({&delay, &width, &feedback, &invert, &mix},
                [&ctx] { return static_cast<int>(ctx.params.getPlain(C::Mode)) == 2; });
        dimWhen({&freq}, [&ctx] { return ctx.params.getPlain(C::LowCut) < 0.5f; });

        if (auto* bus = ctx.analysis)
        {
            meterIn_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? C::PeakInL : C::PeakInR); });
            meterOut_.setLevelSource([bus](int c)
                                     { return bus->takePeak(c == 0 ? C::PeakOutL : C::PeakOutR); });
        }
        setMeters({{&meterIn_, "In"}, {&meterOut_, "Out"}});
    }

private:
    ui::Meter meterIn_{2}, meterOut_{2};
};
} // namespace

const ui::PanelFactory& chorusPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "chorus",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<ChorusPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
