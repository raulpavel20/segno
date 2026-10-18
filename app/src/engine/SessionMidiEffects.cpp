// What MIDI effects follow (M3 P11, ADR 0074, docs/design/midi-effects.md §5): a track's Harmony,
// how often the song's chords may change, and the song view each MIDI effect is handed.

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/SongHarmony.h"

namespace fd::engine
{
model::HarmonyUse Session::Impl::harmonyOf(const te::Track& track)
{
    return model::harmonyUseOf(track.state.getProperty(harmonyPropertyId).toString().toStdString())
        .value_or(model::HarmonyUse::Auto);
}

model::ChordChange Session::Impl::chordChange() const
{
    return model::chordChangeOf(edit->state.getProperty(chordChangePropertyId).toString().toStdString())
        .value_or(model::ChordChange::Beat);
}

bool Session::setTrackHarmony(std::uint64_t track, model::HarmonyUse use)
{
    auto* t = impl_->channelTrack(track);
    if (t == nullptr || Impl::isReturn(*t) || Impl::harmonyOf(*t) == use)
        return false;
    static constexpr std::array<const char*, 3> names{"Harmony: Auto", "Harmony: On", "Harmony: Off"};
    impl_->beginEdit(names[static_cast<std::size_t>(use)]);
    auto* um = &impl_->edit->getUndoManager();
    if (use == model::HarmonyUse::Auto) // the default: nothing kept
        t->state.removeProperty(Impl::harmonyPropertyId, um);
    else
        t->state.setProperty(Impl::harmonyPropertyId, juce::String(std::string(model::idOf(use))), um);
    return true;
}

bool Session::setChordChange(model::ChordChange change)
{
    if (impl_->chordChange() == change)
        return false;
    impl_->beginEdit("Chords change");
    auto* um = &impl_->edit->getUndoManager();
    if (change == model::ChordChange::Beat) // the default
        impl_->edit->state.removeProperty(Impl::chordChangePropertyId, um);
    else
        impl_->edit->state.setProperty(Impl::chordChangePropertyId,
                                       juce::String(std::string(model::idOf(change))), um);
    return true;
}

void Session::Impl::publishSongViews(const model::Snapshot& song)
{
    std::map<std::uint64_t, std::shared_ptr<const SongView>> kept;
    for (auto* t : te::getAudioTracks(*edit))
    {
        std::vector<DevicePlugin*> effects;
        for (auto* p : t->pluginList)
            if (auto* native = dynamic_cast<DevicePlugin*>(p); native != nullptr && native->isMidiEffect())
                effects.push_back(native);
        if (effects.empty())
            continue;
        // The same song as before keeps its view: the modules aren't handed it again.
        const auto id = t->itemID.getRawID();
        auto view = model::songView(song, id);
        auto known = songViews.find(id);
        auto shared = known != songViews.end() && *known->second == view
                          ? known->second
                          : std::make_shared<const SongView>(std::move(view));
        for (auto* effect : effects)
            effect->setSongView(shared);
        kept[id] = std::move(shared);
    }
    songViews = std::move(kept);
}
} // namespace fd::engine
