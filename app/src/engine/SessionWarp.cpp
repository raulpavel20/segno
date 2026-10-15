// Warping and transposition (M3 step P6b, ADR 0063, docs/design/audio-editor.md §7, §8.1): Warp,
// the clip's tempo, its mode and its setting, sensitivity, transposition, and warp markers.

#include "engine/ClipFades.h"
#include "engine/SessionImpl.h"
#include "engine/WarpPlayer.h"

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

/// The engine's loop metadata kept to the clip's map: its length in beats is the file's, as warped
/// (what bounds the clip's length while it follows the tempo).
void writeLoopInfo(te::AudioClipBase& c, const model::AudioWarp& warp)
{
    const double length = c.getSourceLength().inSeconds();
    if (!(length > 0.0))
        return;
    auto info = c.getLoopInfo();
    const double beats = model::beatAtFile(warp, length);
    info.setNumBeats(beats);
    info.setBpm(beats * 60.0 / length, c.getWaveInfo());
    if (info.getNumerator() <= 0)
    {
        info.setNumerator(4);
        info.setDenominator(4);
    }
    c.setLoopInfo(info);
}

/// The tempo a clip warps at when it has none of its own (§7.1): its file's loop metadata, a tempo
/// in its name, else the song's, where it plays untouched.
double firstTempo(te::AudioClipBase& c)
{
    if (const double bpm = c.getLoopInfo().getBpm(c.getWaveInfo());
        bpm >= model::kMinClipTempo && bpm <= model::kMaxClipTempo && c.getLoopInfo().getNumBeats() > 0.0)
        return bpm;
    if (const auto named =
            model::tempoFromName(c.getOriginalFile().getFileNameWithoutExtension().toStdString()))
        return *named;
    return c.edit.tempoSequence.getBpmAt(c.getPosition().getStart());
}
} // namespace

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
                writeLoopInfo(*c, now);
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
            // Without Warp the engine's own reader transposes it, in real time.
            c->setUsesProxy(false);
            c->setPitchChange(static_cast<float>(pitch.semitones()));
        }
    }
    return true;
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
    writeLoopInfo(*c, warp);
    return true;
}
} // namespace fd::engine
