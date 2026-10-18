#include "model/SongHarmony.h"

#include "model/ClipLoops.h"
#include "model/DrumKit.h"
#include "model/NoteTools.h"
#include "model/Sections.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace fd::model
{
namespace
{
/// A clip's notes in song beats, every lap of a looped clip, within the clip's span.
void songNotes(const ClipInfo& clip, const TempoMap& tempo, std::vector<Note>& out)
{
    const double clipStart = tempo.beatsForSeconds(clip.start), clipEnd = tempo.beatsForSeconds(clip.end());
    const auto add = [&](const Note& n, double at)
    {
        const double end = std::min(at + n.length, clipEnd);
        if (at >= clipEnd || end <= clipStart)
            return;
        auto copy = n;
        copy.start = std::max(at, clipStart);
        copy.length = end - copy.start;
        out.push_back(copy);
    };
    if (!clip.loop.on())
    {
        const double zero = tempo.beatsForSeconds(clip.start - clip.offset);
        for (const auto& n : clip.notes)
            if (!n.muted)
                add(n, zero + n.start);
        return;
    }
    // Every lap plays the loop's notes (ADR 0048), the first lap from where the clip starts in it.
    const double anchor = tempo.beatsForSeconds(lapAnchor(clip));
    const double loopStart = tempo.beatsForSeconds(clip.loop.start),
                 lap = tempo.beatsForSeconds(clip.loop.length);
    for (double from = anchor; from < clipEnd; from += lap)
        for (const auto& n : clip.notes)
            if (!n.muted && n.start >= loopStart && n.start < loopStart + lap)
                add(n, from + (n.start - loopStart));
}
} // namespace

namespace
{
/// A track's notes in song beats, if they make the harmony: Auto, a track that plays chords
/// somewhere; On, whatever it plays.
bool harmonyNotes(const TrackInfo& track, const TempoMap& tempo, std::vector<Note>& out)
{
    if (!track.holdsClips() || track.harmony == HarmonyUse::Off ||
        (track.harmony == HarmonyUse::Auto && kit::trackKit(track.devices) != nullptr))
        return false;
    std::vector<Note> own;
    for (const auto& clip : track.clips)
        if (clip.kind == ClipInfo::Kind::Midi && !clip.muted)
            songNotes(clip, tempo, own);
    if (track.harmony == HarmonyUse::Auto && chordSpans(own).empty())
        return false;
    out.insert(out.end(), own.begin(), own.end());
    return true;
}
} // namespace

std::vector<std::uint64_t> harmonyTracks(const Snapshot& snapshot)
{
    std::vector<std::uint64_t> out;
    std::vector<Note> unused;
    for (const auto& track : snapshot.tracks)
        if (harmonyNotes(track, snapshot.tempo, unused))
            out.push_back(track.id);
    return out;
}

std::vector<SongChordInfo> songHarmony(const Snapshot& snapshot)
{
    std::vector<Note> notes;
    for (const auto& track : snapshot.tracks)
        harmonyNotes(track, snapshot.tempo, notes);
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    const auto* key = snapshot.keys.first() ? &*snapshot.keys.first() : nullptr;
    const double shortest =
        shortestChord(snapshot.chordChange, snapshot.tempo.numerator, snapshot.tempo.denominator);
    std::vector<SongChordInfo> out;
    for (const auto& span : chordSpans(notes))
    {
        const double start = snapshot.tempo.secondsForBeats(span.start),
                     end = snapshot.tempo.secondsForBeats(span.end);
        // Shorter than the song allows: part of the chord before it (a passing chord read note by
        // note).
        if (!out.empty() && span.end - span.start < shortest - 1e-9)
        {
            out.back().end = end;
            out.back().endBeat = span.end;
            continue;
        }
        auto name = music::chordName(span.chord, key);
        int bass = -1;
        for (const auto i : span.notes)
            bass = bass < 0 ? notes[i].pitch : std::min(bass, notes[i].pitch);
        if (!out.empty() && out.back().name == name && std::abs(out.back().endBeat - span.start) < 1e-6)
        {
            out.back().end = end;
            out.back().endBeat = span.end;
            out.back().bass = std::min(out.back().bass, bass);
        }
        else
            out.push_back({.start = start,
                           .end = end,
                           .name = std::move(name),
                           .startBeat = span.start,
                           .endBeat = span.end,
                           .root = span.chord.root,
                           .notes = music::pitchClasses(span.chord),
                           .bass = bass});
    }
    return out;
}

namespace
{
/// A name's hash, the same on every run (FNV-1a): sections of one name share it.
std::uint32_t nameHash(const std::string& name)
{
    std::uint32_t h = 2166136261u;
    for (const unsigned char c : name)
        h = (h ^ c) * 16777619u;
    return h;
}
} // namespace

SongView songView(const Snapshot& snapshot, std::uint64_t track)
{
    SongView v;
    for (const auto& c : snapshot.songChords)
        v.chords.push_back(
            {.start = c.startBeat, .end = c.endBeat, .root = c.root, .notes = c.notes, .bass = c.bass});
    std::map<std::string, int> times;
    for (const auto& s : snapshot.sections)
    {
        const auto base = sectionBaseName(s.name);
        v.sections.push_back({.start = snapshot.tempo.beatsForSeconds(s.start),
                              .end = snapshot.tempo.beatsForSeconds(s.end),
                              .name = nameHash(base),
                              .time = ++times[base]});
    }
    if (const auto* t = snapshot.channel(track); t != nullptr && t->holdsClips())
    {
        std::vector<Note> notes;
        for (const auto& clip : t->clips)
        {
            if (clip.muted)
                continue;
            v.clips.push_back(
                {snapshot.tempo.beatsForSeconds(clip.start), snapshot.tempo.beatsForSeconds(clip.end())});
            if (clip.kind == ClipInfo::Kind::Midi)
                songNotes(clip, snapshot.tempo, notes);
        }
        std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
        for (const auto& n : notes)
            v.notes.push_back({.start = n.start,
                               .length = n.length,
                               .key = static_cast<std::uint8_t>(std::clamp(n.pitch, 0, 127)),
                               .velocity = n.velocity});
    }
    return v;
}
} // namespace fd::model
