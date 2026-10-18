// Groups, step 0 (docs/design/groups.md §15, ADR 0072): what a group needs from tracktion, checked
// before groups are built on it. A group is a submix folder (te::FolderTrack) holding the channel
// plugins every Segno channel has, its members moved into it. Checked: a neutral group, three deep
// too, nulls against no group; members' sends go straight to the returns, a group's own sends
// work; a muted group silences its members' sends, a soloed member keeps its groups and the returns
// heard; a group's compressor ducks from a track, a group is a sidechain source and the Follower
// hears one; a lookahead module on a group keeps everything in time; a group's automation and
// modulators play; a track moved into a group while playing keeps sounding; grouping is one undo
// step and a group is saved and opened; stems of a member alone and of a group whole.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Modulator.h"
#include "engine/Session.h"
#include "engine/SessionImpl.h"
#include "fd/testkit/WavIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

namespace te = tracktion;
using Catch::Approx;
using fd::engine::Session;
using fd::testkit::AudioBuffer;

namespace
{
/// A group of `members` at the first one's place, as groups.md §15 makes it: a submix folder with
/// the channel plugins (volume and pan at the balance law, the meter), the members moved into it in
/// order. One undo step.
te::FolderTrack& makeGroup(te::Edit& e, const std::vector<te::Track*>& members, const char* name)
{
    e.getUndoManager().beginNewTransaction("Group tracks");
    auto folder = e.insertNewFolderTrack(te::TrackInsertPoint(*members.front(), true), nullptr, true);
    REQUIRE(folder != nullptr);
    REQUIRE(folder->isSubmixFolder());
    folder->setName(name);
    REQUIRE(folder->getVolumePlugin() != nullptr);
    folder->getVolumePlugin()->setPanLaw(te::PanLawBalance);
    te::Track* last = nullptr;
    for (auto* m : members)
    {
        e.moveTrack(m, te::TrackInsertPoint(folder.get(), last));
        last = m;
    }
    return *folder;
}

struct Groups
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-integration-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-groups-spike-" + juce::Uuid().toString());

    Groups()
    {
        fd::engine::registerDevices(engine);
        REQUIRE(root.createDirectory());
        session = std::make_unique<Session>(engine);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
    }

    ~Groups()
    {
        session.reset();
        root.deleteRecursively();
    }

    te::Edit& edit() { return *session->impl().edit; }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    te::Track& find(std::uint64_t id)
    {
        auto* t = te::findTrackForID(edit(), te::EditItemID::fromRawID(id));
        REQUIRE(t != nullptr);
        return *t;
    }

    /// A sine of `seconds` at `hz` and `level`, as a clip at `start` on track `t`, at the session's
    /// rate (a file at another rate resamples).
    void tone(std::uint64_t t, double start, double seconds, double hz, float level)
    {
        const double rate = session->sampleRate();
        AudioBuffer sine(2, static_cast<int>(seconds * rate));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < sine.numSamples(); ++i)
                sine.at(c, i) = level * static_cast<float>(
                                            std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate));
        const auto file = root.getChildFile("tone-" + juce::String(t) + "-" + juce::String(hz) + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, rate));
        REQUIRE(session->insertAudioFile(t, file, start) != 0);
    }

    /// One-sample clicks every `every` seconds from `first`, on track `t`.
    void clicks(std::uint64_t t, double first, double every, int count)
    {
        const double rate = session->sampleRate();
        AudioBuffer b(2, static_cast<int>((first + every * count) * rate));
        for (int k = 0; k < count; ++k)
            for (int c = 0; c < 2; ++c)
                b.at(c, static_cast<int>(std::lround((first + every * k) * rate))) = 0.5f;
        const auto file = root.getChildFile("clicks-" + juce::String(t) + ".wav");
        REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), b, rate));
        REQUIRE(session->insertAudioFile(t, file, 0.0) != 0);
    }

    te::FolderTrack& group(const std::vector<te::Track*>& members, const char* name = "1 Group")
    {
        return makeGroup(edit(), members, name);
    }

    te::Plugin& module(te::Track& on, const std::string& key, int index = 0)
    {
        auto* p = session->impl().insertDevice(on.pluginList, key, index);
        REQUIRE(p != nullptr);
        return *p;
    }

    static void set(te::Plugin& p, const std::string& param, float plain)
    {
        auto& d = dynamic_cast<fd::engine::DevicePlugin&>(p);
        const auto i = *d.device().params().indexOf(param);
        d.setStoredValue(i, d.device().info().params[i].toNormalized(plain));
    }

    /// An aux send from `from` to `ret` at `db`, before or after its fader.
    void send(te::Track& from, std::uint64_t ret, float db, bool pre = false)
    {
        auto* r = session->impl().returnTrack(ret);
        REQUIRE(r != nullptr);
        auto created = edit().getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {});
        auto* s = dynamic_cast<te::AuxSendPlugin*>(created.get());
        REQUIRE(s != nullptr);
        s->busNumber = fd::engine::busOf(*r);
        s->setGainDb(db);
        auto* volume = from.pluginList.getPluginsOfType<te::VolumeAndPanPlugin>().getFirst();
        REQUIRE(volume != nullptr);
        const int at = from.pluginList.indexOf(volume);
        from.pluginList.insertPlugin(created, pre ? at : at + 1, nullptr);
    }

    /// A module's sidechain fed from `source`, as Session::setSidechainSource sets it: the wires
    /// first, then the source.
    static void sidechain(te::Plugin& p, te::Track& source)
    {
        if (p.getNumWires() == 0)
            p.guessSidechainRouting();
        p.setSidechainSourceID(source.itemID);
    }

    static void volume(te::Track& t, float db)
    {
        auto* v = t.pluginList.getPluginsOfType<te::VolumeAndPanPlugin>().getFirst();
        REQUIRE(v != nullptr);
        if (db <= -100.0f)
            v->setSliderPos(0.0f);
        else
            v->setVolumeDb(db);
    }

    /// Renders a copy of the project from 0 to `seconds`: the mix, or with `stems`, those tracks only
    /// (a member alone without its groups; a group whole).
    AudioBuffer render(double seconds, const std::vector<te::Track*>& stems = {})
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        std::vector<te::EditItemID> ids;
        for (auto* t : stems)
            ids.push_back(t->itemID);
        auto job = session->impl().renderCopy(
            [&](te::Edit& copy, te::Renderer::Parameters& params)
            {
                params.destFile = out.getFile();
                params.sampleRateForAudio = session->sampleRate();
                params.time = {te::TimePosition(), te::TimePosition::fromSeconds(seconds)};
                const auto all = te::getAllTracks(copy);
                if (ids.empty())
                {
                    params.tracksToDo = te::toBitSet(all);
                    params.useMasterPlugins = true;
                }
                else
                    for (int i = 0; i < all.size(); ++i)
                        if (std::find(ids.begin(), ids.end(), all[i]->itemID) != ids.end())
                            params.tracksToDo.setBit(i);
                return true;
            },
            [&](const std::string& e)
            {
                error = e;
                done = true;
            });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 2000 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }
};

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

double peakDb(const AudioBuffer& a, int from = 0, int to = -1)
{
    float peak = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int s = from; s < (to < 0 ? a.numSamples() : to); ++s)
            peak = std::max(peak, std::abs(a.at(c, s)));
    return peak > 0.0f ? 20.0 * std::log10(peak) : -200.0;
}

/// `a` plus `b`, sample by sample.
AudioBuffer sum(const AudioBuffer& a, const AudioBuffer& b)
{
    auto out = a;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int s = 0; s < a.numSamples(); ++s)
            out.at(c, s) += b.at(c, s);
    return out;
}
} // namespace

TEST_CASE("groups (spike): a neutral group, three deep too, nulls against no group", "[integration]")
{
    Groups g;
    const auto a = g.track(0), b = g.track(1), c = g.track(2);
    g.tone(a, 0.0, 2.0, 220.0, 0.25f);
    g.tone(b, 0.5, 2.0, 330.0, 0.25f);
    g.tone(c, 0.25, 2.0, 440.0, 0.25f);
    const auto before = g.render(3.0);
    REQUIRE(peakDb(before) > -20.0);

    auto& drums = g.group({&g.find(a), &g.find(b)}, "Drums");
    CHECK(g.find(a).getParentFolderTrack() == &drums);
    // getAudioTracks still walks into it: the code visiting every track sees members as before.
    CHECK(te::getAudioTracks(g.edit()).size() >= 3);
    CHECK(differenceDb(g.render(3.0), before) < -120.0);

    // Three deep: Drums and c in Rhythm, Rhythm in All.
    auto& rhythm = g.group({&drums, &g.find(c)}, "Rhythm");
    auto& all = g.group({&rhythm}, "All");
    CHECK(drums.getParentFolderTrack() == &rhythm);
    CHECK(rhythm.getParentFolderTrack() == &all);
    CHECK(differenceDb(g.render(3.0), before) < -120.0);
}

TEST_CASE("groups (spike): members' sends go straight to the returns, before or after their fader; a group's "
          "own sends work",
          "[integration]")
{
    Groups g;
    const auto a = g.track(0), b = g.track(1);
    g.tone(a, 0.0, 2.0, 220.0, 0.25f);
    const auto ret = g.session->addReturnTrack("A Return");
    REQUIRE(ret != 0);
    // The member's dry sound alone, for reference.
    g.volume(g.find(b), -100.0f);
    const auto dry = g.render(2.5);
    REQUIRE(peakDb(dry) > -20.0);

    // In a group whose fader is all the way down, a's post-fader send still reaches the return: the
    // mix is a's sound through the (empty) return.
    auto& group = g.group({&g.find(a), &g.find(b)});
    g.send(g.find(a), ret, 0.0f);
    Groups::volume(group, -100.0f);
    CHECK(differenceDb(g.render(2.5), dry) < -100.0);

    // A pre-fader send, with the member's own fader down too.
    g.find(a).pluginList.getPluginsOfType<te::AuxSendPlugin>().getFirst()->deleteFromParent();
    g.send(g.find(a), ret, 0.0f, true);
    Groups::volume(g.find(a), -100.0f);
    CHECK(differenceDb(g.render(2.5), dry) < -100.0);

    // The group's own send, after its fader: with the member's fader up and its send gone, the
    // group's fader up and its send at 0 dB, the mix is twice the sound (the group, and the return).
    g.find(a).pluginList.getPluginsOfType<te::AuxSendPlugin>().getFirst()->deleteFromParent();
    Groups::volume(g.find(a), 0.0f);
    Groups::volume(group, 0.0f);
    g.send(group, ret, 0.0f);
    CHECK(differenceDb(g.render(2.5), sum(dry, dry)) < -100.0);
}

TEST_CASE(
    "groups (spike): a muted group silences its members and their sends; a soloed member keeps its groups "
    "and the returns heard",
    "[integration]")
{
    Groups g;
    const auto a = g.track(0), b = g.track(1), c = g.track(2);
    g.tone(a, 0.0, 2.0, 220.0, 0.25f);
    g.tone(b, 0.0, 2.0, 330.0, 0.25f);
    g.tone(c, 0.0, 2.0, 440.0, 0.25f);
    const auto ret = g.session->addReturnTrack("A Return");
    REQUIRE(ret != 0);
    auto& group = g.group({&g.find(a), &g.find(b)});
    g.send(g.find(a), ret, 0.0f);

    // Each alone, for reference: c, and a with its send.
    g.session->setMute(a, true);
    g.session->setMute(b, true);
    const auto cAlone = g.render(2.5);
    g.session->setMute(a, false);
    g.session->setMute(c, true);
    const auto aWithSend = g.render(2.5); // b is muted too
    g.session->setMute(b, false);
    g.session->setMute(c, false);

    // The group muted: c alone is heard; a's send is silent too.
    group.setMute(true);
    CHECK(differenceDb(g.render(2.5), cAlone) < -100.0);
    group.setMute(false);
    // The members keep their own mute states.
    CHECK_FALSE(g.find(a).isMuted(false));

    // a soloed: it's heard through its group, and its send through the return; b and c aren't.
    g.session->setSolo(a, true, true);
    CHECK(group.isSolo(true)); // shown half lit
    CHECK(differenceDb(g.render(2.5), aWithSend) < -100.0);
    g.session->setSolo(a, false, true);
}

TEST_CASE("groups (spike): stems of a member alone (without its group's modules) and of a group whole",
          "[integration]")
{
    Groups g;
    const auto a = g.track(0), b = g.track(1);
    g.tone(a, 0.0, 2.0, 220.0, 0.25f);
    g.tone(b, 0.0, 2.0, 330.0, 0.25f);
    const auto aDry = g.render(2.5, {&g.find(a)});
    const auto bDry = g.render(2.5, {&g.find(b)});
    auto& group = g.group({&g.find(a), &g.find(b)});
    auto& utility = g.module(group, "fd:utility");
    Groups::set(utility, "gain", -6.0f);

    // A member alone: its own sound, the group's -6 dB left out.
    CHECK(differenceDb(g.render(2.5, {&g.find(a)}), aDry) < -120.0);
    // The group whole: both members through its Utility.
    const auto whole = g.render(2.5, {&group});
    auto expected = sum(aDry, bDry);
    const float half = juce::Decibels::decibelsToGain(-6.0f);
    for (int ch = 0; ch < expected.numChannels(); ++ch)
        for (int s = 0; s < expected.numSamples(); ++s)
            expected.at(ch, s) *= half;
    CHECK(differenceDb(whole, expected) < -100.0);
}

namespace
{
/// The loudest difference between `a` and `b` over [from, to) seconds, in dB.
double differenceDb(const AudioBuffer& a, const AudioBuffer& b, double rate, double from, double to)
{
    float worst = 0.0f;
    for (int c = 0; c < a.numChannels(); ++c)
        for (int s = static_cast<int>(from * rate); s < static_cast<int>(to * rate) && s < a.numSamples();
             ++s)
            worst = std::max(worst, std::abs(a.at(c, s) - b.at(c, s)));
    return worst > 0.0f ? 20.0 * std::log10(worst) : -200.0;
}
} // namespace

TEST_CASE(
    "groups (spike): a group's compressor ducks from a track's sidechain; a group is a sidechain source; "
    "the Follower hears a group",
    "[integration]")
{
    Groups g;
    const auto pad = g.track(0), kick = g.track(1), quiet = g.track(2);
    g.tone(pad, 0.0, 2.0, 220.0, 0.25f); // the music, in a group
    g.tone(kick, 0.0, 1.0, 60.0, 0.5f);  // the kick, for its first second
    auto& music = g.group({&g.find(pad)}, "Music");
    const double rate = g.session->sampleRate();

    // The group ducked under the kick: quieter while the kick plays, after it (and its release) as
    // when its sidechain hears nothing (an empty track).
    auto& comp = g.module(music, "fd:compressor");
    Groups::set(comp, "threshold", -40.0f);
    Groups::set(comp, "ratio", 20.0f);
    Groups::sidechain(comp, g.find(quiet));
    const auto unducked = g.render(2.0);
    Groups::sidechain(comp, g.find(kick));
    const auto ducked = g.render(2.0);
    CHECK(differenceDb(ducked, unducked, rate, 0.2, 0.9) > -30.0);
    CHECK(differenceDb(ducked, unducked, rate, 1.6, 2.0) < -60.0);
    comp.deleteFromParent();

    // The group as the source: a compressor on the kick ducks it under the music (with the
    // tracktion patch, groups.md §15: a submix sends what its sidechain readers hear).
    auto& onKick = g.module(g.find(kick), "fd:compressor");
    Groups::set(onKick, "threshold", -40.0f);
    Groups::set(onKick, "ratio", 20.0f);
    Groups::sidechain(onKick, g.find(quiet));
    const auto alone = g.render(2.0);
    Groups::sidechain(onKick, music);
    const auto underMusic = g.render(2.0);
    CHECK(differenceDb(underMusic, alone, rate, 0.2, 0.9) > -30.0);
    onKick.deleteFromParent();

    // The Follower on the kick, listening to the music, moving the kick's Utility down.
    auto& utility = g.module(g.find(kick), "fd:utility");
    const auto follower = g.session->addModulator(kick, fd::model::ModulatorKind::Follower);
    REQUIRE(follower != 0);
    REQUIRE(g.session->routeModulator(
        follower, {kick, "m" + std::to_string(utility.itemID.getRawID()) + "/gain"}, -1.0f));
    auto* m = g.session->impl().modulator(follower);
    REQUIRE(m != nullptr);
    REQUIRE(g.session->setSidechainSource(follower, quiet)); // an empty track: nothing to follow
    const auto unheard = g.render(2.0);
    m->state.setProperty(fd::engine::Modulator::listenId, music.itemID.toVar(), nullptr);
    const auto heard = g.render(2.0);
    CHECK(differenceDb(heard, unheard, rate, 0.2, 0.9) > -30.0);
}

TEST_CASE("groups (spike): a lookahead module on a group keeps every track in time", "[integration]")
{
    Groups g;
    const auto a = g.track(0), c = g.track(1);
    g.clicks(a, 0.5, 0.5, 4);
    g.clicks(c, 0.5, 0.5, 4);
    const auto before = g.render(3.0);
    auto& group = g.group({&g.find(a)});
    auto& comp = g.module(group, "fd:compressor");
    Groups::set(comp, "lookahead", 2.0f); // the longest
    Groups::set(comp, "mix", 0.0f);       // dry: the delay alone
    REQUIRE(comp.getLatencySeconds() > 0.0);
    // The group's clicks land with the other track's, as without it.
    CHECK(differenceDb(g.render(3.0), before) < -100.0);
}

TEST_CASE("groups (spike): a group's automation and its modulators play", "[integration]")
{
    Groups g;
    const auto a = g.track(0);
    g.tone(a, 0.0, 3.0, 220.0, 0.25f);
    auto& group = g.group({&g.find(a)});
    const double rate = g.session->sampleRate();

    // Its volume automated: 0 dB, then -12 dB from 1.5 s.
    auto* volume = group.getVolumePlugin();
    volume->setVolumeDb(-12.0f);
    const float low = volume->volParam->getCurrentValue();
    volume->setVolumeDb(0.0f);
    const float unity = volume->volParam->getCurrentValue();
    g.session->impl().writeCurve(volume->volParam->getCurve(),
                                 {{0.0, unity, 0.0f}, {1.5, unity, 0.0f}, {1.5001, low, 0.0f}}, nullptr);
    const auto automated = g.render(3.0);
    const auto at = [rate](double seconds) { return static_cast<int>(seconds * rate); };
    CHECK(peakDb(automated, at(0.2), at(1.4)) == Approx(-12.04).margin(0.3));
    CHECK(peakDb(automated, at(1.7), at(2.9)) == Approx(-24.04).margin(0.3));
    volume->volParam->getCurve().clear(nullptr);
    volume->setVolumeDb(0.0f);

    // An LFO on its chain moving its Utility's gain.
    auto& utility = g.module(group, "fd:utility");
    const auto still = g.render(3.0);
    auto added = group.getModifierList()->insertModifier(
        fd::engine::Modulator::createState(fd::model::ModulatorKind::Lfo), -1, nullptr);
    auto* lfo = dynamic_cast<fd::engine::Modulator*>(added.get());
    REQUIRE(lfo != nullptr);
    g.session->impl().placeInChain(group, {nullptr, lfo}, 0);
    const auto gain = utility.getAutomatableParameterByID("gain");
    REQUIRE(gain != nullptr);
    REQUIRE(gain->addModifier(*lfo, 1.0f, 0.0f, 0.0f) != nullptr);
    g.session->impl().refreshModulatorRoutes();
    CHECK(differenceDb(g.render(3.0), still) > -30.0);
}

TEST_CASE(
    "groups (spike): grouping is one undo step; a project with groups three deep is saved and opened the "
    "same",
    "[integration]")
{
    Groups g;
    const auto a = g.track(0), b = g.track(1), c = g.track(2);
    g.tone(a, 0.0, 2.0, 220.0, 0.25f);
    g.tone(b, 0.5, 2.0, 330.0, 0.25f);
    g.tone(c, 0.25, 2.0, 440.0, 0.25f);
    const auto order = [&]
    {
        std::vector<std::string> out;
        for (auto* t : te::getAllTracks(g.edit()))
            if (dynamic_cast<te::FolderTrack*>(t) != nullptr || dynamic_cast<te::AudioTrack*>(t) != nullptr)
                out.push_back((t->getParentFolderTrack() != nullptr
                                   ? t->getParentFolderTrack()->getName() + "/"
                                   : juce::String())
                                  .toStdString() +
                              t->getName().toStdString());
        return out;
    };
    const auto flat = order();

    // Group, and one undo puts the tracks back; redo groups them again.
    g.group({&g.find(a), &g.find(b)}, "Drums");
    const auto grouped = order();
    CHECK(grouped != flat);
    g.session->undo();
    CHECK(order() == flat);
    g.session->redo();
    CHECK(order() == grouped);

    // Three deep, with a group's own module and volume, and a fold state of the view's.
    auto* drums = dynamic_cast<te::FolderTrack*>(g.find(a).getParentFolderTrack());
    REQUIRE(drums != nullptr);
    Groups::set(g.module(*drums, "fd:utility"), "gain", -6.0f);
    Groups::volume(*drums, -3.0f);
    auto& rhythm = g.group({drums, &g.find(c)}, "Rhythm");
    g.group({&rhythm}, "All");
    rhythm.state.setProperty("fdFoldedInArrangement", true, nullptr);
    const auto nested = order();
    const auto before = g.render(3.0);

    const auto file = g.root.getChildFile("Groups").getChildFile("Groups.segno");
    REQUIRE(file.getParentDirectory().createDirectory());
    REQUIRE(g.session->saveAs(file));
    REQUIRE(g.session->open(file));
    CHECK(order() == nested);
    te::FolderTrack* reopened = nullptr;
    for (auto* t : te::getAllTracks(g.edit()))
        if (t->getName() == "Rhythm")
            reopened = dynamic_cast<te::FolderTrack*>(t);
    REQUIRE(reopened != nullptr);
    CHECK(reopened->isSubmixFolder());
    CHECK(static_cast<bool>(reopened->state.getProperty("fdFoldedInArrangement")));
    CHECK(differenceDb(g.render(3.0), before) < -120.0);
}

TEST_CASE("groups (spike): a track moved into a group and out again while the song plays keeps sounding",
          "[integration]")
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-drum-kit-spike-live", nullptr, fd::engine::makeEngineBehaviour()};
    fd::engine::registerDevices(engine);
    Session session(engine);
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-groups-live-" + juce::Uuid().toString());
    REQUIRE(root.createDirectory());
    session.setUnsavedRoot(root.getChildFile("Unsaved"));
    te::HostedAudioDeviceInterface::Parameters io;
    io.sampleRate = 48000.0;
    io.blockSize = 480;
    io.inputChannels = 0;
    io.outputChannels = 2;
    te::test_utilities::EnginePlayer player(engine, io);
    auto& edit = *session.impl().edit;

    // A held tone on the first track, four seconds long.
    session.updateSnapshotNow();
    const auto a = session.snapshot()->tracks.at(0).id;
    AudioBuffer sine(2, 4 * 48000);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < sine.numSamples(); ++i)
            sine.at(c, i) = 0.5f * static_cast<float>(
                                       std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0));
    const auto file = root.getChildFile("tone.wav");
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), sine, 48000.0));
    REQUIRE(session.insertAudioFile(a, file, 0.0) != 0);
    auto* track = te::findTrackForID(edit, te::EditItemID::fromRawID(a));
    REQUIRE(track != nullptr);

    std::vector<float> levels; // each block's peak
    const auto pump = [&](double seconds)
    {
        for (int b = 0; b < static_cast<int>(seconds * 100.0); ++b)
        {
            const auto out = player.process(480);
            levels.push_back(out.getMagnitude(0, 0, out.getNumSamples()));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    };
    const auto steadyAround = [&](size_t at)
    {
        float lowest = 1.0f;
        for (size_t b = at - 20; b < at + 50 && b < levels.size(); ++b)
            lowest = std::min(lowest, levels[b]);
        return lowest;
    };
    session.setPosition(0.0);
    session.play();
    pump(1.0);
    REQUIRE(session.isPlaying());
    const size_t into = levels.size();
    auto& group = makeGroup(edit, {track}, "Group");
    pump(1.0);
    const size_t outOf = levels.size();
    edit.moveTrack(track, te::TrackInsertPoint(nullptr, &group));
    pump(1.0);
    session.stop();
    pump(0.1);
    const float steady = levels[into - 5];
    INFO("steady " << steady << ", into " << steadyAround(into) << ", out of " << steadyAround(outOf));
    CHECK(steady > 0.3f);
    CHECK(steadyAround(into) > 0.9f * steady);
    CHECK(steadyAround(outOf) > 0.9f * steady);
    root.deleteRecursively();
}

// ---- timed in Release ([.benchmark]): run the rel build with "[.benchmark]" -----------------

TEST_CASE("groups (benchmark): 64 tracks in 8 groups render as fast as 64 tracks",
          "[integration][.benchmark]")
{
    Groups g;
    g.session->updateSnapshotNow();
    while (g.session->snapshot()->tracks.size() < 64)
    {
        g.session->addTrack();
        g.session->updateSnapshotNow();
    }
    std::vector<te::Track*> tracks;
    for (size_t i = 0; i < 64; ++i)
    {
        const auto t = g.track(i);
        g.tone(t, 0.0, 10.0, 110.0 + 10.0 * static_cast<double>(i), 0.01f);
        g.module(g.find(t), "fd:utility");
        tracks.push_back(&g.find(t));
    }
    const auto timed = [&]
    {
        const auto start = std::chrono::steady_clock::now();
        g.render(10.0);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const double flat = timed();
    for (size_t k = 0; k < 8; ++k)
        g.module(g.group({tracks.begin() + static_cast<std::ptrdiff_t>(k * 8),
                          tracks.begin() + static_cast<std::ptrdiff_t>(k * 8 + 8)}),
                 "fd:utility");
    const double grouped = timed();
    WARN("64 tracks: " << flat << " s flat, " << grouped << " s in 8 groups (10 s rendered)");
    CHECK(grouped < flat * 1.25);
}
