// The analysers' spectrum (devices/spectrum/SPEC.md, "DSP design"): bands, peaks, collisions,
// peak hold and level matching.
#include "fd/dsp/Spectrum.h"
#include "fd/testkit/Signals.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace fd::dsp;
using Catch::Approx;
using fd::testkit::AudioBuffer;

namespace
{
constexpr double sr = 48000.0;

/// Analyses `in` (stereo, or mono for both sides) in one go.
SpectrumAnalyser analyse(const AudioBuffer& in, SpectrumChannel channel = SpectrumChannel::Both,
                         int order = 13, bool cumulative = true, double rate = sr)
{
    SpectrumAnalyser a;
    a.prepare(order, rate);
    a.setChannel(channel);
    a.setCumulative(cumulative);
    a.push(in.channel(0), in.numChannels() > 1 ? in.channel(1) : nullptr,
           static_cast<size_t>(in.numSamples()));
    return a;
}

/// The tilted curve's spread (max - min) over 50 Hz - 15 kHz.
float spread(const SpectrumCurve& c, float slope)
{
    float lo = 1e9f, hi = -1e9f;
    for (int b = static_cast<int>(std::ceil(spectrum::bandAt(50.0)));
         b <= static_cast<int>(spectrum::bandAt(15000.0)); ++b)
    {
        const float v = c[static_cast<size_t>(b)] + spectrum::tiltDb(spectrum::bandHz(b), slope);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return hi - lo;
}

float dbfs(float amplitude)
{
    return 20.0f * std::log10(amplitude);
}
} // namespace

TEST_CASE("spectrum: bands a twelfth of an octave apart from 20 Hz, tilted around 1 kHz", "[fast]")
{
    CHECK(spectrum::bandHz(0) == Approx(20.0));
    CHECK(spectrum::bandHz(12) == Approx(40.0));
    CHECK(spectrum::bandHz(spectrum::numBands - 1) == Approx(20480.0));
    CHECK(spectrum::bandAt(spectrum::bandHz(57)) == Approx(57.0));
    CHECK(spectrum::tiltDb(1000.0, 3.0f) == Approx(0.0f).margin(1e-5));
    CHECK(spectrum::tiltDb(2000.0, 3.0f) == Approx(3.0f));
    CHECK(spectrum::tiltDb(250.0, 4.5f) == Approx(-9.0f));
}

TEST_CASE("spectrum: a sine reads its own frequency and level, whatever the band", "[fast]")
{
    // A -12 dBFS sine at 1 kHz: its peak reading is at 1 kHz (within a bin) and -12 dB (SPEC).
    const float amplitude = std::pow(10.0f, -12.0f / 20.0f);
    for (const int order : {11, 13, 14})
    {
        INFO("order " << order);
        const auto a = analyse(fd::testkit::sine(1000.0, sr, 48000, amplitude), SpectrumChannel::Both, order);
        const auto p = a.peakNear(1000.0);
        CHECK(std::abs(p.hz - 1000.0) <= sr / (1 << order));
        CHECK(p.db == Approx(-12.0f).margin(0.5f));
    }
    // Off a bin's centre too, and on one side only.
    const auto off = analyse(fd::testkit::sine(62.0, sr, 96000, 0.5f, 1), SpectrumChannel::Left, 14);
    CHECK(off.peakNear(62.0).hz == Approx(62.0).margin(0.5));
    CHECK(off.peakNear(62.0).db == Approx(dbfs(0.5f)).margin(0.5f));
}

TEST_CASE("spectrum: pink noise is flat at 3 dB per octave, at any resolution", "[fast]")
{
    const auto pink = fd::testkit::pinkNoise(48000 * 20, 7);
    SpectrumCurve curves[2];
    int i = 0;
    for (const int order : {12, 14})
    {
        const auto c = analyse(pink, SpectrumChannel::Both, order).bands();
        INFO("order " << order);
        CHECK(spread(c, 3.0f) <= 3.0f); // flat within +-1.5 dB, 50 Hz - 15 kHz
        curves[i++] = c;
    }
    // Densities: the same level at either resolution.
    const auto at1k = static_cast<size_t>(spectrum::bandAt(1000.0));
    CHECK(curves[0][at1k] == Approx(curves[1][at1k]).margin(0.5f));
}

TEST_CASE("spectrum: mid, side and both; the same signal on both sides has no side", "[fast]")
{
    const auto same = fd::testkit::sine(440.0, sr, 48000, 0.5f, 2);
    const auto side = analyse(same, SpectrumChannel::Side).bands();
    CHECK(std::all_of(side.begin(), side.end(), [](float v) { return v <= spectrum::floorDb; }));
    CHECK(analyse(same, SpectrumChannel::Mid).peakNear(440.0).db == Approx(dbfs(0.5f)).margin(0.5f));
    CHECK(analyse(same, SpectrumChannel::Both).peakNear(440.0).db == Approx(dbfs(0.5f)).margin(0.5f));

    // The right side silent: the side equals the mid (each is half the left).
    auto leftOnly = fd::testkit::pinkNoise(48000 * 4, 3);
    for (int n = 0; n < leftOnly.numSamples(); ++n)
        leftOnly.at(1, n) = 0.0f;
    const auto mid = analyse(leftOnly, SpectrumChannel::Mid).bands();
    const auto sideOnly = analyse(leftOnly, SpectrumChannel::Side).bands();
    for (int b = 10; b < 115; ++b)
        CHECK(sideOnly[static_cast<size_t>(b)] == Approx(mid[static_cast<size_t>(b)]).margin(0.5f));
    // Left and right apart.
    const auto stereo = fd::testkit::stereoSines(300.0, 3000.0, sr, 48000);
    CHECK(analyse(stereo, SpectrumChannel::Left).peakNear(3000.0).db < -60.0f);
    CHECK(analyse(stereo, SpectrumChannel::Right).peakNear(3000.0).db > -10.0f);
}

TEST_CASE("spectrum: averaging follows the signal; cumulative weighs everything alike", "[fast]")
{
    SpectrumAnalyser a;
    a.prepare(12, sr);
    a.setAveraging(100.0);
    const auto loud = fd::testkit::sine(1000.0, sr, 48000, 0.5f);
    const auto quiet = fd::testkit::sine(1000.0, sr, 48000, 0.05f);
    a.push(loud.channel(0), loud.channel(1), 48000);
    a.push(quiet.channel(0), quiet.channel(1), 48000);
    CHECK(a.peakNear(1000.0).db == Approx(dbfs(0.05f)).margin(0.5f)); // 1 s is ten time constants
    // Cumulative: the power average of both halves.
    a.reset();
    a.setCumulative(true);
    a.push(loud.channel(0), loud.channel(1), 48000);
    a.push(quiet.channel(0), quiet.channel(1), 48000);
    const float mean = 10.0f * std::log10(0.5f * (0.25f + 0.0025f));
    CHECK(a.peakNear(1000.0).db == Approx(mean).margin(0.5f));
    // Nothing until a whole FFT has arrived.
    a.prepare(14, sr);
    a.push(loud.channel(0), loud.channel(1), 1000);
    CHECK_FALSE(a.hasData());
}

TEST_CASE("spectrum: collisions where both are loud, nowhere else", "[fast]")
{
    const auto low = analyse(fd::testkit::sine(100.0, sr, 96000, 0.5f)).bands();
    const auto lowToo = analyse(fd::testkit::sine(100.0, sr, 96000, 0.25f)).bands();
    const auto high = analyse(fd::testkit::sine(5000.0, sr, 96000, 0.5f)).bands();
    const auto both = spectrumCollisions(low, lowToo, 0.0f);
    for (int b = 0; b < spectrum::numBands; ++b)
    {
        INFO("band " << b << " at " << spectrum::bandHz(b) << " Hz");
        const bool near100 =
            std::abs(spectrum::bandAt(spectrum::bandHz(b)) - spectrum::bandAt(100.0)) <= 2.0; // 1/6 octave
        if (both[static_cast<size_t>(b)])
            CHECK(near100);
    }
    CHECK(both[static_cast<size_t>(std::lround(spectrum::bandAt(100.0)))]);
    const auto apart = spectrumCollisions(low, high, 0.0f);
    CHECK(std::none_of(apart.begin(), apart.end(), [](bool c) { return c; }));
    SpectrumCurve nothing;
    nothing.fill(spectrum::floorDb);
    const auto none = spectrumCollisions(low, nothing, 0.0f);
    CHECK(std::none_of(none.begin(), none.end(), [](bool c) { return c; }));
    // Silence stays silence under the display's tilt (which lifts its floor in the highs).
    const auto tilted =
        spectrumCollisions(analyse(fd::testkit::pinkNoise(96000, 3, 0.25f)).bands(), nothing, 4.5f);
    CHECK(std::none_of(tilted.begin(), tilted.end(), [](bool c) { return c; }));
}

TEST_CASE("spectrum: peaks stand out, a third of an octave apart; noise has none", "[fast]")
{
    // Sines at 62 Hz, 440 Hz and 3.15 kHz, -12 dBFS each.
    const float amplitude = std::pow(10.0f, -12.0f / 20.0f);
    auto mix = fd::testkit::sine(62.0, sr, 48000 * 2, amplitude);
    for (const double hz : {440.0, 3150.0})
    {
        const auto s = fd::testkit::sine(hz, sr, 48000 * 2, amplitude);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < mix.numSamples(); ++n)
                mix.at(c, n) += s.at(c, n);
    }
    const auto a = analyse(mix);
    const auto peaks = spectrumPeaks(a.bands(), 3.0f, 6, -90.0f);
    REQUIRE(peaks.size() == 3);
    std::vector<double> found;
    for (const int p : peaks)
        found.push_back(a.peakNear(spectrum::bandHz(p)).hz);
    std::sort(found.begin(), found.end());
    CHECK(found[0] == Approx(62.0).margin(1.0));
    CHECK(found[1] == Approx(440.0).margin(2.0));
    CHECK(found[2] == Approx(3150.0).margin(6.0));
    CHECK(spectrumPeaks(a.bands(), 3.0f, 2, -90.0f).size() == 2); // the count is a limit

    // Pink noise, averaged: nothing stands out.
    const auto pink = analyse(fd::testkit::pinkNoise(48000 * 20, 11));
    CHECK(spectrumPeaks(pink.bands(), 3.0f, 6, -120.0f).empty());
}

TEST_CASE("spectrum: peak hold keeps the highest level, then falls at 20 dB/s", "[fast]")
{
    SpectrumCurve loud, quiet;
    loud.fill(-10.0f);
    quiet.fill(-60.0f);
    SpectrumPeakHold hold;
    hold.update(loud, 0.0, 1.0);
    hold.update(quiet, 0.5, 1.0);
    CHECK(hold.curve()[40] == Approx(-10.0f)); // held
    hold.update(quiet, 0.6, 1.0);              // past 1 s: falling
    CHECK(hold.curve()[40] == Approx(-22.0f));
    hold.update(quiet, 10.0, 1.0);
    CHECK(hold.curve()[40] == Approx(-60.0f)); // never below the signal
    SpectrumPeakHold forever;
    forever.update(loud, 0.0, -1.0);
    forever.update(quiet, 100.0, -1.0);
    CHECK(forever.curve()[40] == Approx(-10.0f));
}

TEST_CASE("spectrum: a reference is matched to the level of what plays", "[fast]")
{
    const auto pink = analyse(fd::testkit::pinkNoise(48000 * 8, 5)).bands();
    auto louder = pink;
    for (auto& v : louder)
        v += 6.0f;
    CHECK(spectrumMatchOffset(louder, pink) == Approx(-6.0f).margin(0.01f));
    SpectrumCurve nothing;
    nothing.fill(spectrum::floorDb);
    CHECK(spectrumMatchOffset(pink, nothing) == 0.0f);
}

// ---- the spectrogram (SPEC.md, stage 2) ----------------------------------------------------

namespace
{
/// Edges for `count` spans across 20 Hz–20 kHz, a span per pixel, as a display's are.
std::vector<double> pixelEdges(int count)
{
    std::vector<double> edges;
    for (int i = 0; i <= count; ++i)
        edges.push_back(20.0 * std::pow(1000.0, static_cast<double>(i) / count));
    return edges;
}
} // namespace

TEST_CASE("spectrogram: a line every hop asked for, whatever the size; each line the moment alone", "[fast]")
{
    SpectrumAnalyser a;
    a.setHop(480); // 10 ms
    a.prepare(14, sr);
    CHECK(a.hop() == 480);
    int lines = 0;
    a.onTransform = [&] { ++lines; };
    const auto in = fd::testkit::sine(1000.0, sr, 48000, 0.25f, 2);
    a.push(in.channel(0), in.channel(1), static_cast<size_t>(in.numSamples()));
    // The first line once a whole transform's worth arrived, then one every 10 ms.
    CHECK(lines == (48000 - 16384) / 480 + 1);
    a.setHop(0);
    CHECK(a.hop() == 16384 / 4);
}

TEST_CASE("spectrogram: a line reads a sine at its own level in its pixel, silence at the floor", "[fast]")
{
    SpectrumAnalyser a;
    a.setHop(480);
    a.prepare(13, sr);
    const auto edges = pixelEdges(600);
    std::vector<float> line(600);
    a.lineAt(edges, line);
    CHECK(std::all_of(line.begin(), line.end(), [](float v) { return v == spectrum::floorDb; }));

    const auto in = fd::testkit::sine(1000.0, sr, 24000, std::pow(10.0f, -12.0f / 20.0f), 2);
    a.push(in.channel(0), in.channel(1), static_cast<size_t>(in.numSamples()));
    a.lineAt(edges, line);
    const auto loudest = std::max_element(line.begin(), line.end()) - line.begin();
    const double at = 20.0 * std::pow(1000.0, (static_cast<double>(loudest) + 0.5) / 600.0);
    CHECK(std::abs(std::log2(at / 1000.0)) < 1.0 / 24.0);
    // Its loudest bin: within the window's loss between bins (Hann: up to 1.4 dB).
    CHECK(line[static_cast<size_t>(loudest)] == Approx(-12.0f).margin(1.5f));
    // An octave away it's far below.
    CHECK(line[static_cast<size_t>(loudest + 60)] < -60.0f);
    // The last transform in bands too (collisions in time).
    const auto bands = a.currentBands();
    CHECK(*std::max_element(bands.begin(), bands.end()) > -40.0f);
}

TEST_CASE("spectrogram: a click in silence lights the lines around it only", "[fast]")
{
    SpectrumAnalyser a;
    a.setHop(480);
    a.prepare(11, sr); // 2048 points: about 43 ms
    const auto edges = pixelEdges(200);
    std::vector<float> line(200);
    std::vector<float> loudness;
    a.onTransform = [&]
    {
        a.lineAt(edges, line);
        loudness.push_back(*std::max_element(line.begin(), line.end()));
    };
    AudioBuffer in(2, 48000);
    in.at(0, 24000) = in.at(1, 24000) = 0.5f;
    a.push(in.channel(0), in.channel(1), 48000);
    // Lit while the click is inside the window: about 2048 / 480 lines, dark before and after.
    const auto lit = std::count_if(loudness.begin(), loudness.end(), [](float v) { return v > -100.0f; });
    CHECK(lit >= 3);
    CHECK(lit <= 5);
    CHECK(loudness.front() == spectrum::floorDb);
    CHECK(loudness.back() == spectrum::floorDb);
}

TEST_CASE("spectrogram history: rows of the loudest lines in their time, the newest first; collisions kept",
          "[fast]")
{
    SpectrogramHistory h;
    h.prepare(4, 3, 0.02); // two 10 ms lines a row
    CHECK(h.filled() == 0);
    const std::vector<float> quiet{-60.0f, -60.0f, -60.0f, -60.0f}, loud{-60.0f, -10.0f, -60.0f, -60.0f};
    const std::vector<std::uint8_t> none, at3{0, 0, 0, 1};
    h.add(quiet, none, 0.01);
    h.add(loud, at3, 0.01);
    CHECK(h.filled() == 1);
    CHECK(h.levels(0)[1] == -10.0f); // the loudest of the row's lines
    CHECK(h.collisions(0)[3] == 1);
    h.add(quiet, none, 0.01); // the row's time is up: a new one
    CHECK(h.filled() == 2);
    CHECK(h.levels(0)[1] == -60.0f);
    CHECK(h.levels(1)[1] == -10.0f); // one row older
    CHECK(h.collisions(0)[3] == 0);
    // Past its rows, the oldest goes.
    for (int i = 0; i < 6; ++i)
        h.add(quiet, none, 0.01);
    CHECK(h.filled() == 3);
    CHECK(std::all_of(h.levels(2).begin(), h.levels(2).end(), [](float v) { return v == -60.0f; }));
    CHECK(h.started() == 5);
    const auto before = h.version();
    h.clear();
    CHECK(h.filled() == 0);
    CHECK(h.version() != before);
}
