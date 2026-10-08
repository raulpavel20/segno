#pragma once

#include <complex>
#include <vector>

namespace fd::dsp
{
/// Radix-2 complex FFT with precomputed twiddles (for analyzers; not used on the audio thread).
class Fft
{
public:
    explicit Fft(int order = 12) { prepare(order); }

    void prepare(int order);
    [[nodiscard]] int size() const noexcept { return size_; }

    /// In place, forward transform of `size()` complex values.
    void forward(std::complex<float>* data) const noexcept;

    /// Magnitude spectrum (size/2 + 1 bins) of a real block, with a Hann window applied.
    /// Normalised so a full-scale sine reads ~1.0 at its bin. `scratch` is resized as needed.
    void magnitudes(const float* input, float* out, std::vector<std::complex<float>>& scratch) const;

private:
    int size_ = 0;
    std::vector<std::complex<float>> twiddles_;
    std::vector<int> bitReverse_;
    std::vector<float> window_;
};
} // namespace fd::dsp
