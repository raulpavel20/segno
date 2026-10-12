#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Modulation.h"
#include "fd/dsp/Smoother.h"

#include <array>
#include <vector>

namespace fd::devices
{
/// An algorithmic reverb in four modes (devices/reverb/SPEC.md): a feedback delay network of 8
/// lines on the modulation core, its decay shaped in three bands.
class Reverb final : public Device
{
public:
    enum Param : size_t
    {
        Mode,
        Decay,
        Size,
        Predelay,
        Diffusion,
        Early,
        Motion,
        Width,
        Freeze,
        LowMult,
        LowFreq,
        HighMult,
        HighFreq,
        Filter,
        FilterLow,
        FilterHigh,
        Duck,
        DuckRelease,
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
        DuckDb = 4, // how far the reverb is ducked (dB, peak-held)
    };

    enum class ModeKind
    {
        Room,
        Hall,
        Plate,
        Space
    };

    static constexpr int maxChannels = 2;
    static constexpr int lines = 8;          // the network
    static constexpr int earlyTaps = 12;     // a side
    static constexpr int inputDiffusers = 4; // a side
    static constexpr double longestDecay = 60.0, shortestDecay = 0.1;

    Reverb();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    // ---- pure functions: the audio path and the panel's display use the same ones --------------

    /// How big the space is at a Size (0..1), in metres; 0 for Plate (a plate has no metres).
    [[nodiscard]] static double metres(ModeKind mode, double size) noexcept;
    /// The network's mean line length at a Size (seconds): half the space's size at the speed of
    /// sound, or a plate's own.
    [[nodiscard]] static double meanLine(ModeKind mode, double size) noexcept;
    /// Line `i`'s length as a share of the mean (an octave's spread, no two alike).
    [[nodiscard]] static double lineRatio(int i) noexcept;
    /// The delay each line's own all-pass adds (seconds).
    [[nodiscard]] static double loopAllpass(int i) noexcept;

    /// The decay in three bands.
    struct Bands
    {
        double decay = 2.5;   // seconds, the middle
        double lowMult = 1.2; // shares of Decay
        double lowHz = 250.0;
        double highMult = 0.6;
        double highHz = 4000.0;
    };
    /// Each band's decay time (seconds), held between shortestDecay and longestDecay.
    [[nodiscard]] static double bandDecay(double decay, double mult) noexcept;
    /// The gains a line `loopSeconds` long gives its low, middle and high bands so each falls
    /// 60 dB in its own time.
    struct BandGains
    {
        double low = 1.0, mid = 1.0, high = 1.0;
    };
    [[nodiscard]] static BandGains bandGains(const Bands& b, double loopSeconds) noexcept;
    /// How long the reverb rings at `hz` (seconds to fall 60 dB): the band filter a line
    /// `loopSeconds` long decays through, at that frequency. What the display draws.
    [[nodiscard]] static double decayAt(const Bands& b, double hz, double loopSeconds,
                                        double sampleRate) noexcept;

    /// Early reflection `i` on `side` (0 left, 1 right) for a mode: its time as a share of the
    /// mode's early span, and its gain (signed). Tap 0 is at the predelay itself.
    struct Tap
    {
        double at = 0.0, gain = 0.0;
    };
    [[nodiscard]] static Tap earlyTap(ModeKind mode, int side, int i) noexcept;
    /// How long the early reflections last for a mode at a Size (seconds); 0 for Plate.
    [[nodiscard]] static double earlySpan(ModeKind mode, double size) noexcept;

private:
    /// A delay line read at a fraction of a sample (linear), for the predelay, the early taps and
    /// the all-passes: cheap where the modulation core's sinc isn't needed.
    struct TapLine
    {
        std::vector<float> buffer;
        int mask = 0, write = 0;
        void prepare(int samples);
        void reset() noexcept;
        void push(float x) noexcept
        {
            write = (write + 1) & mask;
            buffer[static_cast<size_t>(write)] = x;
        }
        /// `delay` samples before the last one pushed (0: that one).
        [[nodiscard]] float read(double delay) const noexcept;
        [[nodiscard]] float at(int delay) const noexcept
        {
            return buffer[static_cast<size_t>((write - delay) & mask)];
        }
    };
    /// A Schroeder all-pass of a fixed length.
    struct Allpass
    {
        TapLine line;
        int length = 1;
        float process(float x, float g) noexcept
        {
            const float delayed = line.at(length - 1);
            const float w = x + g * delayed;
            line.push(w);
            return delayed - g * w;
        }
    };
    struct Line
    {
        dsp::ModulatedDelay delay;
        Allpass allpass;
        dsp::OnePoleLowpass lowSplit, highSplit; // the band filter's crossovers
        BandGains gains;
        dsp::ModulationLfo lfo;
        double length = 0.0; // samples, gliding with Size
    };
    struct Side
    {
        TapLine input; // the predelay and the early taps
        std::array<Allpass, inputDiffusers> diffusers;
        dsp::FeedbackPath filter;
    };

    void snap() noexcept;
    void updateTargets(bool immediate) noexcept;
    void updateBands() noexcept;
    void clearNetwork() noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, slowSamples_ = 2400, dipSamples_ = 960;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother diffusion_, early_, motion_, width_, freeze_, lowMult_, highMult_, filter_, duck_,
        mix_, output_, dip_;
    dsp::MultiplicativeSmoother decay_, lowFreq_, highFreq_, filterLow_, filterHigh_;
    dsp::Glide size_, predelay_;
    ModeKind mode_ = ModeKind::Hall, nextMode_ = ModeKind::Hall;
    float duckEnv_ = 0.0f, duckAttack_ = 0.0f, duckRelease_ = 0.0f;
    bool fresh_ = true;
    std::array<Line, lines> lines_{};
    std::array<Side, maxChannels> sides_{};
};

const DeviceFactory& reverbFactory();
} // namespace fd::devices
