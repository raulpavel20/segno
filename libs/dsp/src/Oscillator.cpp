#include "fd/dsp/Oscillator.h"

#include "fd/dsp/Math.h"

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
void Oscillator::prepare(double sampleRate)
{
    blep_ = signalsmith::blep::EllipticBlep<float>(static_cast<float>(sampleRate));
    reset();
}

void Oscillator::reset(double phase) noexcept
{
    blep_.reset();
    phase_ = phase - std::floor(phase);
}

Oscillator::Mix Oscillator::morph(float shape, float width) noexcept
{
    const float s = std::clamp(shape, 0.0f, 1.0f) * 3.0f;
    Mix m{0.0f, 0.0f, 0.0f, 0.0f, width};
    if (s < 1.0f)
    {
        m.saw = 1.0f - s;
        m.square = s;
    }
    else if (s < 2.0f)
    {
        m.square = 2.0f - s;
        m.triangle = s - 1.0f;
    }
    else
    {
        m.triangle = 3.0f - s;
        m.sine = s - 2.0f;
    }
    return m;
}

float Oscillator::process(double freq, const Mix& m) noexcept
{
    freq = std::clamp(freq, 0.0, 0.49);
    const double w = std::clamp(static_cast<double>(m.width), 0.02, 0.98);
    const double start = phase_;
    double p = start + freq;
    blep_.step();

    // Discontinuities crossed during this sample, at fractional positions (samples in the past).
    auto past = [&](double at) { return static_cast<float>((p - at) / freq); };

    // Square falling edge at the pulse width; triangle peak at 0.5 (slope -8 f per sample).
    if (start < w && p >= w && m.square != 0.0f)
        blep_.add(-2.0f * m.square, 1, past(w));
    if (start < 0.5 && p >= 0.5 && m.triangle != 0.0f)
        blep_.add(static_cast<float>(-8.0 * freq) * m.triangle, 2, past(0.5));
    if (p >= 1.0)
    {
        // Wrap: saw drops by 2, square rises by 2, triangle slope turns from -4f to +4f.
        const float pastWrap = past(1.0);
        p -= 1.0;
        if (m.saw != 0.0f)
            blep_.add(-2.0f * m.saw, 1, pastWrap);
        if (m.square != 0.0f)
            blep_.add(2.0f * m.square, 1, pastWrap);
        if (m.triangle != 0.0f)
            blep_.add(static_cast<float>(8.0 * freq) * m.triangle, 2, pastWrap);
        // A very short pulse can also cross the width after wrapping in the same sample.
        if (p >= w && m.square != 0.0f)
            blep_.add(-2.0f * m.square, 1, static_cast<float>((p - w) / freq));
    }
    phase_ = p;

    double y = 0.0;
    if (m.saw != 0.0f)
        y += m.saw * (2.0 * p - 1.0);
    if (m.square != 0.0f)
        y += m.square * (p < w ? 1.0 : -1.0);
    if (m.triangle != 0.0f)
        y += m.triangle * (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
    if (m.sine != 0.0f)
        y += m.sine * std::sin(twoPi * p);
    return static_cast<float>(y) + blep_.get();
}
} // namespace fd::dsp
