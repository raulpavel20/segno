#pragma once

#include "fd/dsp/Wavetable.h" // CurveTable, lookup

#include <algorithm>
#include <cmath>

namespace fd::devices::granular
{
/// Window's choices, in order (devices/granular/SPEC.md: grain.window).
enum class Window : int
{
    Hann,
    Gaussian,
    Triangle,
    SharpAttack,
    SharpEnd,
    Square,
    Drawn
};

/// A grain's window as the renderer evaluates it and the panel draws it (SPEC, "DSP design":
/// polynomials in the grain's phase, not tables; only Drawn reads one). Every window starts and
/// ends at zero, so grains never click.
struct GrainWindow
{
    Window shape = Window::Hann;
    float skew = 0.0f;     // -1 … +1: the peak earlier or later
    float flatness = 0.0f; // 0 … 1: the share of the grain at full level
    // Square's fades, and the sharp side of Sharp Attack and Sharp End, as a share of the grain.
    float fade = 0.02f;
    const dsp::CurveTable* drawn = nullptr;

    /// The window's level at `phase` (0 … 1 through the grain).
    [[nodiscard]] float at(float phase) const noexcept
    {
        phase = std::clamp(phase, 0.0f, 1.0f);
        // Skew moves the peak; a piecewise-linear warp of the phase, chosen at the grain's start.
        const float peak = 0.5f + 0.45f * std::clamp(skew, -1.0f, 1.0f);
        float v = phase < peak ? 0.5f * phase / peak : 0.5f + 0.5f * (phase - peak) / (1.0f - peak);
        // Flatness holds the middle at full level, the rise and fall squeezed either side.
        const float rise = 0.5f * (1.0f - std::clamp(flatness, 0.0f, 0.98f));
        v = v < rise ? 0.5f * v / rise : v > 1.0f - rise ? 0.5f + 0.5f * (v - (1.0f - rise)) / rise : 0.5f;
        return base(v);
    }

private:
    [[nodiscard]] float base(float v) const noexcept
    {
        switch (shape)
        {
        case Window::Hann:
        {
            // cos²(πu/2) as the square of an even polynomial: four multiply-adds, error under 2e-5.
            const float u = 2.0f * v - 1.0f, u2 = u * u;
            const float c = 1.0f + u2 * (-1.2336979f + u2 * (0.2536695f - 0.0208621f * u2));
            return std::max(0.0f, c) * std::max(0.0f, c);
        }
        case Window::Gaussian:
        {
            constexpr float sigma = 0.16f;
            const float e0 = std::exp(-0.5f * (0.5f / sigma) * (0.5f / sigma));
            const float d = (v - 0.5f) / sigma;
            return std::max(0.0f, (std::exp(-0.5f * d * d) - e0) / (1.0f - e0));
        }
        case Window::Triangle:
            return 1.0f - std::abs(2.0f * v - 1.0f);
        case Window::SharpAttack:
            return sharp(v);
        case Window::SharpEnd:
            return sharp(1.0f - v);
        case Window::Square:
            return std::min({1.0f, v / fade, (1.0f - v) / fade});
        case Window::Drawn:
        {
            // Ends at zero, whatever is drawn.
            const float edge = std::min({1.0f, v / fade, (1.0f - v) / fade});
            return drawn != nullptr ? std::clamp(dsp::lookup(*drawn, v), 0.0f, 1.0f) * edge : edge;
        }
        }
        return 0.0f;
    }

    /// Up at once, then an exponential fall to zero at the end.
    [[nodiscard]] float sharp(float v) const noexcept
    {
        if (v < fade)
            return v / fade;
        constexpr float k = 5.0f;
        const float end = std::exp(-k);
        return std::max(0.0f, (std::exp(-k * (v - fade) / (1.0f - fade)) - end) / (1.0f - end));
    }
};
} // namespace fd::devices::granular
