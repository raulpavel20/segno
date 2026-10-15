// Modulators in the shell (M3 P5, ADR 0061, docs/design/modulators.md §15): Module ▸ Add
// modulator, Keep as played and Remove routes for the selected modulator, and Song ▸ Keep all as
// played. The chain view (DeviceChain) does the rest: the handle's drags, its routes, its menus.

#include "model/Modulators.h"
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
    actions_.add({.id = "module.keepRoll",
                  .name = "Keep as played",
                  .category = "Module",
                  .perform =
                      [this, selected]
                  {
                      if (const auto* info = detail_.chain().selectedModulator())
                          session_.keepRoll(*selected(), !info->kept);
                  },
                  .isEnabled =
                      [this]
                  {
                      const auto* info = detail_.chain().selectedModulator();
                      return info != nullptr && model::hasKeep(info->kind);
                  },
                  .label =
                      [this]
                  {
                      const auto* info = detail_.chain().selectedModulator();
                      return juce::String(info != nullptr && info->kept ? "Let it roll" : "Keep as played");
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

    // Every modulator in the song that rolls dice (ADR 0062): kept, or rolling again.
    const auto rolls = [this]
    {
        struct Rolls
        {
            int all = 0, kept = 0;
        } rolls;
        const auto snapshot = session_.snapshot();
        for (const auto* list : {&snapshot->tracks, &snapshot->returns})
            for (const auto& t : *list)
                for (const auto& d : t.devices)
                    if (d.modulator != nullptr && model::hasKeep(d.modulator->kind))
                    {
                        ++rolls.all;
                        rolls.kept += d.modulator->kept ? 1 : 0;
                    }
        return rolls;
    };
    actions_.add({.id = "song.keepAllRolls",
                  .name = "Keep all as played",
                  .category = "Song",
                  .perform =
                      [this, rolls]
                  {
                      const auto r = rolls();
                      session_.keepAllRolls(r.kept < r.all);
                  },
                  .isEnabled = [rolls] { return rolls().all > 0; },
                  .label =
                      [rolls]
                  {
                      const auto r = rolls();
                      return juce::String(r.all > 0 && r.kept == r.all ? "Let all roll"
                                                                       : "Keep all as played");
                  }});
}
} // namespace fd::app
