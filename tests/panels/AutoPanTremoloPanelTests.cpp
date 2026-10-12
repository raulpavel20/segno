// The Auto Pan & Tremolo's panel (devices/auto-pan-tremolo/SPEC.md, "UI layout"): the first
// section takes the mode's name, Rate follows Sync and the stereo knob Stereo, the display sets
// Rate and Amount and reads the depth or the swing, and what a mode doesn't use dims.
#include "AutoPanTremolo.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using A = fd::devices::AutoPanTremolo;

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
        binding.setPlain(A::Mode, mode);
        const fd::ui::PanelFactory* panelFactory = nullptr;
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "auto-pan-tremolo")
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
    std::unique_ptr<fd::Device> device = fd::findDevice("auto-pan-tremolo")->create();
    fd::ui::LocalParamBinding binding{*device};
    fd::ui::DeviceUiContext ctx{fd::findDevice("auto-pan-tremolo")->info(), binding, &device->analysis(),
                                device.get(), nullptr};
    std::unique_ptr<fd::ui::DevicePanel> panel;
};
} // namespace

TEST_CASE("auto-pan-tremolo panel: the section's name follows the mode; Rate and the stereo knob follow "
          "their switches",
          "[ui]")
{
    Panel p;
    CHECK(p.panel->findSection("effect")->sectionTitle() == "Panning");
    p.set(A::Mode, 1.0f);
    CHECK(p.panel->findSection("effect")->sectionTitle() == "Tremolo");

    auto* rate = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.getName() == "Rate"; });
    auto* stereo = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.param() == A::Offset; });
    REQUIRE(rate != nullptr);
    REQUIRE(stereo != nullptr);
    CHECK(rate->param() == A::Div); // synced by default, a quarter note
    CHECK(rate->textFromValue(static_cast<float>(rate->getValue())) == "1/4");
    p.set(A::Sync, 0.0f);
    CHECK(rate->param() == A::Rate);
    p.set(A::Stereo, 1.0f);
    CHECK(stereo->param() == A::Spin);
}

TEST_CASE("auto-pan-tremolo panel: dragging the movement sets Rate sideways and Amount up and down", "[ui]")
{
    Panel p;
    p.set(A::Sync, 0.0f);
    auto& movement = p.display();
    const auto centre = movement.getLocalBounds().toFloat().getCentre();
    const auto travel = fd::ui::Theme::current().input.valueTravel;
    drag(movement, centre, centre + juce::Point<float>(travel * 0.1f, -travel * 0.2f));
    CHECK(p.get(A::Rate) > 1.0f);
    CHECK(p.get(A::Amount) > 65.0f);
    CHECK(p.get(A::Amount) < 75.0f);
}

TEST_CASE("auto-pan-tremolo panel: the display reads the swing or the depth, and the rate", "[ui]")
{
    Panel p;
    auto& movement = p.display();
    const auto dash = juce::String::fromUTF8("\xE2\x80\x93");
    CHECK(movement.getDescription() ==
          "50L" + dash + "50R, 2.00 Hz"); // 1/4 at 120 BPM where nothing plays it

    p.set(A::Mode, 1.0f);
    CHECK(movement.getDescription() == "dips to -6.0 dB, 2.00 Hz");
    p.set(A::Vintage, 1.0f);
    CHECK(movement.getDescription() == "dips to -12.0 dB, 2.00 Hz"); // squared
    p.set(A::Amount, 100.0f);
    CHECK(movement.getDescription() == "dips to silence, 2.00 Hz");
}

TEST_CASE("auto-pan-tremolo panel: what a mode doesn't use dims", "[ui]")
{
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("mode " << mode);
        Panel p(mode);
        for (const char* name : {"Offset", "Harmonic", "Vintage"})
        {
            INFO(name);
            CHECK((p.named(name).getAlpha() < 0.5f) == (mode == 0.0f));
        }
        CHECK(p.named("Amount").getAlpha() == 1.0f);
        CHECK(p.named("Phase").getAlpha() == 1.0f); // synced
        p.set(A::Sync, 0.0f);
    }
}
