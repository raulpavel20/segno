#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Modulation.h"
#include "fd/dsp/Smoother.h"

#include <array>
#include <span>

namespace fd::devices
{
/// Echoes: Live's Delay and Echo in one (devices/delay/SPEC.md), on the modulation core.
class Delay final : public Device
{
public:
    enum Param : size_t
    {
        Mode,
        Sync,
        TimeLeft,
        TimeRight,
        DivLeft,
        DivRight,
        Link,
        Swing,
        Stereo,
        Feedback,
        Freeze,
        Filter,
        FilterLow,
        FilterHigh,
        Drive,
        Wobble,
        ModShape,
        ModRate,
        ModTime,
        ModFilter,
        Duck,
        DuckRelease,
        Transition,
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
        DuckDb = 4,   // how far the echoes are ducked (dB, peak-held)
        TempoBpm = 5, // the tempo the times follow (for the repeats display)
        BeatsPerBar = 6,
    };

    enum class ModeKind
    {
        Clean,
        Tape,
        Analog
    };
    enum class StereoKind
    {
        Stereo,
        PingPong,
        MidSide
    };

    static constexpr int maxChannels = 2;
    static constexpr double maxSeconds = 10.0; // a synced time longer than this is held here
    static constexpr int numDivisions = 17;

    Delay();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    // ---- pure functions: the audio path and the panel's displays use the same ones ------------

    /// A note value's length in beats (quarter notes), "1/8D" = 0.75; a bar is `beatsPerBar`.
    [[nodiscard]] static double divisionBeats(int index, double beatsPerBar) noexcept;

    struct TimeSettings
    {
        bool sync = true;
        double leftMs = 375.0, rightMs = 375.0;
        int leftDiv = 9, rightDiv = 9;
        bool link = true;
        double swingPercent = 0.0;
    };
    struct Times
    {
        double left = 0.0, right = 0.0; // seconds
    };
    /// The two sides' times: milliseconds, or note values at `tempoBpm`, Swing on the right
    /// while synced; at most maxSeconds.
    [[nodiscard]] static Times times(const TimeSettings& s, double tempoBpm, double beatsPerBar) noexcept;

    /// What one pass through the feedback loop does to a 1 kHz tone (the filter and the mode's
    /// tone): the repeats display's levels.
    [[nodiscard]] static double passGain(ModeKind mode, bool filter, double lowHz, double highHz,
                                         double sampleRate) noexcept;

    struct Repeat
    {
        double seconds = 0.0;
        int lane = 0;       // 0: left (or mid), 1: right (or side)
        double level = 1.0; // the first echo is 1
    };
    /// The echoes of an impulse in the left (and right) input, as the loop plays them, up to
    /// `horizon` seconds: their times, sides and levels. Returns how many were written.
    [[nodiscard]] static int repeats(const Times& t, StereoKind stereo, double feedback, double passGain,
                                     double horizon, std::span<Repeat> out) noexcept;

private:
    struct Channel
    {
        dsp::ModulatedDelay line;
        dsp::FeedbackPath path;
        dsp::Biquad tapeShelf, tapeHigh, analogHigh;
        double delay = 0.0;              // Repitch: the read position now (samples)
        double headA = 0.0, headB = 0.0; // Fade: the old and the new
        int fadeLeft = 0;
        double target = 0.0;
    };

    /// Every setting straight to its value and the times to their targets: nothing has played.
    void snap() noexcept;
    void updateTargets(bool immediate) noexcept;
    void updateFilters(float modulation) noexcept;
    [[nodiscard]] float loop(Channel& c, float x, float tape, float analog, float drive,
                             float filter) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, modeSamples_ = 1440, fadeSamples_ = 2400, dipSamples_ = 720;
    double glide_ = 0.0; // Repitch's one-pole coefficient

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother feedback_, freeze_, filter_, tape_, analog_, drive_, wobble_, modTime_, modFilter_,
        duck_, mix_, output_, dip_;
    dsp::MultiplicativeSmoother low_, high_;
    dsp::ModulationLfo mod_, wow_, flutter_;
    float modL_ = 0.0f, modR_ = 0.0f, modCoef_ = 0.0f; // the modulation, smoothed (shape changes)
    float duckEnv_ = 0.0f, duckAttack_ = 0.0f, duckRelease_ = 0.0f;
    StereoKind stereo_ = StereoKind::Stereo, nextStereo_ = StereoKind::Stereo;
    double tempo_ = 120.0, beatsPerBar_ = 4.0;
    bool fresh_ = true; // nothing processed since the last reset
    std::array<Channel, maxChannels> ch_{};
};

const DeviceFactory& delayFactory();
} // namespace fd::devices
