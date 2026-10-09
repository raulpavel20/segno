// The mixer (M2 step C3a, ADR 0026, docs/design/mixer.md): laws, sends, solo, undo, and the
// master and return tracks, through engine::Session and offline renders.

#include "engine/EngineBehaviour.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "fd/testkit/Signals.h"
#include "fd/testkit/WavIO.h"

#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Catch::Approx;
using fd::testkit::AudioBuffer;

namespace
{
constexpr double kRate = 48000.0;

struct Mixer
{
    juce::ScopedJuceInitialiser_GUI init;
    tracktion::Engine engine{"fd-mixer-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<fd::engine::Session> session;
    std::vector<std::unique_ptr<juce::TemporaryFile>> files;

    Mixer()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// Places `audio` on a track at `seconds`.
    void place(std::uint64_t trackId, const AudioBuffer& audio, double seconds = 0.0)
    {
        auto& file = *files.emplace_back(std::make_unique<juce::TemporaryFile>(".wav"));
        REQUIRE(fd::testkit::writeWav(file.getFile().getFullPathName().toStdString(), audio, kRate));
        REQUIRE(session->insertAudioFile(trackId, file.getFile(), seconds) != 0);
    }

    /// Renders the mix (or one channel) offline.
    AudioBuffer render(double start, double end, std::optional<std::uint64_t> channel = std::nullopt)
    {
        juce::TemporaryFile out(".wav");
        bool done = false;
        std::string error;
        auto job = session->render({start, end, channel, kRate}, out.getFile(), [&](const std::string& e) {
            error = e;
            done = true;
        });
        REQUIRE(job != nullptr);
        for (int i = 0; i < 600 && !done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        REQUIRE(done);
        REQUIRE(error.empty());
        auto wav = fd::testkit::readWav(out.getFile().getFullPathName().toStdString());
        REQUIRE(wav.has_value());
        return wav->buffer;
    }

    const fd::model::TrackInfo& snapshotOf(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }
};

/// RMS of a channel over [from, to) seconds, in dB.
double levelDb(const AudioBuffer& b, int channel, double from, double to)
{
    double sum = 0.0;
    const int i0 = static_cast<int>(from * kRate), i1 = static_cast<int>(to * kRate);
    for (int i = i0; i < i1; ++i)
        sum += double(b.at(channel, i)) * b.at(channel, i);
    return 10.0 * std::log10(std::max(sum / (i1 - i0), 1e-30));
}

AudioBuffer tone(double hz, double seconds, float amplitude = 0.25f)
{
    return fd::testkit::sine(hz, kRate, static_cast<int>(seconds * kRate), amplitude, 2);
}
} // namespace

TEST_CASE("mixer: volume and pan follow their laws exactly", "[integration]")
{
    Mixer m;
    const auto t = m.track(0);
    m.place(t, tone(1000.0, 2.0));
    const double source = 20.0 * std::log10(0.25 / std::sqrt(2.0));
    auto gains = [&] {
        const auto y = m.render(0.5, 1.5, t);
        return std::pair{levelDb(y, 0, 0.1, 0.9) - source, levelDb(y, 1, 0.1, 0.9) - source};
    };
    CHECK(gains().first == Approx(0.0).margin(0.01));

    m.session->setVolume(t, -6.0f);
    CHECK(gains().first == Approx(-6.0).margin(0.01));
    m.session->setVolume(t, 6.0f);
    CHECK(gains().second == Approx(6.0).margin(0.01));
    m.session->setVolume(t, 12.0f); // clamped
    CHECK(m.snapshotOf(t).mix.volumeDb == Approx(6.0f).margin(0.001f));
    m.session->setVolume(t, 0.0f);

    // Balance: the near side stays at 0 dB, the far side follows a quarter-cosine.
    m.session->setPan(t, 0.5f);
    auto [left, right] = gains();
    CHECK(left == Approx(20.0 * std::log10(std::cos(0.25 * std::numbers::pi))).margin(0.01)); // -3.01 dB
    CHECK(right == Approx(0.0).margin(0.01));
    m.session->setPan(t, -1.0f);
    std::tie(left, right) = gains();
    CHECK(left == Approx(0.0).margin(0.01));
    CHECK(right < -100.0);
    CHECK(m.snapshotOf(t).mix.pan == Approx(-1.0f));
    m.session->setPan(t, 0.0f);

    m.session->setVolume(t, fd::model::minusInfinityDb);
    CHECK(gains().first < -100.0);
    CHECK(m.snapshotOf(t).mix.volumeDb == fd::model::minusInfinityDb);
    m.session->setVolume(t, 0.0f);

    // The master's volume applies to the mix.
    const auto master = m.session->snapshot()->master.id;
    REQUIRE(master != 0);
    m.session->setVolume(master, -3.0f);
    const auto mix = m.render(0.5, 1.5);
    CHECK(levelDb(mix, 0, 0.1, 0.9) - source == Approx(-3.0).margin(0.01));
}

TEST_CASE("mixer: a neutral mix nulls against its sources", "[integration]")
{
    Mixer m;
    const auto a = tone(1000.0, 2.0), b = tone(311.0, 2.0, 0.3f);
    m.place(m.track(0), a);
    m.place(m.track(1), b);
    m.session->addReturnTrack(); // a return with nothing sent to it changes nothing
    const auto y = m.render(0.0, 2.0);
    double worst = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < y.numSamples() && i < a.numSamples(); ++i)
            worst = std::max(worst, std::abs(double(y.at(c, i)) - a.at(c, i) - b.at(c, i)));
    CHECK(worst < 1e-6); // < -120 dBFS
}

TEST_CASE("mixer: return tracks, and sends before or after the volume", "[integration]")
{
    Mixer m;
    const auto t = m.track(0);
    m.place(t, tone(1000.0, 2.0));
    const double source = 20.0 * std::log10(0.25 / std::sqrt(2.0));

    const auto r = m.session->addReturnTrack();
    REQUIRE(r != 0);
    CHECK(m.session->undoDescription() == "Add return track");
    m.session->updateSnapshotNow();
    const auto snap = m.session->snapshot();
    REQUIRE(snap->returns.size() == 1);
    CHECK(snap->returns[0].name == "A Return");
    CHECK(snap->returns[0].role == fd::model::TrackInfo::Role::Return);
    REQUIRE(snap->tracks[0].mix.sends.size() == 1);
    CHECK(snap->tracks[0].mix.sends[0].returnId == r);
    CHECK(snap->tracks[0].mix.sends[0].levelDb == fd::model::minusInfinityDb);
    CHECK(m.session->addReturnTrack() != 0);
    m.session->updateSnapshotNow();
    CHECK(m.session->snapshot()->returns[1].name == "B Return");

    // Post-fader (the default) follows the track's volume; pre-fader doesn't.
    m.session->setVolume(t, fd::model::minusInfinityDb);
    REQUIRE(m.session->setSend(t, r, -6.0f, std::nullopt));
    CHECK(m.session->undoDescription() == "Send to A Return");
    CHECK(levelDb(m.render(0.5, 1.5), 0, 0.1, 0.9) < -100.0);
    REQUIRE(m.session->setSend(t, r, std::nullopt, true));
    CHECK(levelDb(m.render(0.5, 1.5), 0, 0.1, 0.9) - source == Approx(-6.0).margin(0.01));
    const auto& send = m.snapshotOf(t).mix.sends[0];
    CHECK(send.preFader);
    CHECK(send.levelDb == Approx(-6.0f).margin(0.001f));

    // Muting the track mutes its sends; the return's own volume applies.
    m.session->setVolume(r, -6.0f);
    CHECK(levelDb(m.render(0.5, 1.5), 0, 0.1, 0.9) - source == Approx(-12.0).margin(0.01));
    m.session->setMute(t, true);
    CHECK(m.session->undoDescription() == "Mute");
    CHECK(m.snapshotOf(t).mix.mute);
    CHECK(levelDb(m.render(0.5, 1.5), 0, 0.1, 0.9) < -100.0);

    CHECK_FALSE(m.session->setSend(r, r, 0.0f, std::nullopt)); // returns have no sends
    CHECK_FALSE(m.session->setSend(t, t, 0.0f, std::nullopt)); // and only returns take them
}

TEST_CASE("mixer: solo in place keeps the returns, silences the others and their sends", "[integration]")
{
    Mixer m;
    const auto a = m.track(0), b = m.track(1);
    m.place(a, tone(1000.0, 1.0), 0.0); // A plays 0-1 s, B 1-2 s
    m.place(b, tone(1000.0, 1.0), 1.0);
    const auto r = m.session->addReturnTrack();
    REQUIRE(m.session->setSend(a, r, 0.0f, std::nullopt));
    REQUIRE(m.session->setSend(b, r, 0.0f, std::nullopt));
    const double source = 20.0 * std::log10(0.25 / std::sqrt(2.0));

    juce::TemporaryFile project(fd::engine::Session::fileExtension);
    REQUIRE(m.session->saveAs(project.getFile()));
    const auto undo = m.session->undoDescription();
    m.session->setSolo(a, true, true);
    CHECK(m.session->undoDescription() == undo); // never an undo step
    CHECK_FALSE(m.session->hasUnsavedChanges()); // nor an unsaved change on its own
    CHECK(m.snapshotOf(a).mix.solo);

    const auto y = m.render(0.0, 2.0);
    CHECK(levelDb(y, 0, 0.2, 0.8) - source == Approx(20.0 * std::log10(2.0)).margin(0.02)); // A and its send
    CHECK(levelDb(y, 0, 1.2, 1.8) < -100.0); // B and its send are silent

    m.session->setSolo(b, true, true); // exclusive
    CHECK_FALSE(m.snapshotOf(a).mix.solo);
    CHECK(m.snapshotOf(b).mix.solo);
    m.session->setSolo(a, true, false); // adds
    CHECK(m.snapshotOf(a).mix.solo);
    CHECK(m.snapshotOf(b).mix.solo);
}

TEST_CASE("mixer: each change is one named undo step; a gesture is one", "[integration]")
{
    Mixer m;
    const auto t = m.track(0);
    m.session->setVolume(t, -3.0f);
    CHECK(m.session->undoDescription() == "Volume");
    m.session->setPan(t, 0.25f);
    CHECK(m.session->undoDescription() == "Pan");
    {
        fd::engine::Session::EditGroup drag(*m.session, "Volume");
        for (float db : {-4.0f, -5.0f, -6.0f, -7.0f})
            m.session->setVolume(t, db);
    }
    CHECK(m.snapshotOf(t).mix.volumeDb == Approx(-7.0f).margin(0.001f));
    m.session->undo(); // the whole drag
    CHECK(m.snapshotOf(t).mix.volumeDb == Approx(-3.0f).margin(0.001f));
    m.session->undo(); // pan
    CHECK(m.snapshotOf(t).mix.pan == Approx(0.0f));
    m.session->undo(); // volume
    CHECK(m.snapshotOf(t).mix.volumeDb == Approx(0.0f).margin(0.001f));
}

TEST_CASE("mixer: returns hold no clips; returns and the master have chains", "[integration]")
{
    Mixer m;
    const auto r = m.session->addReturnTrack();
    CHECK(m.session->createMidiClip(r, 0.0, 2.0) == 0);
    juce::TemporaryFile wav(".wav");
    REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), tone(440.0, 0.5), kRate));
    CHECK(m.session->insertAudioFile(r, wav.getFile(), 0.0) == 0);

    const auto master = m.session->snapshot()->master.id;
    const auto limiter = m.session->addDevice(master, "fd:limiter");
    REQUIRE(limiter != 0);
    CHECK(m.session->addDevice(r, "fd:eq") != 0);
    m.session->updateSnapshotNow();
    const auto snap = m.session->snapshot();
    REQUIRE(snap->master.devices.size() == 1);
    CHECK(snap->master.devices[0].deviceId == "limiter");
    CHECK(snap->master.role == fd::model::TrackInfo::Role::Master);
    CHECK(snap->returns[0].devices.size() == 1);
    // The master's chain plays: a limiter at -6 dB caps a full-scale tone.
    m.place(m.track(0), tone(1000.0, 1.0, 1.0f));
    auto handle = m.session->openDevice(limiter);
    REQUIRE(handle != nullptr);
    handle->context().params.setPlain(1, -6.0f); // ceiling
    handle.reset();
    const auto y = m.render(0.2, 0.8);
    float peak = 0.0f;
    for (int i = 0; i < y.numSamples(); ++i)
        peak = std::max(peak, std::abs(y.at(0, i)));
    CHECK(20.0 * std::log10(peak) <= -5.99);

    for (auto id : {m.track(0), r, master})
        CHECK(m.session->levelReader(id) != nullptr);
    CHECK(m.session->levelReader(12345) == nullptr);
}

TEST_CASE("mixer: the mix is saved with the project", "[integration]")
{
    juce::TemporaryFile project(fd::engine::Session::fileExtension);
    std::uint64_t t = 0, r = 0;
    {
        Mixer m;
        t = m.track(0);
        r = m.session->addReturnTrack("Verb");
        m.session->setVolume(t, -4.5f);
        m.session->setPan(t, -0.3f);
        m.session->setMute(t, true);
        REQUIRE(m.session->setSend(t, r, -9.0f, true));
        REQUIRE(m.session->saveAs(project.getFile()));
    }
    Mixer m;
    REQUIRE(m.session->open(project.getFile()));
    m.session->updateSnapshotNow();
    const auto snap = m.session->snapshot();
    REQUIRE(snap->returns.size() == 1);
    CHECK(snap->returns[0].name == "Verb");
    const auto& mix = snap->tracks[0].mix;
    CHECK(mix.volumeDb == Approx(-4.5f).margin(0.001f));
    CHECK(mix.pan == Approx(-0.3f).margin(0.001f));
    CHECK(mix.mute);
    REQUIRE(mix.sends.size() == 1);
    CHECK(mix.sends[0].levelDb == Approx(-9.0f).margin(0.001f));
    CHECK(mix.sends[0].preFader);
}
