#include "NoteRun.h"

#include <algorithm>
#include <cmath>
#include <fd/device/Realtime.h>
#include <map>

namespace fd::devices
{
namespace
{
struct Timed
{
    std::int64_t at = 0;
    Event event{};
};

/// Notes as note-ons and note-offs at their samples from `from`, in time order (ends first).
std::vector<Timed> eventsOf(std::span<const SongNote> notes, double from, double samplesPerBeat)
{
    std::vector<Timed> out;
    const auto at = [&](double beat)
    { return static_cast<std::int64_t>(std::llround((beat - from) * samplesPerBeat)); };
    for (const auto& n : notes)
    {
        out.push_back({at(n.start),
                       {.type = EventType::NoteOn, .channel = n.channel, .key = n.key, .value = n.velocity}});
        out.push_back(
            {at(n.start + n.length), {.type = EventType::NoteOff, .channel = n.channel, .key = n.key}});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Timed& a, const Timed& b)
                     {
                         if (a.at != b.at)
                             return a.at < b.at;
                         return a.event.type == EventType::NoteOff && b.event.type != EventType::NoteOff;
                     });
    return out;
}
} // namespace

std::vector<SongNote> runNotes(std::span<Device* const> chain, std::span<const SongNote> notes,
                               const NoteRun& run)
{
    const double samplesPerBeat = run.sampleRate * 60.0 / std::max(1.0, run.transport.tempoBpm);
    const auto total = static_cast<std::int64_t>(std::ceil((run.to - run.from) * samplesPerBeat));
    const auto input = eventsOf(notes, run.from, samplesPerBeat);
    std::vector<std::vector<Timed>> heard(chain.size());
    for (std::size_t i = 0; i < chain.size() && i < run.listen.size(); ++i)
        if (run.listen[i] != nullptr)
            heard[i] = eventsOf(*run.listen[i], run.from, samplesPerBeat);
    for (std::size_t i = 0; i < chain.size(); ++i)
    {
        chain[i]->prepare({.sampleRate = run.sampleRate, .maxBlockSize = run.blockSize});
        if (i < run.rolls.size())
            chain[i]->startPass(run.rolls[i]);
    }

    EventList in, out, listen;
    in.reserve(4096);
    out.reserve(4096);
    listen.reserve(4096);
    std::vector<std::size_t> nextHeard(chain.size(), 0);
    std::size_t next = 0;
    std::map<int, std::pair<double, float>> sounding; // by channel and key: start, velocity
    std::vector<SongNote> played;
    const auto close = [&](int slot, double at)
    {
        if (const auto it = sounding.find(slot); it != sounding.end())
        {
            played.push_back({.start = it->second.first,
                              .length = std::max(0.0, at - it->second.first),
                              .key = static_cast<std::uint8_t>(slot % 128),
                              .channel = static_cast<std::uint8_t>(slot / 128),
                              .velocity = it->second.second});
            sounding.erase(it);
        }
    };
    for (std::int64_t pos = 0; pos < total; pos += run.blockSize)
    {
        const int n = static_cast<int>(std::min<std::int64_t>(run.blockSize, total - pos));
        in.clear();
        while (next < input.size() && input[next].at < pos + n)
        {
            auto e = input[next++].event;
            e.sampleOffset = static_cast<int>(std::max<std::int64_t>(0, input[next - 1].at - pos));
            in.push(e);
        }
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            listen.clear();
            while (nextHeard[i] < heard[i].size() && heard[i][nextHeard[i]].at < pos + n)
            {
                auto e = heard[i][nextHeard[i]++].event;
                e.sampleOffset =
                    static_cast<int>(std::max<std::int64_t>(0, heard[i][nextHeard[i] - 1].at - pos));
                listen.push(e);
            }
            out.clear();
            ProcessContext ctx;
            ctx.events = &in;
            ctx.outEvents = &out;
            ctx.numSamples = n;
            ctx.transport = run.transport;
            ctx.transport.ppqPosition = run.from + static_cast<double>(pos) / samplesPerBeat;
            ctx.transport.samplePosition = pos;
            ctx.transport.playing = true;
            ctx.song = run.song;
            ctx.listen = heard[i].empty() ? nullptr : &listen;
            processRealtime(*chain[i], ctx);
            out.sortByTime();
            in.clear();
            for (const auto& e : out)
                in.push(e);
        }
        for (const auto& e : in)
        {
            const double beat = run.from + static_cast<double>(pos + e.sampleOffset) / samplesPerBeat;
            const int slot = (e.channel & 15) * 128 + (e.key & 127);
            if (e.type == EventType::NoteOn)
            {
                close(slot, beat); // played again over itself
                sounding[slot] = {beat, e.value};
            }
            else if (e.type == EventType::NoteOff)
                close(slot, beat);
        }
    }
    while (!sounding.empty())
        close(sounding.begin()->first, run.to);
    std::stable_sort(played.begin(), played.end(),
                     [](const SongNote& a, const SongNote& b) { return a.start < b.start; });
    return played;
}
} // namespace fd::devices
