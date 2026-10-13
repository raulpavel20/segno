// The Wavetable Synth's panel (devices/wavetable/SPEC.md, "UI layout"): the sections follow the
// selected oscillator, filter, envelope and LFO; a parameter shown turns to its page; a source
// dragged onto a knob makes a route the matrix lists; the displays edit through the host binding;
// drawn for review (FD_DUMP_DIR).
#include "WavetableUi.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

using W = fd::devices::WavetableSynth;
using namespace fd::devices::wavetable_ui;

namespace
{
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

void refresh(juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (auto* control = dynamic_cast<fd::ui::ParamControl*>(c))
            control->refresh();
        refresh(*c);
    }
}

template <typename T, typename Match = bool (*)(T&)>
T* find(juce::Component& root, Match match = [](T&) { return true; })
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(c); t != nullptr && t->isVisible() && match(*t))
            return t;
        if (auto* deeper = find<T>(*c, match))
            return deeper;
    }
    return nullptr;
}

fd::ui::ParamKnob* knobOf(juce::Component& root, size_t param)
{
    return find<fd::ui::ParamKnob>(root, [param](fd::ui::ParamKnob& k) { return k.param() == param; });
}

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged)
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

/// Presses at `from` and drags to `to` on `c` (its own coordinates).
void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown(mouse(c, from, from, false));
    c.mouseDrag(mouse(c, to, from, true));
    c.mouseUp(mouse(c, to, from, true));
}

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<fd::ui::DevicePanel> panel;

    Fixture()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        const auto* factory = fd::findDevice("wavetable");
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, nullptr});
        panel = fd::ui::findPanel("wavetable")->create(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setVisible(true);
        layout();
    }
    ~Fixture() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }

    void layout() const
    {
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        refresh(*panel);
    }
    [[nodiscard]] W& synth() const { return dynamic_cast<W&>(*device); }
    [[nodiscard]] float get(size_t p) const { return binding->getPlain(p); }
    void shot(const juce::String& name) const
    {
        refresh(*panel);
        dump(name + "@2x.png", panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    }
};
} // namespace

TEST_CASE("wavetable panel: three pages at one width, about 1 200 px", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    REQUIRE(panel.pageCount() == 3);
    CHECK(panel.pageId(0) == "oscillators");
    CHECK(panel.pageId(1) == "filter");
    CHECK(panel.pageId(2) == "modulation");
    const int width = panel.preferredWidth();
    CHECK(width > 1100);
    CHECK(width < 1300);
    for (int page = 0; page < 3; ++page)
    {
        panel.setPage(page);
        f.layout();
        CHECK(panel.preferredWidth() == width);
    }
}

TEST_CASE("wavetable panel: the sections show the selected oscillator, sub or noise", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    CHECK(knobOf(panel, W::osc(0, W::OscPos)) != nullptr);
    auto* selector = find<fd::ui::Selector>(panel);
    REQUIRE(selector != nullptr);
    selector->setSelected(1, juce::sendNotification);
    f.layout();
    CHECK(knobOf(panel, W::osc(0, W::OscPos)) == nullptr);
    CHECK(knobOf(panel, W::osc(1, W::OscPos)) != nullptr);
    CHECK(panel.findSection("osc")->sectionTitle() == "Osc 2");
    selector->setSelected(3, juce::sendNotification);
    f.layout();
    CHECK(panel.findSection("osc")->sectionTitle() == "Sub");
    CHECK(knobOf(panel, W::SubOct) != nullptr);
    f.shot("wavetable-sub");
    selector->setSelected(4, juce::sendNotification);
    f.layout();
    CHECK(knobOf(panel, W::NoiseColor) != nullptr);
    f.shot("wavetable-noise");
}

TEST_CASE("wavetable panel: showing a parameter selects what holds it and turns to its page", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    CHECK(panel.showParameter(W::filter(1, W::FilterRes)));
    f.layout();
    CHECK(panel.page() == 1);
    CHECK(panel.findSection("filter")->sectionTitle() == "Filter 2");
    CHECK(knobOf(panel, W::filter(1, W::FilterRes)) != nullptr);

    CHECK(panel.showParameter(W::lfo(2, W::LfoRate)));
    f.layout();
    CHECK(panel.page() == 2);
    CHECK(panel.findSection("lfo")->sectionTitle() == "LFO 3");

    CHECK(panel.showParameter(W::osc(2, W::OscDetune)));
    f.layout();
    CHECK(panel.page() == 0);
    CHECK(knobOf(panel, W::osc(2, W::OscDetune)) != nullptr);
}

TEST_CASE("wavetable panel: an LFO dragged onto a knob on another page makes a route the matrix lists",
          "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    panel.setPage(2);
    f.layout();
    auto* shape = find<ShapeEditor>(panel);
    REQUIRE(shape != nullptr);
    auto& handle = shape->handle();
    // Held on the Oscillators tab until it turns, then onto Osc 1's Position.
    const auto* tabs = panel.pageTabs();
    const auto onTab = tabs->getPosition() + tabs->tabAreas()[0].getCentre();
    panel.dragSource(handle.source(), onTab);
    juce::Thread::sleep(static_cast<int>(fd::ui::Theme::current().motion.springLoad * 1000.0) + 50);
    panel.dragSource(handle.source(), onTab);
    REQUIRE(panel.page() == 0);
    f.layout();
    auto* position = knobOf(panel, W::osc(0, W::OscPos));
    REQUIRE(position != nullptr);
    panel.dropSource(handle.source(), panel.getLocalPoint(position, position->getLocalBounds().getCentre()),
                     false);
    CHECK(static_cast<int>(f.get(W::route(0, W::RouteSource))) == static_cast<int>(W::Source::Lfo1));
    CHECK(static_cast<int>(f.get(W::route(0, W::RouteTarget))) == W::oscTarget(0, W::TPosition));
    CHECK(f.get(W::route(0, W::RouteAmount)) == 50.0f);
    position->refresh();
    REQUIRE(position->ring().has_value());
    // An LFO swings both ways, +-50 % around Position's 0 %: clamped below.
    CHECK(position->ring()->from == 0.0f);
    CHECK(position->ring()->to == 0.5f);

    panel.setPage(2);
    f.layout();
    auto* matrix = find<MatrixList>(panel);
    REQUIRE(matrix != nullptr);
    matrix->refresh();
    CHECK(matrix->rows() == std::vector<int>{0});

    // Removed from a knob's menu (through the model the menu uses).
    f.context->modulation->removeRoute(0);
    matrix->refresh();
    CHECK(matrix->rows().empty());
}

TEST_CASE("wavetable panel: the matrix adds routes and scrolls past five", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    panel.setPage(2);
    f.layout();
    auto* matrix = find<MatrixList>(panel);
    REQUIRE(matrix != nullptr);
    for (int i = 0; i < 7; ++i)
        matrix->addButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // clicks are delivered async
    matrix->refresh();
    REQUIRE(matrix->rows().size() == 7);
    for (int n = 0; n < 7; ++n)
        f.binding->setPlain(W::route(n, W::RouteTarget),
                            static_cast<float>(W::oscTarget(n % 3, W::TPosition) + n / 3));
    f.shot("wavetable-matrix");
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -1.0f;
    matrix->mouseWheelMove(mouse(*matrix, {10.0f, 10.0f}, {10.0f, 10.0f}, false), wheel);
    matrix->mouseWheelMove(mouse(*matrix, {10.0f, 10.0f}, {10.0f, 10.0f}, false), wheel);
    matrix->mouseWheelMove(mouse(*matrix, {10.0f, 10.0f}, {10.0f, 10.0f}, false), wheel);
    CHECK(matrix->firstShown() == 2); // the last five
    CHECK(panel.findSection("matrix")->sectionTitle() == juce::String::fromUTF8("Matrix: 3\u20137 of 7"));
}

TEST_CASE("wavetable panel: the displays edit through the host", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;

    // The table: dragged up, Position rises; the picker steps through the factory tables.
    auto* table = find<TableDisplay>(panel);
    REQUIRE(table != nullptr);
    const auto mid = table->getLocalBounds().getCentre().toFloat();
    drag(*table, mid, mid.translated(0.0f, -40.0f));
    CHECK(f.get(W::osc(0, W::OscPos)) > 10.0f);
    auto* picker = find<TablePicker>(panel);
    REQUIRE(picker != nullptr);
    const auto before = f.synth().tableId(0);
    picker->step(+1);
    CHECK(f.synth().tableId(0) != before);
    picker->step(-1);
    CHECK(f.synth().tableId(0) == before);

    // The filter: dragged right, the cutoff rises.
    panel.setPage(1);
    f.layout();
    auto* response = find<FilterResponse>(panel);
    REQUIRE(response != nullptr);
    const float cutoff = f.get(W::filter(0, W::FilterCutoff));
    const auto c = response->getLocalBounds().getCentre().toFloat();
    drag(*response, c, c.translated(30.0f, 0.0f));
    CHECK(f.get(W::filter(0, W::FilterCutoff)) > cutoff * 1.5f);

    // The envelope: its attack's peak dragged right lengthens the attack.
    auto* envelope = find<EnvelopeEditor>(panel);
    REQUIRE(envelope != nullptr);
    const float attack = f.get(W::env(0, W::EnvAttack));
    juce::Point<float> peak;
    for (float x = 0.0f; x < static_cast<float>(envelope->getWidth()) && peak.isOrigin(); x += 1.0f)
        for (float y = 0.0f; y < static_cast<float>(envelope->getHeight()); y += 1.0f)
            if (envelope->partAt({x, y}) == 0)
            {
                peak = {x, y};
                break;
            }
    REQUIRE_FALSE(peak.isOrigin());
    drag(*envelope, peak, peak.translated(60.0f, 0.0f));
    CHECK(f.get(W::env(0, W::EnvAttack)) >= attack * 2.0f); // doubled for each bend travel dragged

    // An LFO: a double-click adds a point to its shape.
    panel.setPage(2);
    f.layout();
    auto* shape = find<ShapeEditor>(panel);
    REQUIRE(shape != nullptr);
    CHECK(f.synth().lfoShape(0).points.empty()); // a named shape
    const auto plot = shape->plot();
    shape->mouseDoubleClick(mouse(*shape, {plot.getX() + plot.getWidth() * 0.6f, plot.getCentreY()},
                                  {plot.getX() + plot.getWidth() * 0.6f, plot.getCentreY()}, false));
    CHECK(f.synth().lfoShape(0).points.size() == 6); // the sine's five, and the new one
}

TEST_CASE("wavetable panel: drawn for review", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    // Osc 1: a table and warps; a route moving its position.
    f.binding->applyExtraState(W::factoryTablePatch(0, "formant/vowels"));
    f.binding->setPlain(W::osc(0, W::OscPos), 35.0f);
    f.binding->setPlain(W::osc(0, W::OscWarp1), static_cast<float>(fd::dsp::Warp::BendPlus));
    f.binding->setPlain(W::osc(0, W::OscWarp1Amt), 40.0f);
    f.binding->setPlain(W::osc(0, W::OscWarp2), static_cast<float>(fd::dsp::Warp::Remap));
    f.binding->setPlain(W::osc(0, W::OscWarp2Amt), 100.0f);
    f.binding->applyExtraState(
        {{"osc1.remap2.points", {{0.0f, 0.0f, 0.0f}, {0.4f, 0.8f, 0.3f}, {1.0f, 1.0f, 0.0f}}}});
    f.binding->setPlain(W::osc(0, W::OscUni), 7.0f);
    f.binding->setPlain(W::route(0, W::RouteSource), static_cast<float>(W::Source::Lfo1));
    f.binding->setPlain(W::route(0, W::RouteTarget), static_cast<float>(W::oscTarget(0, W::TPosition)));
    f.binding->setPlain(W::route(0, W::RouteAmount), 30.0f);
    f.binding->setPlain(W::route(1, W::RouteSource), static_cast<float>(W::Source::Macro1));
    f.binding->setPlain(W::route(1, W::RouteTarget), static_cast<float>(W::filterTarget(0, W::TCutoff)));
    f.binding->setPlain(W::route(1, W::RouteAmount), -40.0f);
    f.binding->applyExtraState(
        {{"macro1.name", "Brightness"},
         {"lfo1.points", {{0.0f, 0.0f, 0.5f}, {0.3f, 1.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}}});
    f.layout();
    f.shot("wavetable-osc");
    panel.setPage(1);
    f.layout();
    f.shot("wavetable-filter");
    panel.setPage(2);
    f.layout();
    f.shot("wavetable-modulation");
    // Random 1 selected.
    auto* lfoSelector =
        find<fd::ui::Selector>(panel, [](fd::ui::Selector& s) { return s.buttons().size() == 10; });
    REQUIRE(lfoSelector != nullptr);
    lfoSelector->setSelected(8, juce::sendNotification);
    f.layout();
    f.shot("wavetable-random");
    CHECK(panel.findSection("lfo")->sectionTitle() == "Random 1");
}
