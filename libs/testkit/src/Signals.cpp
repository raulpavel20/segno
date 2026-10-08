#include "fd/testkit/Signals.h"

#include <cmath>
#include <numbers>

namespace fd::testkit
{
namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;
}

AudioBuffer silence(int numSamples, int numChannels)
{
    return AudioBuffer(numChannels, numSamples);
}

AudioBuffer sine(double freqHz, double sampleRate, int numSamples, float amplitude, int numChannels)
{
    AudioBuffer b(numChannels, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto v = static_cast<float>(amplitude * std::sin(twoPi * freqHz * i / sampleRate));
        for (int c = 0; c < numChannels; ++c)
            b.at(c, i) = v;
    }
    return b;
}

AudioBuffer stereoSines(double freqLeftHz, double freqRightHz, double sampleRate, int numSamples, float amplitude)
{
    AudioBuffer b(2, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        b.at(0, i) = static_cast<float>(amplitude * std::sin(twoPi * freqLeftHz * i / sampleRate));
        b.at(1, i) = static_cast<float>(amplitude * std::sin(twoPi * freqRightHz * i / sampleRate));
    }
    return b;
}

AudioBuffer logSweep(double f0Hz, double f1Hz, double sampleRate, int numSamples, float amplitude, int numChannels)
{
    AudioBuffer b(numChannels, numSamples);
    const double T = numSamples / sampleRate;
    const double k = std::log(f1Hz / f0Hz);
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / sampleRate;
        const double phase = twoPi * f0Hz * T / k * (std::exp(t / T * k) - 1.0);
        const auto v = static_cast<float>(amplitude * std::sin(phase));
        for (int c = 0; c < numChannels; ++c)
            b.at(c, i) = v;
    }
    return b;
}

AudioBuffer impulse(int numSamples, int numChannels, float amplitude, int position)
{
    AudioBuffer b(numChannels, numSamples);
    if (position >= 0 && position < numSamples)
        for (int c = 0; c < numChannels; ++c)
            b.at(c, position) = amplitude;
    return b;
}

AudioBuffer whiteNoise(int numSamples, std::uint32_t seed, float amplitude, int numChannels)
{
    AudioBuffer b(numChannels, numSamples);
    for (int c = 0; c < numChannels; ++c)
    {
        Rng rng(seed + static_cast<std::uint32_t>(c) * 7919u);
        for (int i = 0; i < numSamples; ++i)
            b.at(c, i) = amplitude * (2.0f * rng.next01() - 1.0f);
    }
    return b;
}

AudioBuffer dc(int numSamples, float value, int numChannels)
{
    AudioBuffer b(numChannels, numSamples);
    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < numSamples; ++i)
            b.at(c, i) = value;
    return b;
}
} // namespace fd::testkit
