#pragma once

#include <algorithm>
#include <cmath>

namespace fd::dsp
{
/// Linear ramp to a target over a fixed number of samples.
///
/// The state advances one step per call to next(), so given the same sequence of
/// setTarget() calls at the same absolute sample positions, the output is identical
/// whatever the host block size (see ControlGrid).
class LinearSmoother
{
public:
    void reset(float value) noexcept
    {
        current_ = target_ = value;
        stepsLeft_ = 0;
    }

    /// Ramp from the current value to `target` over `rampSamples` (<= 0 jumps immediately,
    /// even in the middle of a ramp to the same target).
    void setTarget(float target, int rampSamples) noexcept
    {
        if (rampSamples <= 0)
        {
            reset(target);
            return;
        }
        if (target == target_)
            return;
        target_ = target;
        stepsLeft_ = rampSamples;
        step_ = (target_ - current_) / static_cast<float>(rampSamples);
    }

    float next() noexcept
    {
        if (stepsLeft_ > 0)
        {
            current_ += step_;
            if (--stepsLeft_ == 0)
                current_ = target_;
        }
        return current_;
    }

    [[nodiscard]] float current() const noexcept { return current_; }
    [[nodiscard]] float target() const noexcept { return target_; }
    [[nodiscard]] bool isSmoothing() const noexcept { return stepsLeft_ > 0; }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int stepsLeft_ = 0;
};

/// Exponential (constant-ratio) ramp, for strictly positive values such as frequencies.
class MultiplicativeSmoother
{
public:
    void reset(float value) noexcept
    {
        current_ = target_ = value;
        stepsLeft_ = 0;
    }

    /// Like LinearSmoother::setTarget, at a constant ratio.
    void setTarget(float target, int rampSamples) noexcept
    {
        if (rampSamples <= 0)
        {
            reset(target);
            return;
        }
        if (target == target_)
            return;
        target_ = target;
        if (current_ <= 0.0f || target <= 0.0f)
        {
            current_ = target;
            stepsLeft_ = 0;
            return;
        }
        stepsLeft_ = rampSamples;
        ratio_ = static_cast<float>(
            std::exp((std::log(static_cast<double>(target_)) - std::log(static_cast<double>(current_))) /
                     rampSamples));
    }

    float next() noexcept
    {
        if (stepsLeft_ > 0)
        {
            current_ *= ratio_;
            if (--stepsLeft_ == 0)
                current_ = target_;
        }
        return current_;
    }

    [[nodiscard]] float current() const noexcept { return current_; }
    [[nodiscard]] float target() const noexcept { return target_; }
    [[nodiscard]] bool isSmoothing() const noexcept { return stepsLeft_ > 0; }

private:
    float current_ = 1.0f;
    float target_ = 1.0f;
    float ratio_ = 1.0f;
    int stepsLeft_ = 0;
};

/// Two one-poles in a row: a value that glides to its target with no step in its speed, so a
/// jump in what moves a delay repitches it smoothly instead of clicking (the Chorus's Delay, an
/// LFO's jump in phase). Each pole's time constant is set; it settles in about five of them.
class Glide
{
public:
    void setTime(double ms, double sampleRate) noexcept
    {
        coef_ = 1.0 - std::exp(-1.0 / (std::max(ms, 1e-3) * 0.001 * sampleRate));
    }
    /// At `value` at once, at rest.
    void snap(double value) noexcept { a_ = b_ = target_ = value; }
    void setTarget(double value) noexcept { target_ = value; }

    double next() noexcept
    {
        a_ += coef_ * (target_ - a_);
        b_ += coef_ * (a_ - b_);
        return b_;
    }

    [[nodiscard]] double current() const noexcept { return b_; }
    [[nodiscard]] double target() const noexcept { return target_; }

private:
    double coef_ = 1.0, a_ = 0.0, b_ = 0.0, target_ = 0.0;
};
} // namespace fd::dsp
