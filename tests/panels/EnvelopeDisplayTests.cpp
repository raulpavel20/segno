// The instruments' envelope displays dragged (devices/subtractive-synth/SPEC.md, "UI layout";
// device-panels.md, "Displays"): the Subtractive's and the Sampler's, through the kit's
// EnvelopeDrag, as the Wavetable's and the Envelope modulator's. Drawn for review with
// FD_DUMP_DIR.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using Catch::Approx;

namespace
{
juce::MouseEvent mouse(juce::Component& c, juce::Point<float> pos, juce::Point<float> down,
                       juce::ModifierKeys mods)
{
    return {juce::Desktop::getInstance().getMainMouseSource(),
            pos,
            mods,
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
            pos != down};
}

void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to, bool shift = false)
{
    const auto mods =
        shift ? juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)
              : juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    c.mouseDown(mouse(c, from, from, mods));
    c.mouseDrag(mouse(c, to, from, mods));
    c.mouseUp(mouse(c, to, from, mods));
}

template <typename T> void collect(juce::Component& root, std::vector<T*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(c))
            out.push_back(t);
        collect(*c, out);
    }
}

/// A module's panel on its own parameters.
struct Panel
{
    fd::ui::LookAndFeel look;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<fd::ui::DevicePanel> panel;

    explicit Panel(std::string_view id)
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&look);
        const auto* factory = fd::findDevice(id);
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, nullptr});
        const auto* panelFactory = fd::ui::findPanel(id);
        REQUIRE(panelFactory != nullptr);
        panel = panelFactory->create(*context);
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    }
    ~Panel() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    [[nodiscard]] size_t param(std::string_view paramId) const
    {
        const auto& params = context->info.params;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == paramId)
                return i;
        FAIL("no parameter " << paramId);
        return 0;
    }
    [[nodiscard]] float get(std::string_view paramId) const { return binding->getPlain(param(paramId)); }
    void set(std::string_view paramId, float v) const { binding->setPlain(param(paramId), v); }

    std::vector<fd::ui::EnvelopeDisplay*> envelopes() const
    {
        std::vector<fd::ui::EnvelopeDisplay*> out;
        collect(*panel, out);
        return out;
    }

    void dump(const juce::String& name, juce::Component& c) const
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            for (const float scale : {1.0f, 2.0f})
            {
                const auto file = juce::File(dir).getChildFile(name + (scale > 1.0f ? "@2x.png" : "@1x.png"));
                file.deleteFile();
                juce::FileOutputStream stream(file);
                const auto area = panel->getLocalArea(&c, c.getLocalBounds()).expanded(8);
                juce::PNGImageFormat().writeImageToStream(panel->createComponentSnapshot(area, true, scale),
                                                          stream);
            }
    }
};

/// Every point's handle sits whole inside its display, clear of the label line and the accessory.
void checkHandlesInside(fd::ui::EnvelopeDisplay& display)
{
    const auto handle = static_cast<float>(fd::ui::Theme::current().deviceGrid.displayHandle);
    for (const auto& p : display.points())
    {
        const auto r = juce::Rectangle<float>(handle, handle).withCentre(p);
        CHECK(display.getLocalBounds().toFloat().contains(r));
        CHECK(r.getY() >= 15.0f); // under the label line (MiniDisplay: 3 px in, 12 px tall)
        if (auto* accessory = display.accessory())
            CHECK_FALSE(r.intersects(accessory->getBounds().toFloat()));
    }
}
} // namespace

TEST_CASE("envelope displays: the Subtractive's points dragged set its times and sustain; Mod's release "
          "only while it doesn't cycle",
          "[ui]")
{
    juce::ScopedJuceInitialiser_GUI init;
    Panel f("subtractive-synth");
    const auto envelopes = f.envelopes();
    REQUIRE(envelopes.size() == 2);
    auto& amp = *envelopes[0];
    auto& modEnvelope = *envelopes[1];
    f.panel->resized();
    CHECK(amp.getTooltip().contains("Drag the points"));

    const auto travel = fd::ui::Theme::current().input.bendTravel;
    auto points = amp.points();
    REQUIRE(points.size() == 3);
    checkHandlesInside(amp);
    CHECK(amp.partAt(points[0]) == fd::ui::EnvelopeDrag::attack);

    // The attack's peak a bend travel right: twice as long.
    const float attack = f.get("amp.a");
    drag(amp, points[0], points[0].translated(travel, 0.0f));
    CHECK(f.get("amp.a") == Approx(std::max(attack, 1.0f) * 2.0f).epsilon(0.01));
    // Shift: a tenth as far.
    const float longer = f.get("amp.a");
    points = amp.points();
    drag(amp, points[0], points[0].translated(travel, 0.0f), true);
    CHECK(f.get("amp.a") == Approx(longer * std::exp2(0.1f)).epsilon(0.01));

    // The decay's end: right lengthens the decay, down lowers the sustain.
    const float decay = f.get("amp.d"), sustain = f.get("amp.s");
    points = amp.points();
    drag(amp, points[1], points[1].translated(travel, 10.0f));
    CHECK(f.get("amp.d") == Approx(std::max(decay, 1.0f) * 2.0f).epsilon(0.01));
    CHECK(f.get("amp.s") < sustain);
    // The release's end: left shortens the release.
    const float release = f.get("amp.r");
    points = amp.points();
    drag(amp, points[2], points[2].translated(-travel, 0.0f));
    CHECK(f.get("amp.r") == Approx(std::max(release, 1.0f) * 0.5f).epsilon(0.01));
    // Nowhere near a point: nothing.
    const auto before = std::vector{f.get("amp.a"), f.get("amp.d"), f.get("amp.s"), f.get("amp.r")};
    CHECK(amp.partAt({1.0f, 1.0f}) == fd::ui::EnvelopeDrag::none);
    drag(amp, {1.0f, 1.0f}, {40.0f, 30.0f});
    CHECK(std::vector{f.get("amp.a"), f.get("amp.d"), f.get("amp.s"), f.get("amp.r")} == before);

    // The Mod envelope cycling: no release to drag.
    REQUIRE(modEnvelope.accessory() != nullptr);
    CHECK(modEnvelope.points().size() == 3);
    f.set("mod.cycle", 1.0f);
    modEnvelope.refresh();
    CHECK(modEnvelope.points().size() == 2);
    checkHandlesInside(modEnvelope);
    f.dump("envelope-display-subtractive", *amp.getParentComponent());
}

TEST_CASE("envelope displays: the Sampler's are dragged as the Subtractive's", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI init;
    Panel f("sampler");
    const auto envelopes = f.envelopes();
    REQUIRE(envelopes.size() == 2);
    for (auto* e : envelopes)
    {
        REQUIRE(e->points().size() == 3);
        checkHandlesInside(*e);
    }
    auto& filter = *envelopes[1];
    const auto points = filter.points();
    const float attack = f.get("fenv.a");
    drag(filter, points[0], points[0].translated(fd::ui::Theme::current().input.bendTravel, 0.0f));
    CHECK(f.get("fenv.a") == Approx(std::max(attack, 1.0f) * 2.0f).epsilon(0.01));
}
