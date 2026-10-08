#pragma once

#include "fd/device/Device.h"
#include "fd/device/Registry.h"
#include "fd/dsp/ControlGrid.h"
#include "fd/dsp/MatchedFilters.h"
#include "fd/dsp/Smoother.h"

#include <array>
#include <complex>

namespace fd::devices
{
/// 24-band analog-matched EQ with per-band stereo placement. See devices/eq/SPEC.md.
class Eq final : public Device
{
public:
    static constexpr int numBands = 24;
    static constexpr int paramsPerBand = 7;
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
    enum GlobalParam : size_t
    {
        Output = numBands * paramsPerBand,
        Scale,
        Analyzer,
        NumParams
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
        PostFifo = 1
    };

    [[nodiscard]] static size_t paramIndex(int band, BandParam p) noexcept
    {
        return static_cast<size_t>(band) * paramsPerBand + p;
    }
    [[nodiscard]] static int slopeDbPerOct(int slopeIndex) noexcept;

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

    Eq();

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

private:
    struct Band
    {
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
    void redesign(Band& b) noexcept;
    [[nodiscard]] BandSettings settingsOf(const Band& b) const noexcept;

    double sampleRate_ = 48000.0;
    int rampSamples_ = 960;
    int fadeSamples_ = 240;
    dsp::ControlGrid grid_{32};
    dsp::LinearSmoother output_;
    float lastScale_ = 100.0f;
    std::array<Band, numBands> bands_{};
};

const DeviceFactory& eqFactory();
} // namespace fd::devices
