#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Smoother.h"

#include <array>

namespace fd::devices
{
/// Gate and downward expander with hysteresis, hold, Flip, lookahead and a sidechain filter.
/// Built on the dynamics core (libs/dsp). See devices/gate/SPEC.md.
class Gate final : public Device
{
public:
    enum Param : size_t
    {
        Threshold,
        Return,
        Mode,
        Ratio,
        Range,
        Attack,
        Hold,
        Release,
        Flip,
        Lookahead,
        ScFilter,
        ScFreq,
        ScQ,
        ScGain,
        ScListen,
        NumParams
    };

    enum class ModeType { Gate, Expander };
    using ScFilterType = dsp::SidechainFilter::Type;

    /// Analysis bus slots and FIFO (devices/gate/SPEC.md, "Analysis").
    enum Analysis : int
    {
        PeakInL = 0, // input and output peaks, linear
        PeakInR = 1,
        PeakOutL = 2,
        PeakOutR = 3,
        DetectorLevel = 4, // the level the gate reads, linear
        GainReduction = 5, // how far the gain is turned down, in dB as a positive number (peak-held)
        Open = 6,          // 1 if the gate was open at any time since last taken
    };
    /// FIFO 0: frames of (detector level dB, output level dB, open 0 or 1) every historyStepMs.
    static constexpr int historyFifo = 0;
    static constexpr double historyStepMs = 10.0;

    static constexpr int maxChannels = 2;
    /// The detector reads the peak over this window, so low notes read steadily. Holds shorter
    /// than the window act as the window.
    static constexpr double detectorWindowMs = 10.0;

    Gate();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    /// Follows the Lookahead setting (ADR 0020): 0, 1 or 10 ms.
    [[nodiscard]] int latencySamples() const noexcept override;

    /// The Range in dB as the audio path uses it: the bottom of its travel is silence.
    [[nodiscard]] static float rangeDb(float rangeParam) noexcept;
    /// The expander's static gain (dB, <= 0) at a level, from the same function the audio uses.
    [[nodiscard]] static float expanderGain(float levelDb, float thresholdDb, float ratio, float rangeParam) noexcept;
    [[nodiscard]] static double lookaheadMs(int setting) noexcept;

private:
    void updateTargets(bool immediate) noexcept;
    void updateTimes(bool immediate) noexcept;
    void updateSidechainFilter(bool immediate) noexcept;
    void applyLookahead() noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, switchSamples_ = 480, windowSamples_ = 480;
    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother threshold_, return_, ratio_, range_, expander_, flip_, listen_;
    dsp::MultiplicativeSmoother attackMs_, releaseMs_; // advanced once per control tick
    dsp::LinearSmoother holdMs_;                       // likewise
    dsp::SidechainFilter scFilter_;
    int appliedLookahead_ = 0;

    // One detector and one gain for all channels (stereo-linked).
    dsp::SlidingMinimum window_; // of -|x|: the peak over the window
    dsp::GainSmoother gain_;
    bool open_ = false;
    int holdArm_ = 0;               // the hold, less the detector's window, in samples
    int holdLeft_ = 0;              // gate: samples until it may close
    float expanderHeld_ = 0.0f;     // expander: the held target
    int expanderHoldLeft_ = 0;
    std::array<dsp::DelayLine, maxChannels> audio_{}; // the lookahead

    // History for the display.
    int historyStep_ = 480, historyPos_ = 0;
    float historyLevel_ = 0.0f, historyOut_ = 0.0f, historyOpen_ = 0.0f;
};

const DeviceFactory& gateFactory();
} // namespace fd::devices
