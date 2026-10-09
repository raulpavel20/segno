#include "UtilityPanel.h"

#include "Utility.h"

namespace fd::devices
{
namespace
{
/// Sections per devices/utility/SPEC.md, on the module grid (docs/design/device-panels.md).
class UtilityPanel final : public ui::DevicePanel
{
public:
    explicit UtilityPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& level = addSection("level", "Level", 1);
        level.knob(Utility::Gain, 0, 0);
        level.knob(Utility::Balance, 0, 3);

        auto& stereo = addSection("stereo", "Stereo", 1);
        stereo.knob(Utility::Width, 0, 0);
        stereo.toggle(Utility::Mono, 0, 3);

        auto& channels = addSection("channels", "Channels", 1);
        channels.toggle(Utility::InvertL, 0, 0).setButtonText(juce::String::fromUTF8("\xC3\x98 L")); // "Ø L"
        channels.toggle(Utility::InvertR, 0, 1).setButtonText(juce::String::fromUTF8("\xC3\x98 R"));
        channels.toggle(Utility::DcFilter, 0, 2);
        channels.toggle(Utility::Mute, 0, 3);

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int ch) {
                return bus->takePeak(ch == 0 ? Utility::PeakOutL : Utility::PeakOutR);
            });
        setMeters({{&meter_, "Out"}});
    }

private:
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
