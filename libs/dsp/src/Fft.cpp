#include "fd/dsp/Fft.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
void Fft::prepare(int order)
{
    size_ = 1 << order;
    twiddles_.resize(static_cast<size_t>(size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
        twiddles_[static_cast<size_t>(k)] = std::polar(1.0f, static_cast<float>(-twoPi * k / size_));
    // Each stage's twiddles in a row: the stage with `half` butterflies per group uses
    // exp(-i pi k / half), k < half, stored from index half - 1 (size - 1 in all).
    stageTwiddles_.resize(static_cast<size_t>(std::max(1, size_ - 1)));
    for (int half = 1; half < size_; half <<= 1)
        for (int k = 0; k < half; ++k)
            stageTwiddles_[static_cast<size_t>(half - 1 + k)] =
                std::polar(1.0f, static_cast<float>(-pi * k / half));
    bitReverse_.resize(static_cast<size_t>(size_));
    for (int i = 0; i < size_; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            r |= ((i >> b) & 1) << (order - 1 - b);
        bitReverse_[static_cast<size_t>(i)] = r;
    }
    window_.resize(static_cast<size_t>(size_));
    for (int i = 0; i < size_; ++i)
        window_[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(twoPi * i / size_));
}

void Fft::forward(std::complex<float>* data) const noexcept
{
    for (int i = 0; i < size_; ++i)
        if (const int j = bitReverse_[static_cast<size_t>(i)]; i < j)
            std::swap(data[i], data[j]);
    // Radix-2 butterflies on interleaved floats, the twiddles of each stage contiguous
    // (std::complex's operator* checks for infinities through a library call: several times
    // slower).
    auto* x = reinterpret_cast<float*>(data);
    const auto* w = reinterpret_cast<const float*>(stageTwiddles_.data());
    for (int half = 1; half < size_; half <<= 1)
    {
        const float* tw = w + 2 * (half - 1); // this stage's `half` twiddles
        for (int i = 0; i < size_; i += 2 * half)
        {
            float* a = x + 2 * i;
            float* b = x + 2 * (i + half);
            for (int k = 0; k < half; ++k)
            {
                const float wr = tw[2 * k], wi = tw[2 * k + 1];
                const float br = b[2 * k], bi = b[2 * k + 1];
                const float vr = br * wr - bi * wi, vi = br * wi + bi * wr;
                const float ar = a[2 * k], ai = a[2 * k + 1];
                a[2 * k] = ar + vr;
                a[2 * k + 1] = ai + vi;
                b[2 * k] = ar - vr;
                b[2 * k + 1] = ai - vi;
            }
        }
    }
}

void Fft::magnitudes(const float* input, float* out, std::vector<std::complex<float>>& scratch) const
{
    scratch.resize(static_cast<size_t>(size_));
    for (int i = 0; i < size_; ++i)
        scratch[static_cast<size_t>(i)] = {input[i] * window_[static_cast<size_t>(i)], 0.0f};
    forward(scratch.data());
    const float norm = 4.0f / static_cast<float>(size_);
    for (int k = 0; k <= size_ / 2; ++k)
        out[k] = std::abs(scratch[static_cast<size_t>(k)]) * norm;
}
} // namespace fd::dsp
