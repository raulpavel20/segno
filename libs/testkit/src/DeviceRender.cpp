#include "fd/testkit/DeviceRender.h"

#include "fd/device/Realtime.h"
#include "fd/dsp/Denormals.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>

namespace fd::testkit
{
AudioBuffer render(Device& device, const AudioBuffer& input, const RenderOptions& options)
{
    const std::vector<int> pattern = options.blockPattern.empty() ? std::vector<int>{512} : options.blockPattern;
    const int maxBlock = *std::max_element(pattern.begin(), pattern.end());
    const auto& buses = device.info().buses;
    const int total = input.numSamples() + options.tailSamples;

    if (!options.skipPrepare)
        device.prepare({.sampleRate = options.sampleRate, .maxBlockSize = maxBlock});

    const int inChannels = std::max(buses.inputChannels, 0);
    const int outChannels = std::max(buses.outputChannels, 1);
    const int workChannels = std::max(inChannels, outChannels);

    // Whole-render buffers; blocks are views into them.
    AudioBuffer in(std::max(inChannels, 1), total);
    for (int c = 0; c < in.numChannels() && c < input.numChannels(); ++c)
        for (int i = 0; i < input.numSamples(); ++i)
            in.at(c, i) = input.at(c, i);
    AudioBuffer out(options.inPlace ? workChannels : outChannels, total);
    if (options.inPlace)
        for (int c = 0; c < in.numChannels(); ++c)
            for (int i = 0; i < total; ++i)
                out.at(c, i) = in.at(c, i);

    EventList events;
    events.reserve(1024);
    size_t nextEvent = 0;

    std::vector<float*> inPtrs(static_cast<size_t>(in.numChannels()));
    std::vector<float*> outPtrs(static_cast<size_t>(out.numChannels()));
    // The key, padded with silence to the render's length.
    const int keyChannels = options.sidechain != nullptr ? std::min(buses.sidechainChannels, options.sidechain->numChannels()) : 0;
    AudioBuffer key(std::max(keyChannels, 1), total);
    for (int c = 0; c < keyChannels; ++c)
        for (int i = 0; i < std::min(total, options.sidechain->numSamples()); ++i)
            key.at(c, i) = options.sidechain->at(c, i);
    std::vector<float*> keyPtrs(static_cast<size_t>(key.numChannels()));

    dsp::ScopedNoDenormals noDenormals;
    std::int64_t pos = 0;
    size_t blockIndex = 0;
    while (pos < total)
    {
        const int n = std::min(pattern[blockIndex++ % pattern.size()], static_cast<int>(total - pos));
        if (options.beforeBlock)
            options.beforeBlock(device, pos);

        events.clear();
        while (nextEvent < options.events.size() && options.events[nextEvent].samplePosition < pos + n)
        {
            Event e = options.events[nextEvent].event;
            e.sampleOffset = static_cast<int>(std::max<std::int64_t>(0, options.events[nextEvent].samplePosition - pos));
            events.push(e);
            ++nextEvent;
        }

        for (int c = 0; c < in.numChannels(); ++c)
            inPtrs[static_cast<size_t>(c)] = (options.inPlace ? out.channel(c) : in.channel(c)) + pos;
        for (int c = 0; c < out.numChannels(); ++c)
            outPtrs[static_cast<size_t>(c)] = out.channel(c) + pos;

        for (int c = 0; c < key.numChannels(); ++c)
            keyPtrs[static_cast<size_t>(c)] = key.channel(c) + pos;

        ProcessContext ctx;
        ctx.input = {inPtrs.data(), inChannels};
        ctx.output = {outPtrs.data(), outChannels};
        ctx.sidechain = {keyPtrs.data(), keyChannels};
        ctx.events = &events;
        ctx.numSamples = n;
        ctx.transport = options.transport;
        ctx.transport.samplePosition = pos;
        ctx.transport.ppqPosition = options.transport.ppqPosition + static_cast<double>(pos) /
                                                                        options.sampleRate *
                                                                        options.transport.tempoBpm / 60.0;
        ctx.transport.playing = true;

        {
            const CountAllocations counting;
            processRealtime(device, ctx);
        }
        pos += n;
    }

    if (options.inPlace && out.numChannels() != outChannels)
    {
        AudioBuffer trimmed(outChannels, total);
        for (int c = 0; c < outChannels; ++c)
            for (int i = 0; i < total; ++i)
                trimmed.at(c, i) = out.at(c, i);
        return trimmed;
    }
    return out;
}
} // namespace fd::testkit
