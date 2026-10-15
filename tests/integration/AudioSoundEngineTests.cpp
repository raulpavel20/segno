// An audio clip's sound in the engine (M3 step P6, ADR 0063, docs/design/audio-editor.md §5, §6):
// gain, the gain line, polarity, channels and reverse, sample for sample through engine::Session and
// offline renders; the audio editor's range commands; Normalise.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/WavIO.h"
#include "model/AudioSound.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;
namespace te = tracktion;
namespace model = fd::model;

namespace
{
constexpr double kRate = 48000.0;

struct Sounder
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-audio-sound-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-sound-" + juce::Uuid().toString());

    Sounder()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        REQUIRE(root.createDirectory());
    }

    ~Sounder()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i = 0)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    std::uint64_t place(const AudioBuffer& audio, const std::string& name, double seconds = 0.0)
    {
        const auto file = root.getChildFile(name + ".wav");
        if (!file.existsAsFile())
            REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), audio, kRate));
        const auto id = session->insertAudioFile(track(), file, seconds);
        REQUIRE(id != 0);
        return id;
    }

    AudioBuffer render(double start, double end)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, kRate}, out.getFile(),
                                   [&](const std::string& e)
                                   {
                                       error = e;
                                       done = true;
                                   });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 1000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }

    model::ClipInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        for (const auto& t : session->snapshot()->tracks)
            for (const auto& c : t.clips)
                if (c.id == id)
                    return c;
        FAIL("no such clip");
        return {};
    }

    std::vector<model::ClipInfo> clips()
    {
        session->updateSnapshotNow();
        auto out = session->snapshot()->tracks.at(0).clips;
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        return out;
    }
};

/// A ramp on the left and a falling one on the right: every sample a different value on each side,
/// so a render shows exactly which sample played on which channel.
AudioBuffer ramps(double seconds)
{
    AudioBuffer b(2, static_cast<int>(seconds * kRate));
    for (int i = 0; i < b.numSamples(); ++i)
    {
        const float x = static_cast<float>(i) / static_cast<float>(b.numSamples());
        b.at(0, i) = 0.8f * x - 0.4f;
        b.at(1, i) = 0.3f - 0.6f * x;
    }
    return b;
}

/// The largest difference between `rendered` and what `expected(channel, frame)` says.
template <typename F> double worst(const AudioBuffer& rendered, F expected, int from = 0, int to = -1)
{
    if (to < 0)
        to = rendered.numSamples();
    double out = 0.0;
    for (int c = 0; c < rendered.numChannels(); ++c)
        for (int i = from; i < to; ++i)
            out = std::max(out, std::abs(static_cast<double>(rendered.at(c, i)) - expected(c, i)));
    return out;
}

double peak(const AudioBuffer& b, int from, int to)
{
    double out = 0.0;
    for (int c = 0; c < b.numChannels(); ++c)
        for (int i = from; i < to; ++i)
            out = std::max(out, static_cast<double>(std::abs(b.at(c, i))));
    return out;
}
} // namespace

TEST_CASE("audio sound: gain plays every sample at its level", "[audio-sound][engine]")
{
    Sounder s;
    const auto source = ramps(1.0);
    const auto clip = s.place(source, "ramps");
    REQUIRE(s.session->setSound({clip}, {.gainDb = static_cast<float>(20.0 * std::log10(0.5))}, "Gain"));
    CHECK(s.session->undoDescription() == "Gain");
    CHECK(s.info(clip).sound.gainDb == Approx(-6.0206f));
    const auto out = s.render(0.0, 1.0);
    CHECK(worst(out, [&](int c, int i) { return 0.5 * source.at(c, i); }) < 1e-5);

    s.session->undo();
    CHECK(s.info(clip).sound.gainDb == 0.0f);
    CHECK(worst(s.render(0.0, 1.0), [&](int c, int i) { return source.at(c, i); }) < 1e-6);
}

TEST_CASE("audio sound: polarity and channels play as named", "[audio-sound][engine]")
{
    Sounder s;
    const auto source = ramps(0.5);
    const auto clip = s.place(source, "ramps");

    REQUIRE(s.session->setSound({clip}, {.inverted = true}, "Polarity"));
    CHECK(worst(s.render(0.0, 0.5), [&](int c, int i) { return -source.at(c, i); }) < 1e-6);
    REQUIRE(s.session->setSound({clip}, {.inverted = false}));

    const auto expect = [&](model::ChannelMode mode, int c, int i)
    {
        float l = source.at(0, i), r = source.at(1, i);
        model::applyChannels(mode, l, r);
        return static_cast<double>(c == 0 ? l : r);
    };
    for (auto mode : {model::ChannelMode::Left, model::ChannelMode::Right, model::ChannelMode::Mono,
                      model::ChannelMode::Swapped})
    {
        INFO(model::channelModeName(mode));
        REQUIRE(s.session->setSound({clip}, {.channels = mode}, "Channels"));
        CHECK(s.info(clip).sound.channels == mode);
        CHECK(worst(s.render(0.0, 0.5), [&](int c, int i) { return expect(mode, c, i); }) < 1e-6);
    }
}

TEST_CASE("audio sound: the gain line is pinned to the file, wherever the clip plays it",
          "[audio-sound][engine]")
{
    Sounder s;
    const auto source = ramps(1.0);
    const auto clip = s.place(source, "ramps");
    // From 0 dB at 0.2 s of the file to −∞ at 0.8 s.
    const model::AutomationPoints line{{0.2, model::lineValueForDb(0.0), 0.0f}, {0.8, 0.0f, 0.0f}};
    REQUIRE(s.session->setGainLine(clip, line));
    CHECK(s.info(clip).sound.line == line);
    const auto out = s.render(0.0, 1.0);
    CHECK(worst(out, [&](int c, int i) { return source.at(c, i) * model::lineGainAt(line, i / kRate); }) <
          1e-5);

    // Moved half a second later, the line moves with the audio.
    s.session->applyClipEdits(
        {{.clip = clip, .track = s.track(), .start = 0.5, .length = 1.0, .offset = 0.0}});
    const auto later = s.render(0.5, 1.5);
    CHECK(worst(later, [&](int c, int i) { return source.at(c, i) * model::lineGainAt(line, i / kRate); }) <
          1e-5);
}

TEST_CASE("audio sound: a reversed clip plays its file backwards, from a copy in the project",
          "[audio-sound][engine]")
{
    Sounder s;
    const auto source = ramps(0.5);
    const int n = source.numSamples();
    const auto clip = s.place(source, "ramps");
    const auto forward = s.info(clip).sourcePath;
    REQUIRE(s.session->setSound({clip}, {.reversed = true}, "Reverse"));
    CHECK(s.session->undoDescription() == "Reverse");
    const auto reversed = s.info(clip);
    CHECK(reversed.sound.reversed);
    CHECK(juce::File(reversed.sourcePath).getParentDirectory().getFileName() == "Reversed");
    CHECK(juce::File(forward).existsAsFile()); // the file itself is never changed
    CHECK(worst(s.render(0.0, 0.5), [&](int c, int i) { return source.at(c, n - 1 - i); }, 0, n) < 1e-6);

    // Back again: the file it played forwards.
    REQUIRE(s.session->setSound({clip}, {.reversed = false}, "Reverse"));
    CHECK(s.info(clip).sourcePath == forward);
    CHECK(worst(s.render(0.0, 0.5), [&](int c, int i) { return source.at(c, i); }, 0, n) < 1e-6);

    // Another clip of the same file reversed uses the same copy.
    REQUIRE(s.session->setSound({clip}, {.reversed = true}));
    const auto other = s.place(source, "ramps", 1.0);
    REQUIRE(s.session->setSound({other}, {.reversed = true}));
    CHECK(s.info(other).sourcePath == reversed.sourcePath);
}

TEST_CASE("audio sound: reversed, a part of a clip plays the same audio backwards, its gain line with it",
          "[audio-sound][engine]")
{
    Sounder s;
    const auto source = ramps(1.0);
    const auto clip = s.place(source, "ramps");
    // The clip plays 0.25 … 0.75 s of its file; the line dips at 0.3 s of the file.
    s.session->applyClipEdits(
        {{.clip = clip, .track = s.track(), .start = 0.0, .length = 0.5, .offset = 0.25}});
    const model::AutomationPoints line{{0.29, model::lineValueForDb(0.0), 0.0f},
                                       {0.3, 0.0f, 0.0f},
                                       {0.31, model::lineValueForDb(0.0), 0.0f}};
    REQUIRE(s.session->setGainLine(clip, line));
    REQUIRE(s.session->setSound({clip}, {.reversed = true}));
    const auto c = s.info(clip);
    CHECK(c.offset == Approx(0.25).margin(1e-9)); // 1 − (0.25 + 0.5)
    const auto out = s.render(0.0, 0.5);
    // At t, it plays the file at 0.75 − t, through the line there.
    const auto expected = [&](int ch, int i)
    {
        const double fileTime = 0.75 - static_cast<double>(i) / kRate;
        const int frame = static_cast<int>(std::lround(fileTime * kRate)) - 1;
        return source.at(ch, frame) * model::lineGainAt(line, fileTime - 1.0 / kRate);
    };
    // Its edges cut into the file, so they fade over 4 ms (fades.md §2): compared between them.
    CHECK(worst(out, expected, static_cast<int>(0.005 * kRate), static_cast<int>(0.495 * kRate)) < 2e-3);
    // The dip is at 0.75 − 0.3 = 0.45 s.
    CHECK(peak(out, static_cast<int>(0.4495 * kRate), static_cast<int>(0.4505 * kRate)) < 0.05);
}

TEST_CASE("audio sound: a range splits, deletes, crops, silences and fades as asked", "[audio-sound][engine]")
{
    Sounder s;
    const auto source = ramps(1.0);
    const auto clip = s.place(source, "ramps");
    using Action = Session::RangeAction;

    SECTION("split: the range becomes a clip of its own, and plays as before")
    {
        const auto r = s.session->editRange(clip, 0.25, 0.5, Action::Split);
        REQUIRE(r.why.empty());
        CHECK(s.session->undoDescription() == "Split");
        const auto parts = s.clips();
        REQUIRE(parts.size() == 3);
        CHECK(parts[1].id == r.clips.front());
        CHECK(parts[1].start == Approx(0.25));
        CHECK(parts[1].length == Approx(0.25));
        CHECK(worst(s.render(0.0, 1.0), [&](int c, int i) { return source.at(c, i); }) < 1e-6);
        s.session->undo();
        CHECK(s.clips().size() == 1);
    }
    SECTION("delete: a gap where the range was; the rest stays where it is")
    {
        const auto r = s.session->editRange(clip, 0.25, 0.5, Action::Delete);
        REQUIRE(r.why.empty());
        CHECK(s.session->undoDescription() == "Delete");
        const auto parts = s.clips();
        REQUIRE(parts.size() == 2);
        CHECK(parts[0].end() == Approx(0.25));
        CHECK(parts[1].start == Approx(0.5));
        const auto out = s.render(0.0, 1.0);
        CHECK(peak(out, static_cast<int>(0.26 * kRate), static_cast<int>(0.49 * kRate)) == 0.0);
    }
    SECTION("crop: only the range is left")
    {
        REQUIRE(s.session->editRange(clip, 0.25, 0.5, Action::Crop).why.empty());
        const auto parts = s.clips();
        REQUIRE(parts.size() == 1);
        CHECK(parts[0].start == Approx(0.25));
        CHECK(parts[0].length == Approx(0.25));
    }
    SECTION("silence: nothing over the range but its ramps; the rest as recorded")
    {
        REQUIRE(s.session->editRange(clip, 0.25, 0.5, Action::Silence).why.empty());
        CHECK(s.session->undoDescription() == "Silence");
        const auto out = s.render(0.0, 1.0);
        CHECK(peak(out, static_cast<int>(0.255 * kRate), static_cast<int>(0.495 * kRate)) == 0.0);
        CHECK(worst(
                  out, [&](int c, int i) { return source.at(c, i); }, 0, static_cast<int>(0.25 * kRate)) <
              1e-6);
        CHECK(
            worst(out, [&](int c, int i) { return source.at(c, i); }, static_cast<int>(0.5 * kRate)) < 1e-6);
    }
    SECTION("gain: the range at −6 dB")
    {
        REQUIRE(s.session->editRange(clip, 0.25, 0.5, Action::Gain, 20.0 * std::log10(0.5)).why.empty());
        const auto out = s.render(0.0, 1.0);
        CHECK(worst(
                  out, [&](int c, int i) { return 0.5 * source.at(c, i); }, static_cast<int>(0.26 * kRate),
                  static_cast<int>(0.49 * kRate)) < 1e-4);
    }
    SECTION("fade: from the start or to the end, never in the middle")
    {
        REQUIRE(s.session->editRange(clip, 0.0, 0.1, Action::Fade).why.empty());
        CHECK(s.info(clip).fadeIn.length == Approx(0.1));
        REQUIRE(s.session->editRange(clip, 0.8, 1.0, Action::Fade).why.empty());
        CHECK(s.info(clip).fadeOut.length == Approx(0.2));
        CHECK_FALSE(s.session->editRange(clip, 0.4, 0.6, Action::Fade).why.empty());
    }
    SECTION("loop: the clip loops the range")
    {
        REQUIRE(s.session->editRange(clip, 0.25, 0.5, Action::Loop).why.empty());
        const auto c = s.info(clip);
        REQUIRE(c.loop.on());
        CHECK(c.loop.start == Approx(0.25).margin(1e-4));
        CHECK(c.loop.length == Approx(0.25).margin(1e-4));
    }
    SECTION("refusals: the whole clip, outside it, and nothing changes")
    {
        CHECK_FALSE(s.session->rangeRefusal(clip, 0.0, 1.0, Action::Delete).empty());
        CHECK_FALSE(s.session->rangeRefusal(clip, 0.5, 2.0, Action::Split).empty());
        CHECK_FALSE(s.session->editRange(clip, 0.0, 1.0, Action::Split).why.empty());
        CHECK(s.clips().size() == 1);
    }
}

TEST_CASE("audio sound: Normalise sets each clip's gain to reach its target", "[audio-sound][engine]")
{
    Sounder s;
    AudioBuffer quiet(2, static_cast<int>(kRate)), loud(2, static_cast<int>(kRate)),
        silent(2, static_cast<int>(kRate));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < quiet.numSamples(); ++i)
        {
            const auto x = std::sin(2.0 * 3.141592653589793 * 440.0 * i / kRate);
            quiet.at(c, i) = static_cast<float>(0.25 * x);
            loud.at(c, i) = static_cast<float>(0.5 * x);
        }
    const auto a = s.place(quiet, "quiet", 0.0);
    const auto b = s.place(loud, "loud", 2.0);
    const auto z = s.place(silent, "silent", 4.0);

    SECTION("each to −1 dBFS")
    {
        const auto changed = s.session->normalise({a, b, z}, {});
        CHECK(changed.size() == 2);
        CHECK(s.session->undoDescription() == "Normalise");
        CHECK(s.info(a).sound.gainDb == Approx(-1.0 + 12.04).margin(0.02));
        CHECK(s.info(b).sound.gainDb == Approx(-1.0 + 6.02).margin(0.02));
        CHECK(s.info(z).sound.gainDb == 0.0f);
        const auto out = s.render(0.0, 3.0);
        CHECK(20.0 * std::log10(peak(out, 0, static_cast<int>(kRate))) == Approx(-1.0).margin(0.05));
        CHECK(20.0 * std::log10(peak(out, static_cast<int>(2.0 * kRate), static_cast<int>(3.0 * kRate))) ==
              Approx(-1.0).margin(0.05));
    }
    SECTION("together: the loudest to −1 dBFS, the balance kept")
    {
        s.session->normalise({a, b}, {.together = true});
        CHECK(s.info(b).sound.gainDb == Approx(5.02).margin(0.02));
        CHECK(s.info(a).sound.gainDb == Approx(5.02).margin(0.02));
    }
}
