#pragma once

// The recording fixture: a session on tracktion's hosted test device, fed by a function, for the
// recording and take tests (docs/design/recording.md).

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/SessionImpl.h"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/testing/tracktion_EnginePlayer.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

namespace recording_test
{
namespace te = tracktion;
using fd::engine::Session;
constexpr double kRate = 48000.0;
constexpr int kBlock = 480; // 10 ms

struct Recording
{
    juce::ScopedJuceInitialiser_GUI init;
    te::Engine engine{"fd-recording-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    std::unique_ptr<te::test_utilities::EnginePlayer> player;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("fd-recording-" + juce::Uuid().toString());
    std::int64_t clock = 0; // samples played
    std::function<float(std::int64_t sample, int channel)> input = [](std::int64_t, int) { return 0.0f; };
    juce::AudioBuffer<float> lastOutput;

    Recording()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        te::HostedAudioDeviceInterface::Parameters io;
        io.sampleRate = kRate;
        io.blockSize = kBlock;
        io.inputChannels = 2;
        io.outputChannels = 2;
        player = std::make_unique<te::test_utilities::EnginePlayer>(engine, io);
        session->setUnsavedRoot(root.getChildFile("Unsaved"));
        pump(0.1);
    }

    ~Recording()
    {
        session->stop();
        pump(0.05);
        root.deleteRecursively();
    }

    /// Plays `seconds` through the hosted device, feeding `input`.
    void pump(double seconds)
    {
        juce::AudioBuffer<float> in(2, kBlock);
        for (int b = 0; b < static_cast<int>(seconds * kRate / kBlock + 0.5); ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < kBlock; ++s)
                    in.setSample(c, s, input(clock + s, c));
            clock += kBlock;
            lastOutput = player->process(in);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        }
    }

    std::uint64_t track(size_t i)
    {
        session->updateSnapshotNow();
        return session->snapshot()->tracks.at(i).id;
    }

    /// A copy: snapshots are replaced.
    fd::model::TrackInfo info(std::uint64_t id)
    {
        session->updateSnapshotNow();
        const auto* t = session->snapshot()->channel(id);
        REQUIRE(t != nullptr);
        return *t;
    }

    void recordFor(double seconds)
    {
        session->record();
        pump(seconds);
        session->stop();
        pump(0.1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    /// The output's level over the last block.
    float outputLevel() const { return lastOutput.getMagnitude(0, 0, lastOutput.getNumSamples()); }
};

/// Every take of a track, its clips' in order (ADR 0032: takes belong to clips).
inline std::vector<fd::model::TakeInfo> allTakes(const fd::model::TrackInfo& track)
{
    std::vector<fd::model::TakeInfo> out;
    for (const auto& s : track.stacks)
        out.insert(out.end(), s.takes.begin(), s.takes.end());
    return out;
}

inline float sine(std::int64_t sample, int)
{
    return 0.5f * std::sin(static_cast<float>(sample) * 0.05f);
}
} // namespace recording_test
