#include "UtilityPanel.h"

#include "Utility.h"

namespace fd::devices
{
namespace
{
/// Layout per devices/utility/SPEC.md: gain/balance and width/mono columns, a 2x2 grid of
/// switches, and the output meter.
class UtilityPanel final : public ui::DevicePanel
{
public:
    explicit UtilityPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        gain_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Utility::Gain));
        balance_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Utility::Balance));
        width_ = &bind(std::make_unique<ui::ParamKnob>(ctx, Utility::Width));
        mono_ = &bind(std::make_unique<ui::ParamToggle>(ctx, Utility::Mono));
        invL_ = &bind(std::make_unique<ui::ParamToggle>(ctx, Utility::InvertL));
        invR_ = &bind(std::make_unique<ui::ParamToggle>(ctx, Utility::InvertR));
        dc_ = &bind(std::make_unique<ui::ParamToggle>(ctx, Utility::DcFilter));
        mute_ = &bind(std::make_unique<ui::ParamToggle>(ctx, Utility::Mute));
        invL_->setButtonText(juce::String::fromUTF8("\xC3\x98 L")); // "Ø L"
        invR_->setButtonText(juce::String::fromUTF8("\xC3\x98 R"));

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int ch) {
                return bus->takePeak(ch == 0 ? Utility::PeakOutL : Utility::PeakOutR);
            });
        addAndMakeVisible(meter_);
    }

    int preferredWidth() const override { return 272; }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const auto& t = ui::Theme::current();
        const int gap = t.grid * 3;

        meter_.setBounds(body.removeFromRight(10).reduced(0, t.grid));
        body.removeFromRight(gap);

        // Bottom row: the five switches.
        auto switches = body.removeFromBottom(22);
        body.removeFromBottom(t.grid * 2);
        const int pillGap = t.grid + 2;
        const int pillW = (switches.getWidth() - 4 * pillGap) / 5;
        for (auto* toggle : {mono_, invL_, invR_, dc_, mute_})
        {
            toggle->setBounds(switches.removeFromLeft(pillW));
            switches.removeFromLeft(pillGap);
        }

        // Top row: three knobs sharing the width.
        const int knobW = (body.getWidth() - 2 * gap) / 3;
        for (auto* knob : {gain_, width_, balance_})
        {
            knob->setBounds(body.removeFromLeft(knobW));
            body.removeFromLeft(gap);
        }
    }

private:
    ui::ParamKnob* gain_ = nullptr;
    ui::ParamKnob* balance_ = nullptr;
    ui::ParamKnob* width_ = nullptr;
    ui::ParamToggle* mono_ = nullptr;
    ui::ParamToggle* invL_ = nullptr;
    ui::ParamToggle* invR_ = nullptr;
    ui::ParamToggle* dc_ = nullptr;
    ui::ParamToggle* mute_ = nullptr;
    ui::Meter meter_{2};
};
} // namespace

const ui::PanelFactory& utilityPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "utility",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<UtilityPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
