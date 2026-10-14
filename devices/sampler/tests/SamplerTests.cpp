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
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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

// ---- stage 2: multisamples (SPEC, "Stage 2: Multisample") ----------------------------------

namespace
{
namespace fs = std::filesystem;
using Map = fd::devices::SampleMap;

/// A folder of 16-bit tones and an SFZ that maps them, for one test, removed afterwards.
struct TempInstrument
{
    fs::path folder;
    explicit TempInstrument(const std::string& name) : folder(fs::temp_directory_path() / name)
    {
        fs::remove_all(folder);
        fs::create_directories(folder);
    }
    ~TempInstrument() { fs::remove_all(folder); }

    /// A sine of `hz`, `frames` long, as a 16-bit WAV (stereo when asked).
    void tone(const std::string& file, double hz, int frames = 4800, bool stereo = false) const
    {
        std::vector<float> ch(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i)
            ch[static_cast<size_t>(i)] = sineAt(hz, i);
        std::vector<std::vector<float>> channels{ch};
        if (stereo)
            channels.push_back(ch);
        REQUIRE(audiofile::writeWav((folder / file).string(), channels, sr, std::nullopt, 16));
    }
    [[nodiscard]] std::string sfz(const std::string& text, const std::string& name = "Test.sfz") const
    {
        std::ofstream(folder / name) << text;
        return (folder / name).string();
    }
};

/// The zone the last note started, as the panel reads it.
int lastZone(S& s)
{
    return static_cast<int>(std::lround(s.analysis().read(S::LastZone)));
}
} // namespace

TEST_CASE("sfz: headers inherited, note names, paths written on Windows; the rest named and left out",
          "[fast]")
{
    const auto file = fd::devices::sfz::parse(R"(
// A comment, and /* a block
   comment */
#define $LOW 36
<control> default_path=Strings\Violin Section\ octave_offset=0
<global> ampeg_release=0.8 amp_veltrack=80 volume=-3
<group> lovel=1 hivel=63 tune=10
<region> sample=Sus A2.wav lokey=$LOW hikey=c#4 pitch_keycenter=a2
<region> sample=Sus B2.wav key=db4 volume=6 transpose=1 offset=100 end=999 loop_mode=loop_continuous loop_start=200 loop_end=899
<group> seq_length=2 lorand=0.25 hirand=0.75 pan=20 group_label=rr ampeg_dynamic=1
<region> sample=Rr 1.wav seq_position=1
<region> sample=Rr 2.wav seq_position=2 sw_lokey=24
<region> sample=Release.wav trigger=release
<region> sample=*sine
<curve> v000=0
)",
                                              "/lib");
    REQUIRE(file.regions.size() == 3);
    const auto& a = file.regions[0];
    CHECK(a.sample == fs::path("/lib/Strings/Violin Section/Sus A2.wav"));
    CHECK(a.loKey == 36);
    CHECK(a.hiKey == 61); // c#4: SFZ's c4 is 60
    CHECK(a.root == 45);  // a2
    CHECK(a.loVel == 1);
    CHECK(a.hiVel == 63);
    CHECK(a.volume == Approx(-3.0)); // the global's
    CHECK(a.tune == Approx(10.0));   // the group's
    const auto& b = file.regions[1];
    CHECK(b.loKey == 61);
    CHECK(b.hiKey == 61);
    CHECK(b.root == 61);            // key= sets all three
    CHECK(b.volume == Approx(6.0)); // the region's own wins
    CHECK(b.transpose == 1);
    CHECK(b.offset == 100);
    CHECK(b.end == 999);
    CHECK(b.loopMode == fd::devices::sfz::LoopMode::Continuous);
    CHECK(b.loopStart == 200);
    CHECK(b.loopEnd == 899);
    const auto& rr = file.regions[2];
    CHECK(rr.loVel == 0); // a new group: the first group's velocities are gone
    CHECK(rr.seqLength == 2);
    CHECK(rr.seqPosition == 1);
    CHECK(rr.loRand == Approx(0.25));
    CHECK(rr.hiRand == Approx(0.75));
    CHECK(file.ampRelease == Approx(0.8));
    CHECK(file.ampVeltrack == Approx(80.0));
    CHECK_FALSE(file.ampAttack);
    // Named once each; the regions that need what isn't read are left out; labels and ARIA's
    // envelope rate are no loss.
    CHECK(file.ignored ==
          std::vector<std::string>{"<curve>", "pan", "sw_lokey", "trigger=release", "sample=*sine"});

    CHECK(fd::devices::sfz::noteNumber("c-1") == 0);
    CHECK(fd::devices::sfz::noteNumber("C4") == 60);
    CHECK(fd::devices::sfz::noteNumber("bb3") == 58);
    CHECK(fd::devices::sfz::noteNumber("g9") == 127);
    CHECK(fd::devices::sfz::noteNumber("72") == 72);
    CHECK_FALSE(fd::devices::sfz::noteNumber("h2"));

    // A map made on Windows names files in another case: found all the same.
    TempInstrument t("fd-sfz-case");
    fs::create_directories(t.folder / "Samples");
    t.tone("Samples/Violin A2.wav", 110.0);
    CHECK(fd::devices::sfz::findFile(t.folder / "samples" / "violin a2.WAV") ==
          t.folder / "Samples" / "Violin A2.wav");
    CHECK(fd::devices::sfz::findFile(t.folder / "samples" / "viola.wav").empty());
}

TEST_CASE("sfz: VSCO 2 CE's maps read whole, nothing ignored", "[fast]")
{
    const fs::path data = FD_SAMPLER_TEST_DATA;
    const std::pair<const char*, size_t> files[] = {{"ViolinEnsSusVib.sfz", 22},
                                                    {"ViolinEnsPizz.sfz", 44},
                                                    {"UprightPiano.sfz", 69},
                                                    {"GM-StylePerc.sfz", 220}};
    for (const auto& [name, regions] : files)
    {
        INFO(name);
        const auto file = fd::devices::sfz::read(data / name);
        REQUIRE(file);
        CHECK(file->regions.size() == regions);
        CHECK(file->ignored.empty());
    }
    const auto violins = fd::devices::sfz::read(data / "ViolinEnsSusVib.sfz");
    const auto& first = violins->regions.front();
    CHECK(first.sample.generic_string().ends_with("Strings/Violin Section/susVib/VlnEns_susVib_A2_v1.wav"));
    CHECK(first.loKey == 56);
    CHECK(first.hiKey == 57);
    CHECK(first.root == 57);
    CHECK(first.hiVel == 62);
    CHECK(first.volume == Approx(20.0));
    CHECK(violins->ampAttack == Approx(0.001));
    CHECK(violins->ampRelease == Approx(0.8));
    // The kit's release: what most of its regions say.
    const auto kitReleases = fd::devices::sfz::read(data / "GM-StylePerc.sfz");
    CHECK(kitReleases->ampRelease == Approx(3.0));
    // The kit takes turns; the pizzicato chooses at random.
    const auto kit = fd::devices::sfz::read(data / "GM-StylePerc.sfz");
    CHECK(std::any_of(kit->regions.begin(), kit->regions.end(),
                      [](const auto& r) { return r.seqLength == 2; }));
    const auto pizz = fd::devices::sfz::read(data / "ViolinEnsPizz.sfz");
    CHECK(std::any_of(pizz->regions.begin(), pizz->regions.end(),
                      [](const auto& r) { return r.hiRand == 0.5; }));
    CHECK(std::any_of(pizz->regions.begin(), pizz->regions.end(),
                      [](const auto& r) { return r.loRand == 0.5; }));
}

TEST_CASE("sampler zones: every key and velocity plays the zone it should", "[fast]")
{
    // Three keys a zone from C3, soft below 64 and loud above.
    TempInstrument t("fd-sampler-zones");
    std::string text;
    for (int z = 0; z < 4; ++z)
        for (const auto* layer : {"soft", "loud"})
        {
            const auto name = std::string(layer) + std::to_string(z) + ".wav";
            t.tone(name, 100.0 + 50.0 * z, 480);
            text += "<region> sample=" + name + " lokey=" + std::to_string(60 + 3 * z) +
                    " hikey=" + std::to_string(62 + 3 * z) +
                    (std::string(layer) == "soft" ? " lovel=0 hivel=63\n" : " lovel=64 hivel=127\n");
        }
    std::string error;
    const auto map = Map::loadSfz(t.sfz(text), &error);
    REQUIRE(map != nullptr);
    REQUIRE(map->zones.size() == 8);
    CHECK(map->lowestKey() == 60);
    CHECK(map->highestKey() == 71);
    std::array<int, Map::maxChosen> chosen{};
    for (int key = 0; key < 128; ++key)
        for (int velocity = 1; velocity < 128; ++velocity)
        {
            const int n = map->choose(key, velocity, 0, 0.5f, chosen);
            if (key < 60 || key > 71)
            {
                CHECK(n == 0);
                continue;
            }
            REQUIRE(n == 1);
            CHECK(chosen[0] == 2 * ((key - 60) / 3) + (velocity >= 64 ? 1 : 0));
        }
}

TEST_CASE("sampler zones: a round robin takes turns key by key and starts over at reset", "[fast]")
{
    TempInstrument t("fd-sampler-rr");
    for (int i = 1; i <= 3; ++i)
        t.tone("rr" + std::to_string(i) + ".wav", 200.0 * i, 480);
    const auto path = t.sfz("<group> lokey=60 hikey=72 seq_length=3\n"
                            "<region> sample=rr1.wav seq_position=1\n"
                            "<region> sample=rr2.wav seq_position=2\n"
                            "<region> sample=rr3.wav seq_position=3\n");
    S s;
    s.readExtraState({{"sample.path", path}});
    s.prepare({.sampleRate = sr, .maxBlockSize = 512});
    const auto noteOnKey = [&](int key)
    {
        RenderOptions o{.sampleRate = sr, .skipPrepare = true};
        o.events = {noteOn(0, key), noteOff(100, key)};
        (void)render(s, silence(512, 0), o); // played on, without a reset
        return lastZone(s);
    };
    CHECK(noteOnKey(60) == 0);
    CHECK(noteOnKey(60) == 1);
    CHECK(noteOnKey(64) == 0); // each key its own turns
    CHECK(noteOnKey(60) == 2);
    CHECK(noteOnKey(60) == 0); // round again
    s.reset();
    CHECK(noteOnKey(64) == 0); // from the first after a reset
}

TEST_CASE("sampler zones: a random choice keeps each zone's share and repeats after reset", "[fast]")
{
    TempInstrument t("fd-sampler-random");
    for (int i = 1; i <= 3; ++i)
        t.tone("hit" + std::to_string(i) + ".wav", 200.0 * i, 480);
    const auto map = Map::loadSfz(t.sfz("<region> sample=hit1.wav lorand=0 hirand=0.2\n"
                                        "<region> sample=hit2.wav lorand=0.2 hirand=0.5\n"
                                        "<region> sample=hit3.wav lorand=0.5 hirand=1\n"),
                                  nullptr);
    REQUIRE(map != nullptr);
    Map::Choice choice;
    std::array<int, Map::maxChosen> chosen{};
    std::array<int, 3> counts{};
    std::vector<int> first;
    for (int i = 0; i < 10000; ++i)
    {
        REQUIRE(map->choose(60, 100, choice.nextTurn(60), choice.nextPick(), chosen) == 1);
        ++counts[static_cast<size_t>(chosen[0])];
        if (i < 50)
            first.push_back(chosen[0]);
    }
    CHECK(counts[0] / 10000.0 == Approx(0.2).margin(0.05));
    CHECK(counts[1] / 10000.0 == Approx(0.3).margin(0.05));
    CHECK(counts[2] / 10000.0 == Approx(0.5).margin(0.05));
    choice.reset();
    for (int i = 0; i < 50; ++i)
    {
        REQUIRE(map->choose(60, 100, choice.nextTurn(60), choice.nextPick(), chosen) == 1);
        CHECK(chosen[0] == first[static_cast<size_t>(i)]);
    }
}

TEST_CASE("sampler zones: a zone plays its sample at its root, tuning and transpose on top", "[fast]")
{
    TempInstrument t("fd-sampler-zone-pitch");
    t.tone("a3.wav", 220.0, 96000);
    const auto path =
        t.sfz("<region> sample=a3.wav lokey=0 hikey=127 pitch_keycenter=57 tune=30 transpose=-12\n");
    S s;
    s.params().set(S::Root, 20.0f); // ignored: the zone has its own
    s.readExtraState({{"sample.path", path}});
    const auto out = play(s, {noteOn(0, 64)}, 48000);
    // 7 semitones above the root, an octave down, 30 cents up.
    const double expected = 220.0 * std::exp2((7.0 - 12.0 + 0.3) / 12.0);
    CHECK(1200.0 * std::log2(estimateFrequency(out, 4800, 38400) / expected) == Approx(0.0).margin(0.5));
}

TEST_CASE(
    "sampler zones: a 16-bit file held as 16-bit with one decimated copy, decoded once for two Samplers",
    "[fast]")
{
    TempInstrument t("fd-sampler-memory");
    t.tone("a.wav", 220.0, 48000, true);
    const auto path = t.sfz("<region> sample=a.wav\n");
    S one, two;
    one.readExtraState({{"sample.path", path}});
    two.readExtraState({{"sample.path", path}});
    const auto a = one.map(), b = two.map();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const auto& sample = a->zones[0].sample;
    REQUIRE(sample != nullptr);
    CHECK(sample.get() == b->zones[0].sample.get()); // one decode
    CHECK(sample->compact);
    CHECK(sample->levels.size() == 2);
    // Two bytes a sample, two channels, the file and its half-rate copy (and the interpolator's
    // padding).
    const double expected = 2.0 * 2.0 * 48000.0 * 1.5;
    CHECK(static_cast<double>(a->bytes()) == Approx(expected).epsilon(0.01));
    // One sample keeps its floats and every level, as it always has.
    TempSample single("fd-sampler-memory-one.wav", 48000, [](int i) { return sineAt(220.0, i); });
    S plain;
    load(plain, single.path);
    CHECK_FALSE(plain.sample()->compact);
    CHECK(plain.sample()->levels.size() > 2);
}

TEST_CASE("sampler zones: a multisample swapped while a note plays fades without clicks", "[fast]")
{
    TempInstrument t("fd-sampler-swap-map");
    t.tone("a.wav", 300.0, 96000);
    t.tone("b.wav", 450.0, 96000);
    const auto first = t.sfz("<region> sample=a.wav pitch_keycenter=60\n", "A.sfz");
    const auto second = t.sfz("<region> sample=b.wav pitch_keycenter=60\n", "B.sfz");
    S s;
    s.readExtraState({{"sample.path", first}});
    const auto out = play(s, {noteOn(0, 60)}, 48000,
                          [&](Device& d, std::int64_t at)
                          {
                              if (at == 24064)
                                  d.readExtraState({{"sample.path", second}});
                          });
    CHECK(peak(out.slice(20000, 4000)) > 0.3);
    CHECK(peakDb(out.slice(26000, 4000)) < -100.0);
    CHECK(clickScore(out.slice(1000, 46000), 0) < 8.0);
}

TEST_CASE("sampler state: an SFZ's path round-trips; missing samples, unreadable files and the Library said",
          "[fast]")
{
    TempInstrument t("fd-sampler-sfz-state");
    t.tone("a.wav", 220.0, 480);
    const auto path = t.sfz("<region> sample=a.wav hikey=59\n<region> sample=gone.wav lokey=60\n");
    S s;
    s.readExtraState({{"sample.path", path}});
    REQUIRE(s.map() != nullptr);
    CHECK_FALSE(s.map()->single);
    CHECK(s.map()->missing == 1);
    CHECK(s.loadError().empty()); // what's there plays; the panel says what isn't
    CHECK(s.sample() == nullptr); // not one sample
    nlohmann::json extra = nlohmann::json::object();
    s.writeExtraState(extra);
    CHECK(extra["sample.path"] == path);
    CHECK(extra["sample.zones"] == 2);
    S restored;
    restored.readExtraState(extra);
    REQUIRE(restored.map() != nullptr);
    CHECK(restored.map()->zones.size() == 2);
    // The missing zone plays nothing, the other plays.
    CHECK(peak(play(restored, {noteOn(0, 64)}, 4800)) == 0.0f);
    CHECK(peak(play(restored, {noteOn(0, 50)}, 4800)) > 0.1f);

    S unreadable;
    unreadable.readExtraState({{"sample.path", (t.folder / "nothing.sfz").string()}});
    CHECK(unreadable.map() == nullptr);
    CHECK_FALSE(unreadable.loadError().empty());

    // In the Library: stored by its place there, found where the Library is.
    library::setRoot(t.folder);
    const auto stored = library::reference(path);
    CHECK(stored == "library:Test.sfz");
    S fromLibrary;
    fromLibrary.readExtraState({{"sample.path", stored}});
    REQUIRE(fromLibrary.map() != nullptr);
    CHECK(fromLibrary.map()->zones.size() == 2);
    library::setRoot({});
}

TEST_CASE("sampler: what a file sets on the Sampler's controls", "[fast]")
{
    TempInstrument t("fd-sampler-sfz-settings");
    t.tone("hit.wav", 220.0, 480);
    const auto drums = Map::loadSfz(t.sfz("<global> ampeg_attack=0.002 ampeg_release=0.3 amp_veltrack=90\n"
                                          "<region> sample=hit.wav loop_mode=one_shot\n"),
                                    nullptr);
    REQUIRE(drums != nullptr);
    std::map<size_t, float> set;
    for (const auto& [param, value] : S::settingsFor(*drums))
        set[param] = value;
    CHECK(set.at(S::Mode) == 1.0f);
    CHECK(set.at(S::VelAmp) == Approx(90.0f));
    CHECK(set.at(S::AmpA) == Approx(2.0f));
    CHECK(set.at(S::AmpR) == Approx(300.0f));
    CHECK_FALSE(set.contains(S::AmpD));
    CHECK_FALSE(set.contains(S::Loop));
    // One sample: its root note, as its file names it.
    TempSample rooted("fd-sampler-root.wav", 480, [](int i) { return sineAt(220.0, i); }, sr, 57);
    const auto one = Map::one(fd::devices::SampleData::load(rooted.path, nullptr));
    REQUIRE(S::settingsFor(*one) == std::vector<std::pair<size_t, float>>{{S::Root, 57.0f}});
}

TEST_CASE("sampler: 50 eight-second stereo zones load in under half a second", "[.benchmark]")
{
    TempInstrument t("fd-sampler-load-bench");
    std::string text;
    for (int z = 0; z < 50; ++z)
    {
        const auto name = "z" + std::to_string(z) + ".wav";
        t.tone(name, 100.0 + z, 8 * 48000, true);
        text += "<region> sample=" + name + " key=" + std::to_string(36 + z) + "\n";
    }
    const auto path = t.sfz(text);
    const auto start = std::chrono::steady_clock::now();
    const auto map = Map::loadSfz(path, nullptr);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(map != nullptr);
    CHECK(map->zones.size() == 50);
    WARN("50 zones of 8 s stereo loaded in " << seconds * 1000.0 << " ms, holding " << map->bytes() / 1.0e6
                                             << " MB");
    CHECK(seconds < 0.5);
}
