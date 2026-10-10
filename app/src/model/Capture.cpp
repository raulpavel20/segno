#include "model/Capture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace fd::model
{
bool inputTakes(const InputInfo& input, const std::string& source, int channel)
{
    if (input.midi.empty())
        return false;
    if (input.midi != "all" && input.midi != source)
        return false;
    return input.channel == 0 || channel == 0 || channel == input.channel;
}

std::vector<MidiEvent> lastPhrase(const std::vector<MidiEvent>& events, double pause, double notBefore, double lead)
{
    std::set<std::pair<int, int>> held; // (channel, note)
    std::array<bool, 17> pedal{};
    auto anything = [&] { return !held.empty() || std::any_of(pedal.begin(), pedal.end(), [](bool p) { return p; }); };
    double quietSince = -std::numeric_limits<double>::infinity();
    std::optional<size_t> start;
    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        if (e.time < notBefore)
            continue;
        if (e.isNoteOn())
        {
            // A note after a long enough silence starts a new phrase.
            if (!start || (!anything() && e.time - quietSince >= pause))
                start = i;
            held.insert({e.channel(), e.bytes[1]});
        }
        else if (e.isNoteOff())
        {
            held.erase({e.channel(), e.bytes[1]});
            if (!anything())
                quietSince = e.time;
        }
        else if (e.isSustain())
        {
            const bool down = e.bytes[2] >= 64;
            // A pedal pressed after a long enough silence starts a phrase too (its notes follow).
            if (down && (!start || (!anything() && e.time - quietSince >= pause)))
                start = i;
            pedal[static_cast<size_t>(e.channel())] = down;
            if (!anything())
                quietSince = e.time;
        }
    }
    if (!start || std::none_of(events.begin() + static_cast<std::ptrdiff_t>(*start), events.end(),
                               [](const auto& e) { return e.isNoteOn(); }))
        return {};
    // Controllers just before its first note come along (a pedal pressed first, a bend set up).
    size_t from = *start;
    while (from > 0 && events[from - 1].time >= notBefore && events[from - 1].time >= events[*start].time - lead &&
           !events[from - 1].isNoteOn() && !events[from - 1].isNoteOff())
        --from;
    return {events.begin() + static_cast<std::ptrdiff_t>(from), events.end()};
}

CaptureDestinations captureDestinations(const Snapshot& snapshot, std::optional<std::uint64_t> selected)
{
    CaptureDestinations out;
    for (const auto& t : snapshot.tracks)
        if (t.input.armed && !t.input.midi.empty() && recordKind(t.input, !t.instrument.empty()) == RecordKind::Midi)
            out.armed.push_back(t.id);
    if (!out.armed.empty())
        return out;
    if (selected)
        if (const auto* c = snapshot.channel(*selected); c != nullptr && c->holdsClips())
        {
            out.track = selected;
            return out;
        }
    out.newTrack = true;
    return out;
}

CapturedClip placeCapture(const std::vector<MidiEvent>& phrase, const TempoMap& tempo, double playhead, double now)
{
    CapturedClip out;
    const auto first = std::find_if(phrase.begin(), phrase.end(), [](const auto& e) { return e.isNoteOn(); });
    if (first == phrase.end())
        return out;
    const bool played = std::all_of(phrase.begin(), phrase.end(), [](const auto& e) { return e.song.has_value(); });
    const double at = played ? *first->song : tempo.secondsForBeats(std::floor(tempo.beatsForSeconds(std::max(0.0, playhead)) + 1e-9));
    double last = at;
    std::set<std::pair<int, int>> held;
    for (const auto& e : phrase)
    {
        out.times.push_back(std::max(0.0, at + (e.time - first->time)));
        last = std::max(last, out.times.back());
        if (e.isNoteOn())
            held.insert({e.channel(), e.bytes[1]});
        else if (e.isNoteOff())
            held.erase({e.channel(), e.bytes[1]});
    }
    if (!held.empty()) // still held: they end now
        last = std::max(last, at + (now - first->time));
    const double startBeat = std::floor(tempo.beatsForSeconds(*std::min_element(out.times.begin(), out.times.end())) + 1e-9);
    double endBeat = std::ceil(tempo.beatsForSeconds(last) - 1e-9);
    endBeat = std::max(endBeat, startBeat + 1.0);
    out.start = tempo.secondsForBeats(startBeat);
    out.end = tempo.secondsForBeats(endBeat);
    return out;
}
} // namespace fd::model
