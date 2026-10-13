#pragma once

#include "fd/dsp/Fft.h"

#include <array>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace fd::dsp
{
/// A real FFT of `size()` points through a complex one of half the size: forward (real samples to
/// size/2 + 1 bins) and inverse (bins to samples, scaled by 1/size). Unnormalised forward, like
/// Fft. Real-time safe after prepare().
class RealFft
{
public:
    explicit RealFft(int order = 11) { prepare(order); }
    /// May allocate.
    void prepare(int order);
    [[nodiscard]] int size() const noexcept { return size_; }
    void forward(const float* in, std::complex<float>* out) noexcept;
    void inverse(const std::complex<float>* in, float* out) noexcept;

private:
    int size_ = 0;
    Fft half_;
    std::vector<std::complex<float>> twiddles_; // exp(-2 pi i k / size), k < size / 2
    std::vector<std::complex<float>> work_;
};

/// A wavetable: 1 to 256 frames, each one cycle of a wave (the Wavetable Synth, its SPEC's "DSP
/// design"). Kept as spectra: frame f's harmonic h (1 ... 1024) is `spectrum(f)[h]`, scaled so a
/// cycle of N samples is sum over h of 2 Re(c[h] e^(2 pi i h n / N)); DC is dropped. Frames are
/// 2 048 samples when rendered at full size. Immutable once made; made off the audio thread.
class Wavetable
{
public:
    static constexpr int frameSize = 2048;
    static constexpr int maxHarmonic = frameSize / 2;
    static constexpr int maxFrames = 256;

    /// From `frames` cycles of `frameLength` samples each (any length from 2), resampled through
    /// their spectra, without DC, normalised so the loudest frame peaks at 1 (`normalise`). May
    /// allocate.
    [[nodiscard]] static Wavetable fromCycles(const float* samples, int frameLength, int frames,
                                              bool normalise = true);
    /// From spectra (harmonics 1 ... 1024 of each frame, in `bins`, frames × 1025, bin 0 ignored).
    [[nodiscard]] static Wavetable fromSpectra(std::vector<std::complex<float>> bins, int frames,
                                               bool normalise = true);

    [[nodiscard]] int frames() const noexcept { return frames_; }
    [[nodiscard]] const std::complex<float>* spectrum(int frame) const noexcept
    {
        return bins_.data() + static_cast<size_t>(frame) * (maxHarmonic + 1);
    }
    /// The frame's 2 048 samples (for displays and export).
    [[nodiscard]] const float* samples(int frame) const noexcept
    {
        return samples_.data() + static_cast<size_t>(frame) * frameSize;
    }
    /// The highest harmonic any frame has (how bright the table can get).
    [[nodiscard]] int highestHarmonic() const noexcept { return highest_; }

    std::string name;

private:
    void finish(bool normalise);

    int frames_ = 0, highest_ = 0;
    std::vector<std::complex<float>> bins_; // frames × 1025
    std::vector<float> samples_;            // frames × 2048
};

/// The ways an oscillator reshapes its wave (the SPEC's warps, in its order: the values are its
/// choice indices, so never reordered).
enum class Warp : std::uint8_t
{
    Off,
    Sync,
    WindowSync,
    BendPlus,
    BendMinus,
    BendBoth,
    Squeeze,
    Pulse,
    AsymPlus,
    AsymMinus,
    Mirror,
    Flip,
    Quantize,
    Fold,
    Remap,
    Fm,
    Pm,
    Rm,
    Am,
    FormantShift,
    HarmonicStretch,
    InharmonicStretch,
    Smear,
    SpectralLowPass,
    SpectralHighPass,
    PhaseDisperse,
    FrameSkew,
    RandomHarmonics,
    Shepard,
};
inline constexpr int numWarps = static_cast<int>(Warp::Shepard) + 1;

/// Warps that reshape the oscillator's own cycle (rendered, so free of aliasing at any setting).
[[nodiscard]] constexpr bool isCycleWarp(Warp w) noexcept
{
    return w >= Warp::Sync && w <= Warp::Remap;
}
/// Warps that bring in another source, sample by sample (FM, PM, RM, AM).
[[nodiscard]] constexpr bool isSampleWarp(Warp w) noexcept
{
    return w >= Warp::Fm && w <= Warp::Am;
}
/// Warps that work on the harmonics (the Wavetable Synth's step 2).
[[nodiscard]] constexpr bool isSpectralWarp(Warp w) noexcept
{
    return w >= Warp::FormantShift;
}

/// A drawn curve from 0..1 to 0..1 (a Remap warp's, an LFO's shape), sampled at 257 points.
using CurveTable = std::array<float, 257>;
[[nodiscard]] float lookup(const CurveTable& curve, double x) noexcept;

/// Applies a cycle warp at `amount` (0..1) to one cycle of `size` samples (band-limited well
/// below its Nyquist, read with cubic interpolation), into `out`. Amount 0 leaves it unchanged
/// (except Window Sync, which is always windowed). `curve` is Remap's drawn curve (identity if
/// null). The same function draws the warp on the panel.
void applyCycleWarp(Warp warp, float amount, const float* in, int size, float* out,
                    const CurveTable* curve = nullptr) noexcept;

/// Renders the cycle an oscillator plays next (the Wavetable Synth's "each voice renders its own
/// cycle"): the table's spectra interpolated at `position`, its cycle warps applied on a cycle
/// twice the size, then cut to the harmonics a note can play without aliasing. Real-time safe
/// after prepare(); one renderer serves every voice in turn.
class CycleRenderer
{
public:
    /// The cycle written: `size` samples from `cycle(buffer)`, with `guard` samples either side
    /// for 8-point reads (`buffer` must hold bufferSize floats).
    static constexpr int maxSize = 2 * Wavetable::frameSize;
    static constexpr int guard = 4;
    static constexpr int bufferSize = maxSize + 2 * guard;
    [[nodiscard]] static float* cycle(float* buffer) noexcept { return buffer + guard; }
    [[nodiscard]] static const float* cycle(const float* buffer) noexcept { return buffer + guard; }

    struct WarpSetting
    {
        Warp warp = Warp::Off;
        float amount = 0.0f;
        const CurveTable* curve = nullptr;
    };

    /// May allocate.
    void prepare();

    /// The cycle size for a harmonic limit: a power of two at least 32 times it (256 ... 4 096), so
    /// reads between its samples leave images far down (the lowest notes reach the 4 096 cap).
    [[nodiscard]] static int sizeFor(int harmonics) noexcept;

    /// Renders into `buffer` (bufferSize floats) and returns the cycle's size. `harmonics` is the
    /// highest harmonic the note may have (1 ... 1024).
    int render(const Wavetable& table, double position, const std::array<WarpSetting, 2>& warps,
               int harmonics, float* buffer) noexcept;

private:
    std::array<RealFft, 14> ffts_; // by order (6 ... 13 used)
    std::vector<std::complex<float>> bins_;
    std::vector<float> wide_, warped_;
};
/// Reading a rendered cycle (`cycle` = CycleRenderer::cycle(buffer), `size` samples, guards
/// either side) at `phase` (0..1): 4-point cubic (Eco and Normal quality), or an 8-point windowed
/// sinc (High: images some 20 dB lower, for the lowest notes and the brightest warps).
[[nodiscard]] float readCubic(const float* cycle, int size, double phase) noexcept;
[[nodiscard]] float readSinc(const float* cycle, int size, double phase) noexcept;
} // namespace fd::dsp
