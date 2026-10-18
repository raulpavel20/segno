// Export (M2 step C8, ADR 0039, docs/design/export.md): settings, ranges, names, normalising and
// dither, JUCE-free.

#include "model/Export.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace fd::model;
using Catch::Approx;

TEST_CASE("export: settings are valid, and kept as JSON", "[fast]")
{
    // FLAC and AIFF hold 16 or 24 bits; a rate that isn't offered is the project's.
    CHECK(validated({.format = ExportFormat::Flac, .bitDepth = 32}).bitDepth == 24);
    CHECK(validated({.format = ExportFormat::Aiff, .bitDepth = 8}).bitDepth == 16);
    CHECK(validated({.format = ExportFormat::Wav, .bitDepth = 32}).bitDepth == 32);
    CHECK(validated({.sampleRate = 22050.0}).sampleRate == 0.0);
    CHECK(validated({.sampleRate = 96000.0}).sampleRate == 96000.0);
    CHECK(validated({.loudnessTarget = -80.0}).loudnessTarget == -36.0);

    const ExportSettings s{.what = ExportWhat::AllTracks,
                           .throughReturns = true,
                           .range = ExportRange::Loop,
                           .format = ExportFormat::Flac,
                           .bitDepth = 16,
                           .sampleRate = 44100.0,
                           .dither = false,
                           .normalise = ExportNormalise::Loudness,
                           .loudnessTarget = -16.0,
                           .folder = "/tmp/out",
                           .name = "Demo"};
    CHECK(exportSettingsFromJson(toJson(s)) == s);
    CHECK(exportSettingsFromJson("not json") == ExportSettings{});
    CHECK(exportSettingsFromJson(R"({"format":"wma","bitDepth":24})") ==
          ExportSettings{}); // unknown: the default
    CHECK(extensionFor(ExportFormat::Aiff) == ".aiff");
    CHECK(formatName(ExportFormat::Flac) == "FLAC");

    // MP3, Ogg and Opus (export.md §17): a bit rate each offers, its default otherwise; no bit depth.
    CHECK(isLossy(ExportFormat::Opus));
    CHECK_FALSE(isLossy(ExportFormat::Flac));
    CHECK(bitDepthsFor(ExportFormat::Mp3).empty());
    CHECK(validated({.format = ExportFormat::Mp3}).kbps == 320);
    CHECK(validated({.format = ExportFormat::Ogg, .kbps = 256}).kbps == 256);
    CHECK(validated({.format = ExportFormat::Opus, .kbps = 320}).kbps == 160);
    CHECK(validated({.format = ExportFormat::Wav, .kbps = 320}).kbps == 0);
    // Also as: another format, valid in turn; the same format again is none.
    const auto both =
        validated({.format = ExportFormat::Mp3, .alsoAs = ExportFormat::Flac, .alsoBitDepth = 32});
    REQUIRE(both.alsoAs == ExportFormat::Flac);
    CHECK(both.alsoBitDepth == 24);
    CHECK_FALSE(validated({.format = ExportFormat::Ogg, .alsoAs = ExportFormat::Ogg}).alsoAs.has_value());
    const auto files = exportFormats(both);
    REQUIRE(files.size() == 2);
    CHECK(files[0] == ExportFileFormat{ExportFormat::Mp3, 24, 320});
    CHECK(files[1].format == ExportFormat::Flac);
    CHECK(formatLabel(files[0]) == "MP3 320 kbps");
    CHECK(formatLabel({ExportFormat::Ogg, 0, 192}) == "Ogg Vorbis \xe2\x89\x88"
                                                      "192 kbps");
    CHECK(formatLabel({ExportFormat::Wav, 32, 0}) == "WAV 32-bit float");
    CHECK(exportSettingsFromJson(toJson(both)) == both);
    CHECK(extensionFor(ExportFormat::Opus) == ".opus");
}

TEST_CASE(
    "export: the song runs to its last clip, then its tail until silent; a loop and a selection are exact",
    "[fast]")
{
    Snapshot s;
    std::string why;
    CHECK_FALSE(exportSpan(s, ExportRange::Song, std::nullopt, std::nullopt, &why));
    CHECK(why.find("no clips") != std::string::npos);
    s.tracks.push_back({.id = 1, .clips = {ClipInfo{.start = 2.0, .length = 4.0}}});
    s.tracks.push_back({.id = 2, .clips = {ClipInfo{.start = 1.0, .length = 7.5}}});
    CHECK(songEnd(s) == 8.5);
    const auto song = exportSpan(s, ExportRange::Song, std::nullopt, std::nullopt);
    REQUIRE(song);
    CHECK(song->start == 0.0);
    CHECK(song->end == 8.5);
    CHECK(song->tail);
    CHECK_FALSE(exportSpan(s, ExportRange::Loop, std::nullopt, std::nullopt, &why));
    CHECK(why.find("loop") != std::string::npos);
    const auto loop = exportSpan(s, ExportRange::Loop, std::pair{4.0, 6.0}, std::nullopt);
    REQUIRE(loop);
    CHECK_FALSE(loop->tail);
    // A selection (time-selection.md §13): exactly its stretch, whatever the loop; none, a refusal.
    CHECK_FALSE(exportSpan(s, ExportRange::Selection, std::pair{4.0, 6.0}, std::nullopt, &why));
    CHECK(why.find("selection") != std::string::npos);
    const auto selection = exportSpan(s, ExportRange::Selection, std::pair{4.0, 6.0}, std::pair{1.5, 3.0});
    REQUIRE(selection);
    CHECK(selection->start == 1.5);
    CHECK(selection->end == 3.0);
    CHECK_FALSE(selection->tail);
    CHECK(exportSettingsFromJson(toJson({.range = ExportRange::Selection})).range == ExportRange::Selection);

    // Blocks of 0.5 s: sound until 9.5 s (a second past the end), then silence.
    BlockLevels levels{.blockSeconds = 0.5};
    for (int b = 0; b < 40; ++b)
        levels.peaks.push_back(b < 19 ? 0.5f : 0.0f);
    CHECK(exportLength(*song, levels) == Approx(9.5));
    CHECK(exportLength(*loop, levels) == Approx(2.0)); // no tail
    // A tail still sounding at the allowance stops there.
    std::fill(levels.peaks.begin(), levels.peaks.end(), 0.5f);
    CHECK(exportLength(*song, levels) == Approx(8.5 + kBounceTailSeconds));
}

TEST_CASE("export: file names, never overwriting", "[fast]")
{
    CHECK(exportFileName("Song", "", "", ExportFormat::Wav) == "Song.wav");
    CHECK(exportFileName("Song", "Bass", "", ExportFormat::Flac) == "Song - Bass.flac");
    CHECK(exportFileName("Song", "", "Bars 9-17", ExportFormat::Aiff) == "Song - Bars 9-17.aiff");
    CHECK(exportFileName("", "Lead: Vox/2", "", ExportFormat::Wav) == "Untitled - Lead- Vox-2.wav");
    const std::set<std::string> taken{"Song.wav", "Song 2.wav"};
    auto exists = [&](const std::string& n) { return taken.contains(n); };
    CHECK(uniqueFileName("Song.wav", exists) == "Song 3.wav");
    CHECK(uniqueFileName("Other.wav", exists) == "Other.wav");

    TempoMap tempo; // 120 BPM, 4/4: a bar is 2 s
    CHECK(loopBars(tempo, 16.0, 32.0) == "Bars 9-17");
}

TEST_CASE("export: normalising is a gain, the true peak always wins", "[fast]")
{
    CHECK(normaliseGain(ExportNormalise::Off, -14.0, -6.0, -20.0).db == 0.0);
    CHECK(normaliseGain(ExportNormalise::Peak, -14.0, -6.0, -20.0).db == Approx(5.0));
    CHECK(normaliseGain(ExportNormalise::Peak, -14.0, 2.0, -8.0).db == Approx(-3.0)); // down, too
    auto g = normaliseGain(ExportNormalise::Loudness, -14.0, -10.0, -20.0);
    CHECK(g.db == Approx(6.0));
    CHECK_FALSE(g.ceilingHeld);
    g = normaliseGain(ExportNormalise::Loudness, -14.0, -3.0, -20.0); // +6 would pass -1 dBTP
    CHECK(g.db == Approx(2.0));
    CHECK(g.ceilingHeld);
    CHECK(normaliseGain(ExportNormalise::Loudness, -14.0, -INFINITY, -INFINITY).db == 0.0); // silence
}

TEST_CASE("export: samples land on the file's grid, dithered by one step", "[fast]")
{
    // 16 bits: k / 32768, as JUCE's writers store them; without dither, plain rounding.
    Quantiser plain(16, false);
    CHECK(plain(0.5f) == 0.5f);
    CHECK(plain(1.0f / 65536.0f * 1.2f) == 1.0f / 32768.0f);
    CHECK(plain(2.0f) == 32767.0f / 32768.0f); // clipped to the largest value
    for (const float x : {0.123456f, -0.7f, 0.000031f})
    {
        const float q = plain(x);
        CHECK(std::abs(q * 32768.0f - std::round(q * 32768.0f)) < 1e-6f);
        CHECK(std::abs(q - x) <= 0.5f / 32768.0f + 1e-9f);
    }
    // Dither: silence becomes noise within one step, centred on zero, the same every time.
    Quantiser dithered(16, true, 7), again(16, true, 7);
    double sum = 0.0, squares = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const float q = dithered(0.0f);
        CHECK(again(0.0f) == q);
        CHECK(std::abs(q) <= 1.0f / 32768.0f + 1e-9f);
        sum += q;
        squares += static_cast<double>(q) * q;
    }
    CHECK(std::abs(sum / 48000.0) < 0.05 / 32768.0);
    CHECK(std::sqrt(squares / 48000.0) * 32768.0 == Approx(0.5).margin(0.1)); // rounded TPDF: about half a step
    // 32-bit float: as it is.
    Quantiser none(32, true);
    CHECK(none(0.123456789f) == 0.123456789f);
}

TEST_CASE(
    "export: stems with groups: each group whole, or every track and group alone; the selected as chosen",
    "[fast]")
{
    // Drums (10: Kick 1, Snare 2), Bass 3 in none; a return 20 (groups.md §11).
    Snapshot s;
    s.tracks.push_back({.id = 10, .role = TrackInfo::Role::Group});
    s.tracks.push_back({.id = 1, .group = 10});
    s.tracks.push_back({.id = 2, .group = 10});
    s.tracks.push_back({.id = 3});
    s.returns.push_back({.id = 20, .role = TrackInfo::Role::Return});
    using Ids = std::vector<std::uint64_t>;
    ExportSettings all{.what = ExportWhat::AllTracks};
    CHECK(stemChannels(s, all, {}) == Ids{10, 3, 20});
    all.stems = ExportStems::Tracks;
    CHECK(stemChannels(s, all, {}) == Ids{10, 1, 2, 3, 20});
    CHECK(stemChannels(s, {.what = ExportWhat::SelectedTracks}, {2, 10}) == Ids{10, 2});
    CHECK(stemChannels(s, {}, {1}).empty()); // the mix
    // Kept with the settings.
    CHECK(exportSettingsFromJson(toJson(all)).stems == ExportStems::Tracks);
    CHECK(ExportSettings{}.stems == ExportStems::Groups);
}
