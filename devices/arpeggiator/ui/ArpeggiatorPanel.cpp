#include "ArpeggiatorPanel.h"

#include "../../midi-shared/ui/LaneFeed.h"
#include "../dsp/Arpeggiator.h"

namespace fd::devices
{
namespace
{
using A = Arpeggiator;

/// Sections per devices/arpeggiator/SPEC.md, on the module grid (docs/design/device-panels.md).
class ArpeggiatorPanel final : public ui::MidiEffectPanel
{
public:
    explicit ArpeggiatorPanel(ui::DeviceUiContext& ctx)
        : MidiEffectPanel(ctx), lanes_(ctx, A::VelocityLength,
                                       {{"Velocity", 0.0f, 200.0f, false, " %"},
                                        {"Gate", 0.0f, 200.0f, false, " %"},
                                        {"Octave", -2.0f, 2.0f, false, ""},
                                        {"Ratchet", 1.0f, 4.0f, false, juce::String::fromUTF8("\xC3\x97")},
                                        {"Chance", 0.0f, 100.0f, false, " %"}})
    {
        addStepsSection(6, {"Vel", "Gate", "Oct", "Ratch", "Chance"}, &lanes_);

        auto& play = addSection("play", "Play", 3);
        play.combo(A::Style, 0, 0, 3).setInlineName(true);
        play.toggle(A::Sync, 0, 1);
        play.toggle(A::Hold, 1, 1);
        play.combo(A::Retrigger, 0, 2, 3).setInlineName(true);
        auto& rate = play.knob(A::Rate, 0, 3);
        rate.follow([&ctx] { return ctx.params.getPlain(A::Sync) >= 0.5f ? A::Rate : A::Hz; });
        play.knob(A::Gate, 1, 3);
        play.knob(A::Swing, 2, 3);

        auto& range = addSection("range", "Range", 2);
        range.knob(A::Steps, 0, 0);
        auto& distance = range.knob(A::Distance, 1, 0);
        range.toggle(A::InKey, 0, 3, 2);
        range.toggle(A::SongChords, 0, 4, 2);
        auto& from = range.bar(A::From, 0, 5, 2);

        addHarmonySection(A::HarmonyFirst);

        auto& params = ctx.params;
        dimWhen({&distance}, [&params] { return params.getPlain(A::Steps) < 1.5f; });
        dimWhen({&from}, [&params] { return params.getPlain(A::SongChords) < 0.5f; });
    }

private:
    LaneFeed<A> lanes_;
};
} // namespace

const ui::PanelFactory& arpeggiatorPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "arpeggiator",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<ArpeggiatorPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
