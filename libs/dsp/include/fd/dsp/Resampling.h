#pragma once

#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace fd::dsp
{
/// Band-limited interpolation for sample playback: Kaiser-windowed sinc kernels in polyphase
/// tables (linear interpolation between phases).
///
/// Reading at a speed above the source rate needs a lower cutoff: `stretch` (1..2) scales the
/// kernel's cutoff down and its length up. Above 2x, play a decimated copy (decimateByTwo)
/// instead, so every speed costs at most 2x the base kernel. Stretch is quantised up to
/// 1/16 steps (the cutoff is then a little conservative; pitch is unaffected).
class SincKernels
{
public:
    static constexpr int zeroCrossings = 24; // per side at stretch 1 (48 taps)
    static constexpr int phases = 256;
    static constexpr int stretchSteps = 16; // per octave of stretch
    static constexpr int maxTaps = 4 * zeroCrossings;
    /// Valid samples a source buffer needs before index 0 and after its last sample.
    static constexpr int padding = maxTaps / 2 + 1;

    struct Table
    {
        int taps = 0;
        std::vector<float> data; // (phases + 1) rows of `taps` coefficients
    };

    /// The shared kernel bank (built once, thread-safe).
    static const SincKernels& instance();

    /// Table for reading at `stretch` (clamped to 1..2) times the source rate.
    [[nodiscard]] const Table& table(double stretch) const noexcept;

    /// Interpolated value of `src` at fractional position `pos`. `src[-padding]` to
    /// `src[floor(pos) + padding]` must be readable.
    [[nodiscard]] static float read(const Table& t, const float* src, double pos) noexcept
    {
        const double base = std::floor(pos);
        const double ph = (pos - base) * phases;
        const int p = static_cast<int>(ph);
        const float mix = static_cast<float>(ph - p);
        const float* h0 = t.data.data() + static_cast<size_t>(p) * static_cast<size_t>(t.taps);
        const float* h1 = h0 + t.taps;
        const float* x = src + static_cast<long>(base) - (t.taps / 2 - 1);
        float a = 0.0f, b = 0.0f;
        for (int k = 0; k < t.taps; ++k)
        {
            a += x[k] * h0[k];
            b += x[k] * h1[k];
        }
        return a + mix * (b - a);
    }

private:
    SincKernels();
    std::array<Table, stretchSteps + 1> tables_;
};

/// Halves the sample rate of `in` (offline, not real-time): a linear-phase low-pass that keeps
/// up to 0.41 of the new rate's band, then every second sample. out[i] is aligned with in[2i].
[[nodiscard]] std::vector<float> decimateByTwo(std::span<const float> in);
} // namespace fd::dsp
