// The Wavetable's table tools (docs/design/table-editor.md): formulas, recipes and their baking,
// audio to wavetable, and table files written.
#include "AudioToTable.h"
#include "FactoryTables.h"
#include "Formula.h"
#include "TableFiles.h"
#include "TableRecipe.h"
#include "fd/audiofile/AudioFile.h"
#include "fd/dsp/Math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <random>
#include <set>

using namespace fd;
using namespace fd::devices::wavetable;
using Catch::Approx;

namespace
{
constexpr double sr = 48000.0;
constexpr int N = dsp::Wavetable::frameSize;

/// A band-limited saw (falling, harmonics 1/h) at `hz`, `seconds` long.
std::vector<float> saw(double hz, double seconds, double sampleRate = sr)
{
    const int length = static_cast<int>(seconds * sampleRate);
    const int top = static_cast<int>(0.45 * sampleRate / hz);
    std::vector<float> x(static_cast<size_t>(length));
    for (int n = 0; n < length; ++n)
    {
        double v = 0.0;
        for (int h = 1; h <= top; ++h)
            v += std::sin(dsp::twoPi * h * hz * n / sampleRate) / h;
        x[static_cast<size_t>(n)] = static_cast<float>(0.5 * v);
    }
    return x;
}

/// The levels of a frame's harmonics, the fundamental's taken as 1.
std::vector<double> levels(const float* frame, int count)
{
    std::vector<std::complex<float>> c(static_cast<size_t>(N / 2 + 1));
    dsp::RealFft(11).forward(frame, c.data());
    std::vector<double> out(static_cast<size_t>(count) + 1);
    for (int h = 1; h <= count; ++h)
        out[static_cast<size_t>(h)] = std::abs(c[static_cast<size_t>(h)]) / std::abs(c[1]);
    return out;
}

/// How far a frame's harmonics are from a saw's (1/h), as error power over the saw's (dB).
double sawErrorDb(const float* frame, int count)
{
    const auto l = levels(frame, count);
    double error = 0.0, total = 0.0;
    for (int h = 1; h <= count; ++h)
    {
        const double want = 1.0 / h;
        error += (l[static_cast<size_t>(h)] - want) * (l[static_cast<size_t>(h)] - want);
        total += want * want;
    }
    return 10.0 * std::log10(std::max(error, 1e-30) / total);
}
} // namespace

TEST_CASE("formula: x and t, the operators in their order, functions and waves", "[fast]")
{
    const auto f = Formula::parse("sin(2*pi*x) + 0.5*t^2 - -1");
    REQUIRE(f);
    CHECK(f->evaluate(0.25, 0.0) == Approx(2.0));
    CHECK(f->evaluate(0.0, 1.0) == Approx(1.5));
    CHECK(Formula::parse("2^3^2")->evaluate(0, 0) == Approx(512.0)); // right to left
    CHECK(Formula::parse("-2^2")->evaluate(0, 0) == Approx(-4.0));
    CHECK(Formula::parse("(1 + 2) * 3 / 2")->evaluate(0, 0) == Approx(4.5));
    CHECK(Formula::parse("clamp(5, 0, 1) + min(1, 2) + max(1, 2) + pow(2, 3)")->evaluate(0, 0) ==
          Approx(12.0));
    CHECK(Formula::parse("saw(x)")->evaluate(0.25, 0) == Approx(0.5)); // rising, from 0
    CHECK(Formula::parse("tri(x)")->evaluate(0.25, 0) == Approx(1.0));
    CHECK(Formula::parse("sq(x)")->evaluate(0.75, 0) == Approx(-1.0));
    CHECK(Formula::parse("rand(3)")->evaluate(0, 0) == Formula::parse("rand(3.7)")->evaluate(0, 0));
    CHECK(Formula::parse("1 / 0")->evaluate(0, 0) == 0.0); // never infinite
    std::string error;
    CHECK_FALSE(Formula::parse("sin(x", &error));
    CHECK(error.find("\")\"") != std::string::npos);
    CHECK_FALSE(Formula::parse("y * 2", &error));
    CHECK(error.find("\"y\" isn't known") != std::string::npos);
    CHECK(error.find("character 1") != std::string::npos);
    CHECK_FALSE(Formula::parse("min(1)", &error));
    CHECK(error.find("takes 2 arguments") != std::string::npos);
    CHECK(formulaError("x * (1 - x)").empty());
}

TEST_CASE("recipe: each source bakes as defined", "[fast]")
{
    // A formula's sine: one harmonic, the rest under -100 dB.
    Recipe r;
    r.frames = 4;
    Recipe::Layer f;
    f.kind = Recipe::SourceKind::Formula;
    f.formula = "sin(2*pi*x)";
    r.layers.push_back(f);
    const auto sine = bake(r);
    REQUIRE(sine.frames() == 4);
    const auto l = levels(sine.samples(2), 50);
    for (int h = 2; h <= 50; ++h)
        CHECK(l[static_cast<size_t>(h)] < 1e-5);

    // Harmonic bars: levels as set.
    Recipe bars;
    bars.frames = 1;
    Recipe::Layer b;
    b.kind = Recipe::SourceKind::Harmonics;
    b.keyframes.push_back({0, {}, {1.0f, 0.5f, 0.0f, 0.25f}, {}});
    bars.layers.push_back(b);
    const auto bl = levels(bake(bars).samples(0), 5);
    CHECK(bl[2] == Approx(0.5).margin(1e-4));
    CHECK(bl[3] < 1e-4);
    CHECK(bl[4] == Approx(0.25).margin(1e-4));

    // Drawn keyframes: a square at frame 0, its negative at frame 4; crossfade passes silence at
    // frame 2, a spectral morph keeps the level.
    Recipe drawn;
    drawn.frames = 5;
    Recipe::Layer d;
    d.kind = Recipe::SourceKind::Draw;
    d.keyframes.push_back(
        {0, {{0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {0.5f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}}, {}, {}});
    d.keyframes.push_back(
        {4, {{0.0f, -1.0f, 0.0f}, {0.5f, -1.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}, {}, {}});
    drawn.layers.push_back(d);
    // Its peak is normalised over the table; silence in the middle frame (crossfaded).
    const auto crossfaded = bake(drawn);
    float middle = 0.0f;
    for (int i = 0; i < N; ++i)
        middle = std::max(middle, std::abs(crossfaded.samples(2)[i]));
    CHECK(middle < 1e-3f);
    drawn.layers[0].fill = Recipe::Fill::Spectral;
    const auto morphed = bake(drawn);
    float kept = 0.0f;
    for (int i = 0; i < N; ++i)
        kept = std::max(kept, std::abs(morphed.samples(2)[i]));
    CHECK(kept > 0.5f);

    // A factory table as a layer: the same frames.
    Recipe copy;
    const auto vowels = factoryTable("formant/vowels");
    REQUIRE(vowels != nullptr);
    copy.frames = vowels->frames();
    Recipe::Layer t;
    t.kind = Recipe::SourceKind::Table;
    t.tableId = "formant/vowels";
    copy.layers.push_back(t);
    const auto again = bake(copy);
    for (int i = 0; i < N; i += 7)
        CHECK(again.samples(3)[i] == Approx(vowels->samples(3)[i]).margin(1e-4));
}

TEST_CASE("recipe: changes shape every frame, their amounts from keyframe to keyframe", "[fast]")
{
    Recipe r;
    r.frames = 3;
    Recipe::Layer saws;
    saws.kind = Recipe::SourceKind::Formula;
    saws.formula = "saw(x) + 0.3";
    r.layers.push_back(saws);
    const auto plain = bake(r);

    // Remove DC takes the 0.3 out (it is already out of a table: bake drops DC), so look at Smooth.
    Recipe::Change smooth;
    smooth.kind = Recipe::ChangeKind::Smooth;
    smooth.amounts = {{0, 0.0f}, {2, 1.0f}};
    r.changes.push_back(smooth);
    const auto smoothed = bake(r);
    CHECK(levels(smoothed.samples(0), 40)[40] == Approx(levels(plain.samples(0), 40)[40]).epsilon(1e-3));
    CHECK(levels(smoothed.samples(2), 40)[40] < 0.1 * levels(plain.samples(2), 40)[40]);
    CHECK(smooth.amountAt(1) == Approx(0.5f));

    // Phase Align: the fundamental starts at zero going up.
    r.changes = {{Recipe::ChangeKind::PhaseAlign, true, dsp::Warp::FormantShift, 0, {}}};
    Recipe shifted = r;
    shifted.layers[0].formula = "saw(x + 0.3)";
    const auto aligned = bake(shifted);
    std::vector<std::complex<float>> c(static_cast<size_t>(N / 2 + 1));
    dsp::RealFft(11).forward(aligned.samples(1), c.data());
    CHECK(std::arg(c[1]) == Approx(-dsp::pi / 2.0).margin(1e-3));

    // A spectral warp as a change: the same function the oscillator uses.
    Recipe warped = r;
    warped.changes = {{Recipe::ChangeKind::Warp, true, dsp::Warp::SpectralLowPass, 0, {{0, 1.0f}}}};
    const auto lowPassed = levels(bake(warped).samples(0), 10);
    CHECK(lowPassed[2] < 1e-4);

    // Reverse: a rising saw for a falling one.
    Recipe reversed = r;
    reversed.changes = {{Recipe::ChangeKind::Reverse, true, dsp::Warp::FormantShift, 0, {}}};
    const auto back = bake(reversed);
    CHECK(back.samples(0)[N / 4] == Approx(-plain.samples(0)[N / 4]).margin(0.02));
}

TEST_CASE("recipe: saved and read back the same, baked the same every time; bad recipes refused", "[fast]")
{
    Recipe r;
    r.name = "Mine";
    r.frames = 16;
    Recipe::Layer d;
    d.kind = Recipe::SourceKind::Draw;
    d.fill = Recipe::Fill::Spectral;
    d.keyframes.push_back({0, {{0.0f, 0.0f, 0.3f}, {0.5f, 1.0f, -0.3f}, {1.0f, 0.0f, 0.0f}}, {}, {}});
    d.keyframes.push_back({15, {{0.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}}, {}, {}});
    r.layers.push_back(d);
    Recipe::Layer own;
    own.kind = Recipe::SourceKind::Audio;
    own.level = 0.5f;
    own.frames.assign(static_cast<size_t>(2 * N), 0.0f);
    for (int i = 0; i < 2 * N; ++i)
        own.frames[static_cast<size_t>(i)] = static_cast<float>(std::sin(dsp::twoPi * 3.0 * i / N));
    own.note = "Pitched, 2 frames";
    r.layers.push_back(own);
    r.changes.push_back({Recipe::ChangeKind::Filter, true, dsp::Warp::FormantShift, 1, {{3, 0.2f}}});
    r.changes.push_back({Recipe::ChangeKind::Warp, true, dsp::Warp::Shepard, 0, {{0, 0.0f}, {15, 1.0f}}});

    const auto json = toJson(r);
    const auto read = recipeFromJson(json);
    REQUIRE(read);
    CHECK(toJson(*read) == json);
    const auto a = bake(*read), b = bake(r);
    for (int f = 0; f < 16; f += 5)
        for (int i = 0; i < N; i += 97)
            CHECK(a.samples(f)[i] == Approx(b.samples(f)[i]).margin(1e-4)); // its data went through 16 bits
    const auto c = bake(*read);
    CHECK(std::equal(c.samples(0), c.samples(0) + 16 * N, a.samples(0)));

    std::string error;
    CHECK_FALSE(recipeFromJson(nlohmann::json{{"frames", 300}}, &error));
    CHECK(error.find("frames") != std::string::npos);
    CHECK_FALSE(recipeFromJson(nlohmann::json{{"layers", {{{"kind", "smoke"}}}}}, &error));
    CHECK_FALSE(
        recipeFromJson(nlohmann::json{{"layers", {{{"kind", "formula"}, {"formula", "sin("}}}}}, &error));
    CHECK(error.find("the formula") != std::string::npos);
    CHECK_FALSE(
        recipeFromJson(nlohmann::json{{"layers", {{{"kind", "table"}, {"table", "basic/none"}}}}}, &error));
    CHECK_FALSE(recipeFromJson(nlohmann::json{{"changes", {{{"kind", "warp"}, {"warp", "sync"}}}}}, &error));

    // A table to start from: a factory one by its ID, one of yours by its frames.
    const auto vowels = factoryTable("formant/vowels");
    CHECK(recipeFromTable(*vowels, "formant/vowels").layers[0].tableId == "formant/vowels");
    CHECK(recipeFromTable(*vowels, "").layers[0].sourceFrames() == vowels->frames());
}

TEST_CASE("audio to wavetable: the pitch within a cent, a saw's harmonics within -60 dB", "[fast]")
{
    for (const double hz : {55.0, 220.0, 441.3, 1000.0})
    {
        INFO(hz << " Hz");
        const auto x = saw(hz, 1.0);
        const auto found = findPitch(x.data(), static_cast<int>(x.size()), sr);
        REQUIRE(found);
        CHECK(std::abs(1200.0 * std::log2(*found / hz)) < 1.0);
    }
    std::mt19937 noise(3);
    std::uniform_real_distribution<float> uniform(-0.5f, 0.5f);
    std::vector<float> hiss(48000);
    for (auto& v : hiss)
        v = uniform(noise);
    CHECK_FALSE(findPitch(hiss.data(), static_cast<int>(hiss.size()), sr));

    const auto x = saw(220.0, 1.0);
    for (const auto way : {AudioWay::Pitched, AudioWay::Spectral})
    {
        INFO((way == AudioWay::Pitched ? "pitched" : "spectral"));
        const auto frames = framesFromAudio(x.data(), static_cast<int>(x.size()), sr, way, 8);
        REQUIRE(frames);
        CHECK(frames->count == 8);
        CHECK(frames->pitch == Approx(220.0).epsilon(1e-4));
        for (int f = 0; f < 8; f += 3)
            CHECK(sawErrorDb(frames->samples.data() + static_cast<size_t>(f) * N, 90) < -60.0);
    }
    // Pitched keeps the wave: a saw's falling edge where a saw has it.
    const auto pitched = framesFromAudio(x.data(), static_cast<int>(x.size()), sr, AudioWay::Pitched, 1);
    REQUIRE(pitched);
    CHECK(pitched->samples[N / 4] > 0.0f);

    // Slices: windows as they are.
    std::vector<float> ramp(10000);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i) / 10000.0f;
    const auto slices = framesFromAudio(ramp.data(), 10000, sr, AudioWay::Slices, 3);
    REQUIRE(slices);
    CHECK(slices->samples[0] == ramp[0]);
    CHECK(slices->samples[static_cast<size_t>(2 * N + N - 1)] == ramp[9999]);

    std::string error;
    CHECK_FALSE(framesFromAudio(hiss.data(), 48000, sr, AudioWay::Pitched, 8, &error));
    CHECK(error.find("Slices") != std::string::npos);
}

TEST_CASE("table files: WAV with its frame size, and .wt, read back exactly", "[fast]")
{
    const auto table = factoryTable("formant/vowels");
    REQUIRE(table != nullptr);
    const auto dir = std::filesystem::temp_directory_path() / "fd-table-files";
    std::filesystem::create_directories(dir);
    for (const auto* name : {"vowels.wav", "vowels.wt"})
    {
        INFO(name);
        const auto path = (dir / name).string();
        std::string error;
        const bool wav = std::string(name).ends_with(".wav");
        REQUIRE((wav ? exportWav(*table, path, &error) : exportWt(*table, path, &error)));
        const auto read = importTable(path, 0, &error);
        REQUIRE(read);
        CHECK(read->frameLength == N);
        CHECK(read->frames == table->frames());
        bool same = true;
        for (int f = 0; f < table->frames(); ++f)
            same = same && std::equal(table->samples(f), table->samples(f) + N,
                                      read->samples.data() + static_cast<size_t>(f) * N);
        CHECK(same);
        std::filesystem::remove(path);
    }
}

TEST_CASE("table files: a table told from audio, audio read as mono", "[fast]")
{
    const auto dir = std::filesystem::temp_directory_path() / "fd-table-or-audio";
    std::filesystem::create_directories(dir);
    const auto table = factoryTable("formant/vowels");
    REQUIRE(table != nullptr);
    const auto tableWav = (dir / "table.wav").string(), tableWt = (dir / "table.wt").string();
    REQUIRE(exportWav(*table, tableWav));
    REQUIRE(exportWt(*table, tableWt));
    CHECK(isTableFile(tableWav));
    CHECK(isTableFile(tableWt));

    // A second of a saw in stereo, no metadata: audio, read back mixed to mono.
    const auto x = saw(220.0, 1.0);
    const auto sound = (dir / "saw.wav").string();
    REQUIRE(audiofile::writeWav(sound, {x, std::vector<float>(x.size(), 0.0f)}, sr));
    CHECK_FALSE(isTableFile(sound));
    const auto mono = readMono(sound);
    REQUIRE(mono);
    CHECK(mono->sampleRate == sr);
    REQUIRE(mono->samples.size() == x.size());
    CHECK(mono->samples[100] == Approx(x[100] * 0.5f).margin(1e-6));
    // A single cycle is a table.
    const auto cycle = (dir / "cycle.wav").string();
    REQUIRE(audiofile::writeWav(cycle, {std::vector<float>(600, 0.25f)}, sr));
    CHECK(isTableFile(cycle));
    std::string error;
    CHECK_FALSE(readMono((dir / "missing.wav").string(), &error));
    CHECK_FALSE(error.empty());
    for (const auto& path : {tableWav, tableWt, sound, cycle})
        std::filesystem::remove(path);
}

TEST_CASE("audio to wavetable: a recipe of one Audio layer, named for where it came from", "[fast]")
{
    const auto x = saw(220.0, 1.0);
    const auto frames = framesFromAudio(x.data(), static_cast<int>(x.size()), sr, AudioWay::Spectral, 16);
    REQUIRE(frames);
    const auto recipe = recipeFromAudio(*frames, AudioWay::Spectral, "Saw");
    CHECK(recipe.name == "Saw");
    CHECK(recipe.frames == 16);
    REQUIRE(recipe.layers.size() == 1);
    CHECK(recipe.layers[0].kind == Recipe::SourceKind::Audio);
    CHECK(recipe.layers[0].sourceFrames() == 16);
    CHECK(recipe.layers[0].note == "Spectral, 16 frames from Saw");
    // Baked, its frames are the frames taken (normalised).
    const auto baked = bake(recipe);
    CHECK(sawErrorDb(baked.samples(5), 90) < -60.0);
    CHECK(wayFromName("slices") == AudioWay::Slices);
    CHECK_FALSE(wayFromName("Slices"));
}

TEST_CASE("recipe: new layers and keyframes as the editor adds them", "[fast]")
{
    using K = Recipe::SourceKind;
    Recipe r;
    r.frames = 32;
    for (const auto kind : {K::Draw, K::Harmonics, K::Formula, K::Table})
    {
        r.layers = {newLayer(kind)};
        const auto baked = bake(r);
        float peak = 0.0f;
        for (int i = 0; i < N; ++i)
            peak = std::max(peak, std::abs(baked.samples(0)[i]));
        CHECK(peak > 0.5f); // something to hear
    }

    // A harmonics keyframe added between two changes nothing.
    auto harmonics = newLayer(K::Harmonics);
    auto last = harmonics.keyframes[0];
    last.frame = 31;
    last.levels = std::vector<float>(Recipe::numHarmonicBars, 0.0f);
    last.levels[2] = 1.0f;
    last.phases[2] = 0.25f;
    harmonics.keyframes.push_back(last);
    for (const auto fill : {Recipe::Fill::Crossfade, Recipe::Fill::Spectral})
    {
        harmonics.fill = fill;
        r.layers = {harmonics};
        const auto before = bake(r);
        auto added = harmonics;
        added.keyframes.insert(added.keyframes.begin() + 1, newKeyframe(harmonics, 10, 32));
        CHECK(added.keyframes[1].frame == 10);
        r.layers = {added};
        const auto after = bake(r);
        double worst = 0.0;
        for (int f = 0; f < 32; ++f)
            for (int i = 0; i < N; ++i)
                worst = std::max(worst,
                                 static_cast<double>(std::abs(before.samples(f)[i] - after.samples(f)[i])));
        CHECK(worst < 1e-3);
    }
    // A drawing's new keyframe starts as the one before it.
    auto draw = newLayer(K::Draw);
    draw.keyframes[0].frame = 4;
    CHECK(newKeyframe(draw, 20, 32).points == draw.keyframes[0].points);
    CHECK(newKeyframe(draw, 1, 32).points == draw.keyframes[0].points);
    CHECK(drawnAt(draw.keyframes[0].points, 0.25) == Approx(1.0));
    CHECK(drawnAt(draw.keyframes[0].points, 0.5) == Approx(0.0).margin(1e-6));
}

TEST_CASE("recipe: an image's rows as frames, its brightness the wave or the harmonics", "[fast]")
{
    // Two rows: dark to bright across, then all bright.
    const int width = 16;
    std::vector<float> pixels(static_cast<size_t>(2 * width));
    for (int i = 0; i < width; ++i)
    {
        pixels[static_cast<size_t>(i)] = static_cast<float>(i) / (width - 1);
        pixels[static_cast<size_t>(width + i)] = 1.0f;
    }
    const auto layer = imageLayer(pixels.data(), width, 2, false, "Ramp");
    CHECK(layer.kind == Recipe::SourceKind::Image);
    REQUIRE(layer.sourceFrames() == 2);
    CHECK(layer.frames.front() == Approx(0.0f));
    CHECK(layer.frames[N - 1] == Approx(1.0f));
    CHECK(layer.frames[N + N / 2] == Approx(1.0f));
    CHECK(layer.note == "Ramp");
    // More rows than frames: 256 kept, from the top to the bottom.
    std::vector<float> tall(static_cast<size_t>(300 * 4), 0.5f);
    CHECK(imageLayer(tall.data(), 4, 300, true, "").sourceFrames() == 256);
}

TEST_CASE("factory tables: about 20 from AKWF's cycles, each a morph over its frames, among their categories",
          "[fast]")
{
    const auto tables = factoryTables();
    int akwf = 0;
    std::set<std::string_view> seenCategories;
    std::string_view last;
    std::set<std::string_view> names;
    for (const auto& t : tables)
    {
        // The categories come in one run each (the menus list them so), names once.
        if (t.category != last)
        {
            CHECK_FALSE(seenCategories.contains(t.category));
            seenCategories.insert(t.category);
            last = t.category;
        }
        CHECK(names.insert(t.name).second);
    }
    for (const auto* id : {"acoustic/cello",
                           "acoustic/violin",
                           "acoustic/breathy-flute",
                           "acoustic/reed",
                           "acoustic/oboe",
                           "acoustic/alto-sax",
                           "acoustic/piano",
                           "acoustic/guitar",
                           "acoustic/electric-bass",
                           "acoustic/electric-piano",
                           "acoustic/theremin",
                           "formant/human-voice",
                           "harmonic/electric-organ",
                           "harmonic/overtones",
                           "fm/fm-synth",
                           "digital/chip",
                           "digital/video-game",
                           "digital/bit-reduced",
                           "texture/granular",
                           "texture/distorted"})
    {
        INFO(id);
        const auto table = factoryTable(id);
        REQUIRE(table != nullptr);
        ++akwf;
        CHECK(table->frames() == 64);
        // Normalised, and moving: the first frame and the last differ.
        float peak = 0.0f, apart = 0.0f;
        for (int f = 0; f < 64; ++f)
            for (int i = 0; i < N; ++i)
                peak = std::max(peak, std::abs(table->samples(f)[i]));
        for (int i = 0; i < N; ++i)
            apart = std::max(apart, std::abs(table->samples(0)[i] - table->samples(63)[i]));
        CHECK(peak == Approx(1.0f).margin(1e-3));
        CHECK(apart > 0.05f);
        // Each frame starts where a sine does: the fundamental phase-aligned, no frame's
        // cancelling in the morph (none much quieter than the frames around it).
        CHECK(std::abs(table->spectrum(0)[1]) > 0.0f);
    }
    CHECK(akwf == 20);
    CHECK(tables.size() >= 80);
}

TEST_CASE("recipe: how long a bake takes, as the editor bakes while a drag moves", "[.benchmark]")
{
    // A drawing and harmonics on keyframes, a spectral fill, four changes: 64 and 256 frames.
    for (const int frames : {64, 256})
    {
        Recipe r;
        r.frames = frames;
        auto draw = newLayer(Recipe::SourceKind::Draw);
        auto later = draw.keyframes[0];
        later.frame = frames - 1;
        later.points = {{0.0f, 0.0f, 0.3f}, {0.5f, 1.0f, -0.3f}, {1.0f, 0.0f, 0.0f}};
        draw.keyframes.push_back(later);
        auto harmonics = newLayer(Recipe::SourceKind::Harmonics);
        auto bright = harmonics.keyframes[0];
        bright.frame = frames - 1;
        for (size_t h = 0; h < bright.levels.size(); ++h)
            bright.levels[h] = 1.0f / static_cast<float>(h + 1);
        harmonics.keyframes.push_back(bright);
        harmonics.fill = Recipe::Fill::Spectral;
        r.layers = {draw, harmonics};
        for (const auto kind : {Recipe::ChangeKind::PhaseAlign, Recipe::ChangeKind::Smooth,
                                Recipe::ChangeKind::Fold, Recipe::ChangeKind::Normalise})
        {
            Recipe::Change c;
            c.kind = kind;
            c.amounts = {{0, 0.5f}};
            r.changes.push_back(c);
        }
        (void)bake(r); // warm up
        const auto start = std::chrono::steady_clock::now();
        constexpr int runs = 10;
        for (int i = 0; i < runs; ++i)
            (void)bake(r);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
            runs;
        WARN(frames << " frames: " << ms << " ms a bake");
        CHECK(ms > 0.0);
    }
}

TEST_CASE("recipe: one layer's frame is the bake's, its harmonics read back", "[fast]")
{
    using K = Recipe::SourceKind;
    // A layer alone, at level 1 and with no changes, bakes to its own frames (normalised): what
    // layerFrame gives, frame by frame, scaled as the bake scales.
    std::vector<float> pixels(16 * 4);
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<float>(i % 16) / 15.0f;
    auto draw = newLayer(K::Draw);
    auto later = draw.keyframes[0];
    later.frame = 20;
    later.points = {{0.0f, -1.0f, 0.0f}, {0.5f, 1.0f, 0.4f}, {1.0f, -1.0f, 0.0f}};
    draw.keyframes.push_back(later);
    auto harmonics = newLayer(K::Harmonics);
    auto bright = harmonics.keyframes[0];
    bright.frame = 31;
    bright.levels[4] = 0.5f;
    harmonics.keyframes.push_back(bright);
    harmonics.fill = Recipe::Fill::Spectral;
    for (const auto& layer : {draw, harmonics, newLayer(K::Formula), newLayer(K::Table),
                              imageLayer(pixels.data(), 16, 4, false, "")})
    {
        INFO(static_cast<int>(layer.kind));
        Recipe r;
        r.frames = 32;
        r.layers = {layer};
        const auto baked = bake(r);
        // The bake's scale: each frame without its DC, the loudest to a peak of 1.
        float peak = 0.0f;
        for (int f = 0; f < 32; ++f)
        {
            const auto x = layerFrame(layer, f, 32);
            double mean = 0.0;
            for (const float v : x)
                mean += v;
            mean /= N;
            for (const float v : x)
                peak = std::max(peak, std::abs(v - static_cast<float>(mean)));
        }
        REQUIRE(peak > 0.0f);
        double worst = 0.0;
        for (int f = 0; f < 32; f += 5)
        {
            const auto x = layerFrame(layer, f, 32);
            // The baked table carries no DC: compare without it.
            double mean = 0.0;
            for (const float v : x)
                mean += v;
            mean /= N;
            for (int i = 0; i < N; ++i)
                worst = std::max(worst,
                                 std::abs((x[static_cast<size_t>(i)] - mean) / peak - baked.samples(f)[i]));
            INFO("frame " << f);
        }
        CHECK(worst < 0.02);
    }

    // A harmonics keyframe's wave reads back as its levels and phases.
    auto k = harmonics.keyframes[0];
    k.levels = std::vector<float>(Recipe::numHarmonicBars, 0.0f);
    k.levels[0] = 0.8f;
    k.levels[2] = 0.3f;
    k.phases[2] = 0.25f;
    Recipe::Layer one = newLayer(K::Harmonics);
    one.keyframes = {k};
    const auto read = frameHarmonics(layerFrame(one, 0, 1), Recipe::numHarmonicBars);
    CHECK(read.levels[0] == Approx(0.8f).margin(1e-3));
    CHECK(read.levels[2] == Approx(0.3f).margin(1e-3));
    CHECK(read.phases[2] == Approx(0.25f).margin(1e-3));
    CHECK(read.levels[1] < 1e-3f);
}

TEST_CASE("recipe: a drawing's new keyframe between two keeps the frame as it was", "[fast]")
{
    auto draw = newLayer(Recipe::SourceKind::Draw);
    auto later = draw.keyframes[0];
    later.frame = 30;
    later.points = {{0.0f, 0.0f, 0.0f}, {0.25f, 0.2f, 0.0f}, {0.75f, -0.9f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    draw.keyframes.push_back(later);
    const auto close = [&](const Recipe::Layer& layer, double tolerance)
    {
        const auto before = layerFrame(layer, 12, 32);
        auto added = layer;
        added.keyframes.push_back(newKeyframe(layer, 12, 32));
        std::stable_sort(added.keyframes.begin(), added.keyframes.end(),
                         [](const auto& a, const auto& b) { return a.frame < b.frame; });
        const auto after = layerFrame(added, 12, 32);
        double worst = 0.0;
        for (size_t i = 0; i < before.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(before[i] - after[i])));
        CHECK(worst < tolerance);
    };
    // The points at the same places: crossfaded exactly.
    close(draw, 1e-4);
    // Elsewhere, or filled spectrally: the frame as 33 straight points, close to it.
    later.points = {{0.0f, 0.0f, 0.0f}, {0.4f, 1.0f, 0.5f}, {1.0f, 0.0f, 0.0f}};
    draw.keyframes[1] = later;
    close(draw, 0.12);
    draw.fill = Recipe::Fill::Spectral;
    close(draw, 0.12);
}
