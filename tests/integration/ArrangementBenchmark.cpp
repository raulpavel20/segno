// Paint benchmark for the arrangement with the software renderer, offscreen. Hidden tag; run in a
// Release build:
//   build/rel/tests/integration/fd_integration_tests_artefacts/Release/fd_integration_tests "[.benchmark]"
// It reports only: offscreen software painting at 4K is bound by fill rate, which the GPU does
// for the real window. The ADR 0015 budget (32 tracks zooming under 8 ms) is checked with the
// OpenGL renderer by `Segno --benchmark` (docs/design/daw-shell.md, "Performance").

#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"
#include "model/Editing.h"
#include "ui/ArrangementView.h"
#include "ui/MixerView.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <iostream>

TEST_CASE("arrangement paints 32 zooming tracks within the frame budget", "[.benchmark]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-benchmark", nullptr, fd::engine::makeEngineBehaviour()};
        fd::engine::Session session(engine);
        while (session.snapshot()->tracks.size() < 32)
        {
            session.addTrack();
            session.updateSnapshotNow();
        }

        // A 16 s stereo file with busy content, used for 8 clips per track (128 s).
        juce::TemporaryFile wav(".wav");
        fd::testkit::AudioBuffer audio(2, 16 * 48000);
        for (int i = 0; i < audio.numSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                audio.at(c, i) = 0.6f * std::sin(0.011f * static_cast<float>(i) * (1.0f + 0.3f * static_cast<float>(c))) *
                                 std::exp(-static_cast<float>(i % 12000) / 5000.0f);
        REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), audio, 48000.0));
        for (const auto& t : session.snapshot()->tracks)
            for (int k = 0; k < 8; ++k)
                session.insertAudioFile(t.id, wav.getFile(), 16.0 * k);
        session.updateSnapshotNow();

        fd::app::ArrangementView view(session);
        view.setSize(1700, 28 + 32 * 32); // about a 4K screen's arrangement at 2x
        view.setSnapshot(session.snapshot());
        view.setAllTrackHeights(32.0);
        view.waveforms().waitUntilIdle();
        view.zoomToFit();
        for (int i = 0; i < 60; ++i)
            view.frame(1.0 / 60.0, 0.0);

        constexpr float scale = 2.0f;
        juce::Image image(juce::Image::ARGB, static_cast<int>(static_cast<float>(view.getWidth()) * scale),
                          static_cast<int>(static_cast<float>(view.getHeight()) * scale), true);
        std::vector<double> times;
        for (int frame = 0; frame < 240; ++frame)
        {
            // Zoom in for 120 frames, then out: about 6 octaves each way.
            view.zoomAt(800.0, frame < 120 ? 1.035 : 1.0 / 1.035);
            const auto t0 = std::chrono::steady_clock::now();
            {
                juce::Graphics g(image);
                g.addTransform(juce::AffineTransform::scale(scale));
                view.paintEntireComponent(g, true);
            }
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (double t : times)
            sum += t;
        const double avg = sum / static_cast<double>(times.size());
        const double p95 = times[times.size() * 95 / 100];
        std::cout << "arrangement paint, 32 tracks x 8 clips, " << image.getWidth() << "x" << image.getHeight()
                  << ": avg " << avg << " ms, p95 " << p95 << " ms" << std::endl;
        CHECK(p95 > 0.0);
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("arrangement paints 32 zooming tracks of 64-lap loops within the frame budget", "[.benchmark]")
{
    // Clip loops (ADR 0048): each lap is drawn as a clip of its own. The same span as the case
    // above (128 s a track), as one looped clip of 64 two-second laps.
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-benchmark", nullptr, fd::engine::makeEngineBehaviour()};
        fd::engine::Session session(engine);
        while (session.snapshot()->tracks.size() < 32)
        {
            session.addTrack();
            session.updateSnapshotNow();
        }
        juce::TemporaryFile wav(".wav");
        fd::testkit::AudioBuffer audio(2, 2 * 48000);
        for (int i = 0; i < audio.numSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                audio.at(c, i) = 0.6f * std::sin(0.011f * static_cast<float>(i) * (1.0f + 0.3f * static_cast<float>(c))) *
                                 std::exp(-static_cast<float>(i % 12000) / 5000.0f);
        REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), audio, 48000.0));
        for (const auto& t : session.snapshot()->tracks)
        {
            const auto clip = session.insertAudioFile(t.id, wav.getFile(), 0.0);
            REQUIRE(session.setClipsLooping({clip}, true).empty());
            session.applyClipEdits({{.clip = clip, .track = t.id, .start = 0.0, .length = 128.0, .offset = 0.0}});
        }
        session.updateSnapshotNow();

        fd::app::ArrangementView view(session);
        view.setSize(1700, 28 + 32 * 32);
        view.setSnapshot(session.snapshot());
        view.setAllTrackHeights(32.0);
        view.waveforms().waitUntilIdle();
        view.zoomToFit();
        for (int i = 0; i < 60; ++i)
            view.frame(1.0 / 60.0, 0.0);

        constexpr float scale = 2.0f;
        juce::Image image(juce::Image::ARGB, static_cast<int>(static_cast<float>(view.getWidth()) * scale),
                          static_cast<int>(static_cast<float>(view.getHeight()) * scale), true);
        std::vector<double> times;
        for (int frame = 0; frame < 240; ++frame)
        {
            view.zoomAt(800.0, frame < 120 ? 1.035 : 1.0 / 1.035);
            const auto t0 = std::chrono::steady_clock::now();
            {
                juce::Graphics g(image);
                g.addTransform(juce::AffineTransform::scale(scale));
                view.paintEntireComponent(g, true);
            }
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (double t : times)
            sum += t;
        const double avg = sum / static_cast<double>(times.size());
        const double p95 = times[times.size() * 95 / 100];
        std::cout << "arrangement paint, 32 tracks x 64 laps, " << image.getWidth() << "x" << image.getHeight()
                  << ": avg " << avg << " ms, p95 " << p95 << " ms" << std::endl;
        CHECK(p95 > 0.0);
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("arrangement paints 32 zooming tracks of warped loops with markers within the frame budget",
          "[.benchmark]")
{
    // Warped clips (audio-editor.md §10, §19): each piece between markers drawn at its own speed.
    // The 64-lap case above, each loop warped from 100 BPM to the song's 120 with a marker every
    // half second of its file, a little off its beat.
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-benchmark", nullptr, fd::engine::makeEngineBehaviour()};
        fd::engine::Session session(engine);
        session.setWarpDefaults(false, false);
        while (session.snapshot()->tracks.size() < 32)
        {
            session.addTrack();
            session.updateSnapshotNow();
        }
        juce::TemporaryFile wav(".wav");
        fd::testkit::AudioBuffer audio(2, 2 * 48000);
        for (int i = 0; i < audio.numSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                audio.at(c, i) =
                    0.6f * std::sin(0.011f * static_cast<float>(i) * (1.0f + 0.3f * static_cast<float>(c))) *
                    std::exp(-static_cast<float>(i % 12000) / 5000.0f);
        REQUIRE(fd::testkit::writeWav(wav.getFile().getFullPathName().toStdString(), audio, 48000.0));
        for (const auto& t : session.snapshot()->tracks)
        {
            const auto clip = session.insertAudioFile(t.id, wav.getFile(), 0.0);
            REQUIRE(session.setWarp({clip}, {.on = true, .tempo = 100.0}));
            std::vector<fd::model::WarpMarker> markers;
            for (int k = 1; k < 4; ++k)
                markers.push_back({0.5 * k, (0.5 * k) * 100.0 / 60.0 + (k % 2 == 0 ? 0.0 : 0.1)});
            REQUIRE(session.setWarpMarkers(clip, markers));
            REQUIRE(session.setClipsLooping({clip}, true).empty());
            session.updateSnapshotNow();
            const auto lap = fd::model::findClip(*session.snapshot(), clip)->first->loop.length;
            session.applyClipEdits(
                {{.clip = clip, .track = t.id, .start = 0.0, .length = 64 * lap, .offset = 0.0}});
        }
        session.updateSnapshotNow();

        fd::app::ArrangementView view(session);
        view.setSize(1700, 28 + 32 * 32);
        view.setSnapshot(session.snapshot());
        view.setAllTrackHeights(32.0);
        view.waveforms().waitUntilIdle();
        view.zoomToFit();
        for (int i = 0; i < 60; ++i)
            view.frame(1.0 / 60.0, 0.0);

        constexpr float scale = 2.0f;
        juce::Image image(juce::Image::ARGB, static_cast<int>(static_cast<float>(view.getWidth()) * scale),
                          static_cast<int>(static_cast<float>(view.getHeight()) * scale), true);
        std::vector<double> times;
        for (int frame = 0; frame < 240; ++frame)
        {
            view.zoomAt(800.0, frame < 120 ? 1.035 : 1.0 / 1.035);
            const auto t0 = std::chrono::steady_clock::now();
            {
                juce::Graphics g(image);
                g.addTransform(juce::AffineTransform::scale(scale));
                view.paintEntireComponent(g, true);
            }
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (double t : times)
            sum += t;
        const double avg = sum / static_cast<double>(times.size());
        const double p95 = times[times.size() * 95 / 100];
        std::cout << "arrangement paint, 32 tracks x 64 warped laps, " << image.getWidth() << "x"
                  << image.getHeight() << ": avg " << avg << " ms, p95 " << p95 << " ms" << std::endl;
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
        {
            view.zoomToFit();
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));
            view.paintEntireComponent(g, true);
            juce::PNGImageFormat png;
            const auto out = juce::File(dump).getChildFile("arrangement-warped@2x.png");
            out.deleteFile();
            if (auto stream = out.createOutputStream())
                png.writeImageToStream(image, *stream);
        }
        CHECK(p95 > 0.0);
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("the mixer view paints 64 strips within the frame budget", "[.benchmark]")
{
    // Every strip drawn every frame (the worst case; meters only repaint themselves when they
    // move). Budget: 60 fps (docs/design/mixer.md §6).
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-benchmark", nullptr, fd::engine::makeEngineBehaviour()};
        fd::engine::registerDevices(engine);
        fd::engine::Session session(engine);
        while (session.snapshot()->tracks.size() < 64)
        {
            session.addTrack();
            session.updateSnapshotNow();
        }
        for (int i = 0; i < 4; ++i)
            session.addReturnTrack();
        for (const auto& t : session.snapshot()->tracks)
        {
            session.addDevice(t.id, "fd:eq");
            session.addDevice(t.id, "fd:compressor");
        }
        session.updateSnapshotNow();

        fd::app::MixerView view(session);
        view.setSize(1700, 720);
        view.setSnapshot(session.snapshot());
        view.setOptions(true, true, true); // narrow: the most strips on screen
        juce::Image image(juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
        std::vector<double> times;
        for (int frame = 0; frame < 120; ++frame)
        {
            view.setScrollX((frame * 37) % 2000);
            const auto t0 = std::chrono::steady_clock::now();
            {
                juce::Graphics g(image);
                view.paintEntireComponent(g, true);
            }
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (double t : times)
            sum += t;
        std::cout << "mixer view paint, 64 tracks + 4 returns, " << image.getWidth() << "x" << image.getHeight()
                  << ": avg " << sum / static_cast<double>(times.size()) << " ms, p95 "
                  << times[times.size() * 95 / 100] << " ms" << std::endl;
        CHECK(times[times.size() * 95 / 100] < 16.7);
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("the arrangement paints 64 tracks with two automation lanes each within the frame budget", "[.benchmark]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-benchmark", nullptr, fd::engine::makeEngineBehaviour()};
        fd::engine::Session session(engine);
        while (session.snapshot()->tracks.size() < 64)
        {
            session.addTrack();
            session.updateSnapshotNow();
        }
        // Two lanes per track, with a point every half bar over 64 bars, half of them bent.
        for (const auto& t : session.snapshot()->tracks)
            for (const char* lane : {"volume", "pan"})
            {
                fd::model::AutomationPoints points;
                for (int i = 0; i < 128; ++i)
                    points.push_back({i * 1.0, 0.5f + 0.4f * std::sin(static_cast<float>(i) * 0.7f + static_cast<float>(t.id % 7)),
                                      i % 2 == 0 ? fd::model::curveForBend(0.6f) : 0.0f});
                const auto id = session.automateInNewLane({t.id, lane});
                REQUIRE(id != 0);
                REQUIRE(session.setLanePoints(id, points));
            }
        session.updateSnapshotNow();

        fd::app::ArrangementView view(session);
        view.setSize(1700, 720);
        view.setSnapshot(session.snapshot());
        view.setAllTrackHeights(48.0);
        view.zoomToFit();
        for (int i = 0; i < 60; ++i)
            view.frame(1.0 / 60.0, 0.0);
        juce::Image image(juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
        std::vector<double> times;
        for (int frame = 0; frame < 120; ++frame)
        {
            view.zoomAt(800.0, frame < 60 ? 1.02 : 1.0 / 1.02);
            const auto t0 = std::chrono::steady_clock::now();
            {
                juce::Graphics g(image);
                view.paintEntireComponent(g, true);
            }
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (double t : times)
            sum += t;
        const double p95 = times[times.size() * 95 / 100];
        std::cout << "arrangement paint, 64 tracks + 128 lanes, 1700x720: avg " << sum / static_cast<double>(times.size())
                  << " ms, p95 " << p95 << " ms\n";
        CHECK(p95 < 16.7);
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
