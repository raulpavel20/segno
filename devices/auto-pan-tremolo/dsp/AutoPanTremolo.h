#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Modulation.h"
#include "fd/dsp/Smoother.h"

#include <array>
#include <utility>

namespace fd::devices
{
/// Panning and Tremolo in one module (devices/auto-pan-tremolo/SPEC.md), on the modulation
/// core's song-locked LFO.
class AutoPanTremolo final : public Device
{
public:
    enum Param : size_t
    {
        Mode,
        Rate,
        Sync,
        Div,
        Shape,
        Amount,
        Edge,
        Phase,
        Invert,
        Attack,
        Stereo,
        Offset,
        Spin,
        Harmonic,
        Vintage,
        LowCut,
        LowCutFreq,
        Output,
        NumParams
    };

    enum Analysis : int
    {
        PeakInL = 0,
        PeakInR = 1,
        PeakOutL = 2,
        PeakOutR = 3,
        LfoPosition = 4, // where the LFO is in its cycle (0..1), for the display's marker
        RateHz = 5,      // the rate it follows, synced or not
        AttackRamp = 6,  // the movement's depth after a note (0..1)
    };

    enum class ModeKind
    {
        Panning,
        Tremolo
    };
    enum class StereoKind
    {
        Offset,
        Spin
    };

    static constexpr int maxChannels = 2;
    static constexpr int numDivisions = dsp::numLfoNoteValues;
    static constexpr double maxCycleSeconds = 100.0; // a synced cycle longer than this is held here
    static constexpr double harmonicHz = 600.0;      // where Harmonic splits the tremolo

    AutoPanTremolo();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    // ---- pure functions: the audio path and the panel's display use the same ones --------------

    /// What shapes the wave.
    struct Wave
    {
        int shape = 0;      // index into the Shape choices
        double edge = 0.0;  // -1..1
        double phase = 0.0; // cycles (0..1)
        bool invert = false;
    };
    /// The wave (-1..1) with the LFO at `position` (cycles): its value at the position plus Phase,
    /// shaped by Edge and flipped by Invert (SPEC: Ranges and limits).
    [[nodiscard]] static double wave(const Wave& w, double position) noexcept;
    /// Edge's shaping alone: v pushed to its extremes (edge > 0) or eased (edge < 0).
    [[nodiscard]] static double edged(double v, double edge) noexcept;
    /// A side's level in the Tremolo for the wave at `w`: full at its top, 1 - Amount at its
    /// bottom; squared with Vintage (before its smoothing). `amount` 0..1.
    [[nodiscard]] static double level(double amount, double w, bool vintage) noexcept;
    /// Harmonic's lows: the level's mirror.
    [[nodiscard]] static double mirrorLevel(double amount, double w, bool vintage) noexcept;
    /// The equal-power balance for a place (-1 left .. 1 right): the left and right gains.
    [[nodiscard]] static std::pair<double, double> panGains(double place) noexcept;

private:
    struct Channel
    {
        dsp::Crossover lows, harmonic; // the Low Cut, and Harmonic's split
        float high = 1.0f, mid = 1.0f; // the gains above 600 Hz and between Low Cut and 600 Hz, smoothed
        float vintage = 1.0f;          // the tremolo's level through Vintage's optical smoothing
        float vintageMid = 1.0f;
    };

    void snap() noexcept;
    void updateTargets(bool immediate) noexcept;
    void updateSync(bool snap) noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960, modeSamples_ = 2400;
    float gainCoef_ = 0.0f, fallCoef_ = 0.0f, riseCoef_ = 0.0f;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother amount_, edge_, invert_, spin_, harmonic_, vintage_, lowOn_, output_, modeFade_;
    dsp::MultiplicativeSmoother rate_, lowFreq_;
    dsp::Glide phase_, offset_, rightJump_;
    dsp::SongLfo lfo_;
    double rightPosition_ = 0.0;
    ModeKind mode_ = ModeKind::Panning, previous_ = ModeKind::Panning;
    StereoKind stereo_ = StereoKind::Offset;
    int shape_ = 0;

    // Attack: a fast and a slow follower of the input's peak; a new note resets the ramp.
    dsp::LevelDetector detector_;
    float fast_ = 0.0f, slow_ = 0.0f, ramp_ = 1.0f, rampStep_ = 1.0f;
    float fastAttack_ = 0.0f, fastRelease_ = 0.0f, slowAttack_ = 0.0f, slowRelease_ = 0.0f;
    int holdoff_ = 0, holdoffSamples_ = 2400;

    double tempo_ = 120.0, beatsPerBar_ = 4.0;
    bool fresh_ = true;
    std::array<Channel, maxChannels> ch_{};
};

const DeviceFactory& autoPanTremoloFactory();
} // namespace fd::devices
