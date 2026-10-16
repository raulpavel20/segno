#include "DrumSynth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <numbers>
#include <vector>

namespace fd::library_build
{
namespace
{
constexpr double kRate = 48000.0;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Noise from a seed: the same seed, the same noise.
struct Noise
{
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed == 0 ? 1u : seed) {}
    double next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 2147483648.0 - 1.0;
    }
};

/// A second-order filter (RBJ's cookbook).
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    enum class Kind
    {
        LowPass,
        HighPass,
        BandPass
    };

    static Biquad make(Kind kind, double hz, double q)
    {
        const double w = kTwoPi * std::clamp(hz, 10.0, kRate * 0.45) / kRate;
        const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
        Biquad f;
        switch (kind)
        {
        case Kind::LowPass:
            f.b0 = (1.0 - c) / 2.0 / a0;
            f.b1 = (1.0 - c) / a0;
            f.b2 = f.b0;
            break;
        case Kind::HighPass:
            f.b0 = (1.0 + c) / 2.0 / a0;
            f.b1 = -(1.0 + c) / a0;
            f.b2 = f.b0;
            break;
        case Kind::BandPass:
            f.b0 = alpha / a0;
            f.b1 = 0.0;
            f.b2 = -alpha / a0;
            break;
        }
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    double run(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

double fade(double t, double decay)
{
    return std::exp(-t / std::max(1e-4, decay));
}

double saturate(double x, double drive)
{
    if (drive <= 0.0)
        return x;
    const double k = 1.0 + drive * 9.0;
    return std::tanh(x * k) / std::tanh(k);
}

using Params = nlohmann::json;

std::vector<float> kick(const Params& p, int frames, Noise& noise)
{
    const double pitch = p.value("pitch", 50.0), sweep = p.value("sweep", 160.0),
                 sweepTime = p.value("sweepTime", 0.04), decay = p.value("decay", 0.45),
                 click = p.value("click", 0.3), body = p.value("noise", 0.0), drive = p.value("drive", 0.0);
    auto clickFilter = Biquad::make(Biquad::Kind::HighPass, 1500.0, 0.7);
    auto bodyFilter = Biquad::make(Biquad::Kind::BandPass, pitch * 3.0, 1.0);
    std::vector<float> out(static_cast<size_t>(frames));
    double phase = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        const double hz = pitch + (sweep - pitch) * std::exp(-t / std::max(1e-4, sweepTime));
        phase += kTwoPi * hz / kRate;
        const double n = noise.next();
        double x = std::sin(phase) * fade(t, decay);
        x += click * clickFilter.run(n) * fade(t, 0.0015);
        x += body * bodyFilter.run(n) * fade(t, decay * 0.3);
        out[static_cast<size_t>(i)] = static_cast<float>(saturate(x, drive));
    }
    return out;
}

std::vector<float> snare(const Params& p, int frames, Noise& noise)
{
    const double tone = p.value("tone", 185.0), tone2 = p.value("tone2", 330.0),
                 toneDecay = p.value("toneDecay", 0.08), color = p.value("color", 5000.0),
                 noiseDecay = p.value("noiseDecay", 0.16), mix = std::clamp(p.value("noise", 0.6), 0.0, 1.0),
                 drive = p.value("drive", 0.0);
    auto band = Biquad::make(Biquad::Kind::BandPass, color, 0.6);
    auto high = Biquad::make(Biquad::Kind::HighPass, 900.0, 0.7);
    std::vector<float> out(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        const double body =
            (std::sin(kTwoPi * tone * t) + 0.5 * std::sin(kTwoPi * tone2 * t)) / 1.5 * fade(t, toneDecay);
        const double hiss = high.run(band.run(noise.next())) * 2.5 * fade(t, noiseDecay);
        out[static_cast<size_t>(i)] = static_cast<float>(saturate((1.0 - mix) * body + mix * hiss, drive));
    }
    return out;
}

std::vector<float> clap(const Params& p, int frames, Noise& noise)
{
    const int bursts = std::max(1, p.value("bursts", 4));
    const double spacing = p.value("spacing", 0.011), color = p.value("color", 1200.0),
                 decay = p.value("decay", 0.18);
    auto band = Biquad::make(Biquad::Kind::BandPass, color, 1.4);
    std::vector<float> out(static_cast<size_t>(frames));
    const double tail = bursts * spacing;
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        double env = 0.0;
        for (int b = 0; b < bursts; ++b)
            if (const double since = t - b * spacing; since >= 0.0)
                env = std::max(env, fade(since, 0.006));
        if (t >= tail - spacing)
            env = std::max(env, 0.6 * fade(t - (tail - spacing), decay));
        out[static_cast<size_t>(i)] = static_cast<float>(band.run(noise.next()) * 3.0 * env);
    }
    return out;
}

std::vector<float> metal(const Params& p, int frames, Noise& noise)
{
    std::vector<double> freqs{205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
    if (p.contains("freqs"))
        freqs = p["freqs"].get<std::vector<double>>();
    const double decay = p.value("decay", 0.05), hp = p.value("hp", 7000.0), hiss = p.value("noise", 0.0);
    const bool bell = p.contains("bp");
    auto first = bell ? Biquad::make(Biquad::Kind::BandPass, p.value("bp", 800.0), 2.5)
                      : Biquad::make(Biquad::Kind::HighPass, hp, 0.7);
    auto second = bell ? Biquad::make(Biquad::Kind::LowPass, p.value("bp", 800.0) * 4.0, 0.7)
                       : Biquad::make(Biquad::Kind::HighPass, hp, 0.7);
    std::vector<double> phases(freqs.size(), 0.0);
    std::vector<float> out(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        double x = 0.0;
        for (size_t k = 0; k < freqs.size(); ++k)
        {
            phases[k] += freqs[k] / kRate;
            x += phases[k] - std::floor(phases[k]) < 0.5 ? 1.0 : -1.0;
        }
        x /= static_cast<double>(freqs.size());
        x += hiss * noise.next();
        out[static_cast<size_t>(i)] =
            static_cast<float>(second.run(first.run(x)) * (bell ? 2.0 : 1.5) * fade(t, decay));
    }
    return out;
}

std::vector<float> rim(const Params& p, int frames, Noise& noise)
{
    const double pitch = p.value("pitch", 1700.0), decay = p.value("decay", 0.025);
    auto click = Biquad::make(Biquad::Kind::BandPass, pitch * 1.5, 2.0);
    std::vector<float> out(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        out[static_cast<size_t>(i)] = static_cast<float>(std::sin(kTwoPi * pitch * t) * fade(t, decay) +
                                                         click.run(noise.next()) * 2.0 * fade(t, 0.004));
    }
    return out;
}

std::vector<float> chip(const Params& p, int frames, Noise& noise)
{
    const auto wave = p.value("wave", std::string("pulse"));
    const double start = p.value("start", 400.0), end = p.value("end", 60.0),
                 sweepTime = p.value("sweepTime", 0.08), decay = p.value("decay", 0.2),
                 duty = p.value("duty", 0.5), rate = p.value("rate", 22050.0);
    const int steps = std::max(2, p.value("steps", 16));
    const bool shortNoise = p.value("noiseMode", std::string("long")) == "short";
    std::uint32_t lfsr = 1u + (noise.state & 0x7ffeu);
    double phase = 0.0, held = 0.0, hold = 0.0, noiseClock = 0.0, noiseOut = 1.0;
    std::vector<float> out(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        // The pitch slides as the old chips did: in steps, from start to end.
        const double hz = end + (start - end) * std::exp(-t / std::max(1e-4, sweepTime));
        phase += hz / kRate;
        const double frac = phase - std::floor(phase);
        double x = 0.0;
        if (wave == "triangle")
            x = std::round((frac < 0.5 ? 4.0 * frac - 1.0 : 3.0 - 4.0 * frac) * 7.5) / 7.5; // 4-bit
        else if (wave == "noise")
        {
            noiseClock += hz / kRate;
            while (noiseClock >= 1.0)
            {
                noiseClock -= 1.0;
                const std::uint32_t bit = (lfsr ^ (lfsr >> (shortNoise ? 6u : 1u))) & 1u;
                lfsr = (lfsr >> 1u) | (bit << 14u);
                noiseOut = (lfsr & 1u) != 0u ? 1.0 : -1.0;
            }
            x = noiseOut;
        }
        else
            x = frac < duty ? 1.0 : -1.0;
        // Its loudness in the chip's few steps, held at the chip's rate.
        const double env = std::floor(fade(t, decay) * steps) / steps;
        hold += rate / kRate;
        if (hold >= 1.0)
        {
            hold -= std::floor(hold);
            held = x * env;
        }
        out[static_cast<size_t>(i)] = static_cast<float>(held * 0.5);
    }
    return out;
}

std::vector<float> sub(const Params& p, int frames, Noise&)
{
    const double pitch = p.value("pitch", 55.0), glide = p.value("glide", 0.0), decay = p.value("decay", 1.6),
                 drive = p.value("drive", 0.3);
    std::vector<float> out(static_cast<size_t>(frames));
    double phase = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        const double hz = pitch * std::pow(2.0, glide / 12.0 * std::exp(-t / 0.06));
        phase += kTwoPi * hz / kRate;
        const double attack = std::min(1.0, t / 0.002);
        out[static_cast<size_t>(i)] =
            static_cast<float>(saturate(std::sin(phase) * attack * fade(t, decay), drive));
    }
    return out;
}
} // namespace

std::optional<audiofile::DecodedAudio> renderVoice(const std::string& json, std::string* error)
{
    const auto p = nlohmann::json::parse(json, nullptr, false);
    const auto fail = [error](std::string why) -> std::optional<audiofile::DecodedAudio>
    {
        if (error != nullptr)
            *error = std::move(why);
        return std::nullopt;
    };
    if (!p.is_object())
        return fail("a voice is a JSON object");
    const auto voice = p.value("voice", std::string());
    const int frames = static_cast<int>(std::clamp(p.value("seconds", 1.0), 0.01, 20.0) * kRate);
    Noise noise(p.value("seed", 1u));
    std::vector<float> samples;
    try
    {
        if (voice == "kick" || voice == "tom")
            samples = kick(p, frames, noise);
        else if (voice == "snare")
            samples = snare(p, frames, noise);
        else if (voice == "clap")
            samples = clap(p, frames, noise);
        else if (voice == "metal")
            samples = metal(p, frames, noise);
        else if (voice == "rim")
            samples = rim(p, frames, noise);
        else if (voice == "chip")
            samples = chip(p, frames, noise);
        else if (voice == "sub")
            samples = sub(p, frames, noise);
        else
            return fail("no voice \"" + voice + "\"");
    }
    catch (const nlohmann::json::exception& e)
    {
        return fail(std::string("a voice's parameter: ") + e.what());
    }
    const double level = std::clamp(p.value("level", 1.0), 0.0, 1.0);
    for (auto& x : samples)
        x = static_cast<float>(std::clamp(x * level, -1.0, 1.0));
    audiofile::DecodedAudio audio;
    audio.sampleRate = kRate;
    audio.channels.push_back(std::move(samples));
    return audio;
}
} // namespace fd::library_build
