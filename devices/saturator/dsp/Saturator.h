#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Oversampler.h"
#include "fd/dsp/Smoother.h"
#include "fd/dsp/Waveshapers.h"

#include <array>
#include <vector>

namespace fd::devices
{
/// Saturation with six anti-aliased curves, Color emphasis and dry/wet. See devices/saturator/SPEC.md.
class Saturator final : public Device
{
public:
    enum Param : size_t
    {
        Drive,
        Curve,
        Bias,
        Color,
        ColorFreq,
        ColorGain,
        Output,
        Mix,
        Quality,
        NumParams
    };

    enum Analysis : int
    {
        PeakInL = 0,
        PeakInR = 1,
        PeakOutL = 2,
        PeakOutR = 3
    };

    static constexpr int maxChannels = 2;

    Saturator();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return oversampler_.latencySamples(); }

    /// The static transfer curve shown in the UI: output for input `x` at the given settings.
    [[nodiscard]] static float transfer(dsp::ShaperCurve curve, float driveDb, float bias, float x) noexcept;

    [[nodiscard]] static int qualityFactor(int qualityIndex) noexcept { return 2 << qualityIndex; }

private:
    void updateTargets(bool immediate) noexcept;
    void updateColorCoeffs() noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960;
    int fadeSamples_ = 480;
    int factor_ = 4;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother drive_, bias_, colorMix_, colorGain_, output_, mix_;
    dsp::MultiplicativeSmoother colorFreq_;
    dsp::Oversampler oversampler_;

    struct Channel
    {
        dsp::AdaaShaper shaper, nextShaper;
        dsp::Biquad colorPre, colorPost;
        dsp::DcBlocker dc;
        std::vector<float> dryDelay;
        int dryPos = 0;
    };
    std::array<Channel, maxChannels> ch_{};

    dsp::ShaperCurve curve_ = dsp::ShaperCurve::Soft;
    int curveFadeLeft_ = 0; // samples left in a curve crossfade, at the oversampled rate
    int curveFadeLength_ = 1;

    std::array<std::vector<float>, maxChannels> work_; // one control segment, base rate
    std::array<std::vector<float>, maxChannels> dry_;
};

const DeviceFactory& saturatorFactory();
} // namespace fd::devices
