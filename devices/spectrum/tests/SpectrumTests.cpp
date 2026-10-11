// Spectrum (devices/spectrum/SPEC.md): the audio passes unchanged, the taps carry the input and
// the compared track, the curves it keeps, a reference file's average, and the labels.
#include "Spectrum.h"
#include "fd/audiofile/AudioFile.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <vector>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using S = fd::devices::Spectrum;
using dsp::SpectrumChannel;

namespace
{
constexpr double sr = 48000.0;

/// Everything in FIFO `f`.
std::vector<float> drain(S& s, int f)
{
    std::vector<float> out(s.analysis().fifo(f).available());
    out.resize(s.analysis().fifo(f).pop(out.data(), out.size()));
    return out;
}

/// A temporary WAV of `audio`, removed when it goes.
struct TempWav
{
    explicit TempWav(const AudioBuffer& audio, double rate = sr)
    {
        path =
            (std::filesystem::temp_directory_path() / ("fd-spectrum-" + std::to_string(std::rand()) + ".wav"))
                .string();
        std::vector<std::vector<float>> channels;
        for (int c = 0; c < audio.numChannels(); ++c)
            channels.emplace_back(audio.channel(c), audio.channel(c) + audio.numSamples());
        REQUIRE(audiofile::writeWav(path, channels, rate));
    }
    ~TempWav() { std::filesystem::remove(path); }
    std::string path;
};

/// The curve's spread (max - min) after `slope` over 50 Hz - 15 kHz.
float spread(const dsp::SpectrumCurve& c, float slope)
{
    float lo = 1e9f, hi = -1e9f;
    for (int b = static_cast<int>(std::ceil(dsp::spectrum::bandAt(50.0)));
         b <= static_cast<int>(dsp::spectrum::bandAt(15000.0)); ++b)
    {
        const float v = c[static_cast<size_t>(b)] + dsp::spectrum::tiltDb(dsp::spectrum::bandHz(b), slope);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return hi - lo;
}
} // namespace

TEST_CASE("spectrum: the audio passes unchanged, with no latency", "[fast]")
{
    S s;
    const auto in = stereoSines(440.0, 660.0, sr, 48000);
    CHECK(nullResidualDb(render(s, in, {.sampleRate = sr}), in) == silenceDb);
    CHECK(nullResidualDb(render(s, in, {.sampleRate = sr, .inPlace = true}), in) == silenceDb);
    CHECK(s.latencySamples() == 0);
    CHECK(s.tailSeconds() == 0.0);
}

TEST_CASE("spectrum: the taps carry the input and, while connected, the compared track", "[fast]")
{
    S s;
    const auto in = stereoSines(440.0, 660.0, sr, 4800);
    render(s, in, {.sampleRate = sr});
    CHECK(s.analysis().sampleRate() == sr);
    const auto left = drain(s, S::InputLeft), right = drain(s, S::InputRight);
    REQUIRE(left.size() == 4800);
    REQUIRE(right.size() == 4800);
    CHECK(left[100] == in.at(0, 100));
    CHECK(right[100] == in.at(1, 100));
    CHECK(s.analysis().fifo(S::CompareLeft).available() == 0); // nothing compared

    const auto key = sine(100.0, sr, 4800, 0.25f);
    render(s, in, {.sampleRate = sr, .sidechain = &key});
    const auto compared = drain(s, S::CompareLeft);
    REQUIRE(compared.size() == 4800);
    CHECK(compared[100] == key.at(0, 100));
    CHECK(drain(s, S::CompareRight).size() == 4800);
}

TEST_CASE("spectrum: the snapshot and reference are kept, cleared by a patch, and survive bad state",
          "[fast]")
{
    S s;
    CHECK_FALSE(s.snapshot());
    CHECK_FALSE(s.reference());
    dsp::SpectrumCurve curve;
    for (size_t b = 0; b < curve.size(); ++b)
        curve[b] = -40.0f - 0.1f * static_cast<float>(b);
    S::Reference ref{"Song.wav", "/music/Song.wav", {}};
    for (auto& c : ref.curves)
        c = curve;
    ref.curves[4].fill(dsp::spectrum::floorDb); // a mono song has no side

    // As the binding applies patches (ADR 0017): the state merged, then read back.
    auto apply = [&s](const nlohmann::json& patch)
    {
        nlohmann::json extra = nlohmann::json::object();
        s.writeExtraState(extra);
        extra.merge_patch(patch);
        s.readExtraState(extra);
    };
    apply(S::snapshotPatch(S::Snapshot{curve, 3}));
    apply(S::referencePatch(ref));
    REQUIRE(s.snapshot());
    CHECK(s.snapshot()->channel == 3);
    CHECK(s.snapshot()->curve[60] == Approx(curve[60]).margin(0.01f));
    REQUIRE(s.reference());
    CHECK(s.reference()->name == "Song.wav");
    CHECK(s.reference()->curves[4][60] == dsp::spectrum::floorDb);

    // A copy reopened elsewhere has the same curves, the file or not (they're stored).
    nlohmann::json saved = nlohmann::json::object();
    s.writeExtraState(saved);
    S reopened;
    reopened.readExtraState(saved);
    REQUIRE(reopened.reference());
    CHECK(reopened.reference()->curves[0][60] == Approx(curve[60]).margin(0.01f));

    apply(S::snapshotPatch(std::nullopt));
    CHECK_FALSE(s.snapshot());
    CHECK(s.reference()); // the other stays
    apply(S::referencePatch(std::nullopt));
    CHECK_FALSE(s.reference());

    // State that doesn't fit is left out, never half read.
    s.readExtraState({{"snapshot.curve", {1.0, 2.0}}, {"reference.curves", {{1.0}}}});
    CHECK_FALSE(s.snapshot());
    CHECK_FALSE(s.reference());
}

TEST_CASE("spectrum: a reference file's average, for each channel choice", "[fast]")
{
    // Pink noise: flat at 3 dB per octave (SPEC).
    const auto pink = pinkNoise(48000 * 12, 21);
    const TempWav file(pink);
    std::string error;
    const auto curves = devices::analyseFile(file.path, &error);
    REQUIRE(curves);
    CHECK(error.empty());
    CHECK(spread((*curves)[static_cast<size_t>(SpectrumChannel::Both)], 3.0f) <= 2.0f); // +-1 dB

    // The same audio live and as the reference: the curves coincide once matched (SPEC).
    dsp::SpectrumAnalyser live;
    live.prepare(S::fftOrder(2.0f), sr);
    live.setAveraging(5000.0);
    live.push(pink.channel(0), pink.channel(1), static_cast<size_t>(pink.numSamples()));
    const auto now = live.bands();
    const auto& ref = (*curves)[static_cast<size_t>(SpectrumChannel::Both)];
    const float offset = dsp::spectrumMatchOffset(ref, now);
    for (int b = static_cast<int>(dsp::spectrum::bandAt(50.0)) + 1;
         b <= static_cast<int>(dsp::spectrum::bandAt(15000.0)); ++b)
        CHECK(ref[static_cast<size_t>(b)] + offset == Approx(now[static_cast<size_t>(b)]).margin(1.0f));

    // A mono file: both sides the same, so no side.
    const auto mono = sine(1000.0, sr, 48000, 0.5f, 1);
    const TempWav monoFile(mono);
    const auto monoCurves = devices::analyseFile(monoFile.path);
    REQUIRE(monoCurves);
    const auto& side = (*monoCurves)[static_cast<size_t>(SpectrumChannel::Side)];
    CHECK(std::all_of(side.begin(), side.end(), [](float v) { return v <= dsp::spectrum::floorDb; }));

    // Unreadable, too short, or cancelled: nothing.
    CHECK_FALSE(devices::analyseFile("/no/such/file.wav", &error));
    CHECK_FALSE(error.empty());
    const TempWav tiny(sine(1000.0, sr, 100, 0.5f));
    CHECK_FALSE(devices::analyseFile(tiny.path, &error));
    const std::atomic<bool> cancelled{true};
    CHECK_FALSE(devices::analyseFile(file.path, nullptr, &cancelled));
}

TEST_CASE("spectrum: labels read frequency and note as everywhere in Segno", "[fast]")
{
    CHECK(devices::peakLabel(62.0) == "62 Hz \xC2\xB7 B0");
    CHECK(devices::peakLabel(440.0) == "440 Hz \xC2\xB7 A3");
    CHECK(devices::peakLabel(3150.0) == "3.15 kHz \xC2\xB7 G6");
    CHECK(devices::noteText(261.63) == "C3"); // middle C
}

TEST_CASE("spectrum: the display settings from the parameters", "[fast]")
{
    CHECK(S::fftOrder(0.0f) == 11);
    CHECK(S::fftOrder(3.0f) == 14);
    CHECK_FALSE(S::holdSeconds(0.0f));
    CHECK(*S::holdSeconds(2.0f) == 3.0);
    CHECK(*S::holdSeconds(3.0f) < 0.0);
    CHECK(S::rangeDb(1.0f) == 90.0f);
    CHECK(S::labelCount(1.0f) == 3);
    // The defaults (SPEC): L+R, 8192 points, 300 ms, 3 s, 3 dB/oct, 90 dB, no side, 3 labels.
    S s;
    CHECK(s.params().get(S::Resolution) == 2.0f);
    CHECK(s.params().get(S::Average) == 300.0f);
    CHECK(S::slopeDb(s.params().get(S::Slope)) == 3.0f);
    CHECK(S::slopeDb(3.0f) == 4.5f);
    CHECK(S::labelCount(s.params().get(S::Labels)) == 3);
    CHECK_FALSE(s.params().getBool(S::Side));
    for (const auto& p : S::staticInfo().params)
        CHECK_FALSE(p.hasFlag(ParamFlags::Automatable)); // display settings
}
