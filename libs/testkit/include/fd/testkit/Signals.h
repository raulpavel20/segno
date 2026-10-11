#pragma once

#include "fd/testkit/AudioBuffer.h"

#include <cstdint>

namespace fd::testkit
{
/// Test signal generators. All are deterministic and platform-independent
/// (noise uses our own PRNG, not std:: distributions).

AudioBuffer silence(int numSamples, int numChannels = 2);

AudioBuffer sine(double freqHz, double sampleRate, int numSamples, float amplitude = 0.5f, int numChannels = 2);

/// Different sine on left and right, to check channel handling (e.g. swap, invert, width).
AudioBuffer stereoSines(double freqLeftHz, double freqRightHz, double sampleRate, int numSamples,
                        float amplitude = 0.5f);

/// Exponential sweep from f0 to f1.
AudioBuffer logSweep(double f0Hz, double f1Hz, double sampleRate, int numSamples, float amplitude = 0.5f,
                     int numChannels = 2);

AudioBuffer impulse(int numSamples, int numChannels = 2, float amplitude = 1.0f, int position = 0);

/// Uniform white noise in [-amplitude, amplitude); independent per channel.
AudioBuffer whiteNoise(int numSamples, std::uint32_t seed, float amplitude = 0.25f, int numChannels = 2);

/// Pink noise (power falling 3 dB per octave), white noise of `amplitude` through Paul Kellet's
/// filter (within 0.05 dB of pink from 10 Hz up); independent per channel.
AudioBuffer pinkNoise(int numSamples, std::uint32_t seed, float amplitude = 0.25f, int numChannels = 2);

/// Constant value on all channels.
AudioBuffer dc(int numSamples, float value, int numChannels = 2);

/// Small deterministic PRNG (xorshift32) for tests and fuzzing.
class Rng
{
public:
    explicit Rng(std::uint32_t seed) noexcept : state_(seed ? seed : 0x9E3779B9u) {}

    std::uint32_t nextU32() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    /// Uniform in [0, 1).
    float next01() noexcept { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }

    /// Uniform integer in [lo, hi].
    int nextInt(int lo, int hi) noexcept
    {
        return lo + static_cast<int>(nextU32() % static_cast<std::uint32_t>(hi - lo + 1));
    }

private:
    std::uint32_t state_;
};
} // namespace fd::testkit
