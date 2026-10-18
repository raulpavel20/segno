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

SongNote songNoteOf(const model::Note& n)
{
    return {.start = n.start,
            .length = n.length,
            .key = static_cast<std::uint8_t>(std::clamp(n.pitch, 0, 127)),
            .velocity = n.velocity};
}

void mixInto(std::size_t& seed, std::size_t value)
{
    seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
}

/// A track's MIDI effects run offline (§9): ours that are on, up to `last` (null: all of them),
/// with what each hears and the roll each played.
struct ChainRun
{
    std::vector<DevicePlugin*> effects;
    std::vector<std::vector<SongNote>> heard;
    std::vector<std::uint64_t> rolls;
    bool foreign = false; // a plugin's MIDI effect among them: it can't run offline
};

ChainRun chainOf(te::AudioTrack& track, const te::Plugin* last, const model::Snapshot& song)
{
    ChainRun run;
    for (auto* p : track.pluginList)
    {
        if (Session::Impl::isNoteEffect(*p) && p->isEnabled())
        {
            if (auto* native = dynamic_cast<DevicePlugin*>(p))
                run.effects.push_back(native);
            else
                run.foreign = true;
        }
        if (p == last)
            break;
    }
    run.heard.resize(run.effects.size());
    run.rolls.resize(run.effects.size(), 0);
    for (std::size_t i = 0; i < run.effects.size(); ++i)
    {
        if (const auto* noteEffect = dynamic_cast<const devices::NoteEffect*>(&run.effects[i]->device()))
            run.rolls[i] = noteEffect->rollHeard();
        if (const auto id = run.effects[i]->getNoteSourceID(); id.isValid())
            if (const auto* source = song.channel(id.getRawID()))
            {
                for (const auto& c : source->clips)
                    for (const auto& n : model::playedNotes(c, song.tempo))
                        run.heard[i].push_back(songNoteOf(n));
                std::sort(run.heard[i].begin(), run.heard[i].end(),
                          [](const auto& a, const auto& b) { return a.start < b.start; });
            }
    }
    return run;
}

/// What `run` plays over a clip, given its notes as they play: in song beats.
std::vector<SongNote> playOver(const ChainRun& run, const model::ClipInfo& clip,
                               const std::vector<SongNote>& in, const model::Snapshot& song,
                               const SongView& view, KeyInfo key)
{
    std::vector<std::unique_ptr<Device>> copies;
    std::vector<Device*> chain;
    std::vector<const std::vector<SongNote>*> listen;
    for (std::size_t i = 0; i < run.effects.size(); ++i)
        if (auto copy = copyOf(*run.effects[i]))
        {
            chain.push_back(copy.get());
            copies.push_back(std::move(copy));
            listen.push_back(run.heard[i].empty() ? nullptr : &run.heard[i]);
        }
    const auto& tempo = song.tempo;
    devices::NoteRun noteRun{.transport = {.tempoBpm = tempo.bpm,
                                           .timeSigNumerator = tempo.numerator,
                                           .timeSigDenominator = tempo.denominator,
                                           .key = key},
                             .song = &view,
                             .from = tempo.beatsForSeconds(clip.start),
                             .to = tempo.beatsForSeconds(clip.end()),
                             .listen = listen,
                             .rolls = run.rolls};
    return devices::runNotes(chain, in, noteRun);
}

/// The seed a clip's chance is decided with, as playback last decided it (or as it keeps it).
std::optional<std::uint32_t> chanceSeedOf(te::Clip& clip, const model::ClipInfo& info, const ChancePass* pass)
{
    if (const auto kept = keptChanceOf(clip))
        return model::passSeed(info.chanceSeed, kept->roll, kept->lap);
    if (pass != nullptr)
        return model::passSeed(info.chanceSeed, pass->roll.load(), 0);
    return std::nullopt;
}

std::vector<SongNote> notesIn(const model::ClipInfo& info, const model::TempoMap& tempo,
                              std::optional<std::uint32_t> seed)
{
    std::vector<SongNote> in;
    for (const auto& n : model::playedNotes(info, tempo, seed))
        in.push_back(songNoteOf(n));
    return in;
}
} // namespace

std::size_t Session::Impl::effectsSignature(te::AudioTrack& track) const
{
    std::size_t seed = 0;
    for (auto* p : track.pluginList)
        if (auto* native = dynamic_cast<DevicePlugin*>(p); native != nullptr && native->isMidiEffect())
        {
            mixInto(seed, p->itemID.getRawID());
            mixInto(seed, p->isEnabled() ? 1 : 2);
            for (std::size_t i = 0; i < native->device().params().size(); ++i)
                mixInto(seed, std::hash<float>{}(native->storedValue(i)));
            nlohmann::json extra = nlohmann::json::object();
            native->device().writeExtraState(extra);
            mixInto(seed, std::hash<std::string>{}(extra.dump()));
            mixInto(seed, native->getNoteSourceID().getRawID());
            if (const auto* noteEffect = dynamic_cast<const devices::NoteEffect*>(&native->device()))
                mixInto(seed, noteEffect->rollHeard());
        }
    return seed;
}

void Session::Impl::PlayedCheck::timerCallback()
{
    if (impl.edit == nullptr)
        return;
    for (auto* t : te::getAudioTracks(*impl.edit))
        if (const auto known = impl.playedSignatures.find(t->itemID.getRawID());
            known != impl.playedSignatures.end() && known->second != impl.effectsSignature(*t))
        {
            impl.triggerAsyncUpdate(); // a new snapshot plays them again
            return;
        }
}

void Session::Impl::fillPlayed(model::Snapshot& song)
{
    std::map<std::uint64_t, PlayedRun> kept;
    playedSignatures.clear();
    for (auto* t : te::getAudioTracks(*edit))
    {
        const auto id = t->itemID.getRawID();
        auto* info = [&]() -> model::TrackInfo*
        {
            for (auto& tr : song.tracks)
                if (tr.id == id)
                    return &tr;
            return nullptr;
        }();
        const auto view = songViews.find(id);
        if (info == nullptr || view == songViews.end())
            continue; // no MIDI effect of ours on it
        const auto run = chainOf(*t, nullptr, song);
        playedSignatures[id] = effectsSignature(*t);
        if (run.effects.empty() || run.foreign)
            continue;
        std::size_t chainKey = playedSignatures[id];
        mixInto(chainKey, std::hash<const void*>{}(view->second.get()));
        mixInto(chainKey, std::hash<double>{}(song.tempo.bpm));
        for (const auto& heard : run.heard)
            for (const auto& n : heard)
                mixInto(chainKey, std::hash<double>{}(n.start) ^ n.key);
        const auto key = run.effects.front()->songKey();
        mixInto(chainKey, static_cast<std::size_t>(key.root) << 16 | key.notes);
        for (auto& c : info->clips)
        {
            if (c.kind != model::ClipInfo::Kind::Midi || c.muted)
                continue;
            auto* clip = this->clip(c.id);
            if (clip == nullptr)
                continue;
            const auto seed = chanceSeedOf(*clip, c, chancePass.get());
            const auto in = notesIn(c, song.tempo, seed);
            std::size_t clipKey = chainKey;
            mixInto(clipKey, std::hash<double>{}(c.start));
            mixInto(clipKey, std::hash<double>{}(c.end()));
            mixInto(clipKey, std::hash<double>{}(c.offset));
            for (const auto& n : in)
                mixInto(clipKey, std::hash<double>{}(n.start) ^ (std::hash<double>{}(n.length) << 1) ^ n.key ^
                                     std::hash<float>{}(n.velocity));
            auto& entry = kept[c.id];
            if (const auto cached = playedCache.find(c.id);
                cached != playedCache.end() && cached->second.key == clipKey)
                entry = std::move(cached->second);
            else
            {
                entry.key = clipKey;
                entry.notes.clear();
                // In the clip's beats, from its content's start, as its notes are.
                const double zero = song.tempo.beatsForSeconds(c.start - c.offset);
                for (const auto& n : playOver(run, c, in, song, *view->second, key))
                    entry.notes.push_back({.pitch = n.key,
                                           .start = n.start - zero,
                                           .length = n.length,
                                           .velocity = n.velocity});
            }
            c.played = entry.notes;
        }
    }
    playedCache = std::move(kept);
    if (!playedSignatures.empty() && !playedCheck.isTimerRunning())
        playedCheck.startTimer(250);
}

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

    updateSnapshotNow();
    const auto snap = snapshot();
    const auto trackId = track->itemID.getRawID();
    const auto* info = snap->channel(trackId);
    if (info == nullptr)
        return false;
    const auto run = chainOf(*track, last, *snap);
    if (run.foreign)
        return refuse("A plugin's MIDI effect can't be kept as notes yet: only Segno's.");
    if (run.effects.empty())
        return refuse("Its MIDI effects are off: there's nothing they play to keep.");
    std::vector<const model::ClipInfo*> clips;
    for (const auto& c : info->clips)
        if (c.kind == model::ClipInfo::Kind::Midi &&
            (clipIds.empty() || std::find(clipIds.begin(), clipIds.end(), c.id) != clipIds.end()))
            clips.push_back(&c);
    if (clips.empty())
        return refuse("There's no MIDI clip to keep its notes in.");

    const auto& tempo = snap->tempo;
    const auto view = model::songView(*snap, trackId);
    const auto key = run.effects.front()->songKey();
    EditGroup group(*this, "Keep as notes");
    auto* um = &impl_->edit->getUndoManager();
    for (const auto* c : clips)
    {
        auto* clip = dynamic_cast<te::MidiClip*>(impl_->clip(c->id));
        if (clip == nullptr)
            continue;
        // What it plays, its chance decided as playback last did (or as it keeps it).
        const auto in = notesIn(*c, tempo, chanceSeedOf(*clip, *c, impl_->chancePass.get()));
        const auto played = playOver(run, *c, in, *snap, view, key);

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
        int count = 0;
        for (auto take : stack)
            count += take[te::IDs::name].toString().startsWith(model::kKeptNotesName) ? 1 : 0;
        const auto name = count == 0 ? juce::String(model::kKeptNotesName)
                                     : juce::String(model::kKeptNotesName) + " " + juce::String(count + 1);
        auto take = impl_->addTake(stack, name, {clip->state}, um);
        clip->state.setProperty(Impl::fromTakeId, static_cast<juce::int64>(Impl::idOf(take.getChild(0))), um);
    }
    for (auto* e : run.effects) // kept: turned off, not removed
        e->setEnabled(false);
    impl_->settle(*track, um);
    return true;
}
} // namespace fd::engine
