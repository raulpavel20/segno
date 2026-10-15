#pragma once

#include "fd/dsp/Fft.h"
#include "fd/dsp/Filters.h"

#include <array>
#include <complex>
#include <cstdint>
#include <utility>
#include <vector>

namespace fd::dsp
{
/// Monophonic pitch detection for the Tuner (devices/tuner/SPEC.md, "DSP design"): the McLeod
/// Pitch Method on a decimated copy of the sound, refined by the phase of its fundamental.
///
/// - **Decimated** by a whole factor to 16–25.6 kHz (×2 at 44.1 and 48 kHz, ×4 at 88.2 and 96),
///   low-passed first: the fastest rate at which the longest window still holds two periods of
///   the lowest note, so the top notes keep five samples a period or more.
/// - **The period:** the normalised square difference function (NSDF) through an FFT; its first
///   key maximum above 0.9 of the highest, refined by a parabola. The peak's height is the
///   clarity (0 … 1): how periodic the sound is.
/// - **The window adapts:** about four periods of the last note, 512 to 2048 decimated samples,
///   growing from the sound's start so a low note's first reading comes as soon as it holds two
///   periods. A reading every 256 decimated samples (about 90 a second).
/// - **To a tenth of a cent:** while the note is steady, the phase of its fundamental between two
///   windows refines the frequency, as a phase vocoder does.
/// - **Gated:** no reading under −60 dBFS, or below 0.8 clarity (noise, chords).
///
/// Real-time safe after prepare(): no allocation in push().
class PitchDetector
{
public:
    static constexpr double minHz = 25.0, maxHz = 4200.0;
    static constexpr float gateDb = -60.0f, clarityGate = 0.8f;
    static constexpr int hop = 256;

    /// One reading: the frequency (Hz), how periodic the sound is (0 … 1), its level (dBFS),
    /// and whether it's a note at all (the gates open).
    struct Reading
    {
        double hz = 0.0;
        float clarity = 0.0f;
        float levelDb = -120.0f;
        bool valid = false;
        /// Refined by phase (the note was steady), not the NSDF's estimate alone.
        bool refined = false;
    };

    /// Allocates: call off the audio thread.
    void prepare(double sampleRate);
    void reset() noexcept;

    /// Feeds mono samples at the prepared rate; `onReading(const Reading&)` is called for each
    /// reading made (every `hop` decimated samples).
    template <typename OnReading> void push(const float* samples, int count, OnReading&& onReading) noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            float y = samples[i];
            for (auto& f : antiAlias_)
                y = f.process(y);
            if (++phase_ < factor_)
                continue;
            phase_ = 0;
            ring_[static_cast<size_t>(write_ & kRingMask)] = y;
            ++write_;
            if (++sinceHop_ >= hop)
            {
                sinceHop_ = 0;
                analyse();
                onReading(reading_);
            }
        }
    }

    [[nodiscard]] const Reading& reading() const noexcept { return reading_; }
    /// The rate it analyses at, and the time between readings.
    [[nodiscard]] double analysisRate() const noexcept { return rate_; }
    [[nodiscard]] double hopSeconds() const noexcept { return hop / rate_; }

private:
    static constexpr int kRing = 8192, kRingMask = kRing - 1;
    static constexpr int kMinWindow = 512, kMaxWindow = 2048;

    void analyse() noexcept;
    /// The NSDF of the newest `w` samples into nsdf_ (lags 0 … maxLag); false if it can't hold a
    /// period in range.
    bool nsdf(int w, int maxLag) noexcept;
    /// The period (in decimated samples) and its clarity from nsdf_, or 0.
    [[nodiscard]] double pickPeriod(int minLag, int maxLag, float& clarity) const noexcept;
    /// The lag of the top of nsdf_'s peak near `lag` (lags up to `limit` hold values).
    [[nodiscard]] double peakOf(int lag, int limit) const noexcept;
    /// A peak of nsdf_ refined by a parabola: its lag and its height.
    [[nodiscard]] std::pair<double, float> parabola(int lag) const noexcept;
    /// The Hann-windowed partial at `hz` in the `w` samples from `start`.
    [[nodiscard]] std::complex<double> partial(double hz, int w, std::int64_t start) const noexcept;
    /// Which of the first four harmonics of `hz` is loudest in the newest window (1 … 4).
    [[nodiscard]] int strongestHarmonic(double hz, int w) const noexcept;
    /// `hz` refined by its phase across two windows `apart` samples apart.
    [[nodiscard]] double refine(double hz, int w, int apart) const noexcept;
    [[nodiscard]] float sample(std::int64_t index) const noexcept
    {
        return ring_[static_cast<size_t>(index & kRingMask)];
    }

    double rate_ = 12000.0;
    int factor_ = 4, phase_ = 0, sinceHop_ = 0;
    std::array<Biquad, 2> antiAlias_{};
    std::vector<float> ring_ = std::vector<float>(kRing, 0.0f);
    std::int64_t write_ = 0;
    int fresh_ = 0; // decimated samples since the sound rose over the gate
    double lastHz_ = 0.0;
    Reading reading_;
    std::array<Fft, 3> ffts_{Fft(10), Fft(11), Fft(12)};
    std::vector<std::complex<float>> spectrum_;
    std::vector<float> window_, nsdf_ = std::vector<float>(kMaxWindow + 8, 0.0f);
};
} // namespace fd::dsp
