#pragma once

#include <cmath>
#include <cstdint>

namespace fd::dsp
{
/// Topology-preserving-transform state-variable filter (12 dB/oct): LP, HP, BP outputs.
/// Cutoff and resonance can change every sample (zero-delay feedback, no zipper).
class StateVariableFilter
{
public:
    enum class Mode
    {
        Lowpass,
        Highpass,
        Bandpass
    };

    void reset() noexcept { s1_ = s2_ = 0.0; }

    /// `cutoff` in Hz, `resonance` 0..1 (Q from 0.5 to ~25).
    float process(float x, double cutoff, double resonance, double sampleRate, Mode mode) noexcept;

    /// Exact magnitude response of process() at `freq` Hz (for displays).
    [[nodiscard]] static double magnitude(double freq, double cutoff, double resonance, double sampleRate,
                                          Mode mode) noexcept;

private:
    double s1_ = 0.0, s2_ = 0.0;
};

/// Zero-delay-feedback 4-pole ladder low-pass (24 dB/oct) with a saturating feedback loop, so
/// it self-oscillates at full resonance with a bounded level.
class LadderFilter
{
public:
    void reset() noexcept { s_[0] = s_[1] = s_[2] = s_[3] = 0.0; }

    /// `cutoff` in Hz, `resonance` 0..1 (1 = self-oscillation).
    float process(float x, double cutoff, double resonance, double sampleRate) noexcept;

    /// Small-signal (linearised) magnitude response of process() at `freq` Hz (for displays).
    [[nodiscard]] static double magnitude(double freq, double cutoff, double resonance, double sampleRate) noexcept;

private:
    double s_[4] = {0, 0, 0, 0};
};

/// ADSR with exponential decay and release (times are to -60 dB) and a slightly convex attack.
/// Retriggers from the current level (no clicks on voice steal or legato).
class Adsr
{
public:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct Settings
    {
        double attack = 0.002, decay = 0.3, sustain = 0.8, release = 0.2; // seconds / level
        bool cycle = false; // loop attack -> decay (LFO-like), ignores sustain
    };

    void setSampleRate(double sr) noexcept { sampleRate_ = sr; }
    void set(const Settings& s) noexcept;
    void noteOn() noexcept { stage_ = Stage::Attack; }
    void noteOff() noexcept
    {
        if (stage_ != Stage::Idle)
            stage_ = Stage::Release;
    }
    void reset() noexcept
    {
        stage_ = Stage::Idle;
        level_ = 0.0;
    }

    float next() noexcept;

    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] bool isActive() const noexcept { return stage_ != Stage::Idle; }
    [[nodiscard]] double level() const noexcept { return level_; }

private:
    double sampleRate_ = 48000.0;
    Settings s_{};
    double attackCoef_ = 0.0, decayCoef_ = 0.0, releaseCoef_ = 0.0;
    Stage stage_ = Stage::Idle;
    double level_ = 0.0;
};

/// Deterministic white noise (xorshift), -1..1.
class Noise
{
public:
    Noise() noexcept = default;
    explicit Noise(std::uint32_t seed) noexcept : state_(seed ? seed : 1u) {}
    void seed(std::uint32_t s) noexcept { state_ = s ? s : 1u; }
    float next() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

private:
    std::uint32_t state_ = 1u;
};

/// Low-frequency oscillator: sine, triangle, saw, square, sample-and-hold. Output -1..1.
class Lfo
{
public:
    enum class Wave
    {
        Sine,
        Triangle,
        Saw,
        Square,
        SampleAndHold
    };

    void reset(double phase = 0.0) noexcept
    {
        phase_ = phase;
        noise_.seed(0x51ED27u); // deterministic sample-and-hold sequence after every reset
        held_ = noise_.next();
    }
    /// Advance by `samples` at `rateHz`; returns the value at the new position.
    float advance(double rateHz, double sampleRate, int samples, Wave wave) noexcept;
    [[nodiscard]] double phase() const noexcept { return phase_; }

private:
    double phase_ = 0.0;
    float held_ = 0.0f;
    Noise noise_{0x51ED27u};
};

/// Slow, smooth random wander (-1..1) for analog-style drift: interpolated noise at ~0.3–1 Hz.
class Drift
{
public:
    void seed(std::uint32_t s) noexcept
    {
        noise_.seed(s);
        from_ = noise_.next();
        to_ = noise_.next();
        t_ = 0.0;
    }
    float advance(int samples, double sampleRate) noexcept;

private:
    Noise noise_{7};
    float from_ = 0.0f, to_ = 0.0f;
    double t_ = 0.0;
};
} // namespace fd::dsp
