#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Smoother.h"

#include <array>

namespace fd::devices
{
/// Downward compressor with a Clean and a Glue character, lookahead, a sidechain filter and
/// parallel mix. Built on the dynamics core (libs/dsp). See devices/compressor/SPEC.md.
class Compressor final : public Device
{
public:
    enum Param : size_t
    {
        Threshold,
        Ratio,
        Knee,
        Attack,
        Release,
        ReleaseAuto,
        Makeup,
        MakeupAuto,
        Character,
        Detector,
        Link,
        Lookahead,
        ScFilter,
        ScFreq,
        ScQ,
        ScGain,
        ScListen,
        Mix,
        Output,
        NumParams
    };

    enum class CharacterType { Clean, Glue };
    enum class ScFilterType { Off, HighPass, LowPass, BandPass, Bell };

    /// Analysis bus slots and FIFO (devices/compressor/SPEC.md, "Analysis").
    enum Analysis : int
    {
        PeakInL = 0, // input and output peaks, linear
        PeakInR = 1,
        PeakOutL = 2,
        PeakOutR = 3,
        GainReduction = 4, // reduction in dB, as a positive number (peak-held)
        DetectorLevel = 5, // the level the gain computer sees, linear (the curve's moving dot)
    };
    /// FIFO 0: frames of (input dB, output dB, gain reduction dB) every historyStepMs.
    static constexpr int historyFifo = 0;
    static constexpr double historyStepMs = 10.0;

    static constexpr int maxChannels = 2;
    /// The ratio's top value stands for infinity (a limiter curve).
    static constexpr float infiniteRatio = 100.0f;

    Compressor();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    /// Follows the Lookahead setting (ADR 0020): 0, 1 or 10 ms.
    [[nodiscard]] int latencySamples() const noexcept override;

    /// The static curve shown in the UI, from the same function the audio path uses.
    [[nodiscard]] static float curve(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;
    /// The automatic makeup for a curve: what brings a -18 dBFS input back to -18 dBFS.
    [[nodiscard]] static float autoMakeup(float thresholdDb, float ratio, float kneeDb) noexcept;
    [[nodiscard]] static double lookaheadMs(int setting) noexcept;

private:
    void updateTargets(bool immediate) noexcept;
    void updateTimes(bool immediate) noexcept;
    void updateModes(bool immediate) noexcept;
    void updateSidechainFilter(bool immediate) noexcept;
    void applyLookahead() noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960;  // parameter smoothing (20 ms)
    int switchSamples_ = 480; // crossfades when a choice changes (10 ms)
    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother threshold_, slope_, knee_, makeup_, output_, mix_, listen_;
    dsp::LinearSmoother autoMakeup_; // 0..1: from the Makeup setting to the automatic makeup
    dsp::MultiplicativeSmoother attackMs_, releaseMs_; // advanced once per control tick
    dsp::MultiplicativeSmoother scFreq_, scQ_;         // likewise
    dsp::LinearSmoother scGainDb_;                     // likewise
    dsp::LinearSmoother glue_, rms_, scFade_;          // 0..1 crossfades, per sample
    int appliedLookahead_ = 0;
    bool autoRelease_ = false;

    struct Channel
    {
        dsp::LevelDetector detector;
        dsp::GainSmoother gain;
        dsp::AutoReleaseSmoother autoGain;
        dsp::Biquad scFilter;
        dsp::Biquad scOutgoing; // the previous filter type, while the new one fades in
        dsp::DelayLine audio; // the lookahead: the wet and dry paths are both delayed
        float feedback = 0.0f; // Glue: the last gain-reduced sample, for the detector
    };
    std::array<Channel, maxChannels> ch_{};
    ScFilterType scType_ = ScFilterType::Off, scOutgoingType_ = ScFilterType::Off;

    // History frames for the display.
    int historyStep_ = 480, historyPos_ = 0;
    float historyIn_ = 0.0f, historyOut_ = 0.0f, historyReduction_ = 0.0f;
};

const DeviceFactory& compressorFactory();
} // namespace fd::devices
