#include "model/SongHarmony.h"

#include "model/ClipLoops.h"
#include "model/DrumKit.h"
#include "model/NoteTools.h"

#include <algorithm>
#include <cmath>

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

std::vector<SongChordInfo> songHarmony(const Snapshot& snapshot)
{
    std::vector<Note> notes;
    for (const auto& track : snapshot.tracks)
    {
        if (!track.holdsClips() || kit::trackKit(track.devices) != nullptr)
            continue;
        std::vector<Note> own;
        for (const auto& clip : track.clips)
            if (clip.kind == ClipInfo::Kind::Midi && !clip.muted)
                songNotes(clip, snapshot.tempo, own);
        // Harmony Auto: a track that plays chords somewhere.
        if (!chordSpans(own).empty())
            notes.insert(notes.end(), own.begin(), own.end());
    }
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    const auto* key = snapshot.keys.first() ? &*snapshot.keys.first() : nullptr;
    std::vector<SongChordInfo> out;
    for (const auto& span : chordSpans(notes))
    {
        auto name = music::chordName(span.chord, key);
        const double start = snapshot.tempo.secondsForBeats(span.start),
                     end = snapshot.tempo.secondsForBeats(span.end);
        // Shorter than a beat: part of the chord before it (a passing chord read note by note).
        if (!out.empty() && span.end - span.start < 1.0 - 1e-9)
        {
            out.back().end = end;
            continue;
        }
        if (!out.empty() && out.back().name == name && std::abs(out.back().end - start) < 1e-6)
            out.back().end = end;
        else
            out.push_back({start, end, std::move(name)});
    }
    return out;
}
} // namespace fd::model
