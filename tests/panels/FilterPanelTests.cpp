// The Filter's panel (devices/filter/SPEC.md, "UI layout"): the main knob follows the type, the
// display moves the main control and the resonance and reads where the filter is, and the
// controls a type doesn't use dim.
#include "Filter.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using F = fd::devices::Filter;

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
    explicit Panel(float type = 0.0f)
    {
        binding.setPlain(F::Type, type);
        const fd::ui::PanelFactory* panelFactory = nullptr;
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "filter")
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
    juce::Component& display() const { return placed(0); }
    /// The FILTER section's controls in the order they're placed: the display, Freq, Res, Morph,
    /// Drive, Type, Circuit, Slope.
    juce::Component& placed(size_t index) const
    {
        return *panel->findSection("filter")->placements()[index].component;
    }
    juce::Component& named(const juce::String& name) const
    {
        auto* c = find<juce::Component>(*panel, [&](auto& k) { return k.getName() == name; });
        REQUIRE(c != nullptr);
        return *c;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<fd::Device> device = fd::findDevice("filter")->create();
    fd::ui::LocalParamBinding binding{*device};
    fd::ui::DeviceUiContext ctx{fd::findDevice("filter")->info(), binding, &device->analysis(), device.get(),
                                nullptr};
    std::unique_ptr<fd::ui::DevicePanel> panel;
};
} // namespace

TEST_CASE("filter panel: the main knob follows the type", "[ui]")
{
    Panel p;
    auto* main = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.getName() == "Freq"; });
    REQUIRE(main != nullptr);
    CHECK(main->param() == F::Freq);
    p.set(F::Type, 5.0f); // DJ
    CHECK(main->param() == F::Sweep);
    CHECK(main->getName() == "Sweep");
    p.set(F::Type, 7.0f); // Vowel
    CHECK(main->param() == F::Vowel);
    p.set(F::Type, 6.0f); // Comb: its pitch is Freq
    CHECK(main->param() == F::Freq);
}

TEST_CASE("filter panel: dragging the display moves the filter and its resonance", "[ui]")
{
    const auto travel = fd::ui::Theme::current().input.valueTravel;
    SECTION("right raises Freq, up raises Resonance")
    {
        Panel p;
        auto& d = p.display();
        const auto c = d.getLocalBounds().toFloat().getCentre();
        drag(d, c, {c.x + travel * 0.2f, c.y});
        CHECK(p.get(F::Freq) > 2000.0f);
        CHECK(p.get(F::Res) == 25.0f);
        drag(d, c, {c.x, c.y - travel * 0.2f});
        CHECK(p.get(F::Res) > 25.0f);
    }
    SECTION("the DJ filter: sideways moves Sweep")
    {
        Panel p(5.0f);
        auto& d = p.display();
        const auto c = d.getLocalBounds().toFloat().getCentre();
        drag(d, c, {c.x - travel * 0.2f, c.y});
        CHECK(p.get(F::Sweep) < 0.0f);
        CHECK(p.get(F::Freq) == 2000.0f);
    }
}

TEST_CASE("filter panel: the display reads where the filter is", "[ui]")
{
    Panel p;
    CHECK(p.display().getDescription() == "2.00 kHz");
    p.set(F::Freq, 440.0f);
    CHECK(p.display().getDescription() == "440 Hz");
    // The LFO at 50 %: a quarter of the scale either way, about two and a half octaves.
    p.set(F::Lfo, 1.0f);
    CHECK(p.display().getDescription() == juce::String::fromUTF8("78 Hz\xE2\x80\x93") + "2.5 kHz");
    p.set(F::Lfo, 0.0f);
    p.set(F::Type, 5.0f);
    CHECK(p.display().getDescription() == "flat");
    p.set(F::Sweep, 100.0f);
    CHECK(p.display().getDescription() == "high pass 10.00 kHz");
    p.set(F::Type, 7.0f);
    CHECK(p.display().getDescription() == "a");
    p.set(F::Vowel, 37.5f);
    CHECK(p.display().getDescription() == juce::String::fromUTF8("e\xE2\x80\x93i"));
}

TEST_CASE("filter panel: what a type doesn't use dims", "[ui]")
{
    for (int type = 0; type < F::numTypes; ++type)
    {
        INFO("type " << type);
        Panel p(static_cast<float>(type));
        const auto t = static_cast<F::TypeKind>(type);
        using TK = F::TypeKind;
        const bool usesMorph = t == TK::Morph || t == TK::Comb || t == TK::Vowel || t == TK::NotchLowPass;
        const bool ownSound = t == TK::Comb || t == TK::Vowel || t == TK::Resampling;
        CHECK((p.named("Res").getAlpha() < 0.5f) == (t == TK::Resampling));
        CHECK((p.named("Morph").getAlpha() < 0.5f) == !usesMorph);
        CHECK((p.placed(6).getAlpha() < 0.5f) == ownSound); // Circuit
        CHECK((p.placed(7).getAlpha() < 0.5f) == ownSound); // Slope
        CHECK(p.named("Drive").getAlpha() == 1.0f);
    }
}
