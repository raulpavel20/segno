// What MIDI effects follow (M3 P11, ADR 0074, docs/design/midi-effects.md §5): a track's Harmony,
// how often the song's chords may change, and the song view each MIDI effect is handed.

#include "NoteEffect.h"
#include "NoteRun.h"
#include "engine/ClipChance.h"
#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/Chance.h"
#include "model/SongHarmony.h"
#include "model/Takes.h"

#include <fd/device/Registry.h>
#include <functional>

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

std::vector<std::uint64_t> Session::Impl::listenSources(DevicePlugin& effect) const
{
    auto* home = effect.getOwnerTrack();
    if (home == nullptr || effect.isInRack())
        return {}; // in a rack, step 2
    const auto all = tracks();
    // Whether a track's notes come, through what it listens to, from the module's own track.
    std::function<bool(te::Track&, int)> hearsOwner = [&](te::Track& t, int depth)
    {
        if (&t == home)
            return true;
        if (depth > static_cast<int>(all.size()))
            return false;
        for (auto* p : t.getAllPlugins())
            if (const auto id = p->getNoteSourceID(); id.isValid())
                if (auto* source = te::findTrackForID(*edit, id))
                    if (hearsOwner(*source, depth + 1))
                        return true;
        return false;
    };
    std::vector<std::uint64_t> out;
    for (auto* t : all)
        if (!isReturn(*t) && !hearsOwner(*t, 0))
            out.push_back(t->itemID.getRawID());
    return out;
}

bool Session::Impl::setListen(DevicePlugin& effect, std::optional<std::uint64_t> trackId)
{
    te::EditItemID source;
    if (trackId)
    {
        const auto allowed = listenSources(effect);
        if (std::find(allowed.begin(), allowed.end(), *trackId) == allowed.end())
            return false;
        source = track(*trackId)->itemID;
    }
    if (effect.getNoteSourceID() == source)
        return true;
    beginEdit("Set Listen");
    effect.setListenTo(source, &edit->getUndoManager()); // playback rebuilt: the notes reach it
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
namespace
{
/// A fresh copy of a MIDI effect as the project keeps it: its parameters' stored values (under any
/// automation), its extra state.
std::unique_ptr<Device> copyOf(DevicePlugin& effect)
{
    const auto* factory = fd::findDevice(effect.device().info().id);
    if (factory == nullptr)
        return nullptr;
    auto copy = factory->create();
    for (std::size_t i = 0; i < copy->params().size(); ++i)
        copy->params().setNormalized(i, effect.storedValue(i));
    nlohmann::json extra = nlohmann::json::object();
    effect.device().writeExtraState(extra);
    copy->readExtraState(extra);
    return copy;
}
} // namespace

bool Session::keepAsNotes(std::uint64_t upTo, const std::vector<std::uint64_t>& clipIds, std::string* why)
{
    const auto refuse = [why](const char* text)
    {
        if (why != nullptr)
            *why = text;
        return false;
    };
    auto* last = dynamic_cast<DevicePlugin*>(impl_->plugin(upTo));
    auto* track = last != nullptr ? dynamic_cast<te::AudioTrack*>(last->getOwnerTrack()) : nullptr;
    if (last == nullptr || !last->isMidiEffect() || last->isInRack() || track == nullptr)
        return refuse("Keep as notes keeps what a track's MIDI effects play: choose one of them.");
    // Its MIDI effects up to it, in order; the ones turned off play nothing.
    std::vector<DevicePlugin*> effects;
    for (auto* p : track->pluginList)
    {
        if (Impl::isNoteEffect(*p) && p->isEnabled())
        {
            auto* native = dynamic_cast<DevicePlugin*>(p);
            if (native == nullptr)
                return refuse("A plugin's MIDI effect can't be kept as notes yet: only Segno's.");
            effects.push_back(native);
        }
        if (p == last)
            break;
    }
    if (effects.empty())
        return refuse("Its MIDI effects are off: there's nothing they play to keep.");

    updateSnapshotNow();
    const auto snap = snapshot();
    const auto trackId = track->itemID.getRawID();
    const auto* info = snap->channel(trackId);
    if (info == nullptr)
        return false;
    std::vector<const model::ClipInfo*> clips;
    for (const auto& c : info->clips)
        if (c.kind == model::ClipInfo::Kind::Midi &&
            (clipIds.empty() || std::find(clipIds.begin(), clipIds.end(), c.id) != clipIds.end()))
            clips.push_back(&c);
    if (clips.empty())
        return refuse("There's no MIDI clip to keep its notes in.");

    // What the modules read: the song, the notes each hears, the rolls heard.
    const auto tempo = snap->tempo;
    const auto view = model::songView(*snap, trackId);
    std::vector<std::vector<SongNote>> heard(effects.size());
    std::vector<const std::vector<SongNote>*> listen(effects.size(), nullptr);
    std::vector<std::uint64_t> rolls(effects.size(), 0);
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        if (const auto* noteEffect = dynamic_cast<const devices::NoteEffect*>(&effects[i]->device()))
            rolls[i] = noteEffect->rollHeard();
        if (const auto id = effects[i]->getNoteSourceID(); id.isValid())
            if (const auto* source = snap->channel(id.getRawID()))
            {
                for (const auto& c : source->clips)
                    for (const auto& n : model::playedNotes(c, tempo))
                        heard[i].push_back({.start = n.start,
                                            .length = n.length,
                                            .key = static_cast<std::uint8_t>(std::clamp(n.pitch, 0, 127)),
                                            .velocity = n.velocity});
                std::sort(heard[i].begin(), heard[i].end(),
                          [](const auto& a, const auto& b) { return a.start < b.start; });
                listen[i] = &heard[i];
            }
    }

    EditGroup group(*this, "Keep as notes");
    auto* um = &impl_->edit->getUndoManager();
    const auto keyInfo = effects.front()->songKey();
    for (const auto* c : clips)
    {
        auto* clip = dynamic_cast<te::MidiClip*>(impl_->clip(c->id));
        if (clip == nullptr)
            continue;
        // What it plays, its chance decided as playback last did (or as it keeps it).
        std::optional<std::uint32_t> seed;
        if (const auto kept = keptChanceOf(*clip))
            seed = model::passSeed(c->chanceSeed, kept->roll, kept->lap);
        else if (impl_->chancePass != nullptr)
            seed = model::passSeed(c->chanceSeed, impl_->chancePass->roll.load(), 0);
        std::vector<SongNote> in;
        for (const auto& n : model::playedNotes(*c, tempo, seed))
            in.push_back({.start = n.start,
                          .length = n.length,
                          .key = static_cast<std::uint8_t>(std::clamp(n.pitch, 0, 127)),
                          .velocity = n.velocity});
        std::vector<std::unique_ptr<Device>> copies;
        std::vector<Device*> chain;
        for (auto* e : effects)
            if (auto copy = copyOf(*e))
            {
                chain.push_back(copy.get());
                copies.push_back(std::move(copy));
            }
        devices::NoteRun run{.transport = {.tempoBpm = tempo.bpm,
                                           .timeSigNumerator = tempo.numerator,
                                           .timeSigDenominator = tempo.denominator,
                                           .key = keyInfo},
                             .song = &view,
                             .from = tempo.beatsForSeconds(c->start),
                             .to = tempo.beatsForSeconds(c->end()),
                             .listen = listen,
                             .rolls = rolls};
        const auto played = devices::runNotes(chain, in, run);

        // The clip's notes before, as a take; a plain clip gets a stack (as bounce in place does).
        auto stack = impl_->stackOf(*clip);
        bool keepSource = true;
        if (stack.isValid())
        {
            if (const auto* s = info->stack(Impl::idOf(stack)))
                keepSource = !model::playingTake(*s, info->clips).has_value();
        }
        else
            stack = impl_->makeStack(*track, c->start, c->end(), um);
        if (keepSource)
            impl_->addTake(stack, model::kBeforeMidiEffectsName, {clip->state}, um);
        // A looped clip's laps become notes: the clip plays them once, from its start.
        if (clip->isLooping())
        {
            const auto position = clip->getPosition();
            clip->disableLooping();
            clip->setPosition({position.time, te::TimeDuration()});
        }
        const double zero = tempo.beatsForSeconds(c->start);
        std::vector<model::Note> notes;
        for (const auto& n : played)
            notes.push_back(
                {.pitch = n.key, .start = n.start - zero, .length = n.length, .velocity = n.velocity});
        setNotes(c->id, notes);
        std::vector<model::TakeInfo> names;
        for (auto take : stack)
            names.push_back({.name = take[te::IDs::name].toString().toStdString()});
        int count = 0;
        for (const auto& t : names)
            count += t.name.rfind(model::kKeptNotesName, 0) == 0 ? 1 : 0;
        const auto name = count == 0 ? juce::String(model::kKeptNotesName)
                                     : juce::String(model::kKeptNotesName) + " " + juce::String(count + 1);
        auto take = impl_->addTake(stack, name, {clip->state}, um);
        clip->state.setProperty(Impl::fromTakeId, static_cast<juce::int64>(Impl::idOf(take.getChild(0))), um);
    }
    for (auto* e : effects) // kept: turned off, not removed
        e->setEnabled(false);
    impl_->settle(*track, um);
    return true;
}
} // namespace fd::engine
