#include "MelodyPanel.h"

#include "../../midi-shared/ui/LaneFeed.h"
#include "../dsp/Melody.h"

namespace fd::devices
{
namespace
{
using M = Melody;

/// Sections per devices/melody/SPEC.md (drawn, provisional), on the module grid.
class MelodyPanel final : public ui::MidiEffectPanel
{
public:
    explicit MelodyPanel(ui::DeviceUiContext& ctx)
        : MidiEffectPanel(ctx), lanes_(ctx, M::PitchLength,
                                       {{"Pitch", -7.0f, 14.0f, true, ""},
                                        {"Velocity", 0.0f, 200.0f, false, " %"},
                                        {"Gate", 0.0f, 200.0f, false, " %"},
                                        {"Chance", 0.0f, 100.0f, false, " %"},
                                        {"Ratchet", 1.0f, 4.0f, false, juce::String::fromUTF8("\xC3\x97")},
                                        {"Slide", 0.0f, 1.0f, false, ""}})
    {
        addStepsSection(8, {"Pitch", "Vel", "Gate", "Chance", "Ratch", "Slide"}, &lanes_);

        auto& write = addSection("write", "Write", 4);
        write.knob(M::Density, 0, 0);
        write.knob(M::Leaps, 1, 0);
        write.knob(M::Lowest, 2, 0);
        write.knob(M::Highest, 3, 0);
        write.combo(M::Contour, 0, 3, 4).setInlineName(true);
        write.combo(M::Phrase, 0, 4, 4).setInlineName(true);
        addButton(write, "Vary", "A new line from the same settings (Keep holds the one you hear)", 0, 5, 2);
        addButton(write, juce::String::fromUTF8("Learn\xE2\x80\xA6"),
                  "Drop a clip here, or choose one: its notes become the steps", 2, 5, 2);

        auto& change = addSection("change", "Change", 2);
        change.knob(M::Change, 0, 0);
        change.knob(M::Climb, 1, 0);
        auto& mode = change.segmented(M::ChangeMode, 0, 3, 2, 1);
        addButton(change, "Keep", "Holds the line you hear; its menu has the last rolls heard", 0, 4, 2);

        auto& play = addSection("play", "Play", 3);
        auto& rate = play.knob(M::Rate, 0, 0);
        rate.follow([&ctx] { return ctx.params.getPlain(M::Sync) >= 0.5f ? M::Rate : M::Hz; });
        play.toggle(M::Sync, 1, 1); // beside the dial, not its name
        play.combo(M::Plays, 0, 3, 3).setInlineName(true);
        play.combo(M::Sections, 0, 4, 3).setInlineName(true);
        play.combo(M::Notes, 0, 5, 3).setInlineName(true);

        addHarmonySection(M::HarmonyFirst);

        auto& params = ctx.params;
        dimWhen({&mode}, [&params] { return params.getPlain(M::Change) <= 0.0f; });
        listenAlsoWhen([&params] { return params.getPlain(M::Answer) >= 0.5f; });
    }

private:
    LaneFeed<M> lanes_;
};
} // namespace

const ui::PanelFactory& melodyPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "melody",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<MelodyPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
