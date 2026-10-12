#include "LoudnessMeterPanel.h"

#include "LoudnessMeter.h"

namespace fd::devices
{
namespace
{
using M = LoudnessMeter;

class LoudnessMeterPanel final : public ui::DevicePanel
{
public:
    explicit LoudnessMeterPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& measure = addSection("measure", "Measure", 4);
        measure.knob(M::Target, 0, 0);
        measure.knob(M::Ceiling, 1, 0);
    }
};
} // namespace

const ui::PanelFactory& loudnessMeterPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "loudness-meter",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<LoudnessMeterPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
