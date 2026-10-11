// The Spectrum's panel with sound flowing (devices/spectrum/SPEC.md): Freeze and the reference go
// through the host binding, and the curves, labels, legend and readout are drawn for review
// (FD_DUMP_DIR).
#include "Spectrum.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/audiofile/AudioFile.h>
#include <fd/device/Registry.h>
#include <fd/testkit/Signals.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>

using fd::testkit::AudioBuffer;
using S = fd::devices::Spectrum;

namespace
{
constexpr double sr = 48000.0;

/// The DAW's sources, with a track chosen to compare with.
struct Compare final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Kick"}; }
    int selected() const override { return choice; }
    void select(int index) override { choice = index; }
    int choice = 1;
};

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

/// Plays `in` (and `key` into the sidechain) through the device a quarter second at a time,
/// letting the panel take each part as it would on screen.
void play(fd::Device& device, juce::Component& panel, const AudioBuffer& in, const AudioBuffer* key)
{
    constexpr int block = 512, part = 12000;
    AudioBuffer out(2, block);
    for (int start = 0; start < in.numSamples(); start += part)
    {
        for (int at = start; at < std::min(start + part, in.numSamples()); at += block)
        {
            const int n = std::min(block, in.numSamples() - at);
            const float* inPtrs[] = {in.channel(0) + at, in.channel(1) + at};
            float* outPtrs[] = {out.channel(0), out.channel(1)};
            const float* keyPtrs[] = {key != nullptr ? key->channel(0) + at : nullptr,
                                      key != nullptr ? key->channel(1) + at : nullptr};
            fd::ProcessContext ctx;
            ctx.input = {const_cast<float* const*>(inPtrs), 2};
            ctx.output = {outPtrs, 2};
            if (key != nullptr)
                ctx.sidechain = {const_cast<float* const*>(keyPtrs), 2};
            ctx.numSamples = n;
            device.process(ctx);
        }
        pump(20);
        refresh(panel);
    }
}

/// `b` added into `a`.
void mixInto(AudioBuffer& a, const AudioBuffer& b)
{
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < std::min(a.numSamples(), b.numSamples()); ++i)
            a.at(c, i) += b.at(c, i % b.numSamples());
}

/// The first descendant of `root` that `match` accepts.
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

TEST_CASE("spectrum panel: sound compared with a track, frozen, with a reference, drawn for review", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* factory = fd::findDevice("spectrum");
    REQUIRE(factory != nullptr);
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "spectrum")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);

    auto device = factory->create();
    device->prepare({sr, 512});
    const auto& spectrum = dynamic_cast<const S&>(*device);
    fd::ui::LocalParamBinding binding(*device);
    Compare compare;
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), &compare};
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    auto* display = panel->findSection("spectrum")->placements().front().component;
    REQUIRE(display != nullptr);

    // A bass (55 Hz and its harmonics) over a pink bed; the kick: a 60 Hz thump.
    auto mix = fd::testkit::pinkNoise(static_cast<int>(sr * 2), 9, 0.2f);
    for (const double hz : {55.0, 110.0, 165.0})
        mixInto(mix, fd::testkit::sine(hz, sr, mix.numSamples(), hz == 55.0 ? 0.3f : 0.1f));
    mixInto(mix, fd::testkit::sine(2600.0, sr, mix.numSamples(), 0.05f)); // a resonance to find
    auto kick = fd::testkit::sine(60.0, sr, mix.numSamples(), 0.4f);
    mixInto(kick, fd::testkit::pinkNoise(mix.numSamples(), 4, 0.02f));
    play(*device, *panel, mix, &kick);

    // Freeze keeps the curve, through the binding.
    auto* freeze = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Freeze"; });
    REQUIRE(freeze != nullptr);
    freeze->triggerClick();
    pump(100);
    refresh(*panel);
    REQUIRE(spectrum.snapshot());
    CHECK(spectrum.snapshot()->channel == 0);

    // A reference dropped on the display: read off the message thread, then kept.
    const auto path = std::filesystem::temp_directory_path() / "fd-spectrum-reference.wav";
    auto bright = fd::testkit::whiteNoise(static_cast<int>(sr * 4), 3, 0.05f);
    mixInto(bright, fd::testkit::pinkNoise(bright.numSamples(), 8, 0.2f));
    std::vector<std::vector<float>> channels{{bright.channel(0), bright.channel(0) + bright.numSamples()},
                                             {bright.channel(1), bright.channel(1) + bright.numSamples()}};
    REQUIRE(fd::audiofile::writeWav(path.string(), channels, sr));
    auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(display);
    REQUIRE(target != nullptr);
    CHECK(target->isInterestedInFileDrag({juce::String(path.string())}));
    CHECK_FALSE(target->isInterestedInFileDrag({"notes.txt"}));
    target->filesDropped({juce::String(path.string())}, 0, 0);
    for (int i = 0; i < 100 && !spectrum.reference(); ++i)
    {
        pump(50);
        refresh(*panel);
    }
    REQUIRE(spectrum.reference());
    CHECK(spectrum.reference()->name == "fd-spectrum-reference.wav");
    std::filesystem::remove(path);

    // A little more, with everything shown, and the side overlay on.
    binding.setPlain(S::Side, 1.0f);
    play(*device, *panel, mix, &kick);
    dump("spectrum-panel@1x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f));
    dump("spectrum-panel@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    const auto displayArea = panel->getLocalArea(display, display->getLocalBounds());
    dump("spectrum-display@2x.png", panel->createComponentSnapshot(displayArea, true, 2.0f));

    // The levels under the pointer.
    const juce::Point<float> at(static_cast<float>(display->getWidth()) * 0.3f,
                                static_cast<float>(display->getHeight()) * 0.5f);
    display->mouseMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, display, display, juce::Time::getCurrentTime(), at,
                                        juce::Time::getCurrentTime(), 0, false));
    dump("spectrum-hover@2x.png", panel->createComponentSnapshot(displayArea, true, 2.0f));

    // Clear takes each away again.
    for (auto* clear = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Clear"; });
         clear != nullptr; clear = nullptr)
        clear->triggerClick();
    pump(100);
    refresh(*panel);
    CHECK_FALSE(spectrum.snapshot());
}

TEST_CASE("eq panel: its spectrum through the shared analyser, drawn for review", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* factory = fd::findDevice("eq");
    REQUIRE(factory != nullptr);
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "eq")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);
    auto device = factory->create();
    device->prepare({sr, 512});
    CHECK(device->analysis().sampleRate() == sr);
    fd::ui::LocalParamBinding binding(*device);
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), nullptr};
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);

    auto mix = fd::testkit::pinkNoise(static_cast<int>(sr), 2, 0.2f);
    mixInto(mix, fd::testkit::sine(220.0, sr, mix.numSamples(), 0.2f));
    auto* display = panel->findSection("curve")->placements().front().component;
    REQUIRE(display != nullptr);
    play(*device, *panel, mix, nullptr);
    dump("eq-display@2x.png",
         panel->createComponentSnapshot(panel->getLocalArea(display, display->getLocalBounds()), true, 2.0f));
}
