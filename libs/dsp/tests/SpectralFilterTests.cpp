// The spectral engine (EQ stage 2): a response that may change every frame, with no phase change.
#include "fd/dsp/Filters.h"
#include "fd/dsp/MatchedFilters.h"
#include "fd/dsp/Math.h"
#include "fd/dsp/SpectralFilter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

using namespace fd::dsp;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;

struct Stereo
{
    std::vector<float> l, r;
};

Stereo noise(size_t n, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    for (size_t i = 0; i < n; ++i)
        s.l[i] = u(rng), s.r[i] = u(rng);
    return s;
}

/// Runs `in` through `f` in uneven blocks, the response set by `respond` every frame.
template <typename Respond> Stereo run(SpectralFilter& f, Stereo in, Respond&& respond)
{
    size_t at = 0;
    for (int block = 1; at < in.l.size(); block = block * 7 % 1021 + 1)
    {
        const auto n = std::min(in.l.size() - at, static_cast<size_t>(block));
        f.process(in.l.data() + at, in.r.data() + at, static_cast<int>(n), respond);
        at += n;
    }
    return in;
}

double worstDb(const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double worst = 0.0;
    for (size_t i = from; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    return worst > 0.0 ? 20.0 * std::log10(worst) : -300.0;
}
} // namespace

TEST_CASE("spectral filter: as built, its latency is M + L/2, for every size", "[fast]")
{
    for (const int order : {12, 13, 14})
    {
        SpectralFilter f;
        f.prepare(14);
        f.setOrder(order);
        const int n = 1 << order;
        CHECK(f.fftSize() == n);
        CHECK(f.frameLength() == n / 4);
        CHECK(f.kernelLength() == 3 * n / 4);
        CHECK(f.latencySamples() == n / 4 + 3 * n / 8);
        // An impulse comes out exactly that late.
        Stereo in{std::vector<float>(static_cast<size_t>(3 * n)),
                  std::vector<float>(static_cast<size_t>(3 * n))};
        in.l[100] = in.r[100] = 1.0f;
        const auto out = run(f, in, [](SpectralFilter::Frame&) { return false; });
        const auto peak = std::max_element(out.l.begin(), out.l.end(),
                                           [](float a, float b) { return std::abs(a) < std::abs(b); });
        CHECK(peak - out.l.begin() == 100 + f.latencySamples());
        CHECK(*peak == Approx(1.0f).margin(1e-4f));
    }
}

TEST_CASE("spectral filter: the identity is the input, delayed; a gain, a swap", "[fast]")
{
    SpectralFilter f;
    f.prepare(13);
    const auto in = noise(48000, 3);
    const auto d = static_cast<size_t>(f.latencySamples());
    const auto delayed = [&](const std::vector<float>& x)
    {
        std::vector<float> y(x.size(), 0.0f);
        for (size_t i = d; i < x.size(); ++i)
            y[i] = x[i - d];
        return y;
    };
    {
        const auto out = run(f, in, [](SpectralFilter::Frame&) { return false; });
        CHECK(worstDb(out.l, delayed(in.l), 0) < -120.0);
        CHECK(worstDb(out.r, delayed(in.r), 0) < -120.0);
    }
    {
        f.reset();
        const auto out = run(f, in,
                             [](SpectralFilter::Frame& fr)
                             {
                                 std::fill(fr.ll, fr.ll + fr.bins, 0.0f);
                                 std::fill(fr.rr, fr.rr + fr.bins, 0.0f);
                                 std::fill(fr.lr, fr.lr + fr.bins, 1.0f);
                                 std::fill(fr.rl, fr.rl + fr.bins, 1.0f);
                                 return true;
                             });
        CHECK(worstDb(out.l, delayed(in.r), d + 8192) < -100.0); // left out is right in
        CHECK(worstDb(out.r, delayed(in.l), d + 8192) < -100.0);
    }
    {
        f.reset();
        const auto out = run(f, in,
                             [](SpectralFilter::Frame& fr)
                             {
                                 std::fill(fr.ll, fr.ll + fr.bins, 0.5f);
                                 std::fill(fr.rr, fr.rr + fr.bins, 0.5f);
                                 return true;
                             });
        auto half = delayed(in.l);
        for (auto& v : half)
            v *= 0.5f;
        CHECK(worstDb(out.l, half, d + 8192) < -100.0);
    }
}

TEST_CASE("spectral filter: an EQ's bell, with no phase change, as designed", "[fast]")
{
    SpectralFilter f;
    f.prepare(13); // N = 8192 at 48 kHz: the EQ's Medium
    const auto bell = designMatched(MatchedType::Bell, 1000.0, 6.0, 1.0, sr);
    const auto respond = [&](SpectralFilter::Frame& fr)
    {
        for (int k = 0; k < fr.bins; ++k)
        {
            const double hz = k * sr / f.fftSize();
            fr.ll[k] = fr.rr[k] = static_cast<float>(std::abs(biquadResponse(bell, std::max(hz, 1.0), sr)));
        }
        return true;
    };
    // The impulse response: symmetric about the latency (linear phase).
    Stereo impulse{std::vector<float>(32768), std::vector<float>(32768)};
    impulse.l[0] = impulse.r[0] = 1.0f;
    const auto ir = run(f, impulse, respond);
    const auto c = static_cast<size_t>(f.latencySamples());
    double asym = 0.0;
    for (size_t k = 1; k < 2000; ++k)
        asym = std::max(asym, static_cast<double>(std::abs(ir.l[c + k] - ir.l[c - k])));
    CHECK(asym < 1e-5);
    // Its magnitude where the design puts it, within 0.1 dB from 40 Hz up.
    for (const double hz : {40.0, 100.0, 300.0, 700.0, 1000.0, 1500.0, 4000.0, 12000.0})
    {
        std::complex<double> h{};
        for (size_t i = 0; i < ir.l.size(); ++i)
            h += static_cast<double>(ir.l[i]) * std::polar(1.0, -twoPi * hz * static_cast<double>(i) / sr);
        INFO(hz << " Hz");
        CHECK(20.0 * std::log10(std::abs(h)) ==
              Approx(20.0 * std::log10(std::abs(biquadResponse(bell, hz, sr)))).margin(0.1));
    }
}

TEST_CASE("spectral filter: a response moving every frame moves smoothly", "[fast]")
{
    SpectralFilter f;
    f.prepare(12);
    // A steady sine, its gain switched between 1 and 0.25 every frame: no step bigger than the
    // sine's own from sample to sample.
    Stereo in{std::vector<float>(48000), std::vector<float>(48000)};
    for (size_t i = 0; i < in.l.size(); ++i)
        in.l[i] = in.r[i] = static_cast<float>(0.5 * std::sin(twoPi * 200.0 * static_cast<double>(i) / sr));
    bool up = true;
    const auto out = run(f, in,
                         [&up](SpectralFilter::Frame& fr)
                         {
                             up = !up;
                             std::fill(fr.ll, fr.ll + fr.bins, up ? 1.0f : 0.25f);
                             std::fill(fr.rr, fr.rr + fr.bins, up ? 1.0f : 0.25f);
                             return true;
                         });
    const double sineStep = 0.5 * twoPi * 200.0 / sr;
    double worst = 0.0;
    for (size_t i = 8192; i < out.l.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(out.l[i] - out.l[i - 1])));
    CHECK(worst < sineStep * 1.1);
}
