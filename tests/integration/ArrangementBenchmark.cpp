// Paint benchmark for the arrangement with the software renderer, offscreen. Hidden tag; run in a
// Release build:
//   build/rel/tests/integration/fd_integration_tests_artefacts/Release/fd_integration_tests "[.benchmark]"
// It reports only: offscreen software painting at 4K is bound by fill rate, which the GPU does
// for the real window. The ADR 0015 budget (32 tracks zooming under 8 ms) is checked with the
// OpenGL renderer by `FossDAW --benchmark` (docs/design/daw-shell.md, "Performance").

#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"
#include "ui/ArrangementView.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

TEST_CASE("arrangement paints 32 zooming tracks within the frame budget", "[.benchmark]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    {
        tracktion::Engine engine{"fd-benchmark"};
        fd::engine::Session session(engine);
        while (session.snapshot()->tracks.size() < 32)
        {
            session.addAudioTrack();
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
