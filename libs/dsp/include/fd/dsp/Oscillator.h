#pragma once

#include <elliptic-blep.h>

namespace fd::dsp
{
/// Band-limited oscillator built from polynomial segments: saw, variable-width square, triangle
/// and sine from one phase, with every discontinuity (steps; slope changes for the triangle)
/// cancelled by Signalsmith's elliptic BLEP (11th-order, 20 kHz). Mixtures of the waveforms stay
/// band-limited, which is how the shape morph works.
class Oscillator
{
public:
    /// Not real-time: builds the BLEP tables for this sample rate.
    void prepare(double sampleRate);
    void reset(double phase = 0.0) noexcept;

    /// Mix weights of the four waveforms (sum 1 for unity level). `width` is the square's duty cycle.
    struct Mix
    {
        float saw = 1.0f, square = 0.0f, triangle = 0.0f, sine = 0.0f;
        float width = 0.5f;
    };

    /// Morph: 0 saw -> 1/3 square -> 2/3 triangle -> 1 sine.
    [[nodiscard]] static Mix morph(float shape, float width) noexcept;

    /// Next sample. `freq` is in cycles per sample (< 0.5).
    float process(double freq, const Mix& mix) noexcept;

    [[nodiscard]] double phase() const noexcept { return phase_; }

private:
    signalsmith::blep::EllipticBlep<float> blep_;
    double phase_ = 0.0;
};
} // namespace fd::dsp
