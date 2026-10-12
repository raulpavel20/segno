// The Reverb's panel (devices/reverb/SPEC.md, "UI layout"): the decay display sets the band
// under the pointer and moves the crossovers, and reads the decay and the size; Early dims in
// Plate.
#include "Reverb.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using R = fd::devices::Reverb;

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
    explicit Panel(float mode = 1.0f)
    {
        binding.setPlain(R::Mode, mode);
        const fd::ui::PanelFactory* panelFactory = nullptr;
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "reverb")
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
    juce::Component& display() const { return *panel->findSection("reverb")->placements().front().component; }
    juce::Component& named(const juce::String& name) const
    {
        auto* c = find<juce::Component>(*panel, [&](auto& k) { return k.getName() == name; });
        REQUIRE(c != nullptr);
        return *c;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<fd::Device> device = fd::findDevice("reverb")->create();
    fd::ui::LocalParamBinding binding{*device};
    fd::ui::DeviceUiContext ctx{fd::findDevice("reverb")->info(), binding, &device->analysis(), device.get(),
                                nullptr};
    std::unique_ptr<fd::ui::DevicePanel> panel;
};

/// Where the display draws `hz`: its plot is the content less the time scale's gutter at the left.
float xOf(juce::Component& display, double hz)
{
    const auto& t = fd::ui::Theme::current();
    const auto gutter =
        juce::GlyphArrangement::getStringWidth(fd::ui::font(fd::ui::TextStyle::ValueSmall), "10 s") +
        static_cast<float>(t.deviceGrid.displayInset);
    const auto area = display.getLocalBounds().toFloat().reduced(5.0f, 4.0f).withTrimmedLeft(gutter);
    const fd::ui::SpectrumPlot axis{.area = area};
    return axis.xOf(hz);
}
} // namespace

TEST_CASE("reverb panel: dragging the decay sets the band under the pointer", "[ui]")
{
    Panel p;
    auto& decay = p.display();
    const auto travel = fd::ui::Theme::current().input.valueTravel;
    const float y = decay.getLocalBounds().toFloat().getCentreY();

    SECTION("the middle: Decay")
    {
        const float x = xOf(decay, 1000.0);
        drag(decay, {x, y}, {x, y - travel * 0.2f});
        CHECK(p.get(R::Decay) > 2.5f);
        CHECK(p.get(R::LowMult) == 120.0f);
    }
    SECTION("the lows: Low Decay")
    {
        const float x = xOf(decay, 60.0);
        drag(decay, {x, y}, {x, y + travel * 0.2f});
        CHECK(p.get(R::LowMult) < 120.0f);
        CHECK(p.get(R::Decay) == 2.5f);
    }
    SECTION("a crossover: sideways moves it")
    {
        const float x = xOf(decay, 4000.0);
        drag(decay, {x, y}, {xOf(decay, 8000.0), y});
        CHECK(p.get(R::HighFreq) > 6000.0f);
        CHECK(p.get(R::HighMult) == 60.0f);
    }
}

TEST_CASE("reverb panel: the decay display reads the decay and the size", "[ui]")
{
    Panel p;
    CHECK(p.display().getDescription() == "2.50 s, 30 m"); // a hall at half size: 15 to 60 m
    p.set(R::Mode, 2.0f);
    CHECK(p.display().getDescription() == "2.50 s"); // a plate has no metres
    p.set(R::Freeze, 1.0f);
    CHECK(p.display().getDescription() == "frozen");
}

TEST_CASE("reverb panel: Early dims in Plate", "[ui]")
{
    for (const float mode : {0.0f, 1.0f, 2.0f, 3.0f})
    {
        INFO("mode " << mode);
        Panel p(mode);
        CHECK((p.named("Early").getAlpha() < 0.5f) == (mode == 2.0f));
    }
}
