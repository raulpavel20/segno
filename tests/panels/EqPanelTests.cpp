// The EQ's panel with sound flowing (devices/eq/SPEC.md, "Stage 2"): a bass, a kick compared in
// the sidechain, a dynamic band moving; Make room and Listen through the host binding; drawn for
// review (FD_DUMP_DIR).
#include "Eq.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd/testkit/Signals.h>
#include <fd_device_ui/fd_device_ui.h>

using fd::testkit::AudioBuffer;
using E = fd::devices::Eq;

namespace
{
constexpr double sr = 48000.0;

struct Compare final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Kick"}; }
    int selected() const override { return 1; }
    void select(int) override {}
};

void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

void refresh(juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (auto* control = dynamic_cast<fd::ui::ParamControl*>(c))
            control->refresh();
        refresh(*c);
    }
}

void play(fd::Device& device, juce::Component& panel, const AudioBuffer& in, const AudioBuffer& key)
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
            const float* keyPtrs[] = {key.channel(0) + at, key.channel(1) + at};
            fd::ProcessContext ctx;
            ctx.input = {const_cast<float* const*>(inPtrs), 2};
            ctx.output = {outPtrs, 2};
            ctx.sidechain = {const_cast<float* const*>(keyPtrs), 2};
            ctx.numSamples = n;
            device.process(ctx);
        }
        pump(20);
        refresh(panel);
    }
}

void mixInto(AudioBuffer& a, const AudioBuffer& b)
{
    for (int c = 0; c < a.numChannels(); ++c)
        for (int i = 0; i < std::min(a.numSamples(), b.numSamples()); ++i)
            a.at(c, i) += b.at(c, i);
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

juce::MouseEvent moveAt(juce::Component& c, juce::Point<float> at)
{
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &c, &c, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 0,
                            false);
}
} // namespace

TEST_CASE("eq panel: dynamics, the masking display, Make room and Listen, drawn for review", "[ui]")
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
    fd::ui::LocalParamBinding binding(*device);
    Compare compare;
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), &compare};
    // Band 2 (as a project opens with it): a dynamic bell on the bass's honk at 700 Hz, cutting
    // 8 dB when it's 6 dB over its usual level... and a set threshold, so it moves on this loop.
    binding.setPlain(E::paramIndex(1, E::Freq), 700.0f);
    binding.setPlain(E::paramIndex(1, E::Gain), 2.0f);
    binding.setPlain(E::paramIndex(1, E::Q), 2.0f);
    binding.setPlain(E::dynIndex(1, E::Dyn), 1.0f);
    binding.setPlain(E::dynIndex(1, E::Range), -8.0f);
    binding.setPlain(E::dynIndex(1, E::AutoThreshold), 0.0f);
    binding.setPlain(E::dynIndex(1, E::Threshold), -36.0f);
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);

    // A bass (55 Hz and its harmonics, a honk at 700 Hz) over a pink bed; the kick: a 60 Hz thump.
    auto bass = fd::testkit::pinkNoise(static_cast<int>(sr * 2), 9, 0.1f);
    for (const double hz : {55.0, 110.0, 165.0, 700.0})
        mixInto(bass, fd::testkit::sine(hz, sr, bass.numSamples(), hz == 55.0 ? 0.3f : 0.1f));
    auto kick = fd::testkit::sine(60.0, sr, bass.numSamples(), 0.4f);
    mixInto(kick, fd::testkit::pinkNoise(kick.numSamples(), 4, 0.02f));
    play(*device, *panel, bass, kick);
    dump("eq-panel@1x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f));
    dump("eq-panel@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));

    // The display: the kick's curve and the collision in the lows; hovering it offers Make room.
    auto* display = panel->findSection("curve")->placements().front().component;
    REQUIRE(display != nullptr);
    const double hz = 60.0;
    const float x = static_cast<float>(std::log(hz / 20.0) / std::log(1000.0) * display->getWidth());
    display->mouseMove(moveAt(*display, {x, static_cast<float>(display->getHeight()) * 0.5f}));
    auto* makeRoom =
        find<fd::ui::Toggle>(*display, [](auto& t) { return t.getButtonText().startsWith("Make room"); });
    REQUIRE(makeRoom != nullptr);
    CHECK(makeRoom->isVisible());
    CHECK(makeRoom->getButtonText() == "Make room for Kick");
    const auto area = panel->getLocalArea(display, display->getLocalBounds());
    dump("eq-display-make-room@2x.png", panel->createComponentSnapshot(area, true, 2.0f));

    // Make room: a dynamic bell in the lows, triggered by the kick, cutting 6 dB.
    makeRoom->triggerClick();
    pump(50);
    int made = -1;
    for (int b = 0; b < E::numBands; ++b)
        if (binding.getPlain(E::dynIndex(b, E::Trigger)) >= 0.5f &&
            binding.getPlain(E::paramIndex(b, E::On)) >= 0.5f)
            made = b;
    REQUIRE(made >= 0);
    CHECK(binding.getPlain(E::dynIndex(made, E::Dyn)) >= 0.5f);
    CHECK(binding.getPlain(E::dynIndex(made, E::Range)) == -6.0f);
    CHECK(binding.getPlain(E::dynIndex(made, E::AutoThreshold)) < 0.5f);
    const float freq = binding.getPlain(E::paramIndex(made, E::Freq));
    CHECK(freq > 30.0f);
    CHECK(freq < 120.0f);
    play(*device, *panel, bass, kick);
    dump("eq-panel-made-room@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));

    // Listen: the selected band's range; transient (never kept).
    auto* listen = find<fd::ui::Toggle>(*display, [](auto& t) { return t.getButtonText() == "Listen"; });
    REQUIRE(listen != nullptr);
    listen->triggerClick();
    pump(50);
    CHECK(binding.getPlain(E::Listen) == static_cast<float>(made + 1));
    refresh(*panel);
    CHECK(listen->getToggleState());
    listen->triggerClick();
    pump(50);
    CHECK(binding.getPlain(E::Listen) == 0.0f);
}
