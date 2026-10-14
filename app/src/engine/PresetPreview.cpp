#include "engine/PresetPreview.h"

#include "Sampler.h"
#include "model/Racks.h"

#include <algorithm>
#include <cmath>
#include <fd/device/Events.h>
#include <fd/device/Registry.h>

namespace fd::engine
{
namespace
{
/// The phrase (browser.md §7): Bass and Drums a held C1, Pad a C3 major chord, the rest a C3, from
/// `root` (in the instrument's register); held 1.2 s, then its tail.
struct Phrase
{
    std::vector<std::uint8_t> keys;
    int held = 0, total = 0;

    Phrase(int root, bool chord, double tailSeconds, double sampleRate)
    {
        const auto key = [root](int above)
        { return static_cast<std::uint8_t>(std::clamp(root + above, 0, 127)); };
        keys = chord ? std::vector<std::uint8_t>{key(0), key(4), key(7)} : std::vector<std::uint8_t>{key(0)};
        held = static_cast<int>(1.2 * sampleRate);
        total = static_cast<int>(std::min(2.0 + std::min(tailSeconds, 1.0), 3.0) * sampleRate);
    }
    void eventsFor(int start, int n, EventList& events) const
    {
        events.clear();
        for (const auto k : keys)
        {
            if (start == 0)
                events.push({.sampleOffset = 0, .type = EventType::NoteOn, .key = k, .value = 0.8f});
            if (held >= start && held < start + n)
                events.push({.sampleOffset = held - start, .type = EventType::NoteOff, .key = k});
        }
    }
};

/// A short fade at the end, so a long tail stops without a click; empty if anything isn't finite.
juce::AudioBuffer<float> finish(juce::AudioBuffer<float> out, double sampleRate)
{
    const int total = out.getNumSamples();
    const int fade = std::min(total, static_cast<int>(0.05 * sampleRate));
    out.applyGainRamp(total - fade, fade, 1.0f, 0.0f);
    for (int c = 0; c < out.getNumChannels(); ++c)
        for (int i = 0; i < total; ++i)
            if (!std::isfinite(out.getSample(c, i)))
                return {};
    return out;
}
} // namespace

juce::AudioBuffer<float> renderPresetPhrase(const DeviceInfo& info, const DeviceState& state,
                                            const std::vector<std::string>& tags, double sampleRate)
{
    const auto* factory = findDevice(std::string(info.id));
    if (factory == nullptr || info.kind != DeviceKind::Instrument)
        return {};
    auto device = factory->create();
    if (applyState(*device, state) != ApplyResult::Ok)
        return {};

    constexpr int block = 512;
    device->prepare({sampleRate, block});
    // A bass, and a kit (its kick), on a C1; in the instrument's register: a Sampler plays its
    // notes by octaves into its zones' keys.
    const bool bass = !tags.empty() && (tags.front() == "Bass" || tags.front() == "Drums");
    const bool pad = !tags.empty() && tags.front() == "Pad";
    const Phrase phrase(devices::Sampler::playableKey(*device, bass ? 36 : 60), pad, device->tailSeconds(),
                        sampleRate);
    const int total = phrase.total;

    const int outs = std::max(1, info.buses.outputChannels), ins = std::max(0, info.buses.inputChannels);
    juce::AudioBuffer<float> out(2, total), in(std::max(1, ins), block), work(outs, block);
    out.clear();
    in.clear();
    EventList events;
    events.reserve(16);
    for (int start = 0; start < total; start += block)
    {
        const int n = std::min(block, total - start);
        phrase.eventsFor(start, n, events);
        work.clear();
        ProcessContext context;
        context.input = {in.getArrayOfWritePointers(), ins};
        context.output = {work.getArrayOfWritePointers(), outs};
        context.events = &events;
        context.numSamples = n;
        context.transport.samplePosition = start;
        device->process(context);
        for (int c = 0; c < 2; ++c)
            out.copyFrom(c, start, work, std::min(c, outs - 1), 0, n);
    }
    return finish(std::move(out), sampleRate);
}
} // namespace fd::engine

namespace fd::engine
{
juce::AudioBuffer<float> renderRackPhrase(const nlohmann::json& rack, double sampleRate)
{
    if (!rack.is_object() || rack.value("kind", std::string()) != "instrument")
        return {};
    // Each chain's modules, made from the file; a chain that's off plays nothing.
    struct Chain
    {
        std::vector<std::unique_ptr<Device>> modules;
        std::vector<bool> on; // a module that's off passes what comes in
        float left = 1.0f, right = 1.0f;
    };
    std::vector<Chain> chains;
    for (const auto& c : rack.value("chains", nlohmann::json::array()))
    {
        auto& chain = chains.emplace_back();
        const float db = c.value("volumeDb", 0.0f), pan = std::clamp(c.value("pan", 0.0f), -1.0f, 1.0f);
        const float gain =
            c.value("on", true) && db > model::minusInfinityDb ? juce::Decibels::decibelsToGain(db) : 0.0f;
        chain.left = gain * (pan > 0.0f ? 1.0f - pan : 1.0f); // a balance, as the chain's mixer pans
        chain.right = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
        for (const auto& m : c.value("modules", nlohmann::json::array()))
        {
            if (!m.contains("device"))
                return {}; // a plugin: not rendered offline
            const auto* factory = findDevice(m["device"].get<std::string>());
            const auto state = stateFromJson(m.value("state", nlohmann::json::object()));
            if (factory == nullptr || !state)
                return {};
            auto device = factory->create();
            if (applyState(*device, *state) != ApplyResult::Ok)
                return {};
            chain.on.push_back(m.value("on", true));
            chain.modules.push_back(std::move(device));
        }
    }
    // The macros where the file has them: each mapping puts its parameter where the engine would.
    for (const auto& macro : rack.value("macros", nlohmann::json::array()))
        for (const auto& m : macro.value("mappings", nlohmann::json::array()))
        {
            const auto c = static_cast<size_t>(m.value("chain", 0)),
                       at = static_cast<size_t>(m.value("module", 0));
            if (c >= chains.size() || at >= chains[c].modules.size())
                continue;
            auto& device = *chains[c].modules[at];
            if (const auto i = device.params().indexOf(m.value("param", std::string())))
                device.params().set(*i, device.info().params[*i].fromNormalized(model::macroMapped(
                                            macro.value("value", 0.0f), m.value("from", 0.0f),
                                            m.value("to", 1.0f), m.value("curve", 0.0f))));
        }

    constexpr int block = 512;
    double tail = 0.0;
    Device* first = nullptr; // the first chain's instrument: its register
    for (auto& chain : chains)
        for (auto& device : chain.modules)
        {
            device->prepare({sampleRate, block});
            tail = std::max(tail, device->tailSeconds());
            if (first == nullptr && device->info().kind == DeviceKind::Instrument)
                first = device.get();
        }
    if (first == nullptr)
        return {};
    std::vector<std::string> tags;
    for (const auto& t : rack.value("tags", nlohmann::json::array()))
        if (t.is_string())
            tags.push_back(t.get<std::string>());
    const bool bass = !tags.empty() && (tags.front() == "Bass" || tags.front() == "Drums");
    const bool pad = !tags.empty() && tags.front() == "Pad";
    const Phrase phrase(devices::Sampler::playableKey(*first, bass ? 36 : 60), pad, tail, sampleRate);

    juce::AudioBuffer<float> out(2, phrase.total), a(2, block), b(2, block);
    out.clear();
    EventList events;
    events.reserve(16);
    for (int start = 0; start < phrase.total; start += block)
    {
        const int n = std::min(block, phrase.total - start);
        phrase.eventsFor(start, n, events);
        for (auto& chain : chains)
        {
            // What comes in is silence (an instrument rack's input); each module from the last.
            a.clear();
            for (size_t i = 0; i < chain.modules.size(); ++i)
            {
                if (!chain.on[i])
                    continue;
                auto& device = chain.modules[i];
                b.clear();
                const auto& buses = device->info().buses;
                ProcessContext context;
                context.input = {a.getArrayOfWritePointers(), std::min(2, buses.inputChannels)};
                context.output = {b.getArrayOfWritePointers(),
                                  std::min(2, std::max(1, buses.outputChannels))};
                context.events = &events;
                context.numSamples = n;
                context.transport.samplePosition = start;
                device->process(context);
                if (buses.outputChannels == 1)
                    b.copyFrom(1, 0, b, 0, 0, n);
                std::swap(a, b);
            }
            out.addFrom(0, start, a, 0, 0, n, chain.left);
            out.addFrom(1, start, a, 1, 0, n, chain.right);
        }
    }
    return finish(std::move(out), sampleRate);
}
} // namespace fd::engine
