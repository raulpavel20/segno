#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/Dynamics.h"
#include "fd/dsp/MatchedFilters.h"
#include "fd/dsp/Smoother.h"
#include "fd/dsp/SpectralDynamics.h"
#include "fd/dsp/SpectralFilter.h"

#include <array>
#include <complex>
#include <optional>
#include <span>
#include <vector>

namespace fd::devices
{
/// 24-band analog-matched EQ with per-band stereo placement; bands can move with the sound,
/// triggered by their own range or a sidechain's, follow each frequency on their own (spectral
/// bands), and the whole EQ can run in linear phase (stage 2). See devices/eq/SPEC.md.
class Eq final : public Device
{
public:
    static constexpr int numBands = 24;
    static constexpr int paramsPerBand = 7;
    static constexpr int dynParamsPerBand = 7; // stage 2: a band's dynamics
    static constexpr int spectralParamsPerBand = 2; // stage 2, step 2b
    static constexpr int maxSections = 8; // 96 dB/oct = 8 second-order sections
    static constexpr int maxChannels = 2;

    enum BandParam : size_t
    {
        On,
        Type,
        Freq,
        Gain,
        Q,
        Slope,
        Place
    };
    /// Stage 2 (state version 2): each band's dynamics, after the core's parameters.
    enum DynParam : size_t
    {
        Dyn,
        Range,
        Threshold,
        AutoThreshold,
        Attack,
        Release,
        Trigger
    };
    /// Stage 2, step 2b (state version 3): each band's spectral settings, after Listen.
    enum SpectralParam : size_t
    {
        Spectral,
        Detail
    };
    enum GlobalParam : size_t
    {
        Output = numBands * paramsPerBand,
        Scale,
        Analyzer,
        AutoGain = Analyzer + 1 + numBands * dynParamsPerBand, // after the bands' dynamics
        Delta,
        Listen,
        Phase = Listen + 1 + numBands * spectralParamsPerBand, // after the bands' spectral settings
        Resolution,
        NumParams
    };
    enum class Processing
    {
        ZeroLatency,
        LinearPhase
    };
    enum class TriggerSource
    {
        Input,
        Sidechain
    };
    enum class BandType
    {
        Bell,
        LowShelf,
        HighShelf,
        LowCut,
        HighCut,
        Notch,
        BandPass,
        Tilt
    };
    enum class Placement
    {
        Stereo,
        Left,
        Right,
        Mid,
        Side
    };
    enum Fifo : int
    {
        PreFifo = 0,
        PostFifo = 1,
        SidechainFifo = 2, // the compared track, for the masking display
        DynamicsFifo = 3,  // frames of dynamicsFrame values, every 10 ms
    };
    /// The spectral bands' reduction, for the display: at points a twelfth of an octave apart from
    /// 20 Hz (to 20.5 kHz).
    static constexpr int spectralPoints = 121;
    [[nodiscard]] static double spectralPointHz(int point) noexcept { return 20.0 * std::exp2(point / 12.0); }
    /// A dynamics frame: each band's dynamic gain now (dB), then each band's trigger level (dB),
    /// then the spectral bands' gain at each point (dB, summed).
    static constexpr int dynamicsFrame = 2 * numBands + spectralPoints;
    static constexpr double dynamicsFrameMs = 10.0;
    /// Analysis slots.
    enum Analysis : int
    {
        AutoGainDb = 0, // what Auto Gain applies, in dB
    };

    [[nodiscard]] static size_t paramIndex(int band, BandParam p) noexcept
    {
        return static_cast<size_t>(band) * paramsPerBand + p;
    }
    [[nodiscard]] static size_t dynIndex(int band, DynParam p) noexcept
    {
        return Analyzer + 1 + static_cast<size_t>(band) * dynParamsPerBand + p;
    }
    [[nodiscard]] static size_t spectralIndex(int band, SpectralParam p) noexcept
    {
        return Listen + 1 + static_cast<size_t>(band) * spectralParamsPerBand + p;
    }
    [[nodiscard]] static int slopeDbPerOct(int slopeIndex) noexcept;

    /// The spectral engine's size for a Resolution (0 Low, 1 Medium, 2 High): an FFT of 2^order
    /// points, doubled at 88.2 and 96 kHz and again above, so the delay is about the same in time.
    [[nodiscard]] static int engineOrder(int resolution, double sampleRate) noexcept;
    /// The delay (samples) with the engine on or off, at a Resolution.
    [[nodiscard]] static int latencyFor(bool engine, int resolution, double sampleRate) noexcept;
    /// Whether these settings run the spectral engine: Linear phase, or any band's Spectral switch
    /// on (on a band that's off too, so the delay depends only on the settings that change it).
    [[nodiscard]] static bool usesEngine(const ParamStore& params) noexcept;

    /// One band's settings, and its sections (the same design the audio uses; for the UI curve).
    struct BandSettings
    {
        BandType type = BandType::Bell;
        double freq = 1000.0, gainDb = 0.0, q = 0.71;
        int slopeIndex = 1;
    };
    struct Sections
    {
        std::array<dsp::BiquadCoeffs, maxSections> coeffs{};
        int count = 0;
    };
    [[nodiscard]] static Sections design(const BandSettings& band, double sampleRate) noexcept;
    [[nodiscard]] static std::complex<double> response(const Sections& s, double freqHz, double sampleRate) noexcept;
    /// True if the band has no effect at these settings (flat bell/shelf/tilt).
    [[nodiscard]] static bool isFlat(const BandSettings& band) noexcept;
    /// Whether a type has a gain to move (bell, shelves, tilt): only these can be dynamic.
    [[nodiscard]] static bool canMove(BandType type) noexcept;
    /// Whether a dynamic band of this type can be spectral (bell and shelves).
    [[nodiscard]] static bool canBeSpectral(BandType type) noexcept;
    /// What a band hears for its trigger, and what Listen plays: its range as a band pass (a bell,
    /// notch or band pass), a low pass (low shelf, low cut) or a high pass (high shelf, high cut).
    /// A tilt hears everything: nullopt.
    [[nodiscard]] static std::optional<dsp::BiquadCoeffs> rangeFilter(BandType type, double freq, double q,
                                                                      double sampleRate) noexcept;
    /// How far a dynamic band moves (dB) when its trigger is `overDb` above the threshold: none
    /// under a soft knee 6 dB wide around it, then in proportion, all of `rangeDb` once 12 dB over.
    [[nodiscard]] static float dynamicGain(float overDb, float rangeDb) noexcept;
    /// A band for Auto Gain's estimate: its settings, whether it's on, and where it acts.
    struct CurveBand
    {
        BandSettings settings;
        bool on = false;
        Placement place = Placement::Stereo;
    };
    /// Auto Gain: the change in loudness (dB) of pink noise through these bands at their static
    /// gains, K-weighted as BS.1770 is. Its negative is what Auto Gain applies.
    [[nodiscard]] static double loudnessChangeDb(std::span<const CurveBand> bands,
                                                 double sampleRate) noexcept;

    Eq();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override;

private:
    struct Band
    {
        // Dynamics (stage 2): its trigger's level, the threshold, and how far it has moved.
        bool dynamic = false, autoThreshold = true;
        TriggerSource trigger = TriggerSource::Input;
        std::optional<dsp::BiquadCoeffs> triggerFilter;
        BandType triggerType = BandType::Bell; // what triggerFilter was designed for
        float triggerFreq = -1.0f, triggerQ = -1.0f;
        std::array<std::array<double, 2>, 2> triggerState{}; // per detected channel
        std::array<dsp::LevelDetector, 2> detectors;
        float usualDb = -120.0f; // the trigger's usual level while it sounds, for Auto Threshold
        bool usualSet = false;
        float levelDb = -120.0f;
        dsp::GainSmoother dynGain;
        float dynDb = 0.0f, designedDyn = 0.0f;
        // Spectral (step 2b): each frequency on its own, in the spectral engine.
        bool spectral = false;
        dsp::SpectralDynamics spectralDetector;
        std::vector<float> magnitude;      // its response per bin in the engine (linear phase)
        int designs = 0, magnitudeOf = -1; // which design `magnitude` is of
        float respondedMix = -1.0f;        // its mix in the engine's response now
        // The same band without its dynamics, for Delta, and what it was designed for.
        Sections staticSections;
        float staticFreq = -1.0f, staticGain = 0.0f, staticQ = -1.0f, staticScale = -1.0f;
        std::array<std::array<std::array<double, 2>, maxSections>, maxChannels> staticState{};

        // Structure currently in use (changes fade the band out and back in).
        BandType type = BandType::Bell;
        Placement place = Placement::Stereo;
        int slopeIndex = 1;
        dsp::MultiplicativeSmoother freq, q;
        dsp::LinearSmoother gain;
        dsp::LinearSmoother mix; // 0 = bypassed, 1 = fully applied
        bool restructurePending = false;
        float designedFreq = -1.0f, designedGain = 0.0f, designedQ = -1.0f;
        Sections sections;     // target coefficients (end of the current grid interval)
        Sections fromSections; // coefficients at the start of the interval
        bool interpolating = false;
        bool idle = true; // skipped (off or settled flat); filter state is cleared on entry
        bool flat = true;
        std::array<std::array<std::array<double, 2>, maxSections>, maxChannels> state{}; // TDF-II
    };

    void tick(bool immediate) noexcept;
    void tickDynamics(Band& b, int index, bool immediate) noexcept;
    void redesign(Band& b) noexcept;
    void updateStatic(Band& b) noexcept;
    [[nodiscard]] BandSettings settingsOf(const Band& b) const noexcept;
    [[nodiscard]] BandSettings staticSettingsOf(const Band& b) const noexcept;
    void detect(Band& b, double l, double r) noexcept;
    void updateAutoGain() noexcept;
    void applyEngine(bool force) noexcept;
    bool respond(dsp::SpectralFilter::Frame& frame) noexcept;
    /// The sections' magnitude (or power) per bin of the engine.
    void magnitudeOf(const Sections& s, float* out, int bins, bool power = false) const noexcept;
    void applyPlaced(Placement place, const float* gain, float* ll, float* lr, float* rl, float* rr,
                     int bins) const noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960;
    int fadeSamples_ = 240;
    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother output_;
    float lastScale_ = 100.0f;
    std::array<Band, numBands> bands_{};

    // Auto Gain: re-estimated when the static curve changes, at most every few ticks.
    dsp::LinearSmoother autoGain_;
    bool autoGainStale_ = true;
    int autoGainWait_ = 0;
    float autoGainDb_ = 0.0f;

    // Listen and Delta.
    dsp::LinearSmoother listenMix_, deltaMix_;
    int listening_ = -1; // the band Listen plays now
    std::array<std::array<double, 2>, 2> listenState_{};
    dsp::BiquadCoeffs listenCoeffs_{};
    bool listenPass_ = false; // a tilt: the whole sound

    int frameLength_ = 480, framePos_ = 0; // the dynamics FIFO's frames

    // The spectral engine (step 2b): linear phase, and spectral bands. Its settings change only at
    // a block's start (ADR 0020).
    dsp::SpectralFilter engine_;
    bool engineOn_ = false, linear_ = false;
    int engineOrder_ = -1;
    bool responseStale_ = true;
    dsp::SpectralDynamics::Scratch spectralScratch_;
    std::vector<double> cosW_, cos2W_;                             // per bin
    std::vector<float> rowA_, rowB_, rowC_, rowD_;                 // per bin, scratch
    std::vector<float> staticLl_, staticLr_, staticRl_, staticRr_; // Delta: the response held still
    std::vector<std::complex<float>> detectL_, detectR_, sideL_,
        sideR_; // a frame of the input, the sidechain
    double respondedDelta_ = -1.0;
    // The input and sidechain, kept for detection, and what's delayed to line up with the engine.
    std::vector<float> inRingL_, inRingR_, scRingL_, scRingR_;
    std::vector<float> delayStaticL_, delayStaticR_, delayListenL_, delayListenR_, delayDeltaMix_,
        delayListenMix_;
    long long ringPos_ = 0, ringMask_ = 0;
    bool sidechainConnected_ = false;
    std::array<int, spectralPoints> pointLo_{}, pointHi_{}; // the display's points, in bins
    std::array<float, spectralPoints> spectralDisplay_{};
};

const DeviceFactory& eqFactory();
} // namespace fd::devices
