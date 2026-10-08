#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <xmmintrin.h>
#define FD_DENORMALS_SSE 1
#endif

namespace fd::dsp
{
/// Enables flush-to-zero / denormals-are-zero for the current thread while in scope.
/// Hosts normally do this already; devices and test harnesses use it defensively.
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if defined(FD_DENORMALS_SSE)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u); // FTZ | DAZ
#elif defined(__aarch64__)
        std::uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24); // FZ
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedNoDenormals()
    {
#if defined(FD_DENORMALS_SSE)
        _mm_setcsr(static_cast<unsigned>(saved_));
#elif defined(__aarch64__)
        std::uint64_t fpcr = saved_;
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    std::uint64_t saved_ = 0;
};
} // namespace fd::dsp
