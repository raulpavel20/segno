#include "fd/testkit/DeviceRender.h"

#include "fd/device/Realtime.h"
#include "fd/dsp/Denormals.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>

// ---------------------------------------------------------------------------
// Allocation tracking: counts operator new while inside Device::process. Disabled under
// sanitizers, which install their own allocators (RTSan does the real-time checking there).
// ---------------------------------------------------------------------------
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(realtime_sanitizer)
#define FD_NO_ALLOC_TRACKING 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(_MSC_VER)
#define FD_NO_ALLOC_TRACKING 1
#endif

namespace
{
thread_local bool tInProcess = false;
std::atomic<std::int64_t> gAllocations{0};
} // namespace

#if !defined(FD_NO_ALLOC_TRACKING)
namespace
{
void* trackedAlloc(std::size_t size, std::size_t alignment)
{
    if (tInProcess)
        gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (size == 0)
        size = 1;
    void* p = alignment > alignof(std::max_align_t)
                  ? std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment)
                  : std::malloc(size);
    return p;
}
} // namespace

void* operator new(std::size_t size)
{
    if (void* p = trackedAlloc(size, 0))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    if (void* p = trackedAlloc(size, 0))
        return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, std::align_val_t a)
{
    if (void* p = trackedAlloc(size, static_cast<std::size_t>(a)))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t a)
{
    if (void* p = trackedAlloc(size, static_cast<std::size_t>(a)))
        return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return trackedAlloc(size, 0); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return trackedAlloc(size, 0); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
#endif

namespace fd::testkit
{
std::int64_t allocationsDuringProcess() noexcept
{
    return gAllocations.load(std::memory_order_relaxed);
}

bool allocationTrackingEnabled() noexcept
{
#if defined(FD_NO_ALLOC_TRACKING)
    return false;
#else
    return true;
#endif
}

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
        ctx.transport.samplePosition = pos;
        ctx.transport.playing = true;

        tInProcess = true;
        processRealtime(device, ctx);
        tInProcess = false;
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
