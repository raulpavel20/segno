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

TEST_CASE("eq panel: linear phase, a spectral band and the delay it costs, drawn for review", "[ui]")
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
    fd::ui::LocalParamBinding binding(*device);
    Compare compare;
    fd::ui::DeviceUiContext ctx{factory->info(), binding, &device->analysis(), device.get(), &compare};
    // Linear phase, Medium; band 1 a subsonic cut; band 2 a wide spectral bell on a guitar's
    // resonance at 3 kHz.
    binding.setPlain(E::Phase, 1.0f);
    binding.setPlain(E::paramIndex(0, E::Type), static_cast<float>(E::BandType::LowCut));
    binding.setPlain(E::paramIndex(0, E::Freq), 30.0f);
    binding.setPlain(E::paramIndex(1, E::Freq), 3000.0f);
    binding.setPlain(E::paramIndex(1, E::Q), 0.6f);
    binding.setPlain(E::dynIndex(1, E::Dyn), 1.0f);
    binding.setPlain(E::dynIndex(1, E::Range), -12.0f);
    binding.setPlain(E::spectralIndex(1, E::Spectral), 1.0f);
    binding.setPlain(E::paramIndex(2, E::On), 0.0f); // band 3's node would sit on band 2's
    device->prepare({sr, 512}); // as a host does, once the settings that change its delay are known
    auto panel = panelFactory->create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);

    // The badge says how late, and why it matters.
    fd::ui::LatencyBadge* badge = nullptr;
    for (auto* c : panel->headerControls())
        if (auto* b = dynamic_cast<fd::ui::LatencyBadge*>(c))
            badge = b;
    REQUIRE(badge != nullptr);
    CHECK(badge->isVisible());
    CHECK(badge->text() == "+107 ms");
    CHECK(badge->getTooltip().contains("what you play live on this track is heard 107 ms late"));
    // Where it's chosen, each choice names its delay.
    auto* processing =
        find<fd::ui::ParamComboBox>(*panel, [](auto& c) { return c.getText() == "Linear phase"; });
    REQUIRE(processing != nullptr);
    CHECK(processing->menuText(0) == "Zero latency: about 105 ms"); // the spectral band delays it anyway
    CHECK(processing->menuText(1) == "Linear phase: about 105 ms");
    auto* resolution = find<fd::ui::ParamComboBox>(*panel, [](auto& c) { return c.getText() == "Medium"; });
    REQUIRE(resolution != nullptr);
    CHECK(resolution->menuText(0) == "Low: about 55 ms");
    CHECK(resolution->menuText(2) == "High: about 215 ms");
    // DYNAMICS: Spectral on, so Detail where the trigger meter would be.
    auto* spectral = find<fd::ui::Toggle>(*panel, [](auto& t) { return t.getButtonText() == "Spectral"; });
    REQUIRE(spectral != nullptr);
    CHECK(spectral->getToggleState());
    CHECK(find<fd::ui::Knob>(*panel, [](auto& k) { return k.getName() == "Detail"; }) != nullptr);

    // A resonance over pink noise: the spectral band's cut shows on the live curve.
    auto in = fd::testkit::pinkNoise(static_cast<int>(sr * 2), 5, 0.2f);
    mixInto(in, fd::testkit::sine(3000.0, sr, in.numSamples(), 0.05f));
    play(*device, *panel, in, AudioBuffer(2, in.numSamples())); // nothing in the sidechain
    dump("eq-panel-linear@1x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f));
    dump("eq-panel-linear@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    auto* display = panel->findSection("curve")->placements().front().component;
    dump("eq-display-spectral@2x.png",
         panel->createComponentSnapshot(panel->getLocalArea(display, display->getLocalBounds()), true, 2.0f));
    const auto header =
        juce::Rectangle<int>(0, 0, panel->getWidth(), fd::ui::Theme::current().deviceHeaderHeight);
    dump("eq-header-latency@2x.png", panel->createComponentSnapshot(header, true, 2.0f));

    // Back to zero latency with no spectral band: no delay, no badge, and Resolution (which
    // matters only with a delay) gone too.
    CHECK(resolution->isVisible());
    binding.setPlain(E::Phase, 0.0f);
    binding.setPlain(E::spectralIndex(1, E::Spectral), 0.0f);
    badge->refresh();
    panel->resized();
    CHECK_FALSE(badge->isVisible());
    CHECK_FALSE(resolution->isVisible());
    CHECK(processing->isVisible());
}

TEST_CASE("eq panel: the header's dropdowns open again after a choice, and after closing", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto* factory = fd::findDevice("eq");
    const fd::ui::PanelFactory* panelFactory = nullptr;
    for (const auto* p : fd::ui::allPanels())
        if (p->deviceId == "eq")
            panelFactory = p;
    REQUIRE(panelFactory != nullptr);
    auto device = factory->create();
    fd::ui::LocalParamBinding binding(*device);
    fd::ui::DeviceUiContext ctx{factory->info(), binding, nullptr, device.get()};
    auto panel = panelFactory->create(ctx);
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    auto* processing =
        find<fd::ui::ParamComboBox>(*panel, [](auto& c) { return c.getText() == "Zero latency"; });
    REQUIRE(processing != nullptr);
    // A click opens the menu (asynchronously: never run here, so no window shows); closing it
    // with a choice or without one lets the next click open it again.
    const auto click = [processing]
    { processing->mouseDown(moveAt(*processing, processing->getLocalBounds().getCentre().toFloat())); };
    click();
    CHECK(processing->isPopupActive());
    processing->menuClosed(2); // Linear phase
    CHECK(binding.getPlain(E::Phase) == 1.0f);
    CHECK_FALSE(processing->isPopupActive());
    click();
    CHECK(processing->isPopupActive());
    processing->menuClosed(0); // closed without a choice
    CHECK_FALSE(processing->isPopupActive());
    CHECK(binding.getPlain(E::Phase) == 1.0f);
    panel.reset(); // before the menus it asked for could open
}
