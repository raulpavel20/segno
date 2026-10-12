#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Modulation.h"
#include "fd/dsp/Smoother.h"

#include <array>

namespace fd::devices
{
/// Phaser, Flanger and Doubler in one module (devices/phaser-flanger/SPEC.md), on the modulation
/// core and the dynamics core's level detector.
class PhaserFlanger final : public Device
{
public:
    enum Param : size_t
    {
        Mode,
        PhaserCenter,
        PhaserNotches,
        PhaserSpread,
        FlangerTime,
        FlangerZero,
        DoublerTime,
        Feedback,
        Invert,
        Warmth,
        Rate,
        Sync,
        Div,
        Shape,
        Amount,
        Stereo,
        Offset,
        Spin,
        Env,
        EnvAmount,
        EnvAttack,
        EnvRelease,
        LowCut,
        LowCutFreq,
        Mix,
        Output,
        NumParams
    };

    enum Analysis : int
    {
        PeakInL = 0,
        PeakInR = 1,
        PeakOutL = 2,
        PeakOutR = 3,
        LfoPosition = 4, // where the LFO is in its cycle (0..1), for the Doubler's display
        LfoLeft = 5,     // each side's LFO value now (-1..1), for the response display
        LfoRight = 6,
        Envelope = 7, // the envelope now (0..1; 0 while it's off)
    };

    enum class ModeKind
    {
        Phaser,
        Flanger,
        Doubler
    };
    enum class StereoKind
    {
        Offset,
        Spin
    };

    static constexpr int maxChannels = 2;
    static constexpr int maxNotches = 12;
    static constexpr int numDivisions = dsp::numLfoNoteValues;
    static constexpr double maxCycleSeconds = 100.0; // a synced cycle longer than this is held here
    static constexpr double zeroMs = 1.0;            // Through Zero holds the dry sound back this much

    PhaserFlanger();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    // ---- pure functions: the audio path and the panel's display use the same ones --------------

    /// What sets where the sweep goes.
    struct Sweep
    {
        ModeKind mode = ModeKind::Phaser;
        double centerHz = 800.0; // Phaser
        double flangerMs = 2.0;  // Flanger: the longest delay
        double doublerMs = 20.0; // Doubler
        double amount = 0.5;     // 0..1
        double envAmount = 0.5;  // -1..1
    };
    /// Where the sweep is with the LFO at `lfo` (-1..1) and the envelope at `env` (0..1): the
    /// notches' frequency (Hz) for the Phaser, or the delay behind the input (ms) for the Flanger
    /// and the Doubler (SPEC: Ranges and limits).
    [[nodiscard]] static double sweep(const Sweep& s, double lfo, double env) noexcept;

    /// The all-passes' Q for a Spread (0..1): 4 bunched close, 0.3 spaced wide.
    [[nodiscard]] static double allpassQ(double spread) noexcept;

    /// What sets the effect's response.
    struct Response
    {
        ModeKind mode = ModeKind::Phaser;
        int notches = 2;
        double spread = 0.5;   // 0..1
        double feedback = 0.3; // 0..0.95
        bool invert = false;
        double mix = 0.5; // 0..1
        bool zero = false;
    };
    /// The level (linear) of the effect at `hz` with the sweep at `at` (Hz, or ms behind the
    /// input), Warmth and the Low Cut aside: what the display draws.
    [[nodiscard]] static double response(const Response& r, double at, double hz, double sampleRate) noexcept;

    /// Dry and wet gains for a Dry/Wet (0..1): equal power, exact at the ends.
    struct Gains
    {
        float dry = 1.0f, wet = 0.0f;
    };
    [[nodiscard]] static Gains gains(float mix) noexcept;

private:
    struct Channel
    {
        dsp::Crossover split;                        // the Low Cut
        std::array<dsp::Biquad, maxNotches> allpass; // the Phaser's chain
        dsp::ModulatedDelay line;                    // the Flanger's and the Doubler's
        dsp::FeedbackPath tonePhaser, toneLine;      // Warmth, in each loop
        dsp::DelayLine dry, lows, phaserIn;          // Through Zero's 1 ms
        float backPhaser = 0.0f, backLine = 0.0f;    // what each loop fed back, last sample
        float lfo = 0.0f;                            // the LFO's value, smoothed
    };

    void snap() noexcept;
    void updateTargets(bool immediate) noexcept;
    void updateSync(bool snap) noexcept;
    void applyZero() noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, modeSamples_ = 2400, notchSamples_ = 1440;
    float lfoCoef_ = 0.0f;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother spread_, feedback_, invert_, warmth_, amount_, spin_, envOn_, envAmount_, lowOn_,
        mix_, output_, modeFade_, notchFade_;
    dsp::MultiplicativeSmoother center_, rate_, lowFreq_;
    dsp::Glide flangerTime_, doublerTime_, offset_, rightJump_;
    dsp::SongLfo lfo_;
    dsp::LevelDetector detector_;
    double rightPosition_ = 0.0; // the right side's LFO position (cycles)
    float envDb_ = -48.0f, envRise_ = 0.0f, envFall_ = 0.0f;
    ModeKind mode_ = ModeKind::Phaser, previous_ = ModeKind::Phaser;
    StereoKind stereo_ = StereoKind::Offset;
    int notches_ = 2, previousNotches_ = 2;
    bool zero_ = false;
    double tempo_ = 120.0, beatsPerBar_ = 4.0;
    bool fresh_ = true;
    std::array<Channel, maxChannels> ch_{};
};

const DeviceFactory& phaserFlangerFactory();
} // namespace fd::devices
