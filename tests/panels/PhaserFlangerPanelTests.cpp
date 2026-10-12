// The Phaser & Flanger's panel (devices/phaser-flanger/SPEC.md, "UI layout"): Center or Time
// follows the mode and so does the first section's name, the display sets the sweep, reads its
// range, and what a mode doesn't use dims.
#include "PhaserFlanger.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using P = fd::devices::PhaserFlanger;

namespace
{
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

juce::MouseEvent mouseEvent(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged)
{
    return {juce::Desktop::getInstance().getMainMouseSource(),
            pos,
            juce::ModifierKeys::leftButtonModifier,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &c,
            &c,
            juce::Time::getCurrentTime(),
            down,
            juce::Time::getCurrentTime(),
            1,
            dragged};
}

void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown(mouseEvent(c, from, from, false));
    for (float k : {0.25f, 0.5f, 1.0f})
        c.mouseDrag(mouseEvent(c, from + (to - from) * k, from, true));
    c.mouseUp(mouseEvent(c, to, from, true));
}

struct Panel
{
    explicit Panel(float mode = 0.0f)
    {
        binding.setPlain(P::Mode, mode);
        const fd::ui::PanelFactory* panelFactory = nullptr;
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "phaser-flanger")
                panelFactory = p;
        REQUIRE(panelFactory != nullptr);
        panel = panelFactory->create(ctx);
        panel->onEnabledChange = [](bool) {};
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        (void)panel->createComponentSnapshot(panel->getLocalBounds());
    }

    float get(size_t p) const { return binding.getPlain(p); }
    void set(size_t p, float v)
    {
        binding.setPlain(p, v);
        refresh(*panel);
    }
    juce::Component& display() const { return *panel->findSection("effect")->placements().front().component; }
    juce::Component& named(const juce::String& name) const
    {
        auto* c = find<juce::Component>(*panel, [&](auto& k) { return k.getName() == name; });
        REQUIRE(c != nullptr);
        return *c;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<fd::Device> device = fd::findDevice("phaser-flanger")->create();
    fd::ui::LocalParamBinding binding{*device};
    fd::ui::DeviceUiContext ctx{fd::findDevice("phaser-flanger")->info(), binding, &device->analysis(),
                                device.get(), nullptr};
    std::unique_ptr<fd::ui::DevicePanel> panel;
};
} // namespace

TEST_CASE("phaser-flanger panel: Center or Time follows the mode, and so does the section's name", "[ui]")
{
    Panel p;
    auto* place = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.getName() == "Center"; });
    REQUIRE(place != nullptr);
    CHECK(place->param() == P::PhaserCenter);
    CHECK(p.panel->findSection("effect")->sectionTitle() == "Phaser");

    p.set(P::Mode, 1.0f);
    CHECK(place->param() == P::FlangerTime);
    CHECK(place->getName() == "Time");
    CHECK(p.panel->findSection("effect")->sectionTitle() == "Flanger");

    p.set(P::Mode, 2.0f);
    CHECK(place->param() == P::DoublerTime);
    CHECK(place->textFromValue(static_cast<float>(place->getValue())) == "20.0 ms");
    CHECK(p.panel->findSection("effect")->sectionTitle() == "Doubler");

    // Rate follows Sync; the stereo knob follows Stereo.
    auto* rate = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.param() == P::Rate; });
    auto* stereo = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.param() == P::Offset; });
    REQUIRE(rate != nullptr);
    REQUIRE(stereo != nullptr);
    p.set(P::Sync, 1.0f);
    p.set(P::Stereo, 1.0f);
    CHECK(rate->param() == P::Div);
    CHECK(stereo->param() == P::Spin);
}

TEST_CASE("phaser-flanger panel: dragging the response moves the sweep and widens it", "[ui]")
{
    Panel p;
    auto& response = p.display();
    const auto centre = response.getLocalBounds().toFloat().getCentre();
    const auto travel = fd::ui::Theme::current().input.valueTravel;

    SECTION("Phaser: right raises Center, up widens Amount")
    {
        drag(response, centre, centre + juce::Point<float>(travel * 0.1f, -travel * 0.2f));
        CHECK(p.get(P::PhaserCenter) > 800.0f);
        CHECK(p.get(P::Amount) > 65.0f);
        CHECK(p.get(P::Amount) < 75.0f);
    }

    SECTION("Flanger: right shortens Time, so the comb rises")
    {
        p.set(P::Mode, 1.0f);
        drag(response, centre, centre + juce::Point<float>(travel * 0.1f, 0.0f));
        CHECK(p.get(P::FlangerTime) < 2.0f);
        CHECK(p.get(P::PhaserCenter) == 800.0f);
    }
}

TEST_CASE("phaser-flanger panel: the display reads the range the sweep covers", "[ui]")
{
    Panel p;
    auto& response = p.display();
    const auto dash = juce::String::fromUTF8("\xE2\x80\x93");
    CHECK(response.getDescription() == "283 Hz" + dash + "2.3 kHz"); // 800 Hz, 1.5 octaves either way
    // With the envelope, as far as it takes the sweep (three octaves up, held at 18 kHz).
    p.set(P::Env, 1.0f);
    p.set(P::EnvAmount, 100.0f);
    CHECK(response.getDescription() == "283 Hz" + dash + "18.0 kHz");
    p.set(P::Env, 0.0f);
    p.set(P::PhaserCenter, 300.0f);
    p.set(P::Amount, 20.0f);
    CHECK(response.getDescription() == "198" + dash + "455 Hz"); // one unit where both ends share it
    p.set(P::Amount, 50.0f);

    p.set(P::Mode, 1.0f);
    CHECK(response.getDescription() == "1.0" + dash + "2.0 ms");
    p.set(P::FlangerZero, 1.0f);
    p.set(P::Amount, 100.0f);
    CHECK(response.getDescription() == "0.0" + dash + "2.0 ms, through zero");
}

TEST_CASE("phaser-flanger panel: what a mode doesn't use dims", "[ui]")
{
    for (const float mode : {0.0f, 1.0f, 2.0f})
    {
        INFO("mode " << mode);
        Panel p(mode);
        CHECK((p.named("Notches").getAlpha() < 0.5f) == (mode != 0.0f));
        CHECK((p.named("Spread").getAlpha() < 0.5f) == (mode != 0.0f));
        CHECK((p.named("Through Zero").getAlpha() < 0.5f) == (mode != 1.0f));
        CHECK(p.named("Warmth").getAlpha() == 1.0f);
    }
}
