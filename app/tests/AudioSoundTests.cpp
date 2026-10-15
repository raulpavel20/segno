// An audio clip's sound (M3 step P6, ADR 0063, docs/design/audio-editor.md §5, §6): gain, the gain
// line and its range edits, channels, where a clip reads its file, and Normalise's gains.

#include "model/AudioSound.h"
#include "model/Snapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fd/audiofile/AudioFile.h>
#include <filesystem>
#include <numbers>

using Catch::Approx;
using namespace fd::model;

namespace
{
float zeroDb()
{
    return lineValueForDb(0.0);
}

double dbOf(float gain)
{
    return 20.0 * std::log10(gain);
}
} // namespace

TEST_CASE("audio sound: channel modes by name, both ways", "[fast][audio-sound]")
{
    for (auto mode : {ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Right, ChannelMode::Mono,
                      ChannelMode::Swapped})
        CHECK(channelModeFromName(channelModeName(mode)) == mode);
    CHECK(channelModeFromName("MONO") == ChannelMode::Mono);
    CHECK_FALSE(channelModeFromName("sideways"));
}

TEST_CASE("audio sound: each channel mode plays a frame as named", "[fast][audio-sound]")
{
    const auto played = [](ChannelMode mode)
    {
        float l = 0.8f, r = -0.2f;
        applyChannels(mode, l, r);
        return std::pair{l, r};
    };
    CHECK(played(ChannelMode::Stereo) == std::pair{0.8f, -0.2f});
    CHECK(played(ChannelMode::Left) == std::pair{0.8f, 0.8f});
    CHECK(played(ChannelMode::Right) == std::pair{-0.2f, -0.2f});
    CHECK(played(ChannelMode::Mono).first == Approx(0.3f));
    CHECK(played(ChannelMode::Mono).second == Approx(0.3f));
    CHECK(played(ChannelMode::Swapped) == std::pair{-0.2f, 0.8f});
}

TEST_CASE("audio sound: the gain line is a volume lane, 0 dB at 80 % and +6 dB at the top",
          "[fast][audio-sound]")
{
    CHECK(zeroDb() == Approx(0.8f));
    CHECK(lineDbForValue(1.0f) == Approx(6.0).margin(1e-6));
    CHECK(lineGainAt({}, 3.0) == 1.0f);
    CHECK(lineGainAt({{0.0, zeroDb(), 0.0f}}, 3.0) == Approx(1.0f));
    CHECK(lineGainAt({{0.0, 0.0f, 0.0f}}, 3.0) == 0.0f); // silence

    // From 0 dB to −6 dB over a second, straight on the taper: the gain halfway is between.
    const AutomationPoints line{{1.0, zeroDb(), 0.0f}, {2.0, lineValueForDb(-6.0), 0.0f}};
    CHECK(dbOf(lineGainAt(line, 0.5)) == Approx(0.0).margin(1e-4)); // before the first point: its value
    CHECK(dbOf(lineGainAt(line, 2.5)) == Approx(-6.0).margin(1e-4));
    const double halfway = dbOf(lineGainAt(line, 1.5));
    CHECK(halfway < -0.5);
    CHECK(halfway > -5.5);
}

TEST_CASE("audio sound: a clip's gain and its line multiply", "[fast][audio-sound]")
{
    AudioSound sound;
    CHECK(sound.neutral());
    sound.gainDb = static_cast<float>(20.0 * std::log10(0.5));
    CHECK_FALSE(sound.neutral());
    CHECK(soundGainAt(sound, 1.0) == Approx(0.5f));
    sound.line = {{0.0, lineValueForDb(20.0 * std::log10(0.5)), 0.0f}};
    CHECK(soundGainAt(sound, 1.0) == Approx(0.25f));
    sound.gainDb = minusInfinityDb;
    CHECK(soundGainAt(sound, 1.0) == 0.0f);

    AudioSound flat;
    flat.line = {{0.0, zeroDb(), 0.0f}, {4.0, zeroDb(), 0.0f}};
    CHECK(flat.neutral()); // a line at 0 dB changes nothing
    flat.inverted = true;
    CHECK_FALSE(flat.neutral());
}

TEST_CASE("audio sound: a reversed clip reads its file from the end", "[fast][audio-sound]")
{
    CHECK(fileTimeOf(1.25, 10.0, false) == 1.25);
    CHECK(fileTimeOf(1.25, 10.0, true) == 8.75);
}

TEST_CASE("audio sound: a range's gain moves the line inside it and keeps its edges", "[fast][audio-sound]")
{
    const auto line = lineWithGain({}, {1.0, 2.0}, -6.0, 0.004);
    REQUIRE(line.size() == 4);
    CHECK(line[0].time == 1.0);
    CHECK(line[0].value == Approx(zeroDb()));
    CHECK(line[1].time == Approx(1.004));
    CHECK(lineDbForValue(line[1].value) == Approx(-6.0).margin(1e-4));
    CHECK(line[2].time == Approx(1.996));
    CHECK(lineDbForValue(line[2].value) == Approx(-6.0).margin(1e-4));
    CHECK(line[3].time == 2.0);
    CHECK(line[3].value == Approx(zeroDb()));
    CHECK(dbOf(lineGainAt(line, 1.5)) == Approx(-6.0).margin(1e-3));
    CHECK(dbOf(lineGainAt(line, 0.5)) == Approx(0.0).margin(1e-4));
    CHECK(dbOf(lineGainAt(line, 2.5)) == Approx(0.0).margin(1e-4));

    // Points inside move with it; points outside stay; the level at each edge is what it was.
    const AutomationPoints shaped{
        {0.0, zeroDb(), 0.0f}, {1.5, lineValueForDb(-3.0), 0.0f}, {3.0, zeroDb(), 0.0f}};
    const auto lowered = lineWithGain(shaped, {1.0, 2.0}, -6.0, 0.004);
    CHECK(lowered.front() == shaped.front());
    CHECK(lowered.back() == shaped.back());
    CHECK(automationValueAt(lowered, 1.0) == Approx(automationValueAt(shaped, 1.0)));
    CHECK(automationValueAt(lowered, 2.0) == Approx(automationValueAt(shaped, 2.0)));
    CHECK(lineDbForValue(automationValueAt(lowered, 1.5)) == Approx(-9.0).margin(1e-3));

    // Never above the line's top (+6 dB).
    const auto raised = lineWithGain({}, {1.0, 2.0}, +20.0, 0.004);
    CHECK(raised[1].value == 1.0f);

    // A span shorter than two ramps ramps over half of it each way.
    const auto tiny = lineWithGain({}, {1.0, 1.004}, -6.0, 0.004);
    CHECK(tiny[1].time == Approx(1.002));
}

TEST_CASE("audio sound: silencing a range falls to silence inside its ramps", "[fast][audio-sound]")
{
    const auto line = lineSilenced({}, {1.0, 2.0}, 0.004);
    CHECK(lineGainAt(line, 1.5) == 0.0f);
    CHECK(lineGainAt(line, 1.0) == Approx(1.0f));
    CHECK(lineGainAt(line, 2.0) == Approx(1.0f));
    CHECK(lineGainAt(line, 1.002) > 0.0f);
    // Silence moved by a gain stays silence.
    CHECK(lineGainAt(lineWithGain(line, {1.2, 1.8}, 6.0), 1.5) == 0.0f);
}

TEST_CASE("audio sound: the file a clip plays over a range of the timeline", "[fast][audio-sound]")
{
    ClipInfo clip;
    clip.kind = ClipInfo::Kind::Audio;
    clip.start = 10.0;
    clip.length = 4.0;
    clip.offset = 1.0;
    clip.sourceLength = clip.fileLength = 8.0;
    CHECK(fileSpans(clip, 11.0, 12.0) == std::vector<LineSpan>{{2.0, 3.0}});
    CHECK(fileSpans(clip, 9.0, 10.5) == std::vector<LineSpan>{{1.0, 1.5}}); // cut to the clip
    auto reversed = clip;
    reversed.sound.reversed = true;
    CHECK(fileSpans(reversed, 11.0, 12.0) == std::vector<LineSpan>{{5.0, 6.0}}); // read from the end

    // Warped (P6b): the file at 120 BPM in a song at 60, so a second of the song is half a second of
    // the file.
    auto warped = clip;
    warped.warp.on = true;
    warped.warp.tempo = 120.0;
    warped.beatsPerSecond = 1.0;
    CHECK(fileSpans(warped, 11.0, 12.0) == std::vector<LineSpan>{{1.0, 1.5}});

    // A looped clip: each lap plays the same stretch of the file, kept once.
    clip.loop = {1.0, 1.0};
    clip.offset = 0.0;
    CHECK(fileSpans(clip, 10.25, 10.75) == std::vector<LineSpan>{{1.25, 1.75}});
    CHECK(fileSpans(clip, 10.25, 11.75) == std::vector<LineSpan>{{1.25, 2.0}, {1.0, 1.75}});
    CHECK(fileSpans(clip, 10.25, 12.75) == std::vector<LineSpan>{{1.25, 2.0}, {1.0, 2.0}, {1.0, 1.75}});
}

TEST_CASE("audio sound: the gain line shows where its moments of the file play", "[fast][audio-sound]")
{
    const AutomationPoints line{{1.0, 0.2f, 0.5f}, {2.0, 0.8f, 0.0f}};
    ContentMap plain;
    plain.fileLength = 4.0;
    auto shown = lineToView(line, 10.0, plain);
    CHECK(shown[0].time == Approx(11.0));
    CHECK(lineFromView(shown, 10.0, plain) == line);
    // Reversed: mirrored, the segment bending the other way.
    auto backwards = plain;
    backwards.reversed = true;
    shown = lineToView(line, 10.0, backwards);
    CHECK(shown[0].time == Approx(12.0));
    CHECK(shown[0].value == 0.8f);
    CHECK(shown[0].curve == -0.5f);
    CHECK(lineFromView(shown, 10.0, backwards) == line);
    // Warped at twice the song's speed: each moment of the file plays at half its time.
    ContentMap warped = plain;
    warped.warped = true;
    warped.played.tempo = 120.0;
    warped.beatsPerSecond = 4.0; // 240 BPM
    shown = lineToView(line, 10.0, warped);
    CHECK(shown[0].time == Approx(10.5));
    CHECK(shown[1].time == Approx(11.0));
    const auto back = lineFromView(shown, 10.0, warped);
    CHECK(back[1].time == Approx(2.0));
}

TEST_CASE("audio sound: Normalise each, together, and leave silence alone", "[fast][audio-sound]")
{
    const std::vector<ClipLevels> levels{{-6.0, -20.0}, {-12.0, -26.0}, {}};
    const std::vector<float> gains{0.0f, 3.0f, 0.0f};

    const auto each = normaliseGains(levels, gains, NormaliseTo::Peak, -1.0, false);
    CHECK(each[0] == Approx(5.0f));
    CHECK(each[1] == Approx(11.0f)); // its gain is set, not added to: measured without it
    CHECK_FALSE(each[2]);

    // Together: as they play now, the first is the loudest (−6, against −12 + 3 = −9), so each
    // gain moves by the +5 dB that brings it to −1.
    const auto together = normaliseGains(levels, gains, NormaliseTo::Peak, -1.0, true);
    CHECK(together[0] == Approx(5.0f));
    CHECK(together[1] == Approx(8.0f));
    CHECK_FALSE(together[2]);

    const auto loudness = normaliseGains(levels, gains, NormaliseTo::Loudness, -14.0, false);
    CHECK(loudness[0] == Approx(6.0f));
    CHECK(loudness[1] == Approx(12.0f));

    // Kept within the clip's range.
    CHECK(normaliseGains({{-60.0, -80.0}}, {0.0f}, NormaliseTo::Peak, 0.0, false)[0] ==
          Approx(kMaxClipGainDb));
}

TEST_CASE("audio sound: measuring a clip streams its file through its gain line", "[fast][audio-sound]")
{
    const auto dir = std::filesystem::temp_directory_path() / "fd-audio-sound-tests";
    std::filesystem::create_directories(dir);
    const auto path = (dir / "sine.wav").string();
    constexpr double rate = 48000.0;
    std::vector<std::vector<float>> audio(2, std::vector<float>(static_cast<size_t>(rate * 4)));
    for (size_t i = 0; i < audio[0].size(); ++i)
    {
        // A second at −6 dBFS, then three at −12 dBFS.
        const float amp = i < static_cast<size_t>(rate) ? 0.5f : 0.25f;
        audio[0][i] = audio[1][i] =
            amp *
            static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / rate));
    }
    REQUIRE(fd::audiofile::writeWav(path, audio, rate));

    AudioSound sound;
    const auto whole = measureClip(path, 0.0, 4.0, sound);
    REQUIRE(whole);
    CHECK(whole->peakDb == Approx(-6.02).margin(0.05));
    const auto tail = measureClip(path, 1.5, 4.0, sound);
    CHECK(tail->peakDb == Approx(-12.04).margin(0.05));
    CHECK(tail->lufs < whole->lufs);

    // Reversed, the first 2.5 s of its content are the file's last 2.5 s.
    sound.reversed = true;
    CHECK(measureClip(path, 0.0, 2.5, sound)->peakDb == Approx(-12.04).margin(0.05));
    sound.reversed = false;

    // The line lowers what it measures.
    sound.line = {{0.0, lineValueForDb(-6.0), 0.0f}};
    CHECK(measureClip(path, 0.0, 4.0, sound)->peakDb == Approx(-12.04).margin(0.05));

    CHECK_FALSE(measureClip((dir / "missing.wav").string(), 0.0, 1.0, {}));
    std::filesystem::remove_all(dir);
}

TEST_CASE("audio sound: where Slice cuts: transients and markers in every lap, the grid, bars, a range",
          "[fast][audio-sound]")
{
    TempoMap tempo; // 120 BPM, 4/4
    ClipInfo clip;
    clip.kind = ClipInfo::Kind::Audio;
    clip.start = 10.0;
    clip.length = 4.0;
    clip.offset = 1.0;
    clip.sourceLength = clip.fileLength = 8.0;
    // Its transients where it plays them; those before its offset, past its end or at its edges, not.
    const std::vector<double> transients{0.5, 1.0, 1.5, 2.25, 4.995, 6.0};
    CHECK(sliceTimes(clip, SliceAt::Transients, transients, 0.25, tempo) == std::vector<double>{10.5, 11.25});
    // A reversed clip plays its file from the end: file 6.0 at 11.0, 4.995 at 12.005; 2.25 is past
    // its end.
    auto reversed = clip;
    reversed.sound.reversed = true;
    const auto backwards = sliceTimes(reversed, SliceAt::Transients, transients, 0.25, tempo);
    REQUIRE(backwards.size() == 2);
    CHECK(backwards[0] == Approx(11.0));
    CHECK(backwards[1] == Approx(12.005));
    // In a range of it only.
    CHECK(sliceTimes(clip, SliceAt::Transients, transients, 0.25, tempo, std::pair{11.0, 14.0}) ==
          std::vector<double>{11.25});
    // Looped: in every lap.
    auto looped = clip;
    looped.loop = {1.0, 1.0};
    looped.offset = 0.0;
    CHECK(sliceTimes(looped, SliceAt::Transients, {1.0, 1.5}, 0.25, tempo) ==
          std::vector<double>{10.5, 11.0, 11.5, 12.0, 12.5, 13.0, 13.5});
    // Warp markers, while it warps (a file at the song's tempo plays as recorded).
    auto warped = clip;
    warped.warp.on = true;
    warped.warp.tempo = 120.0;
    warped.warp.markers = {{2.0, 4.0}, {3.0, 6.0}};
    warped.beatsPerSecond = 2.0;
    CHECK(sliceTimes(warped, SliceAt::Markers, {}, 0.25, tempo) == std::vector<double>{11.0, 12.0});
    CHECK(sliceTimes(clip, SliceAt::Markers, {}, 0.25, tempo).empty()); // not warped: none play
    // The song's grid (a beat: half a second) and its bars (two seconds).
    CHECK(sliceTimes(clip, SliceAt::Grid, {}, 1.0, tempo) ==
          std::vector<double>{10.5, 11.0, 11.5, 12.0, 12.5, 13.0, 13.5});
    CHECK(sliceTimes(clip, SliceAt::Bars, {}, 1.0, tempo) == std::vector<double>{12.0});
    clip.start = 9.9;
    CHECK(sliceTimes(clip, SliceAt::Bars, {}, 1.0, tempo) == std::vector<double>{10.0, 12.0});
    // Names, both ways.
    for (const auto at : {SliceAt::Transients, SliceAt::Markers, SliceAt::Grid, SliceAt::Bars})
        CHECK(sliceAtFromName(sliceAtName(at)) == at);
    CHECK_FALSE(sliceAtFromName("everywhere"));
}
