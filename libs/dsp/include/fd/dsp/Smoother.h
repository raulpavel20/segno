#pragma once

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

    /// Ramp from the current value to `target` over `rampSamples` (<= 0 jumps immediately).
    void setTarget(float target, int rampSamples) noexcept
    {
        if (target == target_)
            return;
        target_ = target;
        if (rampSamples <= 0)
        {
            current_ = target;
            stepsLeft_ = 0;
            return;
        }
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

    void setTarget(float target, int rampSamples) noexcept
    {
        if (target == target_)
            return;
        target_ = target;
        if (rampSamples <= 0 || current_ <= 0.0f || target <= 0.0f)
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
} // namespace fd::dsp
