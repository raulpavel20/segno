#include "engine/PresetPreview.h"

#include <algorithm>
#include <cmath>
#include <fd/device/Events.h>
#include <fd/device/Registry.h>

namespace fd::engine
{
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
    const bool bass = !tags.empty() && tags.front() == "Bass";
    const bool pad = !tags.empty() && tags.front() == "Pad";
    std::vector<std::uint8_t> keys = bass  ? std::vector<std::uint8_t>{36}
                                     : pad ? std::vector<std::uint8_t>{60, 64, 67}
                                           : std::vector<std::uint8_t>{60};
    const int held = static_cast<int>(1.2 * sampleRate);
    const int total =
        static_cast<int>(std::min(2.0 + std::min(device->tailSeconds(), 1.0), 3.0) * sampleRate);

    const int outs = std::max(1, info.buses.outputChannels), ins = std::max(0, info.buses.inputChannels);
    juce::AudioBuffer<float> out(2, total), in(std::max(1, ins), block), work(outs, block);
    out.clear();
    in.clear();
    EventList events;
    events.reserve(16);
    for (int start = 0; start < total; start += block)
    {
        const int n = std::min(block, total - start);
        events.clear();
        for (const auto k : keys)
        {
            if (start == 0)
                events.push({.sampleOffset = 0, .type = EventType::NoteOn, .key = k, .value = 0.8f});
            if (held >= start && held < start + n)
                events.push({.sampleOffset = held - start, .type = EventType::NoteOff, .key = k});
        }
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
    // A short fade at the end, so a long tail stops without a click.
    const int fade = std::min(total, static_cast<int>(0.05 * sampleRate));
    out.applyGainRamp(total - fade, fade, 1.0f, 0.0f);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i)
            if (!std::isfinite(out.getSample(c, i)))
                return {};
    return out;
}
} // namespace fd::engine
