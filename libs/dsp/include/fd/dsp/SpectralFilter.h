#pragma once

#include "fd/dsp/Fft.h"

#include <complex>
#include <vector>

namespace fd::dsp
{
/// Stereo audio through a response that may change frame by frame, with no phase change (the
/// EQ's spectral engine: linear phase and spectral bands, devices/eq/SPEC.md, "Stage 2").
///
/// Frames of M samples (a Hann window, half overlapping) are padded to N and multiplied by a
/// linear-phase kernel L long made from the frame's response, then overlap-added: an exact
/// convolution while the response holds, a crossfade over a frame when it moves. M is N/4 and L is
/// 3N/4, so a frame's convolution never wraps; the latency is M + L/2 (5N/8).
///
/// The response is a 2x2 matrix of real gains per bin (left and right out from left and right in),
/// so left/right and mid/side placements share one frame. Real-time safe after prepare().
class SpectralFilter
{
public:
    /// What a frame's response is made from, and where it goes (all arrays bins() long).
    struct Frame
    {
        /// The response to fill: out left = ll * in left + lr * in right; out right = rl * in left +
        /// rr * in right. They come as the last frame left them.
        float* ll = nullptr;
        float* lr = nullptr;
        float* rl = nullptr;
        float* rr = nullptr;
        int bins = 0;

        /// The frame's input spectra (Hann-windowed, padded): bin k is k * sampleRate / N hertz.
        /// Made when first asked for (a frame whose response is the identity never needs them).
        [[nodiscard]] const std::complex<float>* left() const noexcept
        {
            return filter->spectra(), filter->spectrumL_.data();
        }
        [[nodiscard]] const std::complex<float>* right() const noexcept
        {
            return filter->spectra(), filter->spectrumR_.data();
        }

        SpectralFilter* filter = nullptr;
    };

    /// May allocate: room for FFTs up to 2^maxOrder points.
    void prepare(int maxOrder);
    /// The size to run at (an FFT of 2^order points, at most the prepared one). Resets.
    void setOrder(int order) noexcept;
    /// Clears the audio and sets the response to identity.
    void reset() noexcept;

    [[nodiscard]] int fftSize() const noexcept { return n_; }
    [[nodiscard]] int bins() const noexcept { return n_ / 2 + 1; }
    [[nodiscard]] int frameLength() const noexcept { return m_; }
    [[nodiscard]] int hop() const noexcept { return m_ / 2; }
    [[nodiscard]] int kernelLength() const noexcept { return l_; }
    [[nodiscard]] int latencySamples() const noexcept { return m_ + l_ / 2; }
    /// Samples taken in since the last reset: inside onFrame, where the frame ends.
    [[nodiscard]] long long samplesIn() const noexcept { return written_; }

    /// The spectra of another signal's frame, made as the engine's own are (to detect on a
    /// different signal): `left` and `right` are rings of `mask + 1` samples (a power of two, at
    /// least frameLength()) holding sample i at i & mask; the frame ends before sample `end`. The
    /// spectra are bins() long. Safe inside onFrame.
    void analyse(const float* left, const float* right, long long mask, long long end,
                 std::complex<float>* outLeft, std::complex<float>* outRight) noexcept;

    /// Filters `left` and `right` in place, `n` samples; `onFrame(Frame&)` fills each frame's
    /// response at the frame's end (every hop() samples) and returns whether it changed.
    template <typename OnFrame> void process(float* left, float* right, int n, OnFrame&& onFrame) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto w = static_cast<size_t>(written_ & ringMask_);
            inL_[w] = left[i];
            inR_[w] = right[i];
            ++written_;
            // A frame every hop from the first (silence before the start: the ring holds zeros),
            // so every sample is in two frames whose windows add to one.
            if (++sinceFrame_ == hop())
            {
                sinceFrame_ = 0;
                analysed_ = false;
                Frame f{ll_.data(), lr_.data(), rl_.data(), rr_.data(), bins(), this};
                if (onFrame(f) || !kernelsValid_)
                    designKernels();
                convolve();
            }
            // Out, M samples behind the newest frame's start (see the class comment).
            const auto r = static_cast<size_t>((written_ - 1 - m_) & outMask_);
            left[i] = outL_[r];
            right[i] = outR_[r];
            outL_[r] = outR_[r] = 0.0f;
        }
    }

private:
    /// The newest frame's spectra, once per frame.
    void spectra() noexcept
    {
        if (!analysed_)
            analyse(inL_.data(), inR_.data(), ringMask_, written_, spectrumL_.data(), spectrumR_.data());
        analysed_ = true;
    }
    void designKernels() noexcept; // the response as linear-phase kernels
    void convolve() noexcept;      // the frame through them, added to the output
    /// A real, zero-phase response (bins() gains) as a linear-phase kernel's spectrum (N bins).
    void kernelOf(const float* gains, std::vector<std::complex<float>>& kernel) noexcept;

    int maxOrder_ = 0, n_ = 0, m_ = 0, l_ = 0;
    std::vector<Fft> ffts_; // by order
    const Fft* fft_ = nullptr;
    std::vector<float> window_, taper_;
    std::vector<float> inL_, inR_, outL_, outR_;
    long long written_ = 0, ringMask_ = 0, outMask_ = 0;
    int sinceFrame_ = 0;
    std::vector<std::complex<float>> work_, spectrumL_, spectrumR_;
    std::vector<float> ll_, lr_, rl_, rr_;
    std::vector<std::complex<float>> kll_, klr_, krl_, krr_;
    bool kernelsValid_ = false, diagonal_ = true, same_ = true, analysed_ = false;
    bool identity_ = true; // every gain one: the frame is only delayed (exact, and cheap)
};
} // namespace fd::dsp
