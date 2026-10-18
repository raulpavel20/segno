// MIDI effects in the shell (M3 P11, ADR 0074, docs/design/midi-effects.md §13): Module ▸ Add MIDI
// effect, before the instrument (the chain view places them: DeviceChain::addModule); what they
// follow, Track ▸ Harmony and Song ▸ Chords change.

#include "ui/Shell.h"

#include <fd/device/Registry.h>

namespace fd::app
{
void Shell::registerMidiEffectActions()
{
    auto& chain = detail_.chain();
    // Keep as notes (§9.1): the selected MIDI effect's track's clips get what they play.
    const auto selectedEffect = [this]() -> std::optional<std::pair<std::uint64_t, std::uint64_t>>
    {
        const auto id = detail_.chain().selectedDevice();
        if (!id)
            return std::nullopt;
        const auto snapshot = session_.snapshot();
        for (const auto& t : snapshot->tracks)
            for (const auto& d : t.devices)
                if (d.id == *id && d.midiEffect && d.kind == model::DeviceSlot::Kind::Native)
                    return std::pair{*id, t.id};
        return std::nullopt;
    };
    actions_.add({.id = "module.keepAsNotes",
                  .name = "Keep as notes",
                  .category = "Module",
                  .perform =
                      [this, selectedEffect]
                  {
                      const auto effect = selectedEffect();
                      const auto snapshot = session_.snapshot();
                      const auto* track = effect ? snapshot->channel(effect->second) : nullptr;
                      if (track == nullptr)
                          return;
                      std::string why;
                      if (!session_.keepAsNotes(effect->first,
                                                model::clipsToKeep(*track, arrangement_.selection()), &why))
                          showWarning(juce::String::fromUTF8(why.c_str()));
                  },
                  .isEnabled = [selectedEffect] { return selectedEffect().has_value(); }});
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

juce::PopupMenu Shell::trackHarmonyMenu()
{
    juce::PopupMenu menu;
    const auto channel = commandChannel();
    const auto snapshot = session_.snapshot();
    const auto* track = channel ? snapshot->channel(*channel) : nullptr;
    if (track == nullptr || !track->holdsClips())
        return menu;
    using U = model::HarmonyUse;
    for (const auto& [use, text] :
         {std::pair{U::Auto, "Auto: when it plays chords"}, std::pair{U::On, "On"}, std::pair{U::Off, "Off"}})
        menu.addItem(text, true, track->harmony == use,
                     [this, id = track->id, u = use] { session_.setTrackHarmony(id, u); });
    return menu;
}

juce::PopupMenu Shell::chordChangeMenu()
{
    juce::PopupMenu menu;
    using C = model::ChordChange;
    const auto now = session_.snapshot()->chordChange;
    for (const auto& [change, text] :
         {std::pair{C::Bar, "Every bar"}, std::pair{C::HalfBar, "Every half bar"},
          std::pair{C::Beat, "Every beat"}, std::pair{C::Any, "Any time"}})
        menu.addItem(text, true, now == change, [this, c = change] { session_.setChordChange(c); });
    return menu;
}
} // namespace fd::app
