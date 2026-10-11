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

AudioBuffer pinkNoise(int numSamples, std::uint32_t seed, float amplitude, int numChannels)
{
    AudioBuffer b = whiteNoise(numSamples, seed, amplitude, numChannels);
    for (int c = 0; c < numChannels; ++c)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double white = b.at(c, i);
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            b.at(c, i) = static_cast<float>(0.11 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362));
            b6 = white * 0.115926;
        }
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
