// The shared modulation UI (docs/design/device-panels.md, "Modulation"), on a panel made for the
// test from the Utility's parameters and a model whose routes live in a list: rings, a source's
// handle dragged onto a knob, across pages, the ring dragged to set the amount, a macro's handle.
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <juce_core/juce_core.h>

using fd::ui::ModulationModel;

namespace
{
size_t param(const fd::ui::DeviceUiContext& c, std::string_view id)
{
    for (size_t i = 0; i < c.info.params.size(); ++i)
        if (c.info.params[i].id == id)
            return i;
    FAIL("no parameter " << id);
    return 0;
}

/// Sources 1 (an LFO: both ways) and 2 (an envelope: one way); four slots; Mute can't be moved.
struct ListModel final : ModulationModel
{
    std::vector<Route> list;
    std::optional<size_t> unmovable;
    int begun = 0, ended = 0;

    std::vector<Route> routes() const override { return list; }
    bool canModulate(size_t p) const override { return p != unmovable; }
    juce::String sourceName(int source) const override { return source == 1 ? "LFO 1" : "Env 3"; }
    bool isFull() const override { return list.size() >= 4; }
    bool addRoute(int source, size_t p) override
    {
        if (isFull())
            return false;
        list.push_back({static_cast<int>(list.size()), source, p, 0.5f, source == 1});
        return true;
    }
    void removeRoute(int index) override
    {
        std::erase_if(list, [index](const Route& r) { return r.index == index; });
    }
    void beginAmount(int) override { ++begun; }
    void setAmount(int index, float amount) override
    {
        for (auto& r : list)
            if (r.index == index)
                r.amount = amount;
    }
    void endAmount(int) override { ++ended; }
    std::vector<SourceInfo> sources() const override
    {
        return {{1, "LFO 1", "LFOs"}, {2, "Env 3", "Envelopes"}};
    }
};

/// The item called `text` anywhere in `menu` (submenus too), or null.
const juce::PopupMenu::Item* itemNamed(const juce::PopupMenu& menu, const juce::String& text)
{
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

/// Two pages: Gain and Balance knobs, then Width; an LFO handle in the first section; a macro
/// (the DC switch's knob would be a toggle, so Width's knob stands in on page 2).
class ModPanel final : public fd::ui::DevicePanel
{
public:
    ModPanel(fd::ui::DeviceUiContext& c, ListModel& model) : DevicePanel(c)
    {
        c.modulation = &model;
        addPage("main", "Main");
        auto& level = addSection("level", "Level", 3);
        gain = &level.knob(param(c, "gain"), 0, 0);
        balance = &level.knob(param(c, "balance"), 1, 0);
        handle = &own(std::make_unique<fd::ui::ModulationHandle>(model, 1));
        const int size = fd::ui::Theme::current().deviceGrid.sourceHandle;
        level.place(*handle, 2, 0, 1, 1);
        handle->setSize(size, size);
        macro = &level.knob(param(c, "mute"), 0, 3); // a stand-in source knob
        macro->setModulationSource(2);
        addPage("image", "Image");
        auto& image = addSection("width", "Width", 2);
        width = &image.knob(param(c, "width"), 0, 0);
    }
    fd::ui::ParamKnob *gain = nullptr, *balance = nullptr, *width = nullptr, *macro = nullptr;
    fd::ui::ModulationHandle* handle = nullptr;
};

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ListModel model;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<ModPanel> panel;

    Fixture()
    {
        const auto* factory = fd::findDevice("utility");
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, nullptr});
        model.unmovable = param(*context, "balance");
        panel = std::make_unique<ModPanel>(*context, model);
        panel->onEnabledChange = [](bool) {};
        panel->setVisible(true); // as in a host: what's under the pointer is found among the visible
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    }

    /// A point on `c`, in the panel.
    juce::Point<int> on(juce::Component& c, juce::Point<int> local = {}) const
    {
        return panel->getLocalPoint(&c, local.isOrigin() ? c.getLocalBounds().getCentre() : local);
    }
};

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged,
                       juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
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
            dragged};
}
} // namespace

TEST_CASE("modulation: a ring shows the range the routes move a knob over", "[ui]")
{
    ListModel m;
    m.list = {{0, 2, 5, 0.3f, false}}; // an envelope, one way, up
    auto ring = m.ringFor(5, 0.5f);
    REQUIRE(ring.has_value());
    CHECK(ring->from == 0.5f);
    CHECK(ring->to == 0.8f);
    CHECK_FALSE(ring->dimmed);

    m.list.push_back({1, 1, 5, -0.2f, true}); // an LFO, both ways: its size, either side
    ring = m.ringFor(5, 0.5f);
    CHECK(ring->from == 0.3f);
    CHECK(ring->to == 1.0f); // 1.0, clamped

    m.list = {{0, 2, 5, -0.7f, false}}; // down, past the end: clamped
    ring = m.ringFor(5, 0.5f);
    CHECK(ring->from == 0.0f);
    CHECK(ring->to == 0.5f);
    CHECK_FALSE(m.ringFor(6, 0.5f).has_value()); // no routes, no ring

    // A source hovered: only its rings; a knob it doesn't move is dimmed.
    m.list = {{0, 2, 5, 0.3f, false}, {1, 1, 5, 0.1f, true}, {2, 2, 6, 0.2f, false}};
    m.hoveredSource = 1;
    ring = m.ringFor(5, 0.5f);
    CHECK(ring->from == 0.4f);
    CHECK(ring->to == 0.6f);
    CHECK_FALSE(ring->dimmed);
    CHECK(m.ringFor(6, 0.5f)->dimmed);
}

TEST_CASE("modulation: a source dragged onto a knob makes a route; elsewhere, or cancelled, nothing", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    // Onto Gain: it lights while the source is over it, and the route is made on release.
    panel.dragSource(1, f.on(*panel.gain));
    CHECK(f.model.draggedSource == 1);
    CHECK(f.model.dropParam == panel.gain->param());
    CHECK(panel.gain->isDropTarget());
    panel.dropSource(1, f.on(*panel.gain), false);
    REQUIRE(f.model.list.size() == 1);
    CHECK(f.model.list[0].source == 1);
    CHECK(f.model.list[0].param == panel.gain->param());
    CHECK(f.model.list[0].amount == 0.5f);
    CHECK(f.model.draggedSource == -1);
    CHECK_FALSE(panel.gain->isDropTarget());
    CHECK(panel.gain->ring().has_value());

    // Balance can't take it; the header isn't a knob; Escape cancels over Gain.
    panel.dragSource(1, f.on(*panel.balance));
    CHECK_FALSE(f.model.dropParam.has_value());
    panel.dropSource(1, f.on(*panel.balance), false);
    panel.dropSource(1, {4, 4}, false);
    panel.dragSource(1, f.on(*panel.gain));
    panel.dropSource(1, f.on(*panel.gain), true);
    CHECK(f.model.list.size() == 1);
}

TEST_CASE("modulation: held on a page's tab, a dragged source turns to that page", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    const auto* tabs = panel.pageTabs();
    REQUIRE(tabs != nullptr);
    const auto onTab = tabs->getPosition() + tabs->tabAreas()[1].getCentre();
    panel.dragSource(1, onTab);
    CHECK(panel.page() == 0); // not yet
    juce::Thread::sleep(static_cast<int>(fd::ui::Theme::current().motion.springLoad * 1000.0) + 50);
    panel.dragSource(1, onTab);
    CHECK(panel.page() == 1);
    // On to Width, on the page it turned to.
    panel.dropSource(1, f.on(*panel.width), false);
    REQUIRE(f.model.list.size() == 1);
    CHECK(f.model.list[0].param == panel.width->param());
}

TEST_CASE("modulation: dragging the ring sets the route's amount, as one gesture", "[ui]")
{
    Fixture f;
    auto& knob = *f.panel->gain;
    f.model.list = {{0, 2, knob.param(), 0.2f, false}, {1, 1, knob.param(), 0.1f, true}};
    knob.refresh();
    REQUIRE(knob.ring().has_value());
    // A point on the band outside the arc, at the top of the knob.
    juce::Point<float> onRing;
    for (float y = 0.0f; y < static_cast<float>(knob.getHeight()); y += 0.5f)
        if (const juce::Point<float> at{static_cast<float>(knob.getWidth()) * 0.5f, y}; knob.isOnRing(at))
        {
            onRing = at;
            break;
        }
    REQUIRE(onRing.y > 0.0f);
    CHECK_FALSE(knob.isOnRing(knob.getLocalBounds().getCentre().toFloat())); // the middle turns the knob

    f.model.lastSource = 1; // the LFO, last hovered: its route
    knob.mouseDown(mouse(knob, onRing, onRing, false));
    const auto up = onRing.translated(0.0f, -20.0f);
    knob.mouseDrag(mouse(knob, up, onRing, true));
    knob.mouseUp(mouse(knob, up, onRing, true));
    CHECK(f.model.begun == 1);
    CHECK(f.model.ended == 1);
    const float step = 20.0f / fd::ui::Theme::current().input.valueTravel;
    CHECK(std::abs(f.model.list[1].amount - (0.1f + step)) < 1e-4f);
    CHECK(f.model.list[0].amount == 0.2f);
    CHECK(f.binding->getPlain(knob.param()) ==
          f.context->info.params[knob.param()].def); // the knob didn't move
}

TEST_CASE("modulation: a knob that's a source drags from its name, and its handle lights", "[ui]")
{
    Fixture f;
    auto& macro = *f.panel->macro;
    const auto handle = macro.handleArea();
    REQUIRE_FALSE(handle.isEmpty());
    const auto from = handle.getCentre();
    macro.mouseDown(mouse(macro, from, from, false));
    const auto target = f.panel->getLocalPoint(f.panel->gain, f.panel->gain->getLocalBounds().getCentre());
    const auto there = macro.getLocalPoint(f.panel.get(), target).toFloat();
    macro.mouseDrag(mouse(macro, there, from, true));
    CHECK(f.model.dropParam == f.panel->gain->param());
    macro.mouseUp(mouse(macro, there, from, true));
    REQUIRE(f.model.list.size() == 1);
    CHECK(f.model.list[0].source == 2);
    CHECK(f.binding->getPlain(macro.param()) == f.context->info.params[macro.param()].def); // not turned

    // Hovering Gain lights the handles of what moves it.
    f.model.hoveredParam = f.panel->gain->param();
    f.panel->handle->refresh();
    CHECK(f.model.movesHovered(2));
    CHECK_FALSE(f.model.movesHovered(1));
}

TEST_CASE("modulation: rings, a handle and a macro's handle, drawn for review", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    auto dump = [](const juce::String& name, const juce::Image& image)
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
        {
            const auto file = juce::File(dir).getChildFile(name);
            file.deleteFile();
            juce::FileOutputStream stream(file);
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
    };
    f.model.list = {{0, 1, panel.gain->param(), 0.25f, true}, {1, 2, panel.macro->param(), -0.4f, false}};
    for (auto* k : {panel.gain, panel.balance, panel.macro})
        k->refresh();
    dump("modulation@1x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f));
    dump("modulation@2x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
    // The LFO's handle hovered: its ring at full strength, the envelope's dimmed; then a drag
    // lighting Balance's neighbour as where it would land.
    f.model.hoveredSource = 1;
    for (auto* k : {panel.gain, panel.balance, panel.macro})
        k->refresh();
    panel.handle->refresh();
    dump("modulation-hover@2x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
    f.model.hoveredSource = -1;
    panel.dragSource(1, f.on(*panel.gain));
    dump("modulation-drop@2x.png", panel.createComponentSnapshot(panel.getLocalBounds(), true, 2.0f));
    panel.dropSource(1, {}, true);
    CHECK(f.model.list.size() == 2);
}

TEST_CASE("modulation: while a source is dragged, every knob it can move shows it can", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    panel.dragSource(1, {2, 2}); // over the header: nothing to land on yet
    CHECK(panel.gain->isDropCandidate());
    CHECK_FALSE(panel.balance->isDropCandidate()); // can't be moved
    CHECK_FALSE(panel.gain->isDropTarget());
    panel.dropSource(1, {2, 2}, true);
    CHECK_FALSE(panel.gain->isDropCandidate());
}

TEST_CASE("modulation: a knob's menu offers Modulate with, its sources by group", "[ui]")
{
    Fixture f;
    auto& gain = *f.panel->gain;
    const auto run = [](std::function<void()> action) { return action; };
    juce::PopupMenu menu;
    fd::ui::addModulationItems(f.model, gain.param(), menu, run);
    REQUIRE(itemNamed(menu, "Modulate with") != nullptr);
    CHECK(itemNamed(menu, "LFOs") != nullptr);
    const auto* lfo = itemNamed(menu, "LFO 1");
    REQUIRE(lfo != nullptr);
    lfo->action();
    REQUIRE(f.model.list.size() == 1);
    CHECK(f.model.list[0].source == 1);
    CHECK(f.model.list[0].param == gain.param());

    // Now it lists that route with Remove; Balance (which can't be moved) has none of it.
    juce::PopupMenu again;
    fd::ui::addModulationItems(f.model, gain.param(), again, run);
    CHECK(itemNamed(again, juce::String::fromUTF8("LFO 1 \u2192 50 %")) != nullptr);
    juce::PopupMenu balance;
    fd::ui::addModulationItems(f.model, f.panel->balance->param(), balance, run);
    CHECK(balance.getNumItems() == 0);

    // A full matrix says so.
    for (int i = 0; i < 3; ++i)
        f.model.addRoute(2, f.panel->width->param());
    juce::PopupMenu full;
    fd::ui::addModulationItems(f.model, gain.param(), full, run);
    const auto* note = itemNamed(full, "The matrix is full: remove a route first");
    REQUIRE(note != nullptr);
    CHECK_FALSE(note->isEnabled);
}

TEST_CASE("modulation: a display's handle is a chip with its source's name", "[ui]")
{
    Fixture f;
    auto& handle = *f.panel->handle;
    const auto name =
        juce::GlyphArrangement::getStringWidth(fd::ui::font(fd::ui::TextStyle::Caption), "LFO 1");
    CHECK(static_cast<float>(handle.preferredWidth()) >
          name + static_cast<float>(fd::ui::Theme::current().deviceGrid.sourceHandle));
}
