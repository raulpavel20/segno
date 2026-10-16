// Warping and transposition (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7, §8.1): Warp,
// the clip's tempo, its mode and its setting, sensitivity, transposition, and warp markers.

#include "engine/ClipFades.h"
#include "engine/FileIndexer.h"
#include "engine/SessionImpl.h"
#include "engine/WarpPlayer.h"
#include "model/AudioAnalysis.h"

namespace fd::engine
{
namespace
{
/// Where the clip's content would play its file with `warp` (on or off as it says).
model::ContentMap mapWith(te::AudioClipBase& c, const model::AudioWarp& warp)
{
    model::ContentMap m;
    m.warped = warp.on;
    m.reversed = c.state.hasProperty(soundIds::forward);
    m.fileLength = c.getSourceLength().inSeconds();
    m.beatsPerSecond = c.edit.tempoSequence.getBeatsPerSecondAt(c.getPosition().getStart());
    if (m.warped)
        m.played = m.reversed ? model::reversedWarp(warp, m.fileLength) : warp;
    return m;
}

/// The clip placed so it plays the same audio from the same start when its content's map changes
/// from `before` to `after` (Warp on or off, a new tempo): its offset, length and loop carried
/// through the file. `info` is the clip as it was.
void relayout(te::AudioClipBase& c, const model::ClipInfo& info, const model::ContentMap& before,
              const model::ContentMap& after, juce::UndoManager* um)
{
    const auto to = [&](double content) { return after.contentAt(before.fileAt(content)); };
    const auto start = te::TimePosition::fromSeconds(info.start);
    if (info.loop.on())
    {
        const double from = to(info.loop.start), end = to(info.loop.end());
        const model::ClipLoop loop{from, std::max(end - from, 1e-3)};
        const double phase =
            std::max(0.0, to(info.loop.start + model::phaseOf(info.offset, info.loop)) - from);
        const double length = info.length * loop.length / info.loop.length;
        writeLoop(c, loop, um);
        c.setPosition(
            {{start, start + te::TimeDuration::fromSeconds(length)}, te::TimeDuration::fromSeconds(phase)});
        return;
    }
    const double offset = std::max(0.0, to(info.offset)), end = to(info.offset + info.length);
    c.setPosition({{start, start + te::TimeDuration::fromSeconds(std::max(end - offset, 1e-3))},
                   te::TimeDuration::fromSeconds(offset)});
}

/// How sure a tempo found in a sound must be before a loop placed follows it (§7.2): a drum loop's
/// is around 0.4, noise's and a held tone's under 0.1.
constexpr double kSteadyBeat = 0.25;

/// The tempo a file was made at, by its own say (§7.1): its loop metadata (a WAV's acid chunk, an
/// AIFF's beats, read from its header: the engine guesses one for any file), else a tempo in its
/// name. 0: none.
double ownTempo(const juce::File& file)
{
    const auto found = FileIndexer::read(file);
    return found ? found->tempo : 0.0;
}

/// The tempo a clip warps at when it has none of its own: its file's, else the song's, where it
/// plays untouched.
double firstTempo(te::AudioClipBase& c)
{
    if (const double bpm = ownTempo(forwardFileOf(c));
        bpm >= model::kMinClipTempo && bpm <= model::kMaxClipTempo)
        return bpm;
    return c.edit.tempoSequence.getBpmAt(c.getPosition().getStart());
}
} // namespace

void Session::Impl::keepFirstTempo(te::AudioClipBase& c)
{
    if (!c.state.hasProperty(warpIds::tempo))
        c.state.setProperty(warpIds::tempo, firstTempo(c), &edit->getUndoManager());
}

bool Session::setWarp(const std::vector<std::uint64_t>& clips, const WarpChange& change,
                      const std::string& undoName)
{
    std::vector<te::AudioClipBase*> audio;
    for (auto id : clips)
        if (auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(id)))
            audio.push_back(c);
    if (audio.empty())
        return false;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(juce::String(undoName));
    for (auto* c : audio)
    {
        auto& s = c->state;
        const auto info = clipInfoOf(*c);
        const auto was = warpOf(*c);
        auto now = was;
        if (!s.hasProperty(warpIds::tempo))
            now.tempo = firstTempo(*c);
        if (change.on)
            now.on = *change.on;
        // Warp on for the first time: its mode chosen by its sound (§7.3), Beats if percussive.
        if (now.on && !was.on && !change.mode && !s.hasProperty(warpIds::mode))
            if (const auto found = impl_->analyser.get(forwardFileOf(*c));
                found != nullptr && model::startsInBeats(*found))
                s.setProperty(warpIds::mode, static_cast<int>(model::WarpMode::Beats), um);
        if (change.tempo)
            now = model::withTempo(now, *change.tempo);
        if (change.tempoTimes)
            now = model::withTempo(now, now.tempo * *change.tempoTimes);
        // Whatever reads the file another way keeps the audio the clip plays.
        if (now.on != was.on || now.tempo != was.tempo || now.markers != was.markers)
        {
            const auto before = mapWith(*c, was);
            s.setProperty(warpIds::tempo, now.tempo, um);
            writeWarpMarkers(s, now.markers, um);
            if (now.on)
            {
                writeWarpLoopInfo(*c, now);
                c->setUsesProxy(false);
            }
            if (now.on != was.on)
                c->setAutoTempo(now.on);
            relayout(*c, info, before, mapWith(*c, now), um);
        }
        if (change.mode)
            s.setProperty(warpIds::mode, static_cast<int>(*change.mode), um);
        if (change.tails)
            s.setProperty(warpIds::tails, static_cast<int>(*change.tails), um);
        if (change.formants)
            s.setProperty(warpIds::formants, *change.formants, um);
        if (change.sensitivity)
            s.setProperty(warpIds::sensitivity, std::clamp(*change.sensitivity, 0.0, 1.0), um);
        auto pitch = pitchOf(s);
        if (change.transpose)
            pitch.transpose = *change.transpose;
        if (change.transposeBy)
            pitch.transpose += *change.transposeBy;
        if (change.fine)
            pitch.fine = *change.fine;
        pitch.transpose = std::clamp(pitch.transpose, -model::kMaxTranspose, model::kMaxTranspose);
        pitch.fine = std::clamp(pitch.fine, -model::kMaxFine, model::kMaxFine);
        if (pitch != pitchOf(s))
        {
            s.setProperty(warpIds::transpose, pitch.transpose, um);
            s.setProperty(warpIds::fine, pitch.fine, um);
        }
        impl_->changeKey(*c, change);
        syncPitch(*c);
    }
    return true;
}

void Session::Impl::changeKey(te::AudioClipBase& c, const WarpChange& change)
{
    auto& s = c.state;
    auto* um = &edit->getUndoManager();
    const bool following = followsKey(s) || change.followKey.value_or(false);
    if (change.keyFound)
        writeStoredKey(s, std::nullopt, false, um);
    if (change.key)
        writeStoredKey(s, *change.key, false, um);
    if (change.followKey && !*change.followKey && followsKey(s))
    {
        s.removeProperty(keyIds::follow, um);
        // A key pinned only to be followed goes back to following what's found.
        if (s.getProperty(keyIds::found, false))
            writeStoredKey(s, std::nullopt, false, um);
        return;
    }
    if (!following)
        return;
    // Following: the key it follows is kept on the clip, so what it plays doesn't change by itself.
    if (!storedKeyOf(s))
    {
        const auto found = analyser.now(forwardFileOf(c));
        if (found != nullptr && found->key)
            writeStoredKey(s, found->key, true, um);
    }
    const auto stored = storedKeyOf(s);
    if (stored && *stored)
        s.setProperty(keyIds::follow, true, um);
    else // no key to follow
        s.removeProperty(keyIds::follow, um);
}

std::optional<music::Key> Session::foundKey(std::uint64_t clipId)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr)
        return std::nullopt;
    const auto found = impl_->analyser.now(forwardFileOf(*c));
    return found != nullptr ? found->key : std::nullopt;
}

bool Session::setWarpMarkers(std::uint64_t clipId, const std::vector<model::WarpMarker>& markers,
                             const std::string& undoName)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr)
        return false;
    auto warp = warpOf(*c);
    if (!c->state.hasProperty(warpIds::tempo))
        warp.tempo = firstTempo(*c);
    warp.markers = markers;
    // In order and within the limits, or not at all.
    model::WarpMarker prev{0.0, 0.0};
    for (const auto& m : markers)
    {
        if (!(m.file > prev.file) || !(m.beat > prev.beat))
            return false;
        const double stretch = (m.beat - prev.beat) / (m.file - prev.file) * 60.0 / warp.tempo;
        if (stretch < model::kMinPieceStretch - 1e-6 || stretch > model::kMaxPieceStretch + 1e-6)
            return false;
        prev = m;
    }
    EditGroup group(*this, undoName);
    if (!warp.on)
        setWarp({clipId}, {.on = true}, undoName);
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(juce::String(undoName));
    c->state.setProperty(warpIds::tempo, warp.tempo, um);
    writeWarpMarkers(c->state, markers, um);
    writeWarpLoopInfo(*c, warp);
    return true;
}
void Session::requestAnalysis(std::uint64_t clipId)
{
    if (auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId)))
        (void)impl_->analyser.get(forwardFileOf(*c));
}

std::vector<double> Session::clipTransients(std::uint64_t clipId)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr)
        return {};
    return impl_->transientsOf(*c, true);
}

std::optional<Session::FoundTempo> Session::foundTempo(std::uint64_t clipId)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr)
        return std::nullopt;
    const auto found = impl_->analyser.now(forwardFileOf(*c));
    if (found == nullptr || !found->tempo)
        return std::nullopt;
    return FoundTempo{found->tempo->bpm, found->tempo->firstDownbeat, found->tempo->confidence};
}

std::string Session::quantiseAudio(std::uint64_t clipId, const QuantiseAudio& settings)
{
    if (dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId)) == nullptr)
        return "Not an audio clip.";
    EditGroup group(*this, "Quantise");
    if (settings.sensitivity)
        setWarp({clipId}, {.sensitivity = *settings.sensitivity}, "Quantise");
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    const auto transients = clipTransients(clipId);
    if (transients.empty())
        return "The clip has no transients to quantise.";
    const auto info = clipInfoOf(*c);
    auto withMap = info;
    withMap.warp = warpOf(*c);
    withMap.fileLength = c->getSourceLength().inSeconds();
    withMap.beatsPerSecond = c->edit.tempoSequence.getBeatsPerSecondAt(c->getPosition().getStart());
    if (!withMap.warp.on)
        withMap.warp.tempo = firstTempo(*c);
    const auto warp =
        model::quantisedClip(withMap, transients, settings.grid, settings.range, model::clipViewOrigin(info));
    if (!setWarpMarkers(clipId, warp.markers, "Quantise"))
        return "Quantising would stretch a piece past four times its speed.";
    return {};
}

void Session::applyTempo(std::uint64_t clipId, double bpm, double downbeat, bool songTempo)
{
    if (dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId)) == nullptr)
        return;
    EditGroup group(*this, "Tempo");
    // Song tempo from it: the song plays at the clip's tempo, and the clip, warped at it, untouched.
    if (songTempo)
        setTempo(bpm);
    setWarp({clipId}, {.on = true, .tempo = bpm}, "Tempo");
    // The clip starts at its downbeat, where it starts now (§7.8): the audio before it is still
    // there, past its start.
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    const auto info = clipInfoOf(*c);
    const double content = contentMapOf(*c).contentAt(downbeat);
    if (!info.loop.on() && content > info.offset + 1e-6 && content < info.offset + info.length)
    {
        const auto start = te::TimePosition::fromSeconds(info.start);
        c->setPosition({{start, start + te::TimeDuration::fromSeconds(info.length - (content - info.offset))},
                        te::TimeDuration::fromSeconds(content)});
    }
}

void Session::setWarpDefaults(bool loopsFollowTempo, bool recordingsFollowTempo, bool loopsFollowKey)
{
    impl_->loopsFollowTempo = loopsFollowTempo;
    impl_->recordingsFollowTempo = recordingsFollowTempo;
    impl_->loopsFollowKey = loopsFollowKey;
}

std::optional<std::pair<double, model::WarpMode>> Session::Impl::loopTempo(te::AudioClipBase& c)
{
    // A loop (§7.2): short, with a tempo of its own (its file's, its name's, else its sound's), a
    // whole number of bars at it.
    const double length = c.getSourceLength().inSeconds();
    if (!(length > 0.0) || length > 32.0)
        return std::nullopt;
    double bpm = ownTempo(forwardFileOf(c));
    // Found in its sound only where there's a steady beat to find (a one-shot, a pad or noise has
    // none).
    const auto found = analyser.now(forwardFileOf(c));
    if (!(bpm > 0.0) && found != nullptr && found->tempo && found->tempo->confidence >= kSteadyBeat)
        bpm = found->tempo->bpm;
    if (!model::isLoopAt(length, bpm))
        return std::nullopt;
    return std::pair{bpm, found != nullptr && model::startsInBeats(*found) ? model::WarpMode::Beats
                                                                           : model::WarpMode::Tones};
}

void Session::Impl::followSongTempo(te::AudioClipBase& c)
{
    if (!recordingsFollowTempo)
        return;
    // Made in time (§7.2): it plays untouched at the song's tempo, and follows it if it changes.
    auto* um = &edit->getUndoManager();
    auto warp = warpOf(c);
    warp.on = true;
    warp.tempo = edit->tempoSequence.getBpmAt(c.getPosition().getStart());
    c.state.setProperty(warpIds::tempo, warp.tempo, um);
    writeWarpLoopInfo(c, warp);
    c.setUsesProxy(false);
    c.setAutoTempo(true);
}

std::vector<std::uint64_t> Session::sliceAudio(std::uint64_t clipId, std::vector<double> times)
{
    updateSnapshotNow();
    const auto s = snapshot();
    const auto found = model::findClip(*s, clipId);
    if (!found || times.empty() ||
        !model::whyCantSlice(*found->first, s->tracks.at(static_cast<size_t>(found->second))).empty())
        return {};
    std::sort(times.begin(), times.end());
    const EditGroup group(*this, "Slice");
    std::vector<std::uint64_t> parts{clipId};
    for (const double t : times)
        if (const auto made = splitClips({parts.back()}, t); !made.empty())
            parts.push_back(made.front());
    return parts.size() > 1 ? parts : std::vector<std::uint64_t>{};
}

std::vector<std::uint64_t> Session::slice(std::uint64_t clipId, const SliceAudio& settings, std::string* why)
{
    const auto refuse = [why](std::string text)
    {
        if (why != nullptr)
            *why = std::move(text);
        return std::vector<std::uint64_t>{};
    };
    if (dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId)) == nullptr)
        return refuse("Not an audio clip.");
    if (settings.at == model::SliceAt::Transients)
        (void)clipTransients(clipId); // found now if they weren't
    updateSnapshotNow();
    const auto s = snapshot();
    const auto found = model::findClip(*s, clipId);
    if (!found)
        return refuse("Not an audio clip.");
    auto info = *found->first;
    if (auto text = model::whyCantSlice(info, s->tracks.at(static_cast<size_t>(found->second)));
        !text.empty())
        return refuse(text);
    if (settings.sensitivity)
        info.warp.sensitivity = std::clamp(*settings.sensitivity, 0.0, 1.0);
    const auto times = model::sliceTimes(info, settings.at, model::transientsOf(info), settings.gridBeats,
                                         s->tempo, settings.range);
    if (times.empty())
        switch (settings.at)
        {
        case model::SliceAt::Transients:
            return refuse("There are no transients to cut at: raise the sensitivity.");
        case model::SliceAt::Markers:
            return refuse(info.warp.on && !info.warp.markers.empty() ? "There are no warp markers inside it."
                                                                     : "The clip has no warp markers.");
        case model::SliceAt::Grid:
            return refuse("No line of the grid falls inside it.");
        case model::SliceAt::Bars:
            return refuse("No bar starts inside it.");
        }
    const EditGroup group(*this, "Slice");
    if (settings.sensitivity)
        setWarp({clipId}, {.sensitivity = info.warp.sensitivity}, "Slice");
    return sliceAudio(clipId, times);
}

std::string Session::sliceRefusal(std::uint64_t clipId) const
{
    const auto s = snapshot();
    const auto found = model::findClip(*s, clipId);
    if (!found)
        return "Not an audio clip.";
    return model::whyCantSlice(*found->first, s->tracks.at(static_cast<size_t>(found->second)));
}

bool Session::editTransients(std::uint64_t clipId, const std::vector<double>& add,
                             const std::vector<double>& remove)
{
    auto* c = dynamic_cast<te::AudioClipBase*>(impl_->clip(clipId));
    if (c == nullptr || (add.empty() && remove.empty()))
        return false;
    auto& s = c->state;
    auto added = timesOf(s[warpIds::transientsAdded]), removed = timesOf(s[warpIds::transientsRemoved]);
    const auto drop = [](std::vector<double>& v, double file)
    {
        v.erase(std::remove_if(v.begin(), v.end(), [file](double t) { return std::abs(t - file) < 0.001; }),
                v.end());
    };
    // Added, it's no longer removed; removed, no longer added (and the file's own is hidden).
    for (const auto* times : {&add, &remove})
        for (double file : *times)
        {
            drop(added, file);
            drop(removed, file);
            (times == &add ? added : removed).push_back(file);
        }
    std::sort(added.begin(), added.end());
    std::sort(removed.begin(), removed.end());
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(remove.empty() && add.size() == 1   ? "Add transient"
                     : add.empty() && remove.size() == 1 ? "Remove transient"
                     : add.empty()                       ? "Remove transients"
                                                         : "Edit transients");
    s.setProperty(warpIds::transientsAdded, textOf(added), um);
    s.setProperty(warpIds::transientsRemoved, textOf(removed), um);
    return true;
}
} // namespace fd::engine
