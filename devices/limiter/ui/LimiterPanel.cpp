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
std::vector<ui::Readouts::Value> loudnessValues(const AnalysisBus* bus)
{
    const auto format = [](float x, const char* unit, float floor)
    { return std::isfinite(x) && x > floor ? juce::String(x, 1) + " " + unit : ui::Readouts::none(); };
    const auto read = [bus](int slot) { return bus != nullptr ? bus->read(slot) : NAN; }; // none in snapshots
    return {{"Short-term", format(read(L::ShortTermLufs), "LUFS", -70.0f), {}, {}},
            {"Integrated", format(read(L::IntegratedLufs), "LUFS", -70.0f), {}, {}},
            {"True peak", format(read(L::TruePeakMax), "dBTP", -99.0f), {}, {}}};
}

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
        readout_ = &own(std::make_unique<ui::Readouts>(1, false));
        readout_->set(loudnessValues(ctx.analysis));
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
            readout_->set(loudnessValues(context().analysis));
    }

    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
    ui::DynamicsHistory* history_ = nullptr;
    ui::Readouts* readout_ = nullptr;
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
