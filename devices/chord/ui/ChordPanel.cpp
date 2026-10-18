#include "ChordPanel.h"

#include "../dsp/Chord.h"

namespace fd::devices
{
namespace
{
using C = Chord;

/// Learn (devices/chord/SPEC.md): the chord held becomes the shifts, in semitones from its lowest
/// note, as one undo step. Nothing to learn from fewer than two notes.
void learn(ui::DeviceUiContext& ctx)
{
    const auto* chord = dynamic_cast<const Chord*>(ctx.device);
    if (chord == nullptr)
        return;
    std::array<int, 7> held{};
    const auto n = chord->heldNotes(held);
    if (n < 2)
        return;
    ctx.params.beginEdit("Learn chord");
    const auto set = [&ctx](std::size_t p, float value)
    {
        ctx.params.beginGesture(p);
        ctx.params.setPlain(p, value);
        ctx.params.endGesture(p);
    };
    set(C::InKey, 0.0f);
    for (std::size_t i = 0; i < 6; ++i)
        set(C::Shift1 + i, i + 1 < n ? static_cast<float>(std::clamp(held[i + 1] - held[0], -36, 36)) : 0.0f);
}

/// Sections per devices/chord/SPEC.md, on the module grid (docs/design/device-panels.md).
class ChordPanel final : public ui::MidiEffectPanel
{
public:
    explicit ChordPanel(ui::DeviceUiContext& ctx) : MidiEffectPanel(ctx)
    {
        addNotesSection(4);

        auto& chord = addSection("chord", "Chord", 3);
        chord.segmented(C::Mode, 0, 0, 3, 1);
        chord.label("Strum", 0, 1, 3);
        auto& direction = chord.segmented(C::Direction, 0, 2, 3, 1);
        chord.knob(C::Strum, 0, 3);
        auto& curve = chord.knob(C::Curve, 1, 3);
        auto& swell = chord.knob(C::Swell, 2, 3);

        auto& add = addSection("add", "Add", 4);
        for (int i = 0; i < 6; ++i)
            add.knob(C::Shift1 + static_cast<std::size_t>(i), i % 3, i < 3 ? 0 : 3);
        add.toggle(C::InKey, 3, 0);
        addButton(add, "Learn", "Hold a chord and press Learn: its shape becomes the shifts", 3, 1, 1)
            .onClick = [&ctx] { learn(ctx); };
        add.knob(C::Level, 3, 3);

        auto& voices = addSection("voices", "Voices", 3);
        voices.segmented(C::Kind, 0, 0, 3, 1);
        voices.combo(C::Voicing, 0, 1, 3).setInlineName(true);
        auto& place = voices.combo(C::Place, 0, 2, 3);
        place.setInlineName(true);
        auto& change = voices.combo(C::Change, 0, 3, 3);
        change.setInlineName(true);
        auto& around = voices.bar(C::Around, 0, 4, 3);

        addHarmonySection(C::HarmonyFirst);

        auto& params = ctx.params;
        const auto mode = [&params] { return std::lround(params.getPlain(C::Mode)); };
        // ADD in Add, VOICES in Song and Keys: sections that come and go with the mode.
        showSectionWhen(add, [mode] { return mode() == C::Add; });
        showSectionWhen(voices, [mode] { return mode() != C::Add; });
        dimWhen({&place, &change}, [mode] { return mode() != C::Song; });
        dimWhen({&around}, [mode] { return mode() != C::Keys; });
        dimWhen({&direction, &curve, &swell}, [&params] { return params.getPlain(C::Strum) <= 0.0f; });
    }
};
} // namespace

const ui::PanelFactory& chordPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "chord",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<ChordPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
