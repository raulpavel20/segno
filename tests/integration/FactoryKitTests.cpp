// The factory kits (docs/design/drum-kit.md §27) heard from the Library a development build makes
// (cmake --build --preset dev --target segno-library); skipped where it isn't built. Every pad's
// Sampler loads its piece with nothing missing, and every kit plays its preview beat: it sounds,
// it doesn't clip. FD_PRESET_REPORT=1 prints each kit's level, for balancing them; FD_DUMP_DIR
// writes each kit's beat as a WAV.
#include "Sampler.h"
#include "engine/PresetPreview.h"
#include "model/FactoryPresets.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fd/device/Library.h>
#include <fd/device/Presets.h>
#include <fd/device/Registry.h>
#include <filesystem>
#include <iostream>
#include <juce_audio_formats/juce_audio_formats.h>

TEST_CASE("factory kits: every pad plays its piece from the Library and sounds; every kit's beat sounds, "
          "unclipped",
          "[library][integration]")
{
    const auto library = std::filesystem::path(FD_SOURCE_DIR) / ".cache" / "library";
    if (!std::filesystem::exists(library / "library.json"))
        SKIP("The Library isn't built: cmake --build --preset dev --target segno-library");
    fd::library::setRoot(library);
    const auto* sampler = fd::findDevice("sampler");
    REQUIRE(sampler != nullptr);
    int kits = 0;
    for (const auto& file : fd::model::dawFactoryPresets())
    {
        const auto p = fd::parsePresetFile(file.text, std::string(file.name));
        REQUIRE(p);
        if (p->state.deviceId != "drum-kit")
            continue;
        INFO(p->name);
        for (const auto& pad : p->state.extra["pads"])
        {
            INFO(pad["name"].get<std::string>());
            const auto state = fd::stateFromJson(pad["modules"][0]["state"]);
            REQUIRE(state);
            auto device = sampler->create();
            REQUIRE(fd::applyState(*device, *state) == fd::ApplyResult::Ok);
            const auto& s = dynamic_cast<const fd::devices::Sampler&>(*device);
            CHECK(s.loadError().empty());
            REQUIRE(s.map() != nullptr);
            CHECK(s.map()->missing == 0);
            CHECK(s.map()->ignored.empty());
        }
        const auto beat = fd::engine::renderKitBeat(p->state, 48000.0);
        REQUIRE(beat.getNumSamples() > 0);
        float peak = 0.0f;
        double sum = 0.0;
        for (int c = 0; c < beat.getNumChannels(); ++c)
            for (int i = 0; i < beat.getNumSamples(); ++i)
            {
                const float x = beat.getSample(c, i);
                REQUIRE(std::isfinite(x));
                peak = std::max(peak, std::abs(x));
                sum += static_cast<double>(x) * x;
            }
        const double rms = 10.0 * std::log10(sum / (beat.getNumChannels() * beat.getNumSamples()) + 1e-20);
        const double peakDb = 20.0 * std::log10(peak + 1e-20);
        CHECK(peakDb <= -0.5);
        CHECK(rms > -40.0);
        if (std::getenv("FD_PRESET_REPORT") != nullptr)
            std::cout << "kit | " << p->name << " | level " << rms << " dB, peak " << peakDb << " dBFS\n";
        // Every pad alone, off the beat's notes (the preview plays it once): it sounds. FD_DUMP_DIR:
        // the kit's beat as a WAV, and its every pad in turn, half a second apart, for hearing them all.
        const auto& pads = p->state.extra["pads"];
        const int step = 24000;
        juce::AudioBuffer<float> tour(2, static_cast<int>(pads.size()) * step + 3 * 48000);
        tour.clear();
        for (size_t i = 0; i < pads.size(); ++i)
        {
            INFO(pads[i]["name"].get<std::string>());
            auto alone = p->state;
            alone.extra["pads"] = nlohmann::json::array({pads[i]});
            alone.extra["pads"][0]["note"] = 60;
            const auto hit = fd::engine::renderKitBeat(alone, 48000.0);
            REQUIRE(hit.getNumSamples() > 0);
            CHECK(hit.getMagnitude(0, hit.getNumSamples()) > 0.01f); // -40 dBFS at least
            CHECK(hit.getMagnitude(0, hit.getNumSamples()) < 1.0f);
            for (int c = 0; c < 2 && c < hit.getNumChannels(); ++c)
                tour.addFrom(
                    c, static_cast<int>(i) * step, hit, c, 0,
                    std::min(hit.getNumSamples(), tour.getNumSamples() - static_cast<int>(i) * step));
        }
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            const auto write = [&](const juce::String& name, const juce::AudioBuffer<float>& audio)
            {
                const auto file = juce::File(dir).getChildFile(name + ".wav");
                file.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
                if (auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}
                                                               .withSampleRate(48000.0)
                                                               .withNumChannels(audio.getNumChannels())
                                                               .withBitsPerSample(24)))
                    writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
            };
            write(juce::String::fromUTF8(p->name.c_str()), beat);
            write(juce::String::fromUTF8(p->name.c_str()) + " pads", tour);
        }
        ++kits;
    }
    CHECK(kits > 0);
}
