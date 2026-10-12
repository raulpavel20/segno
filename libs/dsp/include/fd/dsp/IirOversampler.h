#pragma once

#include <array>
#include <vector>

namespace fd::dsp
{
/// A polyphase IIR half-band: two branches of first-order all-passes, the filter
/// H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2 (Valenzuela and Constantinides). Minimum phase, so it
/// delays a sound by a few samples where a linear-phase FIR delays it by dozens.
struct IirHalfband
{
    /// The all-passes' coefficients, ascending (the even ones are A0's, the odd A1's), for a
    /// stopband at least `attenuationDb` down and a transition `transition` wide either side of a
    /// quarter of the rate (a share of it, under 0.25).
    [[nodiscard]] static std::vector<double> design(double attenuationDb, double transition);
};

/// Oversampling by 2 with almost no delay, through a polyphase IIR half-band each way: 106 dB of
/// image and alias rejection, flat to within 0.001 dB up to 0.454 of the rate (20 kHz at 44.1).
/// The round trip delays a sound by about 3.5 samples at low frequencies, more towards the top
/// (it isn't linear phase): a dry path lines up with the processed one at every frequency by
/// making the same round trip (`roundTrip`). The latency it reports is where the round trip's
/// impulse peaks, the same at every rate (ADR 0016).
class IirOversampler
{
public:
    static constexpr int factor = 2;
    static constexpr int numCoefficients = 10;

    /// May allocate. Resets state.
    void prepare(int maxBlockSize, int numChannels);
    void reset() noexcept;

    /// Whole samples at the device's rate, the same at every setting and rate.
    [[nodiscard]] int latencySamples() const noexcept { return latency_; }

    /// Upsamples n samples per channel; returns the oversampled channel buffers (2n each).
    float* const* upsample(const float* const* in, int numChannels, int n) noexcept;
    /// Downsamples the oversampled buffers back into `out` (n samples per channel).
    void downsample(float* const* out, int numChannels, int n) noexcept;
    /// Up and straight back down, in place, through its own filters (not the ones upsample and
    /// downsample use): what a dry path goes through to line up with the processed one.
    void roundTrip(float* const* io, int numChannels, int n) noexcept;

private:
    struct Branch
    {
        std::array<double, numCoefficients / 2> x{}, y{};
        double process(double in, const double* coefficients) noexcept;
    };
    struct Channel
    {
        Branch up0, up1, down0, down1, tripUp0, tripUp1, tripDown0, tripDown1;
    };

    std::array<double, numCoefficients / 2> even_{}, odd_{};
    std::vector<Channel> channels_;
    std::vector<std::vector<float>> buffers_;
    std::vector<float*> pointers_;
    int maxBlock_ = 0, latency_ = 0;
};
} // namespace fd::dsp
