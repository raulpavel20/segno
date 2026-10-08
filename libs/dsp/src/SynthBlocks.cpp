#include "fd/dsp/SynthBlocks.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <complex>

namespace fd::dsp
{
namespace
{
// Both filters are trapezoidal (TPT) discretisations prewarped at the cutoff, so their digital
// response is the analog one evaluated at s = j tan(pi f / fs) / tan(pi fc / fs).
std::complex<double> tptS(double freq, double cutoff, double sampleRate) noexcept
{
    const double g = std::tan(pi * std::clamp(cutoff, 10.0, 0.49 * sampleRate) / sampleRate);
    return {0.0, std::tan(pi * std::clamp(freq, 0.0, 0.4999 * sampleRate) / sampleRate) / g};
}
} // namespace

double StateVariableFilter::magnitude(double freq, double cutoff, double resonance, double sampleRate,
                                      Mode mode) noexcept
{
    const auto s = tptS(freq, cutoff, sampleRate);
    const double k = 1.0 / (0.5 * std::pow(50.0, std::clamp(resonance, 0.0, 1.0)));
    const auto d = s * s + k * s + 1.0;
    switch (mode)
    {
    case Mode::Lowpass:
        return std::abs(1.0 / d);
    case Mode::Highpass:
        return std::abs(s * s / d);
    case Mode::Bandpass:
        return std::abs(s / d);
    }
    return std::abs(1.0 / d);
}

double LadderFilter::magnitude(double freq, double cutoff, double resonance, double sampleRate) noexcept
{
    const auto s = tptS(freq, cutoff, sampleRate);
    const double k = 4.2 * std::clamp(resonance, 0.0, 1.0);
    const auto stage = 1.0 / (1.0 + s);
    const auto g4 = stage * stage * stage * stage;
    return std::abs((1.0 + 0.5 * k) * g4 / (1.0 + k * g4));
}

float StateVariableFilter::process(float x, double cutoff, double resonance, double sampleRate, Mode mode) noexcept
{
    const double g = std::tan(pi * std::clamp(cutoff, 10.0, 0.49 * sampleRate) / sampleRate);
    const double q = 0.5 * std::pow(50.0, std::clamp(resonance, 0.0, 1.0)); // 0.5 .. 25
    const double k = 1.0 / q;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = x - s2_;
    const double v1 = a1 * s1_ + a2 * v3;
    const double v2 = s2_ + a2 * s1_ + a3 * v3;
    s1_ = 2.0 * v1 - s1_;
    s2_ = 2.0 * v2 - s2_;
    switch (mode)
    {
    case Mode::Lowpass:
        return static_cast<float>(v2);
    case Mode::Highpass:
        return static_cast<float>(x - k * v1 - v2);
    case Mode::Bandpass:
        return static_cast<float>(v1);
    }
    return static_cast<float>(v2);
}

float LadderFilter::process(float x, double cutoff, double resonance, double sampleRate) noexcept
{
    const double g = std::tan(pi * std::clamp(cutoff, 10.0, 0.49 * sampleRate) / sampleRate);
    const double G = g / (1.0 + g);
    const double k = 4.2 * std::clamp(resonance, 0.0, 1.0); // > 4 self-oscillates; tanh bounds it
    // Zero-delay feedback: y4 = G^4 u + S, with S from the stage states.
    const double S = (G * G * G * s_[0] + G * G * s_[1] + G * s_[2] + s_[3]) / (1.0 + g);
    // Some input gain compensates the passband loss at high resonance.
    const double in = x * (1.0 + 0.5 * k);
    const double u = std::tanh((in - k * S) / (1.0 + k * G * G * G * G));
    double y = u;
    for (double& s : s_)
    {
        const double v = (y - s) * G;
        const double out = v + s;
        s = out + v;
        y = out;
    }
    return static_cast<float>(y);
}

void Adsr::set(const Settings& s) noexcept
{
    s_ = s;
    // Exponential decay/release reach -60 dB at the set time: coef = exp(-ln(1000) / samples).
    auto coef = [&](double seconds) { return std::exp(-6.907755 / std::max(1.0, seconds * sampleRate_)); };
    // Attack aims past 1 so it is slightly convex and ends at 1 after `attack` seconds:
    // level(t) = 1.3 (1 - e^{-t/tau}) reaches 1 at t = tau ln(1.3 / 0.3).
    attackCoef_ = std::exp(-std::log(1.3 / 0.3) / std::max(1.0, s.attack * sampleRate_));
    decayCoef_ = coef(s.decay);
    releaseCoef_ = coef(s.release);
}

float Adsr::next() noexcept
{
    switch (stage_)
    {
    case Stage::Idle:
        level_ = 0.0;
        break;
    case Stage::Attack:
        level_ = 1.3 - (1.3 - level_) * attackCoef_;
        if (level_ >= 1.0)
        {
            level_ = 1.0;
            stage_ = Stage::Decay;
        }
        break;
    case Stage::Decay:
    {
        const double target = s_.cycle ? 0.0 : s_.sustain;
        level_ = target + (level_ - target) * decayCoef_;
        if (s_.cycle && level_ < 1e-3)
            stage_ = Stage::Attack;
        else if (!s_.cycle && std::abs(level_ - target) < 1e-5)
        {
            level_ = target;
            stage_ = Stage::Sustain;
        }
        break;
    }
    case Stage::Sustain:
        level_ = s_.sustain; // follows sustain changes
        break;
    case Stage::Release:
        level_ *= releaseCoef_;
        if (level_ < 1e-6) // -120 dB
        {
            level_ = 0.0;
            stage_ = Stage::Idle;
        }
        break;
    }
    return static_cast<float>(level_);
}

float Lfo::advance(double rateHz, double sampleRate, int samples, Wave wave) noexcept
{
    phase_ += rateHz * samples / sampleRate;
    if (phase_ >= 1.0)
    {
        phase_ -= std::floor(phase_);
        held_ = noise_.next();
    }
    const double p = phase_;
    switch (wave)
    {
    case Wave::Sine:
        return static_cast<float>(std::sin(twoPi * p));
    case Wave::Triangle:
        return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
    case Wave::Saw:
        return static_cast<float>(1.0 - 2.0 * p);
    case Wave::Square:
        return p < 0.5 ? 1.0f : -1.0f;
    case Wave::SampleAndHold:
        return held_;
    }
    return 0.0f;
}

float Drift::advance(int samples, double sampleRate) noexcept
{
    t_ += 0.6 * samples / sampleRate; // ~0.6 new targets per second
    while (t_ >= 1.0)
    {
        t_ -= 1.0;
        from_ = to_;
        to_ = noise_.next();
    }
    const double s = t_ * t_ * (3.0 - 2.0 * t_); // smoothstep
    return static_cast<float>(from_ + (to_ - from_) * s);
}
} // namespace fd::dsp
