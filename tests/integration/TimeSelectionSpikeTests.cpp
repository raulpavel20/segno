// Time selection, step 0's check (docs/design/time-selection.md §21, ADR 0073): a stretch bounced
// in place keeps the clips it covered, gaps and all, as one "Before bounce" take, which plays them
// back, is switched to and back, survives saving and opening, and goes with one undo step. Step 2
// (§13): a stretch crossing clips is cut at its edges, in place and to a new track.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"
#include "model/Takes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using fd::model::BounceMode;
using fd::model::ClipInfo;
using fd::testkit::AudioBuffer;

namespace
{
struct Stretch
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-bounce-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-time-selection-" + juce::Uuid().toString());

    Stretch()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
    }

    ~Stretch()
    {
        session.reset();
        root.deleteRecursively();
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// A copy (snapshots are replaced).
    fd::model::TrackInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }

    /// A track playing the Synth, its drift off so every render is the same performance; with
    /// `release`, its amp's release.
    std::uint64_t synthTrack(const char* release = nullptr)
    {
        const auto t = track(0);
        const auto synth = session->addDevice(t, "fd:subtractive-synth");
        REQUIRE(synth != 0);
        auto handle = session->openDevice(synth);
        REQUIRE(handle != nullptr);
        auto& context = handle->context();
        const auto& params = context.info.params;
        const auto set = [&](const char* id, const char* text)
        {
            const auto it =
                std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.id == id; });
            REQUIRE(it != params.end());
            const auto value = it->fromText(text);
            REQUIRE(value);
            context.params.setPlain(static_cast<size_t>(it - params.begin()), *value);
        };
        set("voice.drift", "0");
        if (release != nullptr)
            set("amp.r", release);
        return t;
    }

    /// A MIDI clip over [start, start + length) with a note on every beat (120 BPM).
    std::uint64_t phrase(std::uint64_t t, double start, double length, int pitch)
    {
        const auto clip = session->createMidiClip(t, start, length);
        REQUIRE(clip != 0);
        std::vector<fd::model::Note> notes;
        for (double b = 0.0; b + 0.5 <= length * 2.0; b += 1.0)
            notes.push_back(
                {.pitch = pitch + static_cast<int>(b) % 5, .start = b, .length = 0.5, .velocity = 0.8f});
        session->setNotes(clip, notes);
        return clip;
    }

    AudioBuffer render(double start, double end)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, std::nullopt, session->sampleRate()}, out.getFile(),
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

    Session::BounceResult bounceStretch(std::uint64_t t, double from, double to)
    {
        std::optional<Session::BounceResult> result;
        auto job = session->bounceStretch({t}, from, to, BounceMode::InPlace,
                                          [&](const Session::BounceResult& r) { result = r; });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 2000 && !result; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(result);
        session->updateSnapshotNow();
        return *result;
    }
};

/// The largest difference between `a` and `b` (dBFS), over samples [from, to) (all of them by default).
double differenceDb(const AudioBuffer& a, const AudioBuffer& b, int from = 0, int to = -1)
{
    REQUIRE(a.numChannels() == b.numChannels());
    REQUIRE(a.numSamples() == b.numSamples());
    float worst = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int s = from; s < (to < 0 ? a.numSamples() : to); ++s)
            worst = std::max(worst, std::abs(a.at(c, s) - b.at(c, s)));
    return worst > 0.0f ? 20.0 * std::log10(worst) : -200.0;
}

double peakDb(const AudioBuffer& a)
{
    float peak = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int s = 0; s < a.numSamples(); ++s)
            peak = std::max(peak, std::abs(a.at(c, s)));
    return peak > 0.0f ? 20.0 * std::log10(peak) : -200.0;
}

/// The take named `name` in a track's only stack.
const fd::model::TakeInfo& takeNamed(const fd::model::TrackInfo& info, const std::string& name)
{
    REQUIRE(info.stacks.size() == 1);
    for (const auto& t : info.stacks[0].takes)
        if (t.name == name)
            return t;
    FAIL("no take named " << name);
    return info.stacks[0].takes.front();
}

/// The track's main-lane clips as (kind, start, end), in time order.
std::vector<std::tuple<ClipInfo::Kind, double, double>> lane(const fd::model::TrackInfo& info)
{
    std::vector<std::tuple<ClipInfo::Kind, double, double>> out;
    for (const auto& c : info.clips)
        out.emplace_back(c.kind, c.start, c.end());
    return out;
}
} // namespace

TEST_CASE("time selection, step 0: a stretch over two clips and a gap bounces in place to one clip, "
          "the clips kept together as its Before bounce take, gap and all",
          "[integration]")
{
    Stretch s;
    const auto t = s.synthTrack();
    s.phrase(t, 0.5, 1.5, 48); // 0.5 .. 2.0
    s.phrase(t, 3.0, 1.0, 55); // 3.0 .. 4.0: a gap of a second between them
    const auto before = s.render(0.0, 8.0);
    REQUIRE(peakDb(before) > -40.0);
    const auto firstName = s.info(t).clips.at(0).name;

    // The stretch 0 .. 4.5: a gap at its start, the two clips, the gap between them and one after.
    const auto result = s.bounceStretch(t, 0.0, 4.5);
    CHECK(result.error.empty());
    REQUIRE(result.clips.size() == 1);
    CHECK(s.session->undoDescription() == "Bounce in place");
    auto info = s.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].kind == ClipInfo::Kind::Audio);
    CHECK(info.clips[0].start == Approx(0.0));
    CHECK(info.clips[0].end() >= 4.5 - 1e-6); // the stretch, and the release tail after it if any
    CHECK(info.clips[0].name == firstName);   // named after the first clip, as a bounce is
    // One stack: "Before bounce" holding both clips where they were, with the gap between them.
    REQUIRE(info.stacks.size() == 1);
    REQUIRE(info.stacks[0].takes.size() == 2);
    const auto& kept = takeNamed(info, "Before bounce");
    REQUIRE(kept.clips.size() == 2);
    CHECK(kept.clips[0].kind == ClipInfo::Kind::Midi);
    CHECK(kept.clips[0].start == Approx(0.5));
    CHECK(kept.clips[0].end() == Approx(2.0));
    CHECK(kept.clips[1].start == Approx(3.0));
    CHECK(kept.clips[1].end() == Approx(4.0));
    CHECK(info.stacks[0].takes[1].name == "Bounce");
    CHECK(info.stacks[0].start == Approx(0.0));
    // What plays is what played.
    CHECK(differenceDb(s.render(0.0, 8.0), before) < -100.0);

    // Back to the clips: both, in their places, the gap empty; it still sounds the same.
    REQUIRE(s.session->useTake(t, kept.id));
    info = s.info(t);
    const auto back = lane(info);
    REQUIRE(back.size() == 2);
    CHECK(std::get<0>(back[0]) == ClipInfo::Kind::Midi);
    CHECK(std::get<1>(back[0]) == Approx(0.5));
    CHECK(std::get<2>(back[0]) == Approx(2.0));
    CHECK(std::get<1>(back[1]) == Approx(3.0));
    CHECK(std::get<2>(back[1]) == Approx(4.0));
    CHECK(differenceDb(s.render(0.0, 8.0), before) < -100.0);

    // And to the bounce again.
    const auto bounceTake = takeNamed(info, "Bounce").id;
    REQUIRE(s.session->useTake(t, bounceTake));
    info = s.info(t);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].kind == ClipInfo::Kind::Audio);
    CHECK(differenceDb(s.render(0.0, 8.0), before) < -100.0);

    // Saved and opened: the stack, its two takes and the gap are kept, and switching still works.
    const auto project = s.root.getChildFile("Stretch").getChildFile("Stretch.segno");
    REQUIRE(project.getParentDirectory().createDirectory());
    REQUIRE(s.session->saveAs(project));
    REQUIRE(s.session->open(project));
    const auto reopened = s.track(0);
    info = s.info(reopened);
    REQUIRE(info.clips.size() == 1);
    CHECK(info.clips[0].kind == ClipInfo::Kind::Audio);
    const auto& keptAgain = takeNamed(info, "Before bounce");
    REQUIRE(keptAgain.clips.size() == 2);
    CHECK(keptAgain.clips[1].start == Approx(3.0));
    CHECK(differenceDb(s.render(0.0, 8.0), before) < -100.0);
    REQUIRE(s.session->useTake(reopened, keptAgain.id));
    CHECK(lane(s.info(reopened)).size() == 2);
    CHECK(differenceDb(s.render(0.0, 8.0), before) < -100.0);
}

TEST_CASE("time selection, step 0: one undo step takes a bounced stretch back to its clips", "[integration]")
{
    Stretch s;
    const auto t = s.synthTrack();
    const auto a = s.phrase(t, 0.5, 1.5, 48);
    const auto b = s.phrase(t, 3.0, 1.0, 55);
    REQUIRE(s.bounceStretch(t, 0.0, 4.5).clips.size() == 1);
    s.session->undo();
    const auto info = s.info(t);
    CHECK(info.stacks.empty());
    REQUIRE(info.clips.size() == 2);
    CHECK(info.clips[0].id == a);
    CHECK(info.clips[1].id == b);
}

TEST_CASE("time selection, step 0: a stretch with nothing in it bounces nothing, saying why", "[integration]")
{
    Stretch s;
    const auto t = s.synthTrack();
    s.phrase(t, 1.0, 2.0, 48); // 1 .. 3
    CHECK(s.session->bounceStretch({t}, 4.0, 6.0, BounceMode::InPlace, [](const auto&) {}) == nullptr);
    CHECK(s.session->stretchBounceRefusal({t}, 4.0, 6.0, BounceMode::InPlace) ==
          "No clip in the selection to bounce");
    // A clip crossing its end is cut there as it lands (step 2).
    CHECK(s.session->stretchBounceRefusal({t}, 0.0, 2.0, BounceMode::InPlace).empty());
}

TEST_CASE(
    "time selection, step 2: a stretch crossing a clip bounces in place up to its edge, the rest of the clip "
    "staying; it sounds as before, and one undo puts the clips back whole",
    "[integration]")
{
    Stretch s;
    const auto t = s.synthTrack("10 ms");     // a short release: nothing rings across the edge
    const auto a = s.phrase(t, 0.5, 1.5, 48); // 0.5 .. 2.0
    const auto b = s.phrase(t, 3.0, 2.0, 55); // 3.0 .. 5.0, cut at 4.0
    const auto before = s.render(0.0, 8.0);

    const auto result = s.bounceStretch(t, 0.0, 4.0);
    CHECK(result.error.empty());
    CHECK(result.cutTails.empty());
    REQUIRE(result.clips.size() == 1);
    CHECK(s.session->undoDescription() == "Bounce in place");
    auto info = s.info(t);
    const auto now = lane(info);
    REQUIRE(now.size() == 2);
    CHECK(std::get<0>(now[0]) == ClipInfo::Kind::Audio);
    CHECK(std::get<1>(now[0]) == Approx(0.0));
    CHECK(std::get<2>(now[0]) == Approx(4.0)); // it stops where the rest of the clip starts
    CHECK(std::get<0>(now[1]) == ClipInfo::Kind::Midi);
    CHECK(std::get<1>(now[1]) == Approx(4.0));
    CHECK(std::get<2>(now[1]) == Approx(5.0));
    // Its Before bounce take: the first clip, and the part of the second inside the stretch.
    const auto& kept = takeNamed(info, "Before bounce");
    REQUIRE(kept.clips.size() == 2);
    CHECK(kept.clips[0].start == Approx(0.5));
    CHECK(kept.clips[1].start == Approx(3.0));
    CHECK(kept.clips[1].end() == Approx(4.0));
    // The stretch sounds as it did. After it, the rest of the clip plays on an instrument that no
    // longer plays the notes before it, so its voices start a hair differently (about -65 dB,
    // fading within the first note): as when those clips are muted by hand.
    const auto after = s.render(0.0, 8.0);
    const auto edge = static_cast<int>(std::lround(4.0 * s.session->sampleRate()));
    CHECK(differenceDb(after, before, 0, edge) < -100.0);

    s.session->undo();
    info = s.info(t);
    CHECK(info.stacks.empty());
    REQUIRE(info.clips.size() == 2);
    CHECK(info.clips[0].id == a);
    CHECK(info.clips[1].id == b);
    CHECK(info.clips[1].end() == Approx(5.0));

    // By hand: the second clip split at the edge, the clips inside muted.
    REQUIRE(s.session->splitClips({b}, 4.0).size() == 1);
    s.session->updateSnapshotNow();
    std::vector<std::uint64_t> inside;
    for (const auto& c : s.info(t).clips)
        if (c.end() <= 4.0 + 1e-6)
            inside.push_back(c.id);
    REQUIRE(inside.size() == 2);
    s.session->setClipsMuted(inside, true);
    const auto byHand = s.render(0.0, 8.0);
    CHECK(differenceDb(after, byHand, edge, after.numSamples()) < -100.0);
}

TEST_CASE(
    "time selection, step 2: a stretch bounced to a new track is one clip spanning it there, its source "
    "parts muted, cut at its edges; the two sound as the clips did",
    "[integration]")
{
    Stretch s;
    const auto t = s.synthTrack();
    s.phrase(t, 0.5, 1.5, 48); // 0.5 .. 2.0
    s.phrase(t, 3.0, 2.0, 55); // 3.0 .. 5.0, cut at 4.0
    const auto before = s.render(0.0, 8.0);

    std::optional<Session::BounceResult> result;
    REQUIRE(s.session->bounceStretch({t}, 0.0, 4.0, BounceMode::NewTrack,
                                     [&](const Session::BounceResult& r) { result = r; }) != nullptr);
    for (int i = 0; i < 2000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    CHECK(result->error.empty());
    REQUIRE(result->tracks.size() == 1);
    REQUIRE(result->clips.size() == 1);
    CHECK(s.session->undoDescription() == "Bounce to new track");
    s.session->updateSnapshotNow();
    CHECK(s.track(1) == result->tracks[0]); // right after its source
    const auto bounced = s.info(result->tracks[0]);
    REQUIRE(bounced.clips.size() == 1);
    CHECK(bounced.clips[0].kind == ClipInfo::Kind::Audio);
    CHECK(bounced.clips[0].start == Approx(0.0));
    CHECK(bounced.clips[0].end() >= 4.0 - 1e-6); // the stretch, and its tail
    // The source: the clips inside muted, what's past the edge playing.
    const auto source = s.info(t);
    REQUIRE(source.clips.size() == 3);
    CHECK(source.clips[0].muted);
    CHECK(source.clips[1].muted);
    CHECK(source.clips[1].end() == Approx(4.0));
    CHECK_FALSE(source.clips[2].muted);
    CHECK(source.clips[2].start == Approx(4.0));
    CHECK(differenceDb(s.render(0.0, 8.0), before) < -100.0);
}
