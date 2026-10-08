#include "fd/dsp/Fft.h"

#include "fd/dsp/Math.h"

#include <cmath>

namespace fd::dsp
{
void Fft::prepare(int order)
{
    size_ = 1 << order;
    twiddles_.resize(static_cast<size_t>(size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
        twiddles_[static_cast<size_t>(k)] = std::polar(1.0f, static_cast<float>(-twoPi * k / size_));
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
    for (int len = 2; len <= size_; len <<= 1)
    {
        const int step = size_ / len;
        for (int i = 0; i < size_; i += len)
            for (int k = 0; k < len / 2; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2] * twiddles_[static_cast<size_t>(k * step)];
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
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
