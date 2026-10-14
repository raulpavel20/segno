#include "model/NoteTools.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-9;

/// What a tool acts on: the selection, or every note when none is selected.
struct Targets
{
    std::vector<std::size_t> indices; // in the notes' order
    bool all = false;

    [[nodiscard]] bool contains(std::size_t i) const
    {
        return std::binary_search(indices.begin(), indices.end(), i);
    }
};

Targets targetsOf(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    Targets t;
    for (auto i : selection)
        if (i < notes.size())
            t.indices.push_back(i);
    if (t.indices.empty())
    {
        t.all = true;
        for (std::size_t i = 0; i < notes.size(); ++i)
            t.indices.push_back(i);
    }
    return t;
}

Note sanitised(Note n)
{
    n.pitch = std::clamp(n.pitch, 0, 127);
    n.start = std::max(0.0, n.start);
    n.length = std::max(minNoteLength, n.length);
    n.velocity = midiVelocity(n.velocity);
    return n;
}

/// The notes with the targets replaced by `changed`, which are placed (and selected, unless the
/// tool acted on every note); overlaps resolved.
NoteEdit apply(const std::vector<Note>& notes, const Targets& targets, const std::vector<Note>& changed)
{
    std::vector<Note> out;
    std::set<std::size_t> placed;
    out.reserve(notes.size() + changed.size());
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!targets.contains(i))
            out.push_back(notes[i]);
    for (const auto& n : changed)
    {
        placed.insert(out.size());
        out.push_back(sanitised(n));
    }
    auto edit = resolveOverlaps(out, placed);
    if (targets.all)
        edit.selection.clear();
    return edit;
}

/// The notes with new ones added, which are selected.
NoteEdit add(const std::vector<Note>& notes, const std::vector<Note>& added)
{
    auto out = notes;
    std::set<std::size_t> placed;
    for (const auto& n : added)
    {
        placed.insert(out.size());
        out.push_back(sanitised(n));
    }
    return resolveOverlaps(out, placed);
}

double end(const Note& n) noexcept
{
    return n.start + n.length;
}

double mean(const std::vector<int>& pitches) noexcept
{
    double sum = 0.0;
    for (int p : pitches)
        sum += p;
    return pitches.empty() ? 0.0 : sum / static_cast<double>(pitches.size());
}

bool smooth(const ChordStyle& style) noexcept
{
    return style.voicing == music::Voicing::Smooth;
}

/// The last chord span starting before `beat`, of the notes not in `ignoring`; with its pitches.
std::optional<std::pair<music::Chord, std::vector<int>>> spanBefore(const std::vector<Note>& notes, double beat,
                                                                    const std::set<std::size_t>& ignoring)
{
    std::vector<Note> kept;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!ignoring.contains(i))
            kept.push_back(notes[i]);
    const auto spans = chordSpans(kept);
    for (auto it = spans.rbegin(); it != spans.rend(); ++it)
        if (it->start < beat - kEps)
        {
            std::set<int> pitches;
            // The voicing the chord ends with: its notes sounding at its last moment.
            double last = it->start;
            for (auto i : it->notes)
                last = std::max(last, kept[i].start);
            for (auto i : it->notes)
                if (kept[i].start <= last + kEps && end(kept[i]) > last + kEps)
                    pitches.insert(kept[i].pitch);
            return std::pair{it->chord, std::vector<int>(pitches.begin(), pitches.end())};
        }
    return std::nullopt;
}

/// Whether a chord's bass is one of its own tones (an inversion) rather than a note under it.
bool bassIsTone(const music::Chord& chord) noexcept
{
    if (!chord.bass)
        return false;
    auto tones = chord;
    tones.bass.reset();
    return (music::pitchClasses(tones) & (1u << ((*chord.bass % 12 + 12) % 12))) != 0;
}

std::vector<int> pitchesOf(const std::vector<Note>& notes, const std::vector<std::size_t>& which)
{
    std::vector<int> out;
    for (auto i : which)
        out.push_back(notes[i].pitch);
    std::sort(out.begin(), out.end());
    return out;
}

/// The group's notes, low to high (by pitch, then start).
std::vector<std::size_t> byPitch(const std::vector<Note>& notes, std::vector<std::size_t> group)
{
    std::sort(group.begin(), group.end(),
              [&](auto a, auto b) { return notes[a].pitch != notes[b].pitch ? notes[a].pitch < notes[b].pitch : notes[a].start < notes[b].start; });
    return group;
}
} // namespace

// ---- In key ---------------------------------------------------------------------------------

std::vector<int> foldedRows(Fold fold, const music::Key* key, const std::vector<Note>& notes)
{
    std::set<int> used;
    for (const auto& n : notes)
        used.insert(n.pitch);
    const music::PitchSet keyNotes = key != nullptr ? music::notes(*key) : music::PitchSet{0};
    std::vector<int> rows;
    for (int p = 0; p <= 127; ++p)
    {
        const bool shown = fold == Fold::All || (fold == Fold::Key && (keyNotes == 0 || (keyNotes & (1u << (p % 12))) != 0 || used.contains(p))) ||
                           (fold == Fold::Notes && (used.empty() || used.contains(p)));
        if (shown)
            rows.push_back(p);
    }
    return rows;
}

NoteEdit transposeInKey(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const music::Key& key,
                        int steps)
{
    const auto t = targetsOf(notes, selection);
    for (; steps != 0; steps += steps > 0 ? -1 : 1)
    {
        std::vector<Note> changed;
        bool fits = true;
        for (auto i : t.indices)
        {
            auto n = notes[i];
            n.pitch = music::moveInKey(key, n.pitch, steps);
            fits = fits && n.pitch >= 0 && n.pitch <= 127;
            changed.push_back(n);
        }
        if (fits)
            return apply(notes, t, changed);
    }
    return {notes, t.all ? std::set<std::size_t>{} : std::set<std::size_t>(t.indices.begin(), t.indices.end())};
}

NoteEdit fitToKey(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const music::Key& key)
{
    const auto t = targetsOf(notes, selection);
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        const int down = music::nearestInKey(key, n.pitch, false), up = music::nearestInKey(key, n.pitch, true);
        if (down != up)
        {
            // A tie: the way the melody was moving, from the note before (the latest to start
            // before it; of those, the nearest in pitch).
            const Note* before = nullptr;
            for (const auto& m : notes)
                if (m.start < n.start - kEps &&
                    (before == nullptr || m.start > before->start + kEps ||
                     (std::abs(m.start - before->start) <= kEps && std::abs(m.pitch - n.pitch) < std::abs(before->pitch - n.pitch))))
                    before = &m;
            n.pitch = before != nullptr && before->pitch < n.pitch ? up : down;
        }
        else
            n.pitch = down;
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

// ---- Chords ---------------------------------------------------------------------------------

std::vector<ChordSpan> chordSpans(const std::vector<Note>& notes)
{
    std::vector<std::size_t> order;
    std::vector<double> times;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!notes[i].muted)
        {
            order.push_back(i);
            times.push_back(notes[i].start);
            times.push_back(end(notes[i]));
        }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return notes[a].start < notes[b].start; });
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return b - a <= kEps; }), times.end());

    std::vector<ChordSpan> spans;
    std::vector<std::size_t> active;
    std::size_t next = 0;
    std::vector<int> pitches;
    for (std::size_t k = 0; k + 1 < times.size(); ++k)
    {
        const double t0 = times[k], t1 = times[k + 1];
        std::erase_if(active, [&](auto i) { return end(notes[i]) <= t0 + kEps; });
        while (next < order.size() && notes[order[next]].start <= t0 + kEps)
        {
            if (end(notes[order[next]]) > t0 + kEps)
                active.push_back(order[next]);
            ++next;
        }
        if (active.size() < 3)
            continue;
        pitches.clear();
        for (auto i : active)
            pitches.push_back(notes[i].pitch);
        const auto chord = music::nameChord(pitches);
        if (!chord)
            continue;
        if (!spans.empty() && spans.back().chord == *chord && t0 - spans.back().end <= 1.0 + kEps)
        {
            auto& last = spans.back();
            last.end = t1;
            for (auto i : active)
                if (std::find(last.notes.begin(), last.notes.end(), i) == last.notes.end())
                    last.notes.push_back(i);
            continue;
        }
        spans.push_back({t0, t1, *chord, active});
    }
    for (auto& s : spans)
        std::sort(s.notes.begin(), s.notes.end());
    return spans;
}

std::vector<int> pitchesBefore(const std::vector<Note>& notes, double beat, const std::set<std::size_t>& ignoring)
{
    if (auto span = spanBefore(notes, beat, ignoring))
        return std::move(span->second);
    return {};
}

std::optional<music::Chord> chordBefore(const std::vector<Note>& notes, double beat, const std::set<std::size_t>& ignoring)
{
    if (auto span = spanBefore(notes, beat, ignoring))
        return span->first;
    return std::nullopt;
}

NoteEdit drawChord(const std::vector<Note>& notes, const Note& clicked, const music::Key* key, const ChordStyle& style,
                   bool under)
{
    const auto before = spanBefore(notes, clicked.start, {});
    const int pitch = std::clamp(clicked.pitch, 0, 127);
    const auto chord = under ? music::harmonise(key, pitch, style.shape, before ? &before->first : nullptr)
                             : music::keyChord(key, pitch, style.shape);
    music::VoiceRequest request{.voicing = style.voicing,
                                .inversion = style.inversion,
                                .centre = static_cast<double>(pitch)};
    if (smooth(style) && before)
        request.before = before->second;
    if (under)
        request.top = pitch;
    else
        request.including = pitch;
    auto pitches = music::voiceChord(chord, request);
    if (pitches.empty())
        pitches = {pitch};
    std::vector<Note> added;
    for (int p : pitches)
    {
        auto n = clicked;
        n.pitch = p;
        added.push_back(n);
    }
    return add(notes, added);
}

std::vector<int> chordVoicing(const std::vector<Note>& notes, double start, const music::Chord& chord,
                              const std::set<std::size_t>& replacing, const ChordStyle& style)
{
    std::vector<int> old;
    for (auto i : replacing)
        if (i < notes.size())
            old.push_back(notes[i].pitch);
    const auto before = pitchesBefore(notes, start, replacing);
    const int natural = static_cast<int>(music::stackedTones(chord).size()) + (chord.bass && !bassIsTone(chord) ? 1 : 0);
    music::VoiceRequest request{.voicing = style.voicing,
                                .inversion = style.inversion,
                                .centre = !old.empty() ? mean(old) : !before.empty() ? mean(before) : 64.0,
                                .count = old.empty() ? 0 : std::max(static_cast<int>(old.size()), natural)};
    if (smooth(style))
        request.before = before;
    return music::voiceChord(chord, request);
}

NoteEdit writeChord(const std::vector<Note>& notes, double start, double end_, const music::Chord& chord,
                    const std::set<std::size_t>& replacing, const ChordStyle& style, float velocity)
{
    const auto pitches = chordVoicing(notes, start, chord, replacing, style);
    std::vector<std::size_t> old;
    for (auto i : replacing)
        if (i < notes.size())
            old.push_back(i);
    if (pitches.empty())
        return {notes, {}};
    old = byPitch(notes, old);

    std::vector<Note> written;
    for (std::size_t k = 0; k < pitches.size(); ++k)
    {
        Note n{.pitch = pitches[k], .start = start, .length = end_ - start, .velocity = velocity};
        if (!old.empty())
        {
            // Each pitch keeps a note's timing and velocity: the same voice when there are as
            // many, else the nearest in pitch.
            std::size_t from = old[std::min(k, old.size() - 1)];
            if (old.size() != pitches.size())
                for (auto i : old)
                    if (std::abs(notes[i].pitch - pitches[k]) < std::abs(notes[from].pitch - pitches[k]))
                        from = i;
            n = notes[from];
            n.pitch = pitches[k];
        }
        written.push_back(n);
    }
    Targets targets{old, false};
    std::sort(targets.indices.begin(), targets.indices.end());
    return apply(notes, targets, written);
}

NoteEdit makeChords(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const music::Key* key,
                    const ChordStyle& style)
{
    const auto t = targetsOf(notes, selection);
    auto order = t.indices;
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return noteOrder(notes[a], notes[b]); });
    if (order.empty())
        return {notes, {}};
    const std::set<std::size_t> ignoring(t.indices.begin(), t.indices.end());
    auto previous = pitchesBefore(notes, notes[order.front()].start, ignoring);

    std::vector<Note> changed;
    for (auto i : order)
    {
        const auto& n = notes[i];
        const auto chord = music::keyChord(key, n.pitch, style.shape);
        music::VoiceRequest request{.voicing = style.voicing,
                                    .inversion = style.inversion,
                                    .including = n.pitch,
                                    .centre = static_cast<double>(n.pitch)};
        if (smooth(style))
            request.before = previous;
        auto pitches = music::voiceChord(chord, request);
        if (pitches.empty())
            pitches = {n.pitch};
        for (int p : pitches)
        {
            auto m = n;
            m.pitch = p;
            changed.push_back(m);
        }
        previous = pitches;
    }
    return apply(notes, t, changed);
}

std::vector<std::vector<std::size_t>> chordGroups(const std::vector<Note>& notes, const std::set<std::size_t>& which)
{
    std::vector<std::size_t> order;
    for (auto i : which)
        if (i < notes.size())
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return noteOrder(notes[a], notes[b]); });
    std::vector<std::vector<std::size_t>> groups;
    double groupStart = 0.0;
    for (auto i : order)
    {
        if (groups.empty() || notes[i].start > groupStart + chordSpread + kEps)
        {
            groups.emplace_back();
            groupStart = notes[i].start;
        }
        groups.back().push_back(i);
    }
    for (auto& g : groups)
        g = byPitch(notes, g);
    return groups;
}

NoteEdit invertChords(const std::vector<Note>& notes, const std::set<std::size_t>& selection, bool up)
{
    const auto t = targetsOf(notes, selection);
    std::map<std::size_t, Note> changed;
    for (auto i : t.indices)
        changed[i] = notes[i];
    for (const auto& g : chordGroups(notes, {t.indices.begin(), t.indices.end()}))
    {
        if (g.size() < 2)
            continue;
        if (up && notes[g.front()].pitch + 12 <= 127)
            changed[g.front()].pitch += 12;
        if (!up && notes[g.back()].pitch - 12 >= 0)
            changed[g.back()].pitch -= 12;
    }
    std::vector<Note> out;
    for (auto& [i, n] : changed)
        out.push_back(n);
    return apply(notes, t, out);
}

NoteEdit voiceChords(const std::vector<Note>& notes, const std::set<std::size_t>& selection, music::Voicing voicing)
{
    const auto t = targetsOf(notes, selection);
    std::map<std::size_t, Note> changed;
    for (auto i : t.indices)
        changed[i] = notes[i];
    std::vector<int> previous;
    const auto groups = chordGroups(notes, {t.indices.begin(), t.indices.end()});
    for (std::size_t k = 0; k < groups.size(); ++k)
    {
        const auto& g = groups[k];
        const auto pitches = pitchesOf(notes, g);
        auto chord = music::nameChord(pitches);
        if (!chord || (voicing == music::Voicing::Smooth && k == 0))
        {
            previous = pitches; // not a chord, or the first of a smooth run: kept
            continue;
        }
        if (bassIsTone(*chord))
            chord->bass.reset(); // an inversion is a voicing, which this replaces
        music::VoiceRequest request{.voicing = voicing,
                                    .centre = mean(pitches),
                                    .count = static_cast<int>(g.size())};
        if (voicing == music::Voicing::Smooth)
            request.before = previous;
        const auto voiced = music::voiceChord(*chord, request);
        if (voiced.size() != g.size())
        {
            previous = pitches;
            continue;
        }
        for (std::size_t v = 0; v < g.size(); ++v)
            changed[g[v]].pitch = voiced[v];
        previous = voiced;
    }
    std::vector<Note> out;
    for (auto& [i, n] : changed)
        out.push_back(n);
    return apply(notes, t, out);
}

NoteEdit addBass(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    const auto t = targetsOf(notes, selection);
    std::vector<Note> basses;
    for (const auto& g : chordGroups(notes, {t.indices.begin(), t.indices.end()}))
    {
        if (g.size() < 2)
            continue;
        const auto pitches = pitchesOf(notes, g);
        const auto chord = music::nameChord(pitches);
        const int root = chord ? chord->root : pitches.front() % 12;
        int bass = pitches.front() - 12;
        while (bass >= 0 && bass % 12 != root)
            --bass;
        if (bass < 0)
            continue;
        Note n = notes[g.front()];
        n.pitch = bass;
        double last = end(n);
        for (auto i : g)
        {
            n.start = std::min(n.start, notes[i].start);
            n.velocity = std::max(n.velocity, notes[i].velocity);
            last = std::max(last, end(notes[i]));
        }
        n.length = last - n.start;
        basses.push_back(n);
    }
    return add(notes, basses);
}

// ---- Editing --------------------------------------------------------------------------------

NoteEdit joinNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    const auto t = targetsOf(notes, selection);
    std::map<int, std::vector<Note>> byPitch;
    for (auto i : t.indices)
        byPitch[notes[i].pitch].push_back(notes[i]);
    std::vector<Note> changed;
    for (auto& [pitch, list] : byPitch)
    {
        std::sort(list.begin(), list.end(), noteOrder);
        Note current = list.front();
        for (std::size_t k = 1; k < list.size(); ++k)
        {
            if (list[k].start <= end(current) + kEps)
                current.length = std::max(end(current), end(list[k])) - current.start;
            else
            {
                changed.push_back(current);
                current = list[k];
            }
        }
        changed.push_back(current);
    }
    return apply(notes, t, changed);
}

NoteEdit splitNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double beat)
{
    const auto t = targetsOf(notes, selection);
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        const auto& n = notes[i];
        if (beat - n.start >= minNoteLength - kEps && end(n) - beat >= minNoteLength - kEps)
        {
            auto left = n, right = n;
            left.length = beat - n.start;
            right.start = beat;
            right.length = end(n) - beat;
            changed.push_back(left);
            changed.push_back(right);
        }
        else
            changed.push_back(n);
    }
    return apply(notes, t, changed);
}

NoteEdit legatoNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    const auto t = targetsOf(notes, selection);
    std::vector<double> starts;
    for (auto i : t.indices)
        starts.push_back(notes[i].start);
    std::sort(starts.begin(), starts.end());
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        const auto next = std::upper_bound(starts.begin(), starts.end(), n.start + kEps);
        if (next != starts.end())
            n.length = *next - n.start;
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

NoteEdit fixedLength(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double length)
{
    const auto t = targetsOf(notes, selection);
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        n.length = length;
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

NoteEdit stretchNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double pivot,
                      double factor, bool lengths)
{
    const auto t = targetsOf(notes, selection);
    if (std::abs(factor) < 1e-3)
        factor = factor < 0.0 ? -1e-3 : 1e-3;
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        // Reversed, a note's end is where it now starts.
        const double from = factor > 0.0 ? n.start : end(n);
        n.start = pivot + (from - pivot) * factor;
        if (lengths)
            n.length *= std::abs(factor);
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

NoteEdit reverseNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    const auto t = targetsOf(notes, selection);
    const auto span = notesSpan(notes, {t.indices.begin(), t.indices.end()});
    if (!span)
        return {notes, {}};
    return stretchNotes(notes, t.all ? std::set<std::size_t>{} : std::set<std::size_t>(t.indices.begin(), t.indices.end()),
                        (span->first + span->second) * 0.5, -1.0, false);
}

NoteEdit mirrorPitches(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const music::Key* key)
{
    const auto t = targetsOf(notes, selection);
    if (t.indices.empty())
        return {notes, {}};
    int lo = 127, hi = 0;
    for (auto i : t.indices)
    {
        lo = std::min(lo, notes[i].pitch);
        hi = std::max(hi, notes[i].pitch);
    }
    std::vector<int> scale; // the key's pitches, low to high
    if (key != nullptr && music::notes(*key) != 0)
        for (int p = 0; p <= 127; ++p)
            if (music::contains(*key, p))
                scale.push_back(p);
    // In the key, places count half steps of the scale: a key note is 2k, a note between two is odd.
    const auto place = [&](int p)
    {
        const auto below = static_cast<int>(std::lower_bound(scale.begin(), scale.end(), p) - scale.begin());
        const bool in = below < static_cast<int>(scale.size()) && scale[static_cast<std::size_t>(below)] == p;
        return in ? 2 * below : 2 * below - 1;
    };
    const auto pitchAt = [&](int placeValue)
    {
        const int k = std::clamp(placeValue / 2, 0, static_cast<int>(scale.size()) - 1);
        return scale[static_cast<std::size_t>(k)] + (placeValue % 2 != 0 ? 1 : 0);
    };
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        n.pitch = scale.empty() ? lo + hi - n.pitch : pitchAt(std::max(0, place(lo) + place(hi) - place(n.pitch)));
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

std::vector<Note> lineVelocity(const std::vector<Note>& notes, double beat0, float velocity0, double beat1,
                               float velocity1, const std::set<std::size_t>* only)
{
    const double lo = std::min(beat0, beat1), hi = std::max(beat0, beat1);
    auto out = notes;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        if ((only != nullptr && !only->contains(i)) || out[i].start < lo - kEps || out[i].start > hi + kEps)
            continue;
        const double along = hi - lo <= kEps ? 1.0 : (out[i].start - beat0) / (beat1 - beat0);
        const auto v = static_cast<float>(velocity0 + (velocity1 - velocity0) * along);
        out[i].velocity = midiVelocity(std::clamp(v, 1.0f / 127.0f, 1.0f));
    }
    return out;
}

NoteEdit muteNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    const auto t = targetsOf(notes, selection);
    bool anyPlaying = false;
    for (auto i : t.indices)
        anyPlaying = anyPlaying || !notes[i].muted;
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        n.muted = anyPlaying;
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

NoteEdit quantiseNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const BeatGrid& grid,
                       const QuantiseSettings& settings)
{
    const auto t = targetsOf(notes, selection);
    // The grid line nearest a beat; with swing, every other line of each bar comes later.
    const auto target = [&](double beat)
    {
        const double line = grid.round(beat);
        if (grid.step <= 0.0 || settings.swing <= 0.0)
            return line;
        double inBar = line + grid.origin;
        if (grid.bar > 0.0)
            inBar -= std::floor(inBar / grid.bar + kEps) * grid.bar;
        const auto index = std::llround(inBar / grid.step);
        return index % 2 != 0 ? line + settings.swing * grid.step / 3.0 : line;
    };
    const double strength = std::clamp(settings.strength, 0.0, 1.0);
    std::vector<Note> changed;
    for (auto i : t.indices)
    {
        auto n = notes[i];
        const double oldEnd = end(n);
        if (settings.starts)
            n.start = std::max(0.0, n.start + (target(n.start) - n.start) * strength);
        double newEnd = settings.starts ? n.start + (oldEnd - notes[i].start) : oldEnd;
        if (settings.ends)
            newEnd = oldEnd + (target(oldEnd) - oldEnd) * strength;
        n.length = std::max(minNoteLength, newEnd - n.start);
        changed.push_back(n);
    }
    return apply(notes, t, changed);
}

// ---- The inspector --------------------------------------------------------------------------

double fieldOf(const Note& note, NoteField field) noexcept
{
    switch (field)
    {
    case NoteField::Pitch: return note.pitch;
    case NoteField::Start: return note.start;
    case NoteField::Length: return note.length;
    default: return std::round(note.velocity * 127.0f);
    }
}

std::optional<std::pair<double, double>> fieldRange(const std::vector<Note>& notes, const std::set<std::size_t>& which,
                                                    NoteField field)
{
    std::optional<std::pair<double, double>> range;
    for (auto i : which)
        if (i < notes.size())
        {
            const double v = fieldOf(notes[i], field);
            range = range ? std::pair{std::min(range->first, v), std::max(range->second, v)} : std::pair{v, v};
        }
    return range;
}

namespace
{
Note withField(Note n, NoteField field, double value)
{
    switch (field)
    {
    case NoteField::Pitch: n.pitch = std::clamp(static_cast<int>(std::lround(value)), 0, 127); break;
    case NoteField::Start: n.start = std::max(0.0, value); break;
    case NoteField::Length: n.length = std::max(minNoteLength, value); break;
    case NoteField::Velocity:
        n.velocity = static_cast<float>(std::clamp(std::round(value), 1.0, 127.0) / 127.0);
        break;
    }
    return n;
}
} // namespace

NoteEdit setNotesField(const std::vector<Note>& notes, const std::set<std::size_t>& selection, NoteField field, double value)
{
    Targets t;
    for (auto i : selection)
        if (i < notes.size())
            t.indices.push_back(i);
    if (t.indices.empty())
        return {notes, {}};
    std::vector<Note> changed;
    for (auto i : t.indices)
        changed.push_back(withField(notes[i], field, value));
    return apply(notes, t, changed);
}

NoteEdit offsetNotesField(const std::vector<Note>& notes, const std::set<std::size_t>& selection, NoteField field,
                          double delta)
{
    const auto range = fieldRange(notes, selection, field);
    if (!range)
        return {notes, {}};
    // As far as every note can go: pitches within 0..127, starts from 0, velocities 1..127.
    switch (field)
    {
    case NoteField::Pitch: delta = std::clamp(delta, -range->first, 127.0 - range->second); break;
    case NoteField::Start: delta = std::max(delta, -range->first); break;
    case NoteField::Length: delta = std::max(delta, minNoteLength - range->first); break;
    case NoteField::Velocity: delta = std::clamp(delta, 1.0 - range->first, 127.0 - range->second); break;
    }
    Targets t;
    for (auto i : selection)
        if (i < notes.size())
            t.indices.push_back(i);
    std::vector<Note> changed;
    for (auto i : t.indices)
        changed.push_back(withField(notes[i], field, fieldOf(notes[i], field) + delta));
    return apply(notes, t, changed);
}

// ---- Selecting ------------------------------------------------------------------------------

std::set<std::size_t> selectSamePitch(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    std::set<int> pitches;
    for (auto i : selection)
        if (i < notes.size())
            pitches.insert(notes[i].pitch);
    std::set<std::size_t> out;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (pitches.contains(notes[i].pitch))
            out.insert(i);
    return out;
}

std::set<std::size_t> selectOutsideKey(const std::vector<Note>& notes, const music::Key& key)
{
    std::set<std::size_t> out;
    if (music::notes(key) == 0)
        return out;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!music::contains(key, notes[i].pitch))
            out.insert(i);
    return out;
}

std::set<std::size_t> selectStartingIn(const std::vector<Note>& notes, double from, double to)
{
    std::set<std::size_t> out;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].start >= from - kEps && notes[i].start < to - kEps)
            out.insert(i);
    return out;
}

std::set<std::size_t> invertSelection(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    std::set<std::size_t> out;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!selection.contains(i))
            out.insert(i);
    return out;
}

std::optional<std::pair<double, double>> notesSpan(const std::vector<Note>& notes, const std::set<std::size_t>& which)
{
    std::optional<std::pair<double, double>> span;
    for (auto i : which)
    {
        if (i >= notes.size())
            continue;
        if (!span)
            span = std::pair{notes[i].start, end(notes[i])};
        span->first = std::min(span->first, notes[i].start);
        span->second = std::max(span->second, end(notes[i]));
    }
    return span;
}
} // namespace fd::model
