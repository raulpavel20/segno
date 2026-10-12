#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Modulation.h"
#include "fd/dsp/Smoother.h"

#include <array>

namespace fd::devices
{
/// Chorus, Ensemble and Vibrato in one module (devices/chorus/SPEC.md), on the modulation core.
class Chorus final : public Device
{
public:
    enum Param : size_t
    {
        Mode,
        Rate,
        Sync,
        Div,
        Depth,
        Shape,
        Offset,
        DelayTime,
        Width,
        Warmth,
        Feedback,
        Invert,
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
        LfoPosition = 4, // where the movement is in its cycle (0..1), for the display's marker
        RateHz = 5,      // the rate it follows, synced or not, for the display
    };

    enum class ModeKind
    {
        Chorus,
        Ensemble,
        Vibrato
    };

    static constexpr int maxChannels = 2;
    static constexpr int maxVoices = 3; // per side
    static constexpr int numDivisions = dsp::numLfoNoteValues;
    static constexpr double maxCycleSeconds = 100.0; // a synced cycle longer than this is held here

    Chorus();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    // ---- pure functions: the audio path and the panel's display use the same ones --------------

    /// A note value's cycle in beats (quarter notes), "1/2" = 2; "1 bar" = `beatsPerBar`. The
    /// values run from the slowest to the fastest.
    [[nodiscard]] static double divisionBeats(int index, double beatsPerBar) noexcept;

    /// What sets the voices' delays.
    struct Voicing
    {
        ModeKind mode = ModeKind::Chorus;
        double depth = 0.5;   // 0..1 (Depth)
        double delayMs = 6.0; // Delay: the voices' shortest delay in Chorus and Ensemble
        double shape = 0.0;   // 0..1: sine to triangle
        double offset = 0.5;  // cycles (0..0.5): how far the right side leads the left
    };
    [[nodiscard]] static int voiceCount(ModeKind mode) noexcept;
    /// How far the voices sweep beyond their shortest delay (ms): with the square of Depth.
    [[nodiscard]] static double sweepMs(ModeKind mode, double depth) noexcept;
    /// The voices' shortest delay (ms): Delay, or in Vibrato the shortest the line has.
    [[nodiscard]] static double baseMs(ModeKind mode, double delayMs, double sampleRate) noexcept;
    /// The movement (-1..1) of voice `voice` on `side` (0 left, 1 right) with the LFO at
    /// `position` (cycles): the sine and the triangle blended by Shape, the voice's third and the
    /// side's Offset added; Ensemble's shimmer on top.
    [[nodiscard]] static double voiceValue(const Voicing& v, int side, int voice, double position) noexcept;
    /// That voice's delay behind the dry sound (ms).
    [[nodiscard]] static double voiceDelayMs(const Voicing& v, double sampleRate, int side, int voice,
                                             double position) noexcept;
    /// The largest pitch bend a voice makes over a cycle at `rateHz` (cents, positive).
    [[nodiscard]] static double pitchSwing(const Voicing& v, double rateHz, double sampleRate) noexcept;

private:
    struct Channel
    {
        dsp::ModulatedDelay line;
        dsp::FeedbackPath tone;
        dsp::Crossover split; // the Low Cut
    };
    struct Gains
    {
        float dry = 0.0f, wet = 0.0f, width = 1.0f, feedback = 0.0f;
    };

    void snap() noexcept;
    void updateTargets(bool immediate) noexcept;
    /// The LFO's rate on each tick: free, or one cycle per note value. `snap`: lock at once.
    void updateSync(bool snap) noexcept;
    [[nodiscard]] Gains gainsFor(ModeKind mode, float mix, float width, float feedback) const noexcept;
    /// The voices of `mode` summed on each side, at the level of one voice (1/sqrt(n)).
    void voices(ModeKind mode, double position, double depth, double delayMs, double shape, double offset,
                float* sum) const noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, modeSamples_ = 2400;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother shape_, width_, warmth_, feedback_, invert_, lowOn_, mix_, output_, modeFade_;
    dsp::MultiplicativeSmoother rate_, lowFreq_;
    dsp::Glide depth_, delay_, offset_;
    dsp::SongLfo lfo_;
    ModeKind mode_ = ModeKind::Chorus, previous_ = ModeKind::Chorus;
    double tempo_ = 120.0, beatsPerBar_ = 4.0;
    bool fresh_ = true;
    std::array<Channel, maxChannels> ch_{};
};

const DeviceFactory& chorusFactory();
} // namespace fd::devices
