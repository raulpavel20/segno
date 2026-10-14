#include "Sampler.h"
#include "fd/device/Library.h"
#include "fd/device/State.h"
#include "fd/dsp/Math.h"
#include "fd/testkit/DeviceRender.h"
#include "fd/testkit/Golden.h"
#include "fd/testkit/Metrics.h"
#include "fd/testkit/Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>

using namespace fd;
using namespace fd::testkit;
using Catch::Approx;
using S = fd::devices::Sampler;

namespace
{
constexpr double sr = 48000.0;

/// A WAV written to the temp directory for one test, removed afterwards.
struct TempSample
{
    std::string path;
    TempSample(const char* name, int frames, const std::function<float(int)>& fn, double rate = sr,
               std::optional<int> root = std::nullopt)
        : path((std::filesystem::temp_directory_path() / name).string())
    {
        std::vector<std::vector<float>> ch(1, std::vector<float>(static_cast<size_t>(frames)));
        for (int i = 0; i < frames; ++i)
            ch[0][static_cast<size_t>(i)] = fn(i);
        REQUIRE(audiofile::writeWav(path, ch, rate, root));
    }
    ~TempSample() { std::filesystem::remove(path); }
};

void load(S& s, const std::string& path)
{
    s.readExtraState({{"sample.path", path}});
    REQUIRE(s.sample() != nullptr);
}

TimedEvent noteOn(std::int64_t at, int key, float vel = 1.0f)
{
    return {at, {.type = EventType::NoteOn, .key = static_cast<std::uint8_t>(key), .value = vel}};
}
TimedEvent noteOff(std::int64_t at, int key)
{
    return {at, {.type = EventType::NoteOff, .key = static_cast<std::uint8_t>(key)}};
}

AudioBuffer play(S& s, std::vector<TimedEvent> events, int samples, std::function<void(Device&, std::int64_t)> before = {})
{
    RenderOptions o{.sampleRate = sr};
    o.events = std::move(events);
    o.beforeBlock = std::move(before);
    return render(s, silence(samples, 0), o);
}

/// Frequency by linearly interpolated rising zero crossings.
double estimateFrequency(const AudioBuffer& b, int start, int length)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = start + 1; i < start + length; ++i)
    {
        const float a = b.at(0, i - 1), c = b.at(0, i);
        if (a <= 0.0f && c > 0.0f)
        {
            const double t = (i - 1) + a / (a - c);
            if (first < 0.0)
                first = t;
            else
                ++count;
            last = t;
        }
    }
    return count > 0 ? count * sr / (last - first) : 0.0;
}

float sineAt(double hz, int i, double rate = sr)
{
    return static_cast<float>(0.5 * std::sin(dsp::twoPi * hz * i / rate));
}
} // namespace

TEST_CASE("sampler pitch is accurate across +-24 semitones", "[fast]")
{
    // Spec: +-0.5 cent. A 44.1 kHz file also checks the sample-rate conversion.
    TempSample file("fd-sampler-pitch.wav", 4 * 44100, [](int i) { return sineAt(440.0, i, 44100.0); }, 44100.0);
    for (int shift : {-24, -7, 0, 5, 12, 24})
    {
        S s;
        s.params().set(S::Root, 69.0f);
        load(s, file.path);
        const auto out = play(s, {noteOn(0, 69 + shift)}, 48000);
        const double expected = 440.0 * std::exp2(shift / 12.0);
        const double f = estimateFrequency(out, 4800, 38400);
        INFO("shift " << shift << ": " << f << " Hz");
        CHECK(1200.0 * std::log2(f / expected) == Approx(0.0).margin(0.5));
    }
}

TEST_CASE("sampler transposed up two octaves keeps the band and removes aliases", "[fast]")
{
    // Spec: aliasing < -70 dB transposed up 24 st. A 9 kHz tone would land at 36 kHz (it must
    // vanish); a 4 kHz tone lands at 16 kHz (it must stay).
    TempSample high("fd-sampler-alias-high.wav", 96000, [](int i) { return sineAt(9000.0, i); });
    TempSample low("fd-sampler-alias-low.wav", 96000, [](int i) { return sineAt(4000.0, i); });
    auto level = [](const std::string& path) {
        S s;
        s.params().set(S::FadeIn, 0.0f);
        load(s, path);
        const auto out = play(s, {noteOn(0, 60 + 24)}, 12000);
        // Sine RMS -> amplitude, relative to the 0.5 source (peaks of a 16 kHz sine sampled at
        // 48 kHz miss the crest).
        return rmsDb(out.slice(1000, 8000)) + 3.0103 + 20.0 * std::log10(2.0);
    };
    CHECK(level(high.path) < -70.0);
    CHECK(level(low.path) == Approx(0.0).margin(0.5));
}

TEST_CASE("sampler loop seam is smooth with crossfade", "[fast]")
{
    // A tone whose period does not divide the loop: without a crossfade the seam would jump.
    TempSample file("fd-sampler-loop.wav", 48000, [](int i) { return sineAt(233.3, i); });
    S s;
    s.params().set(S::Loop, 1.0f);
    s.params().set(S::LoopStart, 30.0f);
    s.params().set(S::LoopXfade, 10.0f);
    s.params().set(S::End, 50.0f); // loop 0.3..0.5 of the file: 7200..24000 frames
    load(s, file.path);
    const auto out = play(s, {noteOn(0, 60)}, 96000); // several passes through the seam
    CHECK(peak(out.slice(30000, 60000)) > 0.3);         // still playing (looping)
    CHECK(clickScore(out.slice(2000, 90000), 0) < 8.0);
}

TEST_CASE("one-shot plays to the end and ignores note-off", "[fast]")
{
    TempSample file("fd-sampler-oneshot.wav", 10000, [](int i) { return sineAt(500.0, i) + 0.1f; });
    S s;
    s.params().set(S::Mode, 1.0f); // One-shot
    s.params().set(S::FadeOut, 0.0f);
    load(s, file.path);
    const auto out = play(s, {noteOn(0, 60), noteOff(100, 60)}, 20000);
    int last = -1;
    for (int i = 0; i < out.numSamples(); ++i)
        if (std::abs(out.at(0, i)) > 1e-4f)
            last = i;
    CHECK(last == Approx(9999).margin(2)); // exactly the sample's length at the root key

    s.params().set(S::OneShotGate, 1.0f); // with Gate on, note-off fades it out
    const auto gated = play(s, {noteOn(0, 60), noteOff(100, 60)}, 20000);
    CHECK(peakDb(gated.slice(5000, 5000)) < -100.0);
}

TEST_CASE("swapping the sample while playing fades without clicks", "[fast]")
{
    TempSample a("fd-sampler-swap-a.wav", 96000, [](int i) { return sineAt(300.0, i); });
    TempSample b("fd-sampler-swap-b.wav", 96000, [](int i) { return sineAt(450.0, i) * -1.0f; });
    S s;
    load(s, a.path);
    const auto out = play(s, {noteOn(0, 60)}, 48000, [&](Device& d, std::int64_t at) {
        if (at == 24064)
            d.readExtraState({{"sample.path", b.path}}); // as a UI drop would, between blocks
    });
    CHECK(peak(out.slice(20000, 4000)) > 0.3);    // A played
    CHECK(peakDb(out.slice(26000, 4000)) < -100.0); // A faded out; the held note ends
    CHECK(clickScore(out.slice(1000, 46000), 0) < 8.0);
    CHECK(s.sample()->path == b.path);
}

TEST_CASE("sampler state keeps the sample path; missing files are reported", "[fast]")
{
    TempSample file("fd-sampler-state.wav", 4800, [](int i) { return sineAt(220.0, i); }, sr, 57);
    S s;
    load(s, file.path);
    CHECK(s.sample()->rootKey == 57);
    nlohmann::json extra = nlohmann::json::object();
    s.writeExtraState(extra);
    CHECK(extra["sample.path"] == file.path);
    CHECK(extra["sample.frames"] == 4800);

    S restored;
    restored.readExtraState(extra);
    REQUIRE(restored.sample() != nullptr);
    CHECK(restored.sample()->frames == 4800);

    S missing;
    missing.readExtraState({{"sample.path", "/nonexistent/fd-missing.wav"}});
    CHECK(missing.sample() == nullptr);
    CHECK_FALSE(missing.loadError().empty());
    nlohmann::json kept = nlohmann::json::object();
    missing.writeExtraState(kept);
    CHECK(kept["sample.path"] == "/nonexistent/fd-missing.wav"); // the project still references it
    CHECK(peak(play(missing, {noteOn(0, 60)}, 4800)) == 0.0f);
}

TEST_CASE(
    "sampler: a sound in the Library is stored by its place in it, found where the Library is, said when "
    "missing (ADR 0055)",
    "[fast]")
{
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "fd-sampler-library";
    fs::remove_all(dir);
    fs::create_directories(dir / "Library" / "Samples");
    const auto wav = (dir / "Library" / "Samples" / "Tone.wav").string();
    REQUIRE(audiofile::writeWav(wav, {std::vector<float>(4800, 0.25f)}, sr, std::nullopt));
    library::setRoot(dir / "Library");
    const auto stored = library::reference(wav);
    REQUIRE(stored == "library:Samples/Tone.wav");

    S s;
    load(s, stored);
    CHECK(s.sample()->frames == 4800);
    nlohmann::json extra = nlohmann::json::object();
    s.writeExtraState(extra);
    CHECK(extra["sample.path"] == stored); // travels between machines

    // Elsewhere on another machine: the same reference finds it.
    fs::rename(dir / "Library", dir / "Moved");
    library::setRoot(dir / "Moved");
    S moved;
    load(moved, stored);

    // Not there: it says so, keeps the reference, and plays silence.
    library::setRoot(dir / "Gone");
    S missing;
    missing.readExtraState({{"sample.path", stored}});
    CHECK(missing.sample() == nullptr);
    CHECK(missing.loadError().find("Samples/Tone.wav") != std::string::npos);
    CHECK(missing.loadError().find("Library") != std::string::npos);
    nlohmann::json kept = nlohmann::json::object();
    missing.writeExtraState(kept);
    CHECK(kept["sample.path"] == stored);
    CHECK(peak(play(missing, {noteOn(0, 60)}, 4800)) == 0.0f);
    library::setRoot({});
    fs::remove_all(dir);
}

TEST_CASE("Sampler patches match golden metrics", "[fast]")
{
    // A rich decaying tone, played at the root, two octaves up and down, looped, and filtered:
    // level per stage and the octave-band balance of the sustain.
    TempSample file("fd-sampler-golden.wav", 48000, [](int i) {
        const double t = i / sr;
        double x = 0.0;
        for (int h = 1; h <= 12; ++h)
            x += std::sin(dsp::twoPi * 110.0 * h * t) / h;
        return static_cast<float>(0.3 * x * std::exp(-t * 2.0));
    });
    struct Patch
    {
        const char* name;
        int key;
        std::vector<std::pair<S::Param, float>> settings;
    };
    const std::vector<Patch> patches{
        {"root", 60, {}},
        {"up-24", 84, {}},
        {"down-24", 36, {}},
        {"loop", 60, {{S::Loop, 1.0f}, {S::LoopStart, 50.0f}, {S::End, 10.0f}, {S::LoopXfade, 20.0f}}}, // loops from 50 ms
        {"filtered", 60, {{S::FilterOn, 1.0f}, {S::FilterCutoff, 600.0f}, {S::FilterRes, 60.0f}, {S::FilterEnv, 40.0f}}},
    };
    nlohmann::json metrics;
    for (const auto& patch : patches)
    {
        S s;
        for (auto [p, v] : patch.settings)
            s.params().set(p, v);
        load(s, file.path);
        // Windows fit in the first 0.25 s: two octaves up, the 1 s sample lasts that long.
        const auto out = play(s, {noteOn(0, patch.key), noteOff(10600, patch.key)}, 24000);
        const std::string k = patch.name;
        metrics[k + ".attack-rms-db"] = rmsDb(out.slice(0, 2400));
        metrics[k + ".sustain-rms-db"] = rmsDb(out.slice(2400, 8192));
        metrics[k + ".release-rms-db"] = rmsDb(out.slice(10600 + 600, 1200));
        const auto power = powerSpectrum(out, 0, 2400, 8192);
        double total = 0.0;
        for (double p : power)
            total += p;
        for (double centre = 125.0; centre <= 8000.0; centre *= 2.0)
        {
            double band = 0.0;
            for (size_t i = 0; i < power.size(); ++i)
            {
                const double f = static_cast<double>(i) * sr / 8192.0;
                if (f >= centre / std::sqrt(2.0) && f < centre * std::sqrt(2.0))
                    band += power[i];
            }
            metrics[k + ".band-" + std::to_string(static_cast<int>(centre)) + "-db"] =
                10.0 * std::log10(std::max(band, 1e-30) / std::max(total, 1e-30));
        }
    }
    const auto result = checkGolden(std::string(FD_GOLDEN_DIR) + "/sampler/patches.json", metrics, 0.5);
    INFO(result.report);
    CHECK(result.passed);
}
