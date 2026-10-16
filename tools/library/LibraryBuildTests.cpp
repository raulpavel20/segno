// Building the Library (ADR 0055, docs/design/factory-library.md §6): a sample's tail trimmed and
// capped, made 16-bit the same way every time, written as FLAC and read back exactly; roots checked
// by pitch; the SFZ the Sampler reads; a whole instrument built, and built again only where needed.
#include "LibraryBuild.h"
#include "SampleMap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>

namespace lb = fd::library_build;
namespace fs = std::filesystem;
using Catch::Approx;

namespace
{
/// A decaying tone: `seconds` long, falling 6 dB a `halfLife`th of a second.
fd::audiofile::DecodedAudio tone(double hz, double seconds, double decayPerSecondDb, int channels = 2,
                                 int bits = 24, double rate = 44100.0)
{
    fd::audiofile::DecodedAudio a;
    a.sampleRate = rate;
    a.bitsPerSample = bits;
    const auto frames = static_cast<size_t>(seconds * rate);
    a.channels.assign(static_cast<size_t>(channels), std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double level = std::pow(10.0, -decayPerSecondDb * t / 20.0);
        const double x =
            0.5 * level *
            (std::sin(2.0 * std::numbers::pi * hz * t) + 0.3 * std::sin(4.0 * std::numbers::pi * hz * t));
        for (auto& ch : a.channels)
            ch[i] = static_cast<float>(x);
    }
    return a;
}

struct TempDir
{
    fs::path path;
    explicit TempDir(const std::string& name) : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};
} // namespace

TEST_CASE("library build: a tail trimmed below -60 dB, a long sample capped with a fade", "[fast]")
{
    // Falling 20 dB a second: below -60 dB of its peak after 3 s.
    const auto decaying = tone(220.0, 6.0, 20.0);
    const auto trimmed = lb::prepare(decaying, 8.0, 1);
    CHECK_FALSE(trimmed.capped);
    CHECK(trimmed.frames / 44100.0 == Approx(3.02).margin(0.05));
    CHECK(trimmed.channels == 2);
    CHECK(std::abs(trimmed.interleaved.back()) <= 1); // faded to nothing but the dither

    // Held at its level for 10 s: cut at the cap, the last half second fading.
    const auto held = tone(220.0, 10.0, 0.0);
    const auto capped = lb::prepare(held, 8.0, 1);
    CHECK(capped.capped);
    CHECK(capped.frames == 8 * 44100);
    const auto at = [&capped](double seconds)
    { return std::abs(capped.interleaved[static_cast<size_t>(seconds * 44100.0) * 2]); };
    int loudBefore = 0, loudInFade = 0;
    for (int i = 0; i < 400; ++i)
    {
        loudBefore = std::max(loudBefore, static_cast<int>(at(7.0 + i / 44100.0)));
        loudInFade = std::max(loudInFade, static_cast<int>(at(7.9 + i / 44100.0)));
    }
    CHECK(loudInFade < loudBefore / 4);
}

TEST_CASE("library build: 16-bit exactly for a 16-bit file, dithered the same way every time otherwise",
          "[fast]")
{
    // A 16-bit file's own samples come back exactly.
    auto sixteen = tone(220.0, 0.5, 0.0, 1, 16);
    for (auto& x : sixteen.channels[0])
        x = static_cast<float>(std::lround(x * 32768.0)) / 32768.0f;
    const auto exact = lb::prepare(sixteen, 8.0, 7);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(exact.interleaved[static_cast<size_t>(i)] ==
                std::lround(sixteen.channels[0][static_cast<size_t>(i)] * 32768.0f));

    // A 24-bit one: dithered, the same for the same seed (a file's name), not for another.
    const auto deep = tone(220.0, 0.5, 0.0, 1, 24);
    CHECK(lb::prepare(deep, 8.0, 7).interleaved == lb::prepare(deep, 8.0, 7).interleaved);
    CHECK(lb::prepare(deep, 8.0, 7).interleaved != lb::prepare(deep, 8.0, 8).interleaved);
}

TEST_CASE("library build: FLAC read back exactly", "[fast]")
{
    TempDir dir("fd-library-flac");
    const auto prepared = lb::prepare(tone(330.0, 1.0, 10.0), 8.0, 3);
    std::string error;
    REQUIRE(lb::writeFlac(dir.path / "a.flac", prepared, &error));
    const auto back = fd::audiofile::decode((dir.path / "a.flac").string(), &error);
    REQUIRE(back);
    CHECK(back->bitsPerSample == 16);
    CHECK(back->numChannels() == 2);
    REQUIRE(back->numFrames() == prepared.frames);
    for (int i = 0; i < prepared.frames; ++i)
        for (int c = 0; c < 2; ++c)
            REQUIRE(std::lround(back->channels[static_cast<size_t>(c)][static_cast<size_t>(i)] * 32768.0f) ==
                    prepared.interleaved[static_cast<size_t>(i) * 2 + static_cast<size_t>(c)]);
    // Smaller than the 16-bit WAV it would be.
    CHECK(fs::file_size(dir.path / "a.flac") < static_cast<std::uintmax_t>(prepared.frames) * 4 / 2);
}

TEST_CASE("library build: a sample's pitch, for checking its root", "[fast]")
{
    for (const double note : {33.0, 45.0, 57.0, 69.0, 81.0, 96.0})
    {
        INFO(note);
        const auto pitch = lb::detectPitch(tone(440.0 * std::exp2((note - 69.0) / 12.0), 1.0, 3.0));
        REQUIRE(pitch);
        CHECK(*pitch == Approx(note).margin(0.1));
    }
    fd::audiofile::DecodedAudio silence;
    silence.sampleRate = 44100.0;
    silence.channels.assign(1, std::vector<float>(44100, 0.0f));
    CHECK_FALSE(lb::detectPitch(silence));
}

TEST_CASE("library build: an instrument built into the Library, its SFZ played by the Sampler, made again "
          "only where it changed",
          "[fast]")
{
    TempDir dir("fd-library-build");
    const auto cache = dir.path / "sources";
    const auto library = dir.path / "Library";
    // Two notes in the source, one with a name a vendor made up.
    const lb::Source source{"src", "A source", "https://example.org/src", "0123456789abcdef", "CC0-1.0"};
    fs::create_directories(cache / "src" / source.commit / "Folder, with a comma");
    const auto write = [&](const std::string& name, double hz)
    {
        const auto audio = tone(hz, 2.0, 30.0, 2, 24);
        REQUIRE(
            fd::audiofile::writeWav((cache / "src" / source.commit / "Folder, with a comma" / name).string(),
                                    audio.channels, audio.sampleRate));
    };
    write("Brand_A2.wav", 110.0);
    write("Brand_A3.wav", 220.0);

    lb::Recipe recipe;
    recipe.version = "1.0.0";
    recipe.sources.push_back(source);
    lb::Instrument inst;
    inst.sfz = "Samples/Family/Instrument/Sustain.sfz";
    inst.source = "src";
    inst.global = {{"ampeg_release", "0.5"}};
    lb::Zone low{.file = "Folder, with a comma/Brand_A2.wav",
                 .sha256 = "a",
                 .name = "Inst_A2",
                 .loKey = 0,
                 .hiKey = 51,
                 .root = 45};
    lb::Zone high{.file = "Folder, with a comma/Brand_A3.wav",
                  .sha256 = "b",
                  .name = "Inst_A3",
                  .loKey = 52,
                  .hiKey = 127,
                  .root = 57,
                  .volume = -3.0,
                  .seqLength = 2,
                  .seqPosition = 1};
    inst.zones = {low, high};
    recipe.instruments.push_back(inst);

    std::ostringstream log;
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("2 samples to make") != std::string::npos);
    CHECK(log.str().find("warning") == std::string::npos); // the roots are right
    REQUIRE(fs::exists(library / "Samples/Family/Instrument/samples/Inst_A2.flac"));
    REQUIRE(fs::exists(library / "Samples/Family/Instrument/samples/Inst_A3.flac"));

    // The Sampler reads what it made: two zones, the global release, the second's volume and turn.
    std::string error;
    const auto map = fd::devices::SampleMap::loadSfz((library / inst.sfz).string(), &error);
    REQUIRE(map != nullptr);
    REQUIRE(map->zones.size() == 2);
    CHECK(map->missing == 0);
    CHECK(map->ignored.empty());
    CHECK(map->zones[0].root == 45);
    CHECK(map->zones[1].loKey == 52);
    CHECK(map->zones[1].gain == Approx(std::pow(10.0, -3.0 / 20.0)));
    CHECK(map->zones[1].seqLength == 2);
    CHECK(map->settings.ampRelease == Approx(0.5));
    CHECK(map->zones[0].sample->compact); // 16-bit FLAC, kept 16-bit

    // Built again: nothing to make. A root that's wrong is said.
    log.str({});
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("0 samples to make") != std::string::npos);
    recipe.instruments[0].zones[1].root = 69;
    recipe.instruments[0].zones[1].sha256 = "c"; // as if changed
    log.str({});
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("warning: Folder, with a comma/Brand_A3.wav sounds at 57") != std::string::npos);

    // A zone the recipe no longer has: its sample goes.
    recipe.instruments[0].zones.pop_back();
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK_FALSE(fs::exists(library / "Samples/Family/Instrument/samples/Inst_A3.flac"));
}

TEST_CASE("library build: a drum's microphones mixed into one stereo sample, its layers levelled as one "
          "(drum-kit.md §27.3)",
          "[fast]")
{
    TempDir dir("fd-library-build-mix");
    const auto cache = dir.path / "sources";
    const auto library = dir.path / "Library";
    const lb::Source source{"kit", "A kit", "https://example.org/kit", "fedcba9876543210", "CC0-1.0"};
    const auto folder = cache / "kit" / source.commit;
    fs::create_directories(folder);
    // Two layers, each a mono close microphone and a stereo overhead at half its level.
    const auto write = [&](const std::string& name, double level, int channels)
    {
        auto audio = tone(100.0, 0.5, 20.0, channels, 16);
        for (auto& ch : audio.channels)
            for (auto& x : ch)
                x = static_cast<float>(x * level);
        REQUIRE(fd::audiofile::writeWav((folder / name).string(), audio.channels, audio.sampleRate));
    };
    write("close_v1.wav", 0.25, 1);
    write("oh_v1.wav", 0.125, 2);
    write("close_v2.wav", 1.0, 1);
    write("oh_v2.wav", 0.5, 2);

    lb::Recipe recipe;
    recipe.version = "1.0.0";
    recipe.sources.push_back(source);
    lb::Instrument inst;
    inst.sfz = "Samples/Drums/Kit/Kick.sfz";
    inst.source = "kit";
    inst.pitched = false;
    inst.normalize = -6.0;
    for (int v : {1, 2})
    {
        lb::Zone z{
            .name = "Kick_v" + std::to_string(v), .loVel = v == 1 ? 0 : 64, .hiVel = v == 1 ? 63 : 127};
        z.mix = {{"close_v" + std::to_string(v) + ".wav", "c" + std::to_string(v), 0.0, 0.0},
                 {"oh_v" + std::to_string(v) + ".wav", "o" + std::to_string(v), -6.0, 0.0}};
        z.file = z.mix.front().file;
        z.sha256 = z.mix.front().sha256;
        inst.zones.push_back(z);
    }
    recipe.instruments.push_back(inst);

    // A zone's sound: the close mic in both channels, the overhead's 6 dB down added.
    std::string error;
    const auto mixed = lb::zoneAudio(inst.zones[1], source, cache, 1.0, &error);
    REQUIRE(mixed);
    CHECK(mixed->numChannels() == 2);
    CHECK(mixed->bitsPerSample == 0); // dithered when made 16-bit
    const auto close = fd::audiofile::decode((folder / "close_v2.wav").string(), &error);
    REQUIRE(close);
    float peak = 0.0f, closePeak = 0.0f;
    for (const float x : mixed->channels[0])
        peak = std::max(peak, std::abs(x));
    for (const float x : close->channels[0])
        closePeak = std::max(closePeak, std::abs(x));
    CHECK(peak ==
          Approx(closePeak * 1.25f).margin(0.01f)); // the overhead at half the close's level, 6 dB down

    std::ostringstream log;
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("2 samples to make") != std::string::npos);
    const auto loud =
        fd::audiofile::decode((library / "Samples/Drums/Kit/samples/Kick_v2.flac").string(), &error);
    const auto soft =
        fd::audiofile::decode((library / "Samples/Drums/Kit/samples/Kick_v1.flac").string(), &error);
    REQUIRE(loud);
    REQUIRE(soft);
    const auto peakOf = [](const fd::audiofile::DecodedAudio& a)
    {
        float p = 0.0f;
        for (const auto& ch : a.channels)
            for (const float x : ch)
                p = std::max(p, std::abs(x));
        return p;
    };
    // The loudest layer at -6 dBFS, the softer one as far below it as it was recorded.
    CHECK(peakOf(*loud) == Approx(0.5f).margin(0.01f));
    CHECK(peakOf(*soft) == Approx(0.125f).margin(0.01f));
    CHECK(loud->numChannels() == 2);

    // Built again: the level kept, nothing made. The soft layer's overhead turned down: that layer
    // made again; the loud one, which sets the level, left alone.
    log.str({});
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("0 samples to make") != std::string::npos);
    recipe.instruments[0].zones[0].mix[1].gainDb = -12.0;
    log.str({});
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("1 samples to make") != std::string::npos);
    // The loud layer's close mic turned down: the level found again, both made again.
    recipe.instruments[0].zones[1].mix[0].gainDb = -3.0;
    log.str({});
    REQUIRE(lb::buildInstruments(recipe, cache, library, log));
    CHECK(log.str().find("2 samples to make") != std::string::npos);
}

TEST_CASE("library build: a drum starts at its onset, a millisecond before it, faded in", "[fast]")
{
    auto audio = tone(200.0, 0.5, 20.0, 1, 24);
    // 30 ms of near silence ahead of the hit, as some recordings have.
    const auto lead = static_cast<size_t>(0.03 * audio.sampleRate);
    for (auto& ch : audio.channels)
        ch.insert(ch.begin(), lead, 0.0001f);
    // The tone's first sample is its zero crossing: it starts a sample later.
    CHECK(std::abs(lb::onsetOf(audio) - static_cast<int>(lead)) <= 1);
    const auto kept = lb::prepare(audio, 8.0, 1);
    const auto trimmed = lb::prepare(audio, 8.0, 1, true);
    CHECK(std::abs(kept.frames - trimmed.frames -
                   (static_cast<int>(lead) - static_cast<int>(0.001 * audio.sampleRate))) <= 1);
    CHECK(trimmed.interleaved.front() == 0); // faded in from silence
}
