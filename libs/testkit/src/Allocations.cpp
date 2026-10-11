#include "fd/testkit/Allocations.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

// ---------------------------------------------------------------------------
// Allocation tracking: counts operator new inside Device::process (and CountAllocations scopes). Disabled
// under sanitizers, which install their own allocators (RTSan does the real-time checking there).
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
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return trackedAlloc(size, 0);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return trackedAlloc(size, 0);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    std::free(p);
}
#endif

namespace fd::testkit
{
std::int64_t allocationsDuringProcess() noexcept
{
    return gAllocations.load(std::memory_order_relaxed);
}

CountAllocations::CountAllocations() noexcept : was_(tInProcess)
{
    tInProcess = true;
}

CountAllocations::~CountAllocations()
{
    tInProcess = was_;
}

bool allocationTrackingEnabled() noexcept
{
#if defined(FD_NO_ALLOC_TRACKING)
    return false;
#else
    return true;
#endif
}
} // namespace fd::testkit
