#include "model/NoteEditing.h"

#include "model/Grid.h"
#include "model/NoteTools.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace fd::model
{
namespace
{
constexpr double kEps = 1e-9;

struct Item
{
    Note note;
    bool placed = false;
    bool alive = true;
};

std::vector<Item> itemsOf(const std::vector<Note>& notes, const std::set<std::size_t>& placed)
{
    std::vector<Item> items;
    items.reserve(notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i)
        items.push_back({notes[i], placed.contains(i)});
    return items;
}

/// Overlap rules (see resolveOverlaps) on items; dead ones are flagged.
void resolve(std::vector<Item>& items)
{
    const auto end = [](const Note& n) { return n.start + n.length; };
    // Placed notes against the others.
    for (const auto& p : items)
    {
        if (!p.placed || !p.alive)
            continue;
        for (auto& q : items)
        {
            if (q.placed || !q.alive || q.note.pitch != p.note.pitch)
                continue;
            if (q.note.start < end(p.note) - kEps && end(q.note) > p.note.start + kEps)
            {
                if (q.note.start >= p.note.start - kEps)
                    q.alive = false;
                else
                {
                    q.note.length = p.note.start - q.note.start;
                    q.alive = q.note.length >= minNoteLength - kEps;
                }
            }
        }
    }
    // Placed notes among themselves: per pitch in time order, the later one wins.
    std::map<int, std::vector<Item*>> byPitch;
    for (auto& item : items)
        if (item.placed && item.alive)
            byPitch[item.note.pitch].push_back(&item);
    for (auto& [pitch, list] : byPitch)
    {
        std::stable_sort(list.begin(), list.end(), [](const Item* a, const Item* b) { return a->note.start < b->note.start; });
        for (std::size_t k = 0; k + 1 < list.size(); ++k)
        {
            auto& a = list[k]->note;
            const auto& b = list[k + 1]->note;
            if (end(a) > b.start + kEps)
            {
                if (b.start - a.start < minNoteLength - kEps)
                    list[k]->alive = false;
                else
                    a.length = b.start - a.start;
            }
        }
    }
}

NoteEdit collect(std::vector<Item> items)
{
    std::erase_if(items, [](const Item& i) { return !i.alive; });
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return noteOrder(a.note, b.note); });
    NoteEdit out;
    out.notes.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        out.notes.push_back(items[i].note);
        if (items[i].placed)
            out.selection.insert(i);
    }
    return out;
}

std::set<std::size_t> valid(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    std::set<std::size_t> out;
    for (auto i : selection)
        if (i < notes.size())
            out.insert(i);
    return out;
}

/// Where a drag moves the selected notes' pitches: by rows (semitones, or the rows In key shows);
/// limited so every note stays on a row.
std::map<std::size_t, int> movedPitches(const std::vector<Note>& notes, const std::set<std::size_t>& sel,
                                        const NoteDrag& drag)
{
    std::map<std::size_t, int> out;
    if (drag.rows != nullptr && !drag.rows->empty())
    {
        const auto& rows = *drag.rows;
        const auto rowOf = [&](int pitch)
        {
            const auto it = std::lower_bound(rows.begin(), rows.end(), pitch);
            return static_cast<int>(
                std::min<std::ptrdiff_t>(it - rows.begin(), static_cast<std::ptrdiff_t>(rows.size()) - 1));
        };
        int lowest = static_cast<int>(rows.size()), highest = 0;
        for (auto i : sel)
        {
            lowest = std::min(lowest, rowOf(notes[i].pitch));
            highest = std::max(highest, rowOf(notes[i].pitch));
        }
        const int by = std::clamp(drag.deltaPitch, -lowest, static_cast<int>(rows.size()) - 1 - highest);
        for (auto i : sel)
            out[i] = by == 0 ? notes[i].pitch : rows[static_cast<std::size_t>(rowOf(notes[i].pitch) + by)];
        return out;
    }
    int lowest = 127, highest = 0;
    for (auto i : sel)
    {
        lowest = std::min(lowest, notes[i].pitch);
        highest = std::max(highest, notes[i].pitch);
    }
    const int dp = std::clamp(drag.deltaPitch, -lowest, 127 - highest);
    for (auto i : sel)
        out[i] = notes[i].pitch + dp;
    return out;
}
} // namespace

bool noteOrder(const Note& a, const Note& b) noexcept
{
    return std::tie(a.start, a.pitch, a.length, a.velocity, a.muted, a.chance, a.velocitySpread,
                    a.releaseVelocity) < std::tie(b.start, b.pitch, b.length, b.velocity, b.muted, b.chance,
                                                  b.velocitySpread, b.releaseVelocity);
}

float midiVelocity(float velocity) noexcept
{
    const auto k = std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
    return static_cast<float>(k) / 127.0f;
}

double BeatGrid::round(double beat) const noexcept
{
    return step <= 0.0 ? beat : gridRound(beat + origin, step, bar) - origin;
}

double BeatGrid::floor(double beat) const noexcept
{
    return step <= 0.0 ? beat : gridFloor(beat + origin, step, bar) - origin;
}

double BeatGrid::next(double beat) const noexcept
{
    return step <= 0.0 ? beat : gridNext(beat + origin, step, bar) - origin;
}

int NoteGeometry::pitchForY(double y) const noexcept
{
    const int row = static_cast<int>(std::ceil(topEdge - y / rowHeight)) - 1;
    if (rows == nullptr)
        return row;
    if (rows->empty())
        return 60;
    return (*rows)[static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(rows->size()) - 1))];
}

int NoteGeometry::rowOf(int pitch) const noexcept
{
    if (rows == nullptr)
        return pitch;
    return static_cast<int>(std::lower_bound(rows->begin(), rows->end(), pitch) - rows->begin());
}

std::optional<NoteHit> hitNote(const std::vector<Note>& notes, const NoteGeometry& geometry, double x, double y,
                               double edgePixels)
{
    for (std::size_t k = notes.size(); k-- > 0;)
    {
        const auto& n = notes[k];
        const double x0 = geometry.xForBeat(n.start), x1 = geometry.xForBeat(n.start + n.length);
        const double y0 = geometry.yForPitch(n.pitch);
        if (x < x0 || x >= x1 || y < y0 || y >= y0 + geometry.rowHeight)
            continue;
        const double edge = std::min(edgePixels, (x1 - x0) / 3.0);
        const auto zone = x < x0 + edge ? NoteHit::Zone::StartEdge : x >= x1 - edge ? NoteHit::Zone::EndEdge : NoteHit::Zone::Body;
        return NoteHit{k, zone};
    }
    return std::nullopt;
}

NoteEdit dragNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const NoteDrag& drag,
                   const BeatGrid& grid)
{
    const auto sel = valid(notes, selection);
    if (!sel.contains(drag.anchor))
        return {notes, sel};
    const Note a = notes[drag.anchor];
    const double aEnd = a.start + a.length;
    auto items = itemsOf(notes, sel);

    switch (drag.kind)
    {
    case NoteDragKind::Move:
    {
        double shift = grid.round(a.start + drag.deltaBeats) - a.start;
        double earliest = a.start;
        for (auto i : sel)
            earliest = std::min(earliest, notes[i].start);
        shift = std::max(shift, -earliest);
        const auto pitches = movedPitches(notes, sel, drag);
        for (auto i : sel)
        {
            items[i].note.start += shift;
            items[i].note.pitch = pitches.at(i);
        }
        if (drag.copy)
            for (auto i : sel)
            {
                items.push_back(items[i]);
                items[i] = {notes[i], false};
            }
        break;
    }
    case NoteDragKind::ResizeEnd:
    {
        const double minEnd = a.start + (grid.step > 0.0 ? std::max(minNoteLength, grid.next(a.start) - a.start) : minNoteLength);
        const double newEnd = std::max(grid.round(aEnd + drag.deltaBeats), minEnd);
        const double change = newEnd - aEnd;
        for (auto i : sel)
            items[i].note.length = std::max(minNoteLength, notes[i].length + change);
        items[drag.anchor].note.length = newEnd - a.start;
        break;
    }
    case NoteDragKind::ResizeStart:
    {
        double maxStart = aEnd - minNoteLength;
        if (grid.step > 0.0)
            maxStart = std::min(maxStart, grid.floor(aEnd - grid.step * 0.5));
        const double newStart = std::max(0.0, std::min(grid.round(a.start + drag.deltaBeats), maxStart));
        const double change = newStart - a.start;
        for (auto i : sel)
        {
            const double end = notes[i].start + notes[i].length;
            const double s = std::max(0.0, std::min(notes[i].start + change, end - minNoteLength));
            items[i].note.start = s;
            items[i].note.length = end - s;
        }
        items[drag.anchor].note.start = newStart;
        items[drag.anchor].note.length = aEnd - newStart;
        break;
    }
    }
    resolve(items);
    return collect(std::move(items));
}

int draggedPitch(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const NoteDrag& drag)
{
    const auto sel = valid(notes, selection);
    if (!sel.contains(drag.anchor))
        return drag.anchor < notes.size() ? notes[drag.anchor].pitch : 60;
    return movedPitches(notes, sel, drag).at(drag.anchor);
}

NoteEdit addNote(const std::vector<Note>& notes, const Note& note)
{
    auto items = itemsOf(notes, {});
    auto n = note;
    n.start = std::max(0.0, n.start);
    n.length = std::max(minNoteLength, n.length);
    n.pitch = std::clamp(n.pitch, 0, 127);
    n.velocity = midiVelocity(n.velocity);
    items.push_back({n, true});
    resolve(items);
    return collect(std::move(items));
}

NoteEdit deleteNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection)
{
    NoteEdit out;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!selection.contains(i))
            out.notes.push_back(notes[i]);
    return out;
}

NoteEdit duplicateNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double gridStep,
                        double beatsPerBar)
{
    const auto sel = valid(notes, selection);
    if (sel.empty())
        return {notes, sel};
    double first = notes[*sel.begin()].start, last = first;
    for (auto i : sel)
    {
        first = std::min(first, notes[i].start);
        last = std::max(last, notes[i].start + notes[i].length);
    }
    const double span = last - first;
    const double unit = span >= beatsPerBar * 0.5 ? beatsPerBar : gridStep;
    double offset = unit > 0.0 ? std::ceil(span / unit - kEps) * unit : span;
    if (offset <= 0.0)
        offset = unit > 0.0 ? unit : minNoteLength;
    return duplicateNotesBy(notes, sel, offset);
}

NoteEdit duplicateNotesBy(const std::vector<Note>& notes, const std::set<std::size_t>& selection,
                          double beats)
{
    const auto sel = valid(notes, selection);
    if (sel.empty() || beats <= 0.0)
        return {notes, sel};
    auto items = itemsOf(notes, {});
    for (auto i : sel)
    {
        auto copy = notes[i];
        copy.start += beats;
        items.push_back({copy, true});
    }
    resolve(items);
    return collect(std::move(items));
}

NoteEdit transposeNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, int semitones)
{
    const auto sel = valid(notes, selection);
    if (sel.empty())
        return {notes, sel};
    int lowest = 127, highest = 0;
    for (auto i : sel)
    {
        lowest = std::min(lowest, notes[i].pitch);
        highest = std::max(highest, notes[i].pitch);
    }
    const int dp = std::clamp(semitones, -lowest, 127 - highest);
    auto items = itemsOf(notes, sel);
    for (auto i : sel)
        items[i].note.pitch += dp;
    resolve(items);
    return collect(std::move(items));
}

NoteEdit nudgeNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, double beats)
{
    const auto sel = valid(notes, selection);
    if (sel.empty())
        return {notes, sel};
    double earliest = notes[*sel.begin()].start;
    for (auto i : sel)
        earliest = std::min(earliest, notes[i].start);
    const double shift = std::max(beats, -earliest);
    auto items = itemsOf(notes, sel);
    for (auto i : sel)
        items[i].note.start += shift;
    resolve(items);
    return collect(std::move(items));
}

NoteEdit quantiseNotes(const std::vector<Note>& notes, const std::set<std::size_t>& selection, const BeatGrid& grid)
{
    return quantiseNotes(notes, selection, grid, QuantiseSettings{});
}

std::set<std::size_t> notesInBox(const std::vector<Note>& notes, double beat0, double beat1, int pitch0, int pitch1)
{
    const double lo = std::min(beat0, beat1), hi = std::max(beat0, beat1);
    const int plo = std::min(pitch0, pitch1), phi = std::max(pitch0, pitch1);
    std::set<std::size_t> out;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.pitch >= plo && n.pitch <= phi && n.start <= hi && n.start + n.length >= lo)
            out.insert(i);
    }
    return out;
}

float levelOf(const Note& note, NoteLevel level) noexcept
{
    switch (level)
    {
    case NoteLevel::Chance:
        return note.chance;
    case NoteLevel::Release:
        return note.releaseVelocity;
    case NoteLevel::Velocity:
    default:
        return note.velocity;
    }
}

void setLevel(Note& note, NoteLevel level, float value) noexcept
{
    value = std::clamp(value, 0.0f, 1.0f);
    switch (level)
    {
    case NoteLevel::Chance:
        note.chance = std::round(value * 100.0f) / 100.0f;
        break;
    case NoteLevel::Release:
        note.releaseVelocity = std::round(value * 127.0f) / 127.0f;
        break;
    case NoteLevel::Velocity:
    default:
        note.velocity = midiVelocity(std::max(value, 1.0f / 127.0f));
        break;
    }
}

std::vector<Note> paintLevel(const std::vector<Note>& notes, double beat0, double beat1, float value,
                             const std::set<std::size_t>* only, NoteLevel level)
{
    const double lo = std::min(beat0, beat1), hi = std::max(beat0, beat1);
    auto out = notes;
    for (std::size_t i = 0; i < out.size(); ++i)
        if ((only == nullptr || only->contains(i)) && out[i].start >= lo && out[i].start <= hi)
            setLevel(out[i], level, value);
    return out;
}

std::vector<Note> offsetLevel(const std::vector<Note>& notes, const std::set<std::size_t>& which, float delta,
                              NoteLevel level)
{
    auto out = notes;
    for (auto i : which)
        if (i < out.size())
            setLevel(out[i], level, levelOf(notes[i], level) + delta);
    return out;
}

std::optional<double> lengthenedClipEnd(const ClipInfo& clip, const std::vector<ClipInfo>& trackClips,
                                        const std::vector<Note>& notes, const TempoMap& tempo)
{
    if (notes.empty())
        return std::nullopt;
    double lastBeat = 0.0;
    for (const auto& n : notes)
        lastBeat = std::max(lastBeat, n.start + n.length);
    const double contentStart = clip.start - clip.offset;
    const double lastEnd = contentStart + tempo.secondsForBeats(lastBeat);
    if (lastEnd <= clip.end() + 1e-6)
        return std::nullopt;
    // The end of the bar the last note ends in (on the song's bars).
    const double barBeats = tempo.beatsPerBar();
    const double end = tempo.secondsForBeats(std::ceil(tempo.beatsForSeconds(lastEnd) / barBeats - kEps) * barBeats);
    for (const auto& other : trackClips)
        if (other.id != clip.id && other.start < end - 1e-6 && other.end() > clip.end() + 1e-6)
            return std::nullopt; // something is in the way: leave the clip as it is
    return end;
}

NoteEdit resolveOverlaps(const std::vector<Note>& notes, const std::set<std::size_t>& placed)
{
    auto items = itemsOf(notes, valid(notes, placed));
    resolve(items);
    return collect(std::move(items));
}
} // namespace fd::model
