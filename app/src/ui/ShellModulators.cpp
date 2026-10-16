// Modulators in the shell (M3 P5, ADR 0061, docs/design/modulators.md §15): Module ▸ Add
// modulator, Keep as played (the selected modulator's, or a Granular's) and Remove routes for the
// selected modulator, and Song ▸ Keep all as played. The chain view (DeviceChain) does the rest: the handle's
// drags, its routes, its menus.

#include "model/Chance.h"
#include "model/Modulators.h"
#include "model/Racks.h"
#include "ui/Shell.h"

namespace fd::app
{
void Shell::registerModulatorActions()
{
    auto& chain = detail_.chain();
    // Add modulator ▸ the five: after the selected module, or at the end of the chain (§6).
    for (const auto kind : model::allModulators)
    {
        const auto& info = model::modulatorInfo(kind);
        // "mod-lfo" → module.addModulator.lfo
        const auto id = "module.addModulator." + std::string(info.id.substr(4));
        actions_.add({.id = id,
                      .name = "Add " + juce::String(std::string(info.name)),
                      .category = "Module",
                      .perform = [&chain, kind] { chain.addModulator(kind); },
                      .isEnabled = [&chain] { return chain.track().has_value(); }});
    }
    // The selected modulator's (§13, §7.4).
    const auto selected = [this]() -> std::optional<std::uint64_t>
    {
        if (detail_.chain().selectedModulator() != nullptr)
            return detail_.chain().selectedDevice();
        return std::nullopt;
    };
    // The selected module's own dice (a Granular's: devices/granular/SPEC.md, G9): whether it
    // keeps its roll, when it rolls any.
    const auto moduleRoll = [this]() -> std::optional<bool>
    {
        const auto id = detail_.chain().selectedDevice();
        if (!id || detail_.chain().selectedModulator() != nullptr)
            return std::nullopt;
        const auto snapshot = session_.snapshot();
        for (const auto* channels : {&snapshot->tracks, &snapshot->returns})
            for (const auto& t : *channels)
                if (const auto* d = model::findDevice(t.devices, *id); d != nullptr)
                    return d->rollKept;
        if (const auto* d = model::findDevice(snapshot->master.devices, *id); d != nullptr)
            return d->rollKept;
        return std::nullopt;
    };
    actions_.add({.id = "module.keepRoll",
                  .name = "Keep as played",
                  .category = "Module",
                  .perform =
                      [this, selected, moduleRoll]
                  {
                      if (const auto* info = detail_.chain().selectedModulator())
                          session_.keepRoll(*selected(), !info->kept);
                      else if (const auto kept = moduleRoll())
                          session_.keepModuleRoll(*detail_.chain().selectedDevice(), !*kept);
                  },
                  .isEnabled =
                      [this, moduleRoll]
                  {
                      const auto* info = detail_.chain().selectedModulator();
                      return (info != nullptr && model::hasKeep(info->kind)) || moduleRoll().has_value();
                  },
                  .label =
                      [this, moduleRoll]
                  {
                      const auto* info = detail_.chain().selectedModulator();
                      const bool kept = info != nullptr ? info->kept : moduleRoll().value_or(false);
                      return juce::String(kept ? "Let it roll" : "Keep as played");
                  }});
    actions_.add({.id = "module.removeRoutes",
                  .name = "Remove routes",
                  .category = "Module",
                  .perform =
                      [this, selected]
                  {
                      if (const auto id = selected())
                          session_.removeRoutes(*id);
                  },
                  .isEnabled =
                      [this]
                  {
                      const auto* info = detail_.chain().selectedModulator();
                      return info != nullptr && !info->routes.empty();
                  }});

    // Every modulator and module in the song that rolls dice, and every clip with notes left to
    // chance (ADR 0062): kept, or rolling again.
    const auto rolls = [this] { return model::chanceIn(*session_.snapshot()); };
    actions_.add({.id = "song.keepAllRolls",
                  .name = "Keep all as played",
                  .category = "Song",
                  .perform =
                      [this, rolls]
                  {
                      const auto r = rolls();
                      session_.keepAllRolls(r.kept() < r.all());
                  },
                  .isEnabled = [rolls] { return rolls().all() > 0; },
                  .label =
                      [rolls]
                  {
                      const auto r = rolls();
                      return juce::String(r.all() > 0 && r.kept() == r.all() ? "Let all roll"
                                                                             : "Keep all as played");
                  }});
}
} // namespace fd::app
