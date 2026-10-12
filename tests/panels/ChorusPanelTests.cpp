// The Chorus's panel (devices/chorus/SPEC.md, "UI layout"): Rate follows Sync, the voices display
// sets Rate and Depth and reads out the pitch swing, and Vibrato dims what it doesn't use.
#include "Chorus.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using C = fd::devices::Chorus;

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
        binding.setPlain(C::Mode, mode);
        const fd::ui::PanelFactory* panelFactory = nullptr;
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "chorus")
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
    juce::Component& display() const
    {
        return *panel->findSection("modulation")->placements().front().component;
    }
    juce::Component& named(const juce::String& name) const
    {
        auto* c = find<juce::Component>(*panel, [&](auto& k) { return k.getName() == name; });
        REQUIRE(c != nullptr);
        return *c;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<fd::Device> device = fd::findDevice("chorus")->create();
    fd::ui::LocalParamBinding binding{*device};
    fd::ui::DeviceUiContext ctx{fd::findDevice("chorus")->info(), binding, &device->analysis(), device.get(),
                                nullptr};
    std::unique_ptr<fd::ui::DevicePanel> panel;
};
} // namespace

TEST_CASE("chorus panel: Rate turns hertz, or note values while synced", "[ui]")
{
    Panel p;
    auto* rate = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.getName() == "Rate"; });
    REQUIRE(rate != nullptr);
    CHECK(rate->param() == C::Rate);
    CHECK(rate->textFromValue(static_cast<float>(rate->getValue())) == "0.80 Hz");

    p.set(C::Sync, 1.0f);
    CHECK(rate->param() == C::Div);
    CHECK(rate->getName() == "Rate");
    CHECK(rate->textFromValue(static_cast<float>(rate->getValue())) == "1/2");
}

TEST_CASE("chorus panel: dragging the voices sets Rate sideways and Depth up and down", "[ui]")
{
    Panel p;
    auto& voices = p.display();
    const auto centre = voices.getLocalBounds().toFloat().getCentre();
    const auto travel = fd::ui::Theme::current().input.valueTravel;

    SECTION("free")
    {
        drag(voices, centre, centre + juce::Point<float>(travel * 0.1f, -travel * 0.2f));
        CHECK(p.get(C::Rate) > 0.8f);
        CHECK(p.get(C::Depth) > 65.0f); // a fifth of the range up from 50 %
        CHECK(p.get(C::Depth) < 75.0f);
    }

    SECTION("synced: sideways steps through the note values, faster to the right")
    {
        p.set(C::Sync, 1.0f);
        drag(voices, centre, centre + juce::Point<float>(travel * 0.25f, 0.0f));
        CHECK(p.get(C::Div) > 5.0f);
        CHECK(p.get(C::Rate) == 0.8f);
        CHECK(p.get(C::Depth) == 50.0f);
    }
}

TEST_CASE("chorus panel: the voices display reads the pitch swing and the delays", "[ui]")
{
    Panel p;
    auto& voices = p.display();
    const C::Voicing v{};
    const auto swing = juce::roundToInt(C::pitchSwing(v, 0.8, 48000.0));
    CHECK(voices.getDescription() == juce::String::fromUTF8("\xC2\xB1") + juce::String(swing) +
                                         " cents, 6.0" + juce::String::fromUTF8("\xE2\x80\x93") + "8.0 ms");

    p.set(C::Depth, 100.0f);
    CHECK(voices.getDescription().endsWith(juce::String::fromUTF8("6.0\xE2\x80\x93") + "14.0 ms"));
}

TEST_CASE("chorus panel: Vibrato dims what it doesn't use", "[ui]")
{
    for (const float mode : {0.0f, 2.0f})
    {
        INFO("mode " << mode);
        Panel p(mode);
        const bool dimmed = mode == 2.0f;
        for (const char* name : {"Delay", "Width", "Feedback", "Invert", "Mix"})
        {
            INFO(name);
            CHECK((p.named(name).getAlpha() < 0.5f) == dimmed); // stored in eight bits
        }
        CHECK(p.named("Warmth").getAlpha() == 1.0f);
        CHECK(p.named("Depth").getAlpha() == 1.0f);
    }
}
