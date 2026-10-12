#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/Filters.h"
#include "fd/dsp/Modulation.h"
#include "fd/dsp/Oversampler.h"
#include "fd/dsp/Smoother.h"

#include <array>
#include <complex>
#include <vector>

namespace fd::devices
{
/// One filter that moves and has a sound of its own (devices/filter/SPEC.md): ten types, four
/// circuits, Drive, an LFO and an envelope follower, run at twice the rate.
class Filter final : public Device
{
public:
    enum Param : size_t
    {
        Type,
        Circuit,
        Slope,
        Freq,
        Res,
        Sweep,
        Vowel,
        Morph,
        Drive,
        Lfo,
        LfoRate,
        LfoSync,
        LfoDiv,
        LfoShape,
        LfoAmount,
        LfoPhase,
        LfoStereo,
        LfoOffset,
        LfoSpin,
        Env,
        EnvAmount,
        EnvAttack,
        EnvRelease,
        Clip,
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
        MainLeft = 4, // each side's main control now (0..1, its place on the type's scale)
        MainRight = 5,
        Envelope = 6, // the envelope now (0..1; 0 while it's off)
    };
    /// The output (both sides summed), for the display's spectrum.
    static constexpr int OutputFifo = 0;

    enum class TypeKind
    {
        LowPass,
        HighPass,
        BandPass,
        Notch,
        Morph,
        Dj,
        Comb,
        Vowel,
        NotchLowPass,
        Resampling
    };
    enum class CircuitKind
    {
        Clean,
        Warm,
        Acid,
        Gritty
    };
    enum class StereoKind
    {
        Offset,
        Spin
    };

    static constexpr int maxChannels = 2;
    static constexpr int factor = 2; // the filter runs at twice the rate (SPEC: Oversampling)
    static constexpr int numTypes = 10;
    static constexpr double maxCycleSeconds = 100.0; // a synced cycle longer than this is held here
    static constexpr double lowestHz = 20.0, highestHz = 20000.0;
    static constexpr double combHighestHz = 10000.0;

    Filter();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override;
    [[nodiscard]] double tailSeconds() const noexcept override;

    // ---- pure functions: the audio path and the panel's display use the same ones --------------

    /// What shapes the filter, besides its main control.
    struct Settings
    {
        TypeKind type = TypeKind::LowPass;
        CircuitKind circuit = CircuitKind::Clean;
        bool steep = true;  // 24 dB
        double res = 0.25;  // 0..1
        double morph = 0.0; // 0..1
    };

    /// Which parameter is the type's main control (Freq, Sweep or Vowel).
    [[nodiscard]] static size_t mainParam(TypeKind type) noexcept;
    /// A main control's value (Hz, % or %) as its place on the type's scale (0..1): Freq on a log
    /// scale from 20 Hz to 20 kHz, Sweep and Vowel straight across their range.
    [[nodiscard]] static double placeOf(TypeKind type, double value) noexcept;
    /// And back.
    [[nodiscard]] static double valueAt(TypeKind type, double place) noexcept;
    /// Where the main control is moved to: `place` plus half of each modulation's amount times its
    /// value (the LFO's -1..1, the envelope's 0..1), held between 0 and 1 (SPEC: Ranges and limits).
    [[nodiscard]] static double modulated(double place, double lfoAmount, double lfo, double envAmount,
                                          double env) noexcept;

    /// The DJ filter at a Sweep (-1..1): a low pass (below the centre) or a high pass, its cutoff,
    /// and how much of it sounds (it fades to the dry sound within 2 % of the centre).
    struct DjFilter
    {
        bool highPass = false;
        double hz = 20000.0;
        double amount = 0.0;
    };
    [[nodiscard]] static DjFilter djFilter(double sweep, double sampleRate) noexcept;

    /// The first three formants of the vowel at `vowel` (0..1: a, e, i, o, u), shifted by `morph`
    /// (0..1, up to an octave): their frequencies (Hz) and levels (linear).
    struct Formants
    {
        std::array<double, 3> hz{}, level{}, bandwidth{};
    };
    [[nodiscard]] static Formants formants(double vowel, double morph) noexcept;

    /// The filter's small-signal response (complex) at `hz` with its main control at `place`
    /// (0..1), at the device's `sampleRate` (it runs at twice that): what the display draws, and
    /// what the audio matches at low levels.
    [[nodiscard]] static std::complex<double> response(const Settings& s, double place, double hz,
                                                       double sampleRate) noexcept;
    [[nodiscard]] static double magnitude(const Settings& s, double place, double hz,
                                          double sampleRate) noexcept
    {
        return std::abs(response(s, place, hz, sampleRate));
    }

    /// The soft clip: exact under -6 dBFS, approaching 0 dBFS above it and never passing it.
    [[nodiscard]] static float softClip(float x) noexcept;

    /// What a circuit needs at a cutoff: the 2-poles' frequency and damping (k1: a steep slope's
    /// first stage), or the ladder's (gh: Acid's feedback high pass).
    struct Tuning
    {
        double g = 0.0, k1 = 0.0, k2 = 0.0, gh = 0.0, k = 0.0;
    };

private:
    /// A zero-delay-feedback state-variable filter (2 poles), its band pass clipped for Gritty.
    struct Svf
    {
        double s1 = 0.0, s2 = 0.0;
        struct Out
        {
            double lp, bp, hp;
        };
        Out process(double x, double g, double k, double clip) noexcept;
        void reset() noexcept { s1 = s2 = 0.0; }
    };
    /// A four-pole ladder (zero-delay feedback, saturating at its input), with a one-pole high
    /// pass in its feedback for Acid. Its stages' outputs are mixed into each type.
    struct Ladder
    {
        std::array<double, 4> s{};
        double sh = 0.0; // the feedback's high pass
        std::array<double, 5> process(double x, double g, double gh, double k, bool acid) noexcept;
        void reset() noexcept
        {
            s = {};
            sh = 0.0;
        }
    };
    /// A delay read between samples (cubic), for the comb.
    struct CombLine
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
        [[nodiscard]] float read(double delay) const noexcept;
    };
    /// One configuration of the filter (a type, circuit and slope) for one side; two run side by
    /// side while a change crossfades.
    struct Core
    {
        Settings settings;
        std::array<Svf, 2> svf;
        Ladder ladder;
        CombLine combIn, combOut;
        std::array<Svf, 3> vowel;
        Svf notch;
        double held = 0.0, holdPhase = 1.0;
        double dcIn = 0.0, dcOut = 0.0; // Acid's DC blocker
        // The coefficients last worked out, kept while their inputs stay the same.
        double tunedHz = -1.0, tunedRes = -1.0;
        Tuning tuning;
        double vowelPlace = -1.0, vowelRes = -1.0, vowelMorph = -1.0, vowelGain = 0.0;
        std::array<double, 3> vowelG{}, vowelK{}, vowelLevel{};
        void reset() noexcept;
    };
    /// What a core needs each oversampled sample.
    struct DriveNow
    {
        double gain = 1.0, clean = 0.0; // into the filter; how much of Clean's saturation sounds
    };

    double runCore(Core& core, double x, double place, double res, double morph,
                   const DriveNow& drive) noexcept;
    void snap() noexcept;
    void updateTargets(bool immediate) noexcept;
    void updateSync(bool snap) noexcept;
    [[nodiscard]] Settings settingsNow() const noexcept;

    double sampleRate_ = 48000.0, rate2_ = 96000.0;
    int rampSamples_ = 960, fadeSamples_ = 1440;
    float lfoCoef_ = 0.0f;

    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother res_, sweep_, vowel_, morph_, drive_, lfoOn_, lfoAmount_, spin_, envOn_, envAmount_,
        clip_, mix_, output_, fade_;
    dsp::MultiplicativeSmoother freq_, rate_;
    dsp::Glide phase_, offset_, rightJump_;
    dsp::SongLfo lfo_;
    dsp::LevelDetector detector_;
    dsp::Oversampler oversampler_;
    double rightPosition_ = 0.0;
    float envDb_ = -48.0f, envRise_ = 0.0f, envFall_ = 0.0f;
    StereoKind stereo_ = StereoKind::Offset;
    int shape_ = 0;
    double tempo_ = 120.0, beatsPerBar_ = 4.0;
    bool fresh_ = true;
    int active_ = 0; // which core is the current one; the other fades out
    std::array<std::array<Core, 2>, maxChannels> cores_{};
    std::array<float, maxChannels> lfoValue_{};
    // Per control segment: the dry sound (held back by the latency), the work buffers and what
    // each oversampled sample needs.
    std::array<std::vector<float>, maxChannels> work_, dryLine_;
    std::array<int, maxChannels> dryPos_{};
    std::array<std::array<double, maxChannels>, 64> place_{}, placeOld_{}; // each side's, per sample
    std::array<std::array<float, 64>, maxChannels> dryNow_{};
    std::array<float, 64> resNow_{}, morphNow_{}, fadeNow_{}, driveNow_{}, mixNow_{}, outNow_{}, clipNow_{};
    std::array<std::array<float, 64>, maxChannels> djNow_{}; // how much of the DJ filter sounds
    std::array<double, maxChannels> lastPlace_{};
};

const DeviceFactory& filterFactory();
} // namespace fd::devices
