#pragma once

#include "fd/device/ParamStore.h"
#include "fd/dsp/Math.h"
#include "fd/dsp/Smoother.h"

namespace fd
{
/// A parameter read from the ParamStore at control ticks (fd::dsp::ControlGrid) and smoothed
/// per sample according to its ParamSpec. For derived values (e.g. linear gain from dB),
/// use fd::dsp::LinearSmoother directly with computed targets.
class SmoothedParam
{
public:
    void prepare(const ParamStore& store, size_t index, double sampleRate) noexcept
    {
        index_ = index;
        const auto& s = store.spec(index).smoothing;
        kind_ = s.kind;
        rampSamples_ = s.kind == SmoothingKind::None ? 0 : dsp::msToSamples(s.ms, sampleRate);
        reset(store);
    }

    void reset(const ParamStore& store) noexcept
    {
        const float v = store.get(index_);
        linear_.reset(v);
        multiplicative_.reset(v);
    }

    /// Call on control ticks.
    void tick(const ParamStore& store) noexcept
    {
        const float v = store.get(index_);
        if (kind_ == SmoothingKind::Multiplicative)
            multiplicative_.setTarget(v, rampSamples_);
        else
            linear_.setTarget(v, rampSamples_);
    }

    float next() noexcept { return kind_ == SmoothingKind::Multiplicative ? multiplicative_.next() : linear_.next(); }

    [[nodiscard]] float current() const noexcept
    {
        return kind_ == SmoothingKind::Multiplicative ? multiplicative_.current() : linear_.current();
    }

private:
    size_t index_ = 0;
    SmoothingKind kind_ = SmoothingKind::Linear;
    int rampSamples_ = 0;
    dsp::LinearSmoother linear_;
    dsp::MultiplicativeSmoother multiplicative_;
};
} // namespace fd
