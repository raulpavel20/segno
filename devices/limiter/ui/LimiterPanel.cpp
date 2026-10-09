#include "LimiterPanel.h"

#include "Limiter.h"
#include "fd/dsp/Math.h"

#include <cmath>

namespace fd::devices
{
namespace
{
using L = Limiter;

/// The output's loudness and peaks, as published by the device: short-term and integrated LUFS
/// and the loudest true peak since the last reset.
class LoudnessReadout final : public juce::Component
{
public:
    explicit LoudnessReadout(ui::DeviceUiContext& ctx) : ctx_(ctx) { setInterceptsMouseClicks(false, false); }

    /// Every frame: repaint when a value moved (at the precision shown).
    void tick()
    {
        if (ctx_.analysis == nullptr)
            return;
        const std::array<float, 3> now{ctx_.analysis->read(L::ShortTermLufs), ctx_.analysis->read(L::IntegratedLufs),
                                       ctx_.analysis->read(L::TruePeakMax)};
        if (texts(now) != texts(values_))
        {
            values_ = now;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        auto r = getLocalBounds().toFloat();
        g.setColour(t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);
        r = r.reduced(8.0f, 6.0f);
        static constexpr std::array<const char*, 3> captions{"Short-term", "Integrated", "True peak"};
        const auto text = texts(values_);
        const float rowHeight = r.getHeight() / 3.0f;
        for (size_t i = 0; i < captions.size(); ++i)
        {
            auto row = r.removeFromTop(rowHeight);
            g.setColour(t.textTertiary);
            g.setFont(ui::font(ui::TextStyle::Caption));
            g.drawText(captions[i], row.removeFromTop(16.0f), juce::Justification::topLeft, false);
            g.setColour(text[i] == kNone ? t.textTertiary : t.textPrimary);
            g.setFont(ui::font(ui::TextStyle::ValueMedium));
            g.drawText(text[i], row, juce::Justification::topLeft, false);
        }
    }

private:
    static inline const juce::String kNone = juce::String::fromUTF8("\xE2\x80\x93"); // en dash

    [[nodiscard]] static std::array<juce::String, 3> texts(const std::array<float, 3>& v)
    {
        auto format = [](float x, const char* unit, float floor) {
            return std::isfinite(x) && x > floor ? juce::String(x, 1) + " " + unit : kNone;
        };
        return {format(v[0], "LUFS", -70.0f), format(v[1], "LUFS", -70.0f), format(v[2], "dBTP", -99.0f)};
    }

    ui::DeviceUiContext& ctx_;
    std::array<float, 3> values_{-INFINITY, -INFINITY, -INFINITY};
};

/// Sections per devices/limiter/SPEC.md, on the module grid (docs/design/device-panels.md).
class LimiterPanel final : public ui::DevicePanel
{
public:
    explicit LimiterPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& history = addSection("history", "History", 6);
        history_ = &own(std::make_unique<ui::DynamicsHistory>(
            ctx, L::historyFifo, [&ctx] { return ctx.params.getPlain(L::Ceiling); }, -36.0f));
        history.place(*history_, 0, 0, 6, 6);

        auto& limit = addSection("limit", "Limit", 3);
        limit.knob(L::Gain, 0, 0);
        limit.knob(L::Ceiling, 1, 0);
        limit.knob(L::Release, 2, 0);
        limit.toggle(L::ReleaseAuto, 0, 3, 2);
        limit.toggle(L::TruePeak, 0, 4, 2);

        auto& mode = addSection("mode", "Mode", 3);
        mode.segmented(L::Mode, 0, 0, 3, 2); // stacked: "Maximise" needs two cells
        mode.segmented(L::Link, 0, 2, 3, 1);
        mode.label("Lookahead", 0, 3, 2);
        mode.combo(L::Lookahead, 0, 4, 2);

        auto& loudness = addSection("loudness", "Loudness", 3);
        readout_ = &own(std::make_unique<LoudnessReadout>(ctx));
        loudness.place(*readout_, 0, 0, 3, 5);
        auto& reset = own(std::make_unique<ui::Toggle>("Reset"));
        reset.setTooltip("Clear the integrated loudness and the true-peak maximum");
        reset.onClick = [&ctx] {
            if (ctx.analysis != nullptr)
                ctx.analysis->request(L::ResetLoudness);
        };
        loudness.place(reset, 0, 5, 3, 1, ui::PanelSection::Kind::Line);

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int c) { return bus->takePeak(c == 0 ? L::PeakOutL : L::PeakOutR); });
        setMeters({{&meter_, "Out"}});
    }

private:
    void tick()
    {
        if (!isShowing())
            return;
        history_->tick(!findSection("history")->isCollapsed());
        if (!findSection("loudness")->isCollapsed())
            readout_->tick();
    }

    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
    ui::DynamicsHistory* history_ = nullptr;
    LoudnessReadout* readout_ = nullptr;
    ui::Meter meter_{2};
};
} // namespace

const ui::PanelFactory& limiterPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "limiter",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<LimiterPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
