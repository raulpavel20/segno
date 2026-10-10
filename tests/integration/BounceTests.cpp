// Bounce (M2 step C7, ADR 0035, docs/design/bounce.md): bounces in place and to new tracks null
// against what played before, keep their source, end where their tails do, land as one undo step,
// and never land on clips that changed meanwhile. Through engine::Session and offline renders.

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"
#include "model/Takes.h"

#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#if defined(FD_HAVE_TEST_PATHS)
#include "fd_test_paths.h"
#endif

using Catch::Approx;
using fd::engine::Session;
using fd::model::BounceMode;
using fd::testkit::AudioBuffer;

namespace
{
struct Bouncer
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-bounce-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-bounce-" + juce::Uuid().toString());

    Bouncer()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
    }

    ~Bouncer()
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

    /// A track playing the Synth into an EQ and a Compressor. The Synth's drift is off: its
    /// random wandering runs from where a render starts, so a bounce (rendered from its clip) and
    /// a render from the song's start would be two performances of it.
    std::uint64_t synthTrack(size_t i = 0)
    {
        const auto t = track(i);
        const auto synth = session->addDevice(t, "fd:subtractive-synth");
        REQUIRE(synth != 0);
        setParam(synth, "voice.drift", "0");
        REQUIRE(session->addDevice(t, "fd:eq") != 0);
        REQUIRE(session->addDevice(t, "fd:compressor") != 0);
        return t;
    }

    /// A MIDI clip over [start, start + length) with a phrase of notes in it.
    std::uint64_t phrase(std::uint64_t t, double start, double length, int pitch = 48)
    {
        const auto clip = session->createMidiClip(t, start, length);
        REQUIRE(clip != 0);
        const double beats = length * 2.0; // 120 BPM
        std::vector<fd::model::Note> notes;
        for (double b = 0.0; b + 0.5 <= beats; b += 1.0)
            notes.push_back({.pitch = pitch + static_cast<int>(b) % 5, .start = b, .length = 0.5, .velocity = 0.8f});
        session->setNotes(clip, notes);
        return clip;
    }

    /// Sets one of our module's parameters by its text ("1.5 s"), through its host binding.
    void setParam(std::uint64_t device, const std::string& paramId, const std::string& text)
    {
        auto handle = session->openDevice(device);
        REQUIRE(handle != nullptr);
        auto& context = handle->context();
        const auto& params = context.info.params;
        const auto it = std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.id == paramId; });
        REQUIRE(it != params.end());
        const auto plain = it->fromText(text);
        REQUIRE(plain);
        context.params.setPlain(static_cast<size_t>(it - params.begin()), *plain);
    }

    /// The mix (or one channel), rendered offline at the engine's rate.
    AudioBuffer render(double start, double end, std::optional<std::uint64_t> channel = std::nullopt)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, channel, session->sampleRate()}, out.getFile(), [&](const std::string& e) {
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

    /// Bounces and waits for it to land (or not).
    Session::BounceResult bounce(const std::vector<std::uint64_t>& clips, BounceMode mode = BounceMode::InPlace)
    {
        std::optional<Session::BounceResult> result;
        auto job = session->bounce(clips, mode, [&](const Session::BounceResult& r) { result = r; });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 2000 && !result; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(result);
        session->updateSnapshotNow();
        return *result;
    }

    /// A copy (snapshots are replaced).
    fd::model::ClipInfo clip(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto found = fd::model::findClip(*session->snapshot(), id);
        REQUIRE(found);
        return *found->first;
    }
};

/// The loudest difference between two renders, in dBFS (-200 when identical).
double differenceDb(const AudioBuffer& a, const AudioBuffer& b)
{
    REQUIRE(a.numChannels() == b.numChannels());
    REQUIRE(a.numSamples() == b.numSamples());
    float worst = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int s = 0; s < a.numSamples(); ++s)
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
} // namespace

TEST_CASE("bounce in place: a synth part nulls, keeps its MIDI as a take, and comes back", "[integration]")
{
    Bouncer b;
    const auto t = b.synthTrack();
    const auto source = b.phrase(t, 1.0, 2.0);
    const auto notes = b.clip(source).notes;
    // A volume ride on the track: it stays live after the bounce.
    const auto lane = b.session->addAutomationLane(Session::ParamTarget{t, "volume"});
    REQUIRE(lane != 0);
    REQUIRE(b.session->setLanePoints(lane, {{0.0, 0.9f, 0.0f}, {4.0, 0.6f, 0.0f}}));
    const auto before = b.render(0.0, 5.0);
    REQUIRE(peakDb(before) > -40.0);

    const auto result = b.bounce({source});
    CHECK(result.error.empty());
    REQUIRE(result.clips.size() == 1);
    const auto info = b.info(t);
    REQUIRE(info.clips.size() == 1);
    const auto& bounced = info.clips[0];
    CHECK(bounced.id == result.clips[0]);
    CHECK(bounced.kind == fd::model::ClipInfo::Kind::Audio);
    CHECK(bounced.name == b.clip(bounced.id).name);
    CHECK(bounced.start == Approx(1.0));
    CHECK(bounced.end() >= 3.0); // its release tail, at least
    CHECK(bounced.end() < 4.0);
    CHECK(juce::File(bounced.sourcePath).isAChildOf(b.session->bouncesFolder()));
    // 32-bit float at the engine's rate.
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(bounced.sourcePath)));
        REQUIRE(reader != nullptr);
        CHECK(reader->bitsPerSample == 32);
        CHECK(reader->usesFloatingPointData);
        CHECK(reader->sampleRate == b.session->sampleRate());
    }
    // The source is a take: "Before bounce" (MIDI) and "Bounce", the clip playing the bounce.
    REQUIRE(info.stacks.size() == 1);
    const auto& stack = info.stacks[0];
    REQUIRE(stack.takes.size() == 2);
    CHECK(stack.takes[0].name == "Before bounce");
    CHECK(stack.takes[0].clips.at(0).kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(stack.takes[1].name == "Bounce");
    CHECK(fd::model::badge(stack, info.clips, false)->text == "2/2");
    CHECK(b.session->undoDescription() == "Bounce in place");

    // The track sounds the same: the modules after the instrument and the lane run on the bounce.
    CHECK(differenceDb(b.render(0.0, 5.0), before) < -100.0);

    // Undo brings the MIDI back and keeps the file; redo uses it again.
    const auto file = juce::File(bounced.sourcePath);
    b.session->undo();
    auto now = b.info(t);
    REQUIRE(now.clips.size() == 1);
    CHECK(now.clips[0].kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(file.existsAsFile());
    b.session->redo();
    now = b.info(t);
    REQUIRE(now.clips.size() == 1);
    CHECK(now.clips[0].kind == fd::model::ClipInfo::Kind::Audio);

    // Choosing "Before bounce" plays the MIDI again, through the instrument.
    REQUIRE(b.session->useTake(t, now.stacks[0].takes[0].id));
    now = b.info(t);
    REQUIRE(now.clips.size() == 1);
    CHECK(now.clips[0].kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(now.clips[0].notes == notes);
    CHECK(differenceDb(b.render(0.0, 5.0), before) < -100.0);

    // Bouncing it again adds "Bounce 2" to the same clip.
    const auto again = b.bounce({now.clips[0].id});
    REQUIRE(again.clips.size() == 1);
    now = b.info(t);
    REQUIRE(now.stacks.size() == 1);
    REQUIRE(now.stacks[0].takes.size() == 3);
    CHECK(now.stacks[0].takes[2].name == "Bounce 2");
}

TEST_CASE("bounce in place: clips it can't bounce are refused, with the reason", "[integration]")
{
    Bouncer b;
    const auto never = [](const Session::BounceResult&) {};
    // No instrument: the MIDI clip is silent (ADR 0033).
    const auto bare = b.track(0);
    const auto silent = b.phrase(bare, 0.0, 1.0);
    CHECK(b.session->bounceRefusal({silent}, BounceMode::InPlace) == fd::model::refusalText(fd::model::BounceRefusal::NoInstrument));
    CHECK(b.session->bounce({silent}, BounceMode::InPlace, never) == nullptr);
    CHECK(b.session->bounce({silent}, BounceMode::NewTrack, never) == nullptr);

    // An audio module before the instrument: not in place; to a new track it's fine.
    const auto t = b.track(1);
    REQUIRE(b.session->addDevice(t, "fd:eq") != 0);
    REQUIRE(b.session->addDevice(t, "fd:subtractive-synth") != 0);
    const auto clip = b.phrase(t, 0.0, 1.0);
    CHECK(b.session->bounceRefusal({clip}, BounceMode::InPlace) ==
          fd::model::refusalText(fd::model::BounceRefusal::ModulesBeforeInstrument));
    CHECK(b.session->bounceRefusal({clip}, BounceMode::NewTrack).empty());

    // A muted clip plays nothing.
    const auto u = b.synthTrack(2);
    const auto muted = b.phrase(u, 0.0, 1.0);
    b.session->setClipsMuted({muted}, true);
    CHECK(b.session->bounceRefusal({muted}, BounceMode::InPlace) == fd::model::refusalText(fd::model::BounceRefusal::Muted));
    CHECK(b.session->bounce({muted}, BounceMode::InPlace, never) == nullptr);
}

TEST_CASE("bounce in place: tails run until silent, stop at the next clip, and carry over in a run", "[integration]")
{
    Bouncer b;
    const auto t = b.synthTrack();
    const auto synth = b.info(t).devices.at(0).id;
    b.setParam(synth, "amp.r", "1.5 s"); // a long release

    // Alone: the bounce rings past its clip, until the release dies away.
    const auto alone = b.phrase(t, 0.0, 1.0);
    auto result = b.bounce({alone});
    REQUIRE(result.clips.size() == 1);
    CHECK(result.cutTails.empty());
    const auto end = b.clip(result.clips[0]).end();
    CHECK(end > 2.0);
    CHECK(end < 1.0 + fd::model::kBounceTailSeconds);
    b.session->undo();

    // A clip that isn't bounced follows at 1.5 s: the tail stops there, and that's reported.
    const auto next = b.phrase(t, 1.5, 1.0, 60);
    result = b.bounce({alone});
    REQUIRE(result.clips.size() == 1);
    CHECK(b.clip(result.clips[0]).end() == Approx(1.5));
    REQUIRE(result.cutTails.size() == 1);
    CHECK(result.cutTails[0].clip == b.clip(result.clips[0]).name);
    CHECK(result.cutTails[0].next == b.clip(next).name);
    b.session->undo();

    // Both bounced together: the first's tail carries on in the second's bounce, and it nulls.
    const auto before = b.render(0.0, 6.0, t);
    result = b.bounce({alone, next});
    REQUIRE(result.clips.size() == 2);
    CHECK(result.cutTails.empty());
    CHECK(b.clip(result.clips[0]).end() == Approx(1.5));
    CHECK(b.clip(result.clips[1]).start == Approx(1.5));
    CHECK(b.clip(result.clips[0]).sourcePath == b.clip(result.clips[1]).sourcePath); // one file per run
    CHECK(differenceDb(b.render(0.0, 6.0, t), before) < -100.0);
}

TEST_CASE("bounce: cancelled, or a clip changed while it rendered: nothing lands", "[integration]")
{
    Bouncer b;
    const auto t = b.synthTrack();
    const auto clip = b.phrase(t, 0.0, 2.0);
    const auto steps = b.session->undoDescription();

    // A change to the clip while it renders.
    std::optional<Session::BounceResult> result;
    REQUIRE(b.session->bounce({clip}, BounceMode::InPlace, [&](const auto& r) { result = r; }) != nullptr);
    CHECK(b.session->bouncing().size() == 1);
    b.session->setNotes(clip, {{.pitch = 60, .start = 0.0, .length = 1.0}});
    for (int i = 0; i < 2000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    CHECK_FALSE(result->error.empty());
    CHECK_FALSE(result->cancelled);
    CHECK(b.info(t).clips.at(0).kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(b.session->bouncing().empty());
    CHECK(b.session->bouncesFolder().findChildFiles(juce::File::findFiles, false).isEmpty());

    // Cancelled.
    result.reset();
    REQUIRE(b.session->bounce({clip}, BounceMode::InPlace, [&](const auto& r) { result = r; }) != nullptr);
    CHECK(b.session->cancelBounces());
    for (int i = 0; i < 2000 && !result; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result);
    CHECK(result->cancelled);
    CHECK(b.info(t).clips.at(0).kind == fd::model::ClipInfo::Kind::Midi);
    CHECK(b.session->bouncesFolder().findChildFiles(juce::File::findFiles, false).isEmpty());
    CHECK_FALSE(b.session->cancelBounces());
}

TEST_CASE("bounce to new track: the whole chain onto a track that copies the mix and its lanes", "[integration]")
{
    Bouncer b;
    const auto t = b.synthTrack();
    const auto ret = b.session->addReturnTrack();
    REQUIRE(b.session->addDevice(ret, "fd:saturator") != 0);
    REQUIRE(b.session->setSend(t, ret, -6.0f, std::nullopt));
    b.session->setVolume(t, -3.0f);
    b.session->setPan(t, 0.3f);
    const auto lane = b.session->addAutomationLane(Session::ParamTarget{t, "volume"});
    REQUIRE(b.session->setLanePoints(lane, {{0.0, 0.8f, 0.0f}, {4.0, 0.5f, 0.0f}}));
    const auto first = b.phrase(t, 0.0, 1.0);
    const auto second = b.phrase(t, 2.0, 1.0, 55);
    const auto before = b.render(0.0, 6.0);

    const auto result = b.bounce({first, second}, BounceMode::NewTrack);
    CHECK(result.error.empty());
    REQUIRE(result.tracks.size() == 1);
    REQUIRE(result.clips.size() == 2);
    CHECK(b.session->undoDescription() == "Bounce to new track");
    const auto& s = *b.session->snapshot();
    // Right after the source, named after it, no modules, the mix copied.
    const auto source = std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto& x) { return x.id == t; });
    REQUIRE(source + 1 != s.tracks.end());
    const auto& made = *(source + 1);
    CHECK(made.id == result.tracks[0]);
    CHECK(made.name == source->name + " bounce");
    CHECK(made.colour == source->colour);
    CHECK(made.devices.empty());
    CHECK(made.mix.volumeDb == Approx(source->mix.volumeDb).margin(0.01));
    CHECK(made.mix.pan == Approx(0.3f).margin(0.001));
    REQUIRE(made.mix.sends.size() == 1);
    CHECK(made.mix.sends[0].levelDb == Approx(-6.0f).margin(0.01));
    CHECK(made.clips.size() == 2);
    CHECK(made.clips[0].kind == fd::model::ClipInfo::Kind::Audio);
    // The lane drives the new track's volume too.
    REQUIRE(s.automationLane(lane) != nullptr);
    CHECK(s.automationLane(lane)->assignment(made.id, "volume") != nullptr);
    // The source clips are muted, and the mix sounds the same.
    CHECK(b.clip(first).muted);
    CHECK(b.clip(second).muted);
    CHECK(differenceDb(b.render(0.0, 6.0), before) < -100.0);

    // One undo step takes it all back.
    b.session->undo();
    b.session->updateSnapshotNow();
    CHECK_FALSE(b.clip(first).muted);
    CHECK(std::none_of(b.session->snapshot()->tracks.begin(), b.session->snapshot()->tracks.end(),
                       [&](const auto& x) { return x.id == result.tracks[0]; }));
}

TEST_CASE("clip mute: a muted clip keeps its place and plays nothing; undo and saving keep it", "[integration]")
{
    Bouncer b;
    const auto t = b.synthTrack();
    const auto clip = b.phrase(t, 0.0, 2.0);
    REQUIRE(peakDb(b.render(0.0, 2.0)) > -40.0);
    b.session->setClipsMuted({clip}, true);
    CHECK(b.clip(clip).muted);
    CHECK(b.session->undoDescription() == "Mute clips");
    CHECK(peakDb(b.render(0.0, 2.0)) < -120.0);

    const auto project = b.root.getChildFile("Muted").getChildFile("Muted.segno");
    REQUIRE(project.getParentDirectory().createDirectory());
    REQUIRE(b.session->saveAs(project));
    REQUIRE(b.session->open(project));
    b.session->updateSnapshotNow();
    REQUIRE(b.session->snapshot()->tracks.at(0).clips.size() == 1);
    CHECK(b.session->snapshot()->tracks.at(0).clips[0].muted);
    b.session->setClipsMuted({b.session->snapshot()->tracks.at(0).clips[0].id}, false);
    CHECK(b.session->undoDescription() == "Unmute clips");
    CHECK(peakDb(b.render(0.0, 2.0)) > -40.0);
}

TEST_CASE("bounce: bounces made before the first save move into the project's folder", "[integration]")
{
    Bouncer b;
    const auto t = b.synthTrack();
    const auto result = b.bounce({b.phrase(t, 0.0, 1.0)});
    REQUIRE(result.clips.size() == 1);
    const auto file = juce::File(b.clip(result.clips[0]).sourcePath);
    CHECK(file.isAChildOf(b.root.getChildFile("Unsaved")));

    const auto project = b.root.getChildFile("Song").getChildFile("Song.segno");
    REQUIRE(project.getParentDirectory().createDirectory());
    REQUIRE(b.session->saveAs(project));
    const auto moved = juce::File(b.clip(result.clips[0]).sourcePath);
    CHECK(moved.isAChildOf(project.getParentDirectory().getChildFile("Bounces")));
    CHECK(moved.existsAsFile());
    CHECK_FALSE(file.existsAsFile());
    CHECK(b.session->bouncesFolder() == project.getParentDirectory().getChildFile("Bounces"));
}

#if defined(FD_SYNTH_VST3_PATH)
TEST_CASE("bounce in place: a third-party instrument nulls too", "[integration]")
{
    Bouncer b;
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(found, FD_SYNTH_VST3_PATH);
    REQUIRE(found.size() == 1);
    b.engine.getPluginManager().knownPluginList.addType(*found[0]);
    const auto t = b.track(0);
    const auto synth = b.session->addDevice(t, "ext:" + found[0]->createIdentifierString().toStdString());
    REQUIRE(synth != 0);
    for (const auto& p : b.session->pluginParameters(synth)) // drift off, as in synthTrack()
        if (p.name == "Drift")
            REQUIRE(b.session->setPluginParameter(synth, p.id, 0.0f));
    REQUIRE(b.session->addDevice(t, "fd:eq") != 0);
    const auto clip = b.phrase(t, 0.5, 2.0);
    const auto before = b.render(0.0, 4.0, t);
    REQUIRE(peakDb(before) > -40.0);
    const auto result = b.bounce({clip});
    REQUIRE(result.clips.size() == 1);
    CHECK(differenceDb(b.render(0.0, 4.0, t), before) < -100.0);
}
#endif
