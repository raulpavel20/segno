// Waveform tiles (C3) must look like direct drawing: same envelope, same place, at the exact zoom.

#include "fd/audiofile/AudioFile.h"
#include "model/Peaks.h"
#include "ui/WaveformTiles.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

TEST_CASE("waveform tiles match direct drawing", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<std::vector<float>> ch(1, std::vector<float>(5 * 48000));
    for (size_t i = 0; i < ch[0].size(); ++i)
        ch[0][i] = 0.8f * std::sin(0.002f * static_cast<float>(i)) * std::exp(-static_cast<float>(i % 30000) / 9000.0f);
    const auto path = (std::filesystem::temp_directory_path() / "fd-tiles-test.wav").string();
    REQUIRE(fd::audiofile::writeWav(path, ch, 48000.0));
    const auto peaks = fd::model::buildPeaks(path);
    REQUIRE(peaks != nullptr);

    fd::model::Timeline timeline;
    timeline.setWidth(1200.0);
    timeline.setView(0.5, 237.3); // an arbitrary zoom: tile edges fall mid-pixel
    const fd::model::ClipInfo clip{.id = 1, .start = 1.0, .length = 3.5, .offset = 0.7, .sourcePath = path};
    const auto body = juce::Rectangle<float>(0.0f, 10.0f, 1200.0f, 60.0f);
    const auto visible = juce::Rectangle<int>(0, 0, 1200, 80);

    auto render = [&](bool tiles) {
        juce::Image image(juce::Image::ARGB, 2400, 160, true, juce::SoftwareImageType());
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        fd::app::WaveformTiles cache;
        cache.beginPaint();
        if (tiles)
            cache.draw(g, *peaks, clip, body, visible, timeline, 0, juce::Colours::white, true);
        else
            fd::app::WaveformTiles::drawDirect(g, *peaks, clip, body, visible, timeline, 0, juce::Colours::white);
        CHECK_FALSE(cache.wantsRepaint());
        return image;
    };
    const auto direct = render(false);
    const auto tiled = render(true);

    // Compare per logical column (two device columns): tiles are drawn at device resolution,
    // direct drawing per logical pixel, so compare the union of each pair.
    int mismatched = 0, painted = 0;
    for (int x = 0; x + 1 < direct.getWidth(); x += 2)
    {
        auto extent = [&](const juce::Image& img) {
            int top = -1, bottom = -1;
            for (int dx = 0; dx < 2; ++dx)
                for (int y = 0; y < img.getHeight(); ++y)
                    if (img.getPixelAt(x + dx, y).getAlpha() > 127)
                    {
                        top = top < 0 ? y : std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
            return std::pair{top, bottom};
        };
        const auto a = extent(direct), b = extent(tiled);
        if (a.first < 0 && b.first < 0)
            continue;
        ++painted;
        if (std::max(std::abs(a.first - b.first), std::abs(a.second - b.second)) > 3)
            ++mismatched;
    }
    // Columns can differ only where a transient falls on a column edge: peak bins round out to
    // their boundaries differently for device and logical columns.
    INFO(mismatched << " of " << painted << " columns differ by more than 3 device px");
    CHECK(painted > 1000);
    CHECK(mismatched <= painted / 100);
    std::filesystem::remove(path);
}
