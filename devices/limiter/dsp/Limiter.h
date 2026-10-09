#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Loudness.h"
#include "fd/dsp/Smoother.h"

#include <array>

namespace fd::devices
{
/// Lookahead peak limiter with true-peak detection, Standard and Maximise modes, stereo, L/R
/// and M/S linking, and a loudness readout. Built on the dynamics core (libs/dsp). See
/// devices/limiter/SPEC.md.
class Limiter final : public Device
{
public:
    enum Param : size_t
    {
        Gain,
        Ceiling,
        Release,
        ReleaseAuto,
        Mode,
        TruePeak,
        Link,
        Lookahead,
        NumParams
    };

    enum class ModeType { Standard, Maximise };
    enum class LinkType { Stereo, LeftRight, MidSide };

    /// Analysis bus slots and FIFO (devices/limiter/SPEC.md, "Analysis").
    enum Analysis : int
    {
        PeakOutL = 0,      // the output's true peak, linear (peak-held)
        PeakOutR = 1,
        GainReduction = 2, // dB as a positive number (peak-held)
        ShortTermLufs = 3, // the output's loudness (-inf before there is enough)
        IntegratedLufs = 4,
        TruePeakMax = 5,   // the loudest true peak since the readout was reset, dBTP
    };
    /// Display commands (AnalysisBus::request).
    enum Request : int { ResetLoudness = 0 };
    /// FIFO 0: frames of (input dB after Gain, output dB, gain reduction dB) every historyStepMs.
    static constexpr int historyFifo = 0;
    static constexpr double historyStepMs = 10.0;

    static constexpr int maxChannels = 2;
    /// After a peak, the gain holds this long before it releases, so low notes aren't distorted.
    static constexpr double holdMs = 10.0;

    Limiter();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    /// The lookahead plus, with True Peak on, the interpolator (ADR 0020).
    [[nodiscard]] int latencySamples() const noexcept override;

    [[nodiscard]] static double lookaheadMs(int setting) noexcept;

private:
    /// One gain computer: the gain each sample needs, held over the lookahead and the hold,
    /// ramped over the lookahead (it arrives exactly with the peak), then released.
    struct GainPath
    {
        dsp::SlidingMinimum hold;
        dsp::MovingAverage ramp;
        dsp::GainSmoother release;
        dsp::AutoReleaseSmoother autoRelease;
    };
    enum Path { Linked, Left, Right, Mid, Side, NumPaths };

    void updateTargets(bool immediate) noexcept;
    void updateTimes(bool immediate) noexcept;
    void updateModes() noexcept;
    void applyLatency() noexcept;
    float gainFor(GainPath& path, float needed) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, switchSamples_ = 480, holdSamples_ = 480;
    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother gainDb_, ceilingDb_, maximise_, linked_, leftRight_, midSide_;
    dsp::MultiplicativeSmoother releaseMs_; // advanced once per control tick
    bool autoRelease_ = true;
    int appliedLookahead_ = -1;
    bool appliedTruePeak_ = false;
    int lookaheadSamples_ = 144;

    std::array<GainPath, NumPaths> paths_{};
    dsp::TruePeakDetector detector_;               // L, R, M, S of the driven input
    std::array<dsp::DelayLine, 4> detectorAlign_{}; // the plain samples, as late as the detector's
    std::array<dsp::DelayLine, maxChannels> audio_{};
    dsp::DelayLine makeupDelay_, ceilingDelay_; // in dB, as late as the audio they apply to
    dsp::TruePeakDetector outputPeaks_; // the output, for the meters and the readout
    dsp::LoudnessMeter loudness_;
    float truePeakMax_ = 0.0f;
    int loudnessUntilPublish_ = 0;

    // History for the display.
    int historyStep_ = 480, historyPos_ = 0;
    float historyIn_ = 0.0f, historyOut_ = 0.0f, historyReduction_ = 0.0f;
};

const DeviceFactory& limiterFactory();
} // namespace fd::devices
