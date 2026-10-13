#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace fd::dsp
{
/// A spectral band's detector (the EQ's stage 2, after the resonance suppressors): frame by frame,
/// how far to turn each frequency down (or up) so what sticks out comes back toward its
/// surroundings, inside the band's shape.
///
/// - The trigger's spectrum is smoothed across neighbouring bins and a little over time (so noise
///   doesn't trigger it), and tilted 3 dB/oct up around 1 kHz, so a full mix's natural slope
///   doesn't make the lows stand out.
/// - **Auto:** each bin is compared with its surroundings: the spectrum averaged in dB over a
///   window `detail` sets (2 octaves either side at 0, a twelfth at 1), its peaks clipped so a
///   resonance doesn't raise its own baseline. What sticks out by more than noise does (2.5 dB)
///   starts to come down, and all the way down to its surroundings once it's sure to be a peak
///   (4 dB more), up to the range. **Manual:** what's over the threshold comes down by as much.
/// - The gains follow with attack and release, faster in the highs and slower in the lows (the
///   trigger too), then are smoothed across frequency, so the cuts are filters, not holes. Weighted
///   by the band's shape.
///
/// Real-time safe after prepare().
class SpectralDynamics
{
public:
    struct Settings
    {
        double sampleRate = 48000.0;
        int fftSize = 8192;         // bin k is k * sampleRate / fftSize hertz
        int padding = 4;            // the FFT is this many times the analysed frame
        double frameSeconds = 0.02; // between frames
        float rangeDb = -6.0f;      // the deepest cut (below zero), or the most boost (above)
        bool autoThreshold = true;
        float thresholdDb = -24.0f; // manual: a bin's tilted level (a full-scale 1 kHz sine reads 0 dB)
        float detail = 0.5f;        // 0..1
        float attackMs = 5.0f, releaseMs = 120.0f;
    };

    /// Working memory the bands of one engine share, and tables for its size and rate.
    class Scratch
    {
    public:
        /// May allocate: room for `maxBins`.
        void prepare(int maxBins);

    private:
        friend class SpectralDynamics;
        static constexpr int timeSteps = 16; // how much faster or slower a bin moves, in steps
        static double timeScale(int step) noexcept;
        static float coefficient(double ms, double frameSeconds) noexcept;
        void tune(double sampleRate, int fftSize, double frameSeconds, int bins) noexcept;

        std::vector<double> sums;              // prefix sums, for windows in octaves
        std::vector<float> work;               // four rows of bins
        std::vector<float> tiltDb;             // per bin: the detection tilt
        std::vector<std::uint8_t> step;        // per bin: how fast it moves (a time step)
        std::array<float, timeSteps> follow{}; // the trigger's smoothing, per time step
        double tunedRate = 0.0, tunedFrame = 0.0;
        int tunedSize = 0, tunedBins = 0;
    };

    /// May allocate: room for `maxBins`.
    void prepare(int maxBins);
    void reset() noexcept;

    /// One frame: `power` is each bin's power (a full-scale sine reads 1 at its bin), `weight` the
    /// band's shape (0..1); `gainDb` gets each bin's gain. `bins` at most the prepared count, and
    /// the scratch's.
    void process(const float* power, const float* weight, int bins, const Settings& s, Scratch& scratch,
                 float* gainDb) noexcept;

private:
    std::vector<float> smoothed_;                                // the trigger, smoothed (power), per bin
    std::vector<float> gain_;                                    // the gain now (dB), per bin
    std::array<float, Scratch::timeSteps> attack_{}, release_{}; // per time step, for the times below
    float tunedAttack_ = -1.0f, tunedRelease_ = -1.0f;
    double tunedFrame_ = 0.0;
    bool primed_ = false;
};
} // namespace fd::dsp
