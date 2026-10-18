#include "PitchPanel.h"

#include "../dsp/Pitch.h"

namespace fd::devices
{
namespace
{
using P = Pitch;

/// Sections per devices/pitch/SPEC.md, on the module grid (docs/design/device-panels.md).
class PitchPanel final : public ui::MidiEffectPanel
{
public:
    explicit PitchPanel(ui::DeviceUiContext& ctx) : MidiEffectPanel(ctx)
    {
        addNotesSection(4);

        auto& move = addSection("move", "Move", 2);
        move.knob(P::Semitones, 0, 0);
        move.knob(P::Steps, 1, 0);
        move.toggle(P::Mirror, 0, 3);
        auto& around = move.knob(P::Axis, 1, 3);

        auto& fit = addSection("fit", "Fit", 3);
        fit.segmented(P::Fit, 0, 0, 3, 1);
        auto& toward = fit.segmented(P::Toward, 0, 1, 3, 1);
        auto& passing = fit.toggle(P::Passing, 0, 2, 3);
        auto& change = fit.combo(P::Change, 0, 3, 3);
        change.setInlineName(true);

        auto& random = addSection("random", "Random", 2);
        random.knob(P::Chance, 0, 0);
        auto& spread = random.knob(P::Spread, 1, 0);
        auto& way = random.combo(P::Way, 0, 3, 2);
        way.setInlineName(true);

        auto& range = addSection("range", "Range", 2);
        range.knob(P::Lowest, 0, 0);
        range.knob(P::Highest, 1, 0);
        range.combo(P::Outside, 0, 3, 2).setInlineName(true);

        addHarmonySection(P::HarmonyFirst);

        auto& params = ctx.params;
        const auto plain = [&params](std::size_t p) { return std::lround(params.getPlain(p)); };
        dimWhen({&around}, [plain] { return plain(P::Mirror) == 0; });
        dimWhen({&toward}, [plain] { return plain(P::Fit) == 0; });
        dimWhen({&passing, &change}, [plain] { return plain(P::Fit) != 2; });
        dimWhen({&spread, &way}, [&params] { return params.getPlain(P::Chance) <= 0.0f; });
    }
};
} // namespace

const ui::PanelFactory& pitchPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "pitch",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<PitchPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
