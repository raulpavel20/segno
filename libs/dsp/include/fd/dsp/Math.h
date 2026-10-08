#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fd::dsp
{
inline constexpr double pi = std::numbers::pi;
inline constexpr double twoPi = 2.0 * std::numbers::pi;

/// Levels at or below this (in dB) are treated as silence (-inf).
inline constexpr float minusInfinityDb = -100.0f;

[[nodiscard]] inline float dbToGain(float db, float minusInfDb = minusInfinityDb) noexcept
{
    return db <= minusInfDb ? 0.0f : std::pow(10.0f, db * 0.05f);
}

[[nodiscard]] inline float gainToDb(float gain, float minusInfDb = minusInfinityDb) noexcept
{
    return gain <= 0.0f ? minusInfDb : std::max(minusInfDb, 20.0f * std::log10(gain));
}

template <typename T>
[[nodiscard]] constexpr T lerp(T a, T b, T t) noexcept
{
    return a + (b - a) * t;
}

[[nodiscard]] inline bool isSubnormal(float x) noexcept
{
    return std::fpclassify(x) == FP_SUBNORMAL;
}

[[nodiscard]] inline int msToSamples(double ms, double sampleRate) noexcept
{
    return static_cast<int>(std::lround(ms * 0.001 * sampleRate));
}
} // namespace fd::dsp
