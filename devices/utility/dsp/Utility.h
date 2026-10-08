#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Smoother.h"

#include <array>

namespace fd::devices
{
/// Gain, balance, width, mono, polarity, DC filter, mute. See devices/utility/SPEC.md.
class Utility final : public Device
{
public:
    enum Param : size_t
    {
        Gain,
        Balance,
        Width,
        Mono,
        InvertL,
        InvertR,
        DcFilter,
        Mute,
        NumParams
    };

    enum Analysis : int
    {
        PeakOutL = 0,
        PeakOutR = 1
    };

    static const DeviceInfo& staticInfo() noexcept;

    Utility();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    void updateTargets(bool immediate) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960;
    int fadeSamples_ = 480;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother gain_, balance_, width_, polarityL_, polarityR_, dcMix_;
    std::array<dsp::DcBlocker, 2> dcBlockers_{};
};

const DeviceFactory& utilityFactory();
} // namespace fd::devices
