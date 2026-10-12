// The Loudness Meter's panel with a song playing (devices/loudness-meter/SPEC.md): the readings,
// the bars, the history over the song and the stereo bars, drawn for review (FD_DUMP_DIR); Pause
// and Reset reach the meter.
#include "LoudnessMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd/testkit/Signals.h>
#include <fd_device_ui/fd_device_ui.h>

using fd::testkit::AudioBuffer;
using M = fd::devices::LoudnessMeter;

namespace
{
constexpr double sr = 48000.0;

void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

/// What the panel's refresh tick does on screen (it rests offscreen): every control refreshed.
void refresh(juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (auto* control = dynamic_cast<fd::ui::ParamControl*>(c))
            control->refresh();
        refresh(*c);
    }
}

/// A song: a quiet intro, a verse, a loud chorus whose peaks pass the ceiling, a fade; a little
/// stereo in it.
AudioBuffer song(double seconds)
{
    const auto n = static_cast<int>(seconds * sr);
    AudioBuffer out(2, n);
    const auto noise = fd::testkit::pinkNoise(n, 5, 1.0f);
    const auto side = fd::testkit::pinkNoise(n, 9, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double db = t < 8 ? -14 : t < 20 ? -6 : t < 32 ? 0 : -(t - 32) * 2.5;
        const float g = static_cast<float>(std::pow(10.0, db / 20.0));
        const float hit = t >= 20 && t < 32 && std::fmod(t, 0.5) < 0.004 ? 4.0f : 1.0f; // peaks over -1 dBTP
        const float mid = noise.at(0, i) * g * hit, s = side.at(0, i) * g * 0.25f;
        out.at(0, i) = mid + s;
        out.at(1, i) = mid - s;
    }
    return out;
}

/// Plays `in` from `at` (samples) through the device, half a second at a time, the panel taking
/// each part as it would on screen.
void play(fd::Device& device, juce::Component& panel, const AudioBuffer& in, std::int64_t at, bool playing)
{
    constexpr int block = 512, part = 24000;
    AudioBuffer out(2, block);
    for (int start = 0; start < in.numSamples(); start += part)
    {
        for (int i = start; i < std::min(start + part, in.numSamples()); i += block)
        {
            const int n = std::min(block, in.numSamples() - i);
            const float* inPtrs[] = {in.channel(0) + i, in.channel(1) + i};
            float* outPtrs[] = {out.channel(0), out.channel(1)};
            fd::ProcessContext ctx;
            ctx.input = {const_cast<float* const*>(inPtrs), 2};
            ctx.output = {outPtrs, 2};
            ctx.numSamples = n;
            ctx.transport.playing = playing;
            ctx.transport.samplePosition = at + i;
            device.process(ctx);
        }
    }
    pump(10);
    refresh(panel);
}

template <typename T, typename Match> T* find(juce::Component& root, Match match)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(c); t != nullptr && match(*t))
            return t;
        if (auto* deeper = find<T>(*c, match))
            return deeper;
    }
    return nullptr;
}

void dump(const juce::String& name, const juce::Image& image)
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
    {
        const auto file = juce::File(dir).getChildFile(name);
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}
} // namespace

TEST_CASE("loudness meter panel: a song counted, paused and reset, drawn for review", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* factory = fd::findDevice("loudness-meter");
    REQUIRE(factory != nullptr);
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "loudness-meter")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);

    auto device = factory->create();
    device->prepare({sr, 512});
    const auto& meter = dynamic_cast<const M&>(*device);
    fd::ui::LocalParamBinding binding(*device);
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get()};
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);

    // Nothing yet: it asks for the song.
    refresh(*panel);
    dump("loudness-panel-empty@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));

    // The song, played through; then the playhead parked in the chorus.
    const auto s = song(44.0);
    play(*device, *panel, s, 0, true);
    CHECK(meter.store().heardCount() == 440);
    play(*device, *panel, AudioBuffer(2, 4800), static_cast<std::int64_t>(26 * sr), false);
    dump("loudness-panel@1x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f));
    dump("loudness-panel@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));

    // The pointer over the history.
    auto* history = panel->findSection("history")->placements().front().component;
    REQUIRE(history != nullptr);
    const juce::Point<float> at(static_cast<float>(history->getWidth()) * 0.55f,
                                static_cast<float>(history->getHeight()) * 0.5f);
    history->mouseMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, history, history, juce::Time::getCurrentTime(), at,
                                        juce::Time::getCurrentTime(), 0, false));
    const auto historyArea = panel->getLocalArea(history, history->getLocalBounds());
    dump("loudness-history-hover@2x.png", panel->createComponentSnapshot(historyArea, true, 2.0f));

    // In LU, on the wide scale.
    binding.setPlain(M::Units, 1.0f);
    binding.setPlain(M::Scale, 1.0f);
    refresh(*panel);
    dump("loudness-panel-lu@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    binding.setPlain(M::Units, 0.0f);
    binding.setPlain(M::Scale, 0.0f);

    // Pause holds the count, and the button shows it; Resume and Reset reach the meter too.
    auto* pause = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Pause"; });
    REQUIRE(pause != nullptr);
    pause->triggerClick();
    pump(50);
    play(*device, *panel, s, 0, true);
    CHECK(pause->getToggleState());
    CHECK(meter.store().heardCount() == 440);
    dump("loudness-panel-paused@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    pause->triggerClick();
    pump(50);
    play(*device, *panel, AudioBuffer(2, 512), 0, true);
    CHECK_FALSE(pause->getToggleState());
    auto* reset = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Reset"; });
    REQUIRE(reset != nullptr);
    reset->triggerClick();
    pump(50);
    play(*device, *panel, AudioBuffer(2, 512), 0, false);
    CHECK(meter.store().heardCount() == 0);
}
