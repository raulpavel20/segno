// The Delay's panel (devices/delay/SPEC.md, "UI layout"): the time knobs follow Sync, and the
// repeats display and the filter curve edit what they show, through the host binding.
#include "Delay.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using D = fd::devices::Delay;

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

/// Press at `from`, drag to `to` in a few steps, release.
void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown(mouseEvent(c, from, from, false));
    for (float k : {0.25f, 0.5f, 1.0f})
        c.mouseDrag(mouseEvent(c, from + (to - from) * k, from, true));
    c.mouseUp(mouseEvent(c, to, from, true));
}

struct Panel
{
    Panel()
    {
        const fd::ui::PanelFactory* panelFactory = nullptr;
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "delay")
                panelFactory = p;
        REQUIRE(panelFactory != nullptr);
        panel = panelFactory->create(ctx);
        panel->onEnabledChange = [](bool) {};
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        (void)panel->createComponentSnapshot(panel->getLocalBounds()); // the displays lay out as they paint
    }

    float get(size_t p) const { return binding.getPlain(p); }
    void set(size_t p, float v)
    {
        binding.setPlain(p, v);
        refresh(*panel);
    }
    juce::Component& display(const char* section) const
    {
        return *panel->findSection(section)->placements().front().component;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<fd::Device> device = fd::findDevice("delay")->create();
    fd::ui::LocalParamBinding binding{*device};
    fd::ui::DeviceUiContext ctx{fd::findDevice("delay")->info(), binding, &device->analysis(), device.get(),
                                nullptr};
    std::unique_ptr<fd::ui::DevicePanel> panel;
};
} // namespace

TEST_CASE("delay panel: the time knobs turn note values while synced, milliseconds when not", "[ui]")
{
    Panel p;
    auto* left = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.getName() == "Left"; });
    auto* right = find<fd::ui::ParamKnob>(*p.panel, [](auto& k) { return k.getName() == "Right"; });
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left->param() == D::DivLeft);
    CHECK(right->param() == D::DivRight);
    CHECK(left->textFromValue(static_cast<float>(left->getValue())) == "1/8D");

    p.set(D::Sync, 0.0f);
    CHECK(left->param() == D::TimeLeft);
    CHECK(right->param() == D::TimeRight);
    CHECK(left->getName() == "Left"); // the same name either way
    CHECK(left->textFromValue(static_cast<float>(left->getValue())) == "375 ms");
    // Turning it now sets the milliseconds, and leaves the note value alone.
    left->onValueChange(p.ctx.info.params[D::TimeLeft].toNormalized(500.0f));
    CHECK(std::abs(p.get(D::TimeLeft) - 500.0f) < 0.5f);
    CHECK(p.get(D::DivLeft) == 9.0f);

    p.set(D::Sync, 1.0f);
    CHECK(left->param() == D::DivLeft);
}

TEST_CASE("delay panel: dragging the first echo changes the time, snapping while synced", "[ui]")
{
    Panel p;
    auto& repeats = p.display("time");
    const auto area = repeats.getLocalBounds().toFloat();
    // The display spans four seconds here (eight dotted eighths at 120 BPM is less): a quarter of
    // its plot is a second. The left lane is the upper half.
    const auto upper = area.getCentreY() - area.getHeight() * 0.25f;

    SECTION("synced: the nearest note value")
    {
        const float x = area.getX() + area.getWidth() * 0.1f;
        drag(repeats, {x, upper}, {x + area.getWidth() * 0.24f, upper});
        CHECK(p.get(D::DivLeft) == 14.0f); // about 1.4 s: 1/2D (1.5 s) is the nearest
        CHECK(p.get(D::TimeLeft) == 375.0f);
    }

    SECTION("in milliseconds")
    {
        p.set(D::Sync, 0.0f);
        const float x = area.getX() + area.getWidth() * 0.1f;
        drag(repeats, {x, upper}, {x + area.getWidth() * 0.1f, upper});
        CHECK(p.get(D::TimeLeft) > 700.0f); // 375 ms and about two fifths of a second
        CHECK(p.get(D::TimeLeft) < 850.0f);
        CHECK(p.get(D::TimeRight) == 375.0f); // linked: the right side follows the left
    }

    SECTION("with Link off, the lower lane is the right side")
    {
        p.set(D::Sync, 0.0f);
        p.set(D::Link, 0.0f);
        const auto lower = area.getCentreY() + area.getHeight() * 0.25f;
        const float x = area.getX() + area.getWidth() * 0.1f;
        drag(repeats, {x, lower}, {x - area.getWidth() * 0.05f, lower});
        CHECK(p.get(D::TimeRight) < 300.0f);
        CHECK(p.get(D::TimeLeft) == 375.0f);
    }
}

TEST_CASE("delay panel: dragging near a cutoff on the filter curve moves it", "[ui]")
{
    Panel p;
    auto& curve = p.display("filter");
    const auto area = curve.getLocalBounds().toFloat().reduced(6.0f);
    const auto y = area.getCentreY();
    // 20 Hz to 20 kHz: 120 Hz is a quarter of the way, 6 kHz four fifths.
    const float lowX =
        area.getX() + area.getWidth() * static_cast<float>(std::log(120.0 / 20.0) / std::log(1000.0));
    drag(curve, {lowX, y}, {lowX + area.getWidth() / 3.0f, y});
    CHECK(p.get(D::FilterLow) > 800.0f); // a third of the width is a decade
    CHECK(p.get(D::FilterLow) < 1500.0f);
    CHECK(p.get(D::FilterHigh) == 6000.0f);

    const float highX =
        area.getX() + area.getWidth() * static_cast<float>(std::log(6000.0 / 20.0) / std::log(1000.0));
    drag(curve, {highX, y}, {highX - area.getWidth() / 3.0f, y});
    CHECK(p.get(D::FilterHigh) < 1000.0f);
    CHECK(p.get(D::FilterHigh) >= 500.0f); // held at its range
}
