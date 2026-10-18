// MIDI effects in the shell (M3 P11, ADR 0074, docs/design/midi-effects.md §13): Module ▸ Add MIDI
// effect, before the instrument. The chain view places them (DeviceChain::addModule).

#include "ui/Shell.h"

#include <fd/device/Registry.h>

namespace fd::app
{
void Shell::registerMidiEffectActions()
{
    auto& chain = detail_.chain();
    // Add MIDI effect ▸ each of ours: "pitch" → module.addMidiEffect.pitch.
    for (const auto* factory : fd::allDevices())
    {
        const auto& info = factory->info();
        if (info.kind != DeviceKind::MidiEffect)
            continue;
        const auto key = "fd:" + std::string(info.id);
        actions_.add({.id = "module.addMidiEffect." + std::string(info.id),
                      .name = "Add " + juce::String(std::string(info.name)),
                      .category = "Module",
                      .perform = [&chain, key] { chain.addModule(key); },
                      .isEnabled = [&chain] { return chain.track().has_value(); }});
    }
}
} // namespace fd::app
