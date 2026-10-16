#include "fd/device/PresetCheck.h"

#include "fd/device/Events.h"
#include "fd/device/Realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace fd
{
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

double toDb(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-10));
}

KeyboardCheck::Take render(const DeviceFactory& factory, const DeviceState& state, std::vector<int> keys,
                           double holdSeconds, int macros = -1, bool controllers = false)
{
    auto device = factory.create();
    (void)applyState(*device, state);
    // The extremes: every macro at 0 or at full.
    if (macros >= 0)
        for (size_t i = 0; i < device->info().params.size(); ++i)
        {
            const auto& spec = device->info().params[i];
            if (spec.id.size() > 5 && spec.id.starts_with("macro") &&
                std::all_of(spec.id.begin() + 5, spec.id.end(),
                            [](char ch) { return ch >= '0' && ch <= '9'; }))
                device->params().set(i, macros == 0 ? spec.min : spec.max);
        }
    device->prepare({.sampleRate = kRate, .maxBlockSize = kBlock});
    const int hold = static_cast<int>(holdSeconds * kRate);
    const int total = hold + static_cast<int>(2.0 * kRate);
    const int skip = static_cast<int>(0.25 * kRate);
    std::vector<float> left(kBlock), right(kBlock);
    std::array<float*, 2> outs{left.data(), right.data()};
    std::vector<float> silence(kBlock, 0.0f);
    std::array<float*, 2> ins{silence.data(), silence.data()};
    EventList events;
    events.reserve(64);
    double peak = 0.0, energy = 0.0;
    long counted = 0;
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = std::min(kBlock, total - pos);
        events.clear();
        if (pos == 0 && controllers)
        {
            events.push({.sampleOffset = 0, .type = EventType::Controller, .key = 1, .value = 1.0f});
            events.push({.sampleOffset = 0, .type = EventType::ChannelPressure, .value = 1.0f});
        }
        for (const int key : keys)
        {
            if (pos == 0)
                events.push({.sampleOffset = 0,
                             .type = EventType::NoteOn,
                             .key = static_cast<std::uint8_t>(key),
                             .value = 1.0f});
            if (hold >= pos && hold < pos + n)
                events.push({.sampleOffset = hold - pos,
                             .type = EventType::NoteOff,
                             .key = static_cast<std::uint8_t>(key)});
        }
        ProcessContext ctx;
        const int inputs = device->info().buses.inputChannels;
        ctx.input = {ins.data(), std::clamp(inputs, 0, 2)};
        ctx.output = {outs.data(), 2};
        ctx.events = &events;
        ctx.numSamples = n;
        ctx.transport.playing = true;
        ctx.transport.tempoBpm = 120.0;
        ctx.transport.samplePosition = pos;
        ctx.transport.ppqPosition = pos / kRate * 2.0;
        processRealtime(*device, ctx);
        for (int i = 0; i < n; ++i)
        {
            const double l = left[static_cast<size_t>(i)], r = right[static_cast<size_t>(i)];
            peak = std::max({peak, std::abs(l), std::abs(r)});
            if (pos + i >= skip && pos + i < hold)
            {
                energy += 0.5 * (l * l + r * r);
                ++counted;
            }
        }
    }
    return {keys.front(), toDb(peak), toDb(std::sqrt(energy / std::max(1L, counted)))};
}
} // namespace

double KeyboardCheck::peakDb() const noexcept
{
    double p = -200.0;
    for (const auto* list : {&notes, &chords, &extremes})
        for (const auto& t : *list)
            p = std::max(p, t.peakDb);
    return p;
}

double KeyboardCheck::spreadDb(int low, int high) const noexcept
{
    double lo = 1e9, hi = -1e9;
    for (const auto& t : notes)
        if (t.rmsDb > -60.0 && t.key >= low && t.key <= high)
        {
            lo = std::min(lo, t.rmsDb);
            hi = std::max(hi, t.rmsDb);
        }
    return hi >= lo ? hi - lo : 0.0;
}

std::vector<std::string> KeyboardCheck::problems(double ceilingDb, double spreadLimitDb, int low,
                                                 int high) const
{
    std::vector<std::string> out;
    const auto say = [&](const char* what, const Take& t)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << what << " " << t.key << " peaks at " << t.peakDb
          << " dBFS (the ceiling is " << ceilingDb << ")";
        out.push_back(s.str());
    };
    for (const auto& t : notes)
        if (t.peakDb > ceilingDb)
            say("the note", t);
    for (const auto& t : chords)
        if (t.peakDb > ceilingDb)
            say("the chord on", t);
    for (const auto& t : extremes)
        if (t.peakDb > ceilingDb)
            say(("the chord, " + t.played + ", on").c_str(), t);
    if (spreadDb(low, high) > spreadLimitDb)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << "its notes from " << low << " to " << high << " are "
          << spreadDb(low, high) << " dB apart (at most " << spreadLimitDb << ")";
        out.push_back(s.str());
    }
    return out;
}

KeyboardCheck checkKeyboard(const DeviceFactory& factory, const DeviceState& state, double holdSeconds)
{
    KeyboardCheck c;
    for (int key = 24; key <= 96; key += 12)
        c.notes.push_back(render(factory, state, {key}, holdSeconds));
    for (int root = 36; root <= 84; root += 12)
        c.chords.push_back(render(factory, state, {root, root + 4, root + 7, root + 12}, holdSeconds));
    const std::vector<int> chord{60, 64, 67, 72};
    const auto& params = factory.info().params;
    if (std::any_of(params.begin(), params.end(), [](const ParamSpec& s) { return s.id == "macro1"; }))
    {
        c.extremes.push_back(render(factory, state, chord, holdSeconds, 0));
        c.extremes.back().played = "every macro at 0";
        c.extremes.push_back(render(factory, state, chord, holdSeconds, 1));
        c.extremes.back().played = "every macro at full";
    }
    c.extremes.push_back(render(factory, state, chord, holdSeconds, -1, true));
    c.extremes.back().played = "the mod wheel and pressure at full";
    return c;
}
} // namespace fd
