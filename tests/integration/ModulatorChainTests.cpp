// Modulators in the chain view (docs/design/modulators.md §6, §7, §15; step 1): their panels among
// the modules, a handle dragged onto a knob making a route (the chain scrolling at its edges), the
// lines from a hovered handle, handles lit by the control under the pointer, the route list,
// Modulate with ▸ on a control's right-click, a ring's drag setting the route, and the chain's
// places counting modulators as modules move, group into racks and come out. Drawn with
// FD_DUMP_DIR for review.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/Session.h"
#include "model/Modulators.h"
#include "ui/DeviceChain.h"
#include "ui/ModulatorPanels.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd_device_ui/fd_device_ui.h>
#include <tracktion_engine/tracktion_engine.h>

using Catch::Approx;
using fd::engine::Session;
using Kind = fd::model::ModulatorKind;
namespace model = fd::model;

namespace
{
void dump(const juce::String& name, juce::Component& c)
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        for (const float scale : {1.0f, 2.0f})
        {
            const auto file = juce::File(dir).getChildFile(name + (scale > 1.0f ? "@2x.png" : "@1x.png"));
            file.deleteFile();
            juce::FileOutputStream stream(file);
            juce::PNGImageFormat().writeImageToStream(
                c.createComponentSnapshot(c.getLocalBounds(), true, scale), stream);
        }
}

struct SegnoLook
{
    fd::ui::LookAndFeel lnf;
    SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(&lnf); }
    ~SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    SegnoLook(const SegnoLook&) = delete;
    SegnoLook& operator=(const SegnoLook&) = delete;
};

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged,
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

template <typename T> void collect(juce::Component& root, std::vector<T*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(c))
            out.push_back(t);
        collect(*c, out);
    }
}

/// The chain view on a session, in a window-sized host (the route list opens over it).
struct Chain
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    tracktion::Engine engine{"fd-modulator-chain-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<Session> session;
    fd::app::ActionRegistry actions;
    juce::Component window;
    std::unique_ptr<fd::app::DeviceChain> chain;
    std::uint64_t t = 0;

    Chain()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<Session>(engine);
        session->updateSnapshotNow();
        t = session->snapshot()->tracks[0].id;
        actions.add({.id = "app.scanPlugins", .name = "Scan", .perform = [] {}});
        actions.add({.id = "module.ungroupRack", .name = "Ungroup rack", .perform = [] {}});
        chain = std::make_unique<fd::app::DeviceChain>(*session, actions);
        window.setSize(1600, 800);
        window.addAndMakeVisible(*chain);
        chain->setBounds(0, window.getHeight() - fd::app::DeviceChain::preferredHeight(), window.getWidth(),
                         fd::app::DeviceChain::preferredHeight());
    }

    ~Chain()
    {
        chain.reset();
        session.reset();
    }

    void show()
    {
        session->updateSnapshotNow();
        chain->setTrack(*session->snapshot()->channel(t));
    }

    std::vector<std::uint64_t> order()
    {
        session->updateSnapshotNow();
        std::vector<std::uint64_t> out;
        for (const auto& d : session->snapshot()->channel(t)->devices)
            out.push_back(d.id);
        return out;
    }

    const model::ModulatorInfo& info(std::uint64_t modulator)
    {
        session->updateSnapshotNow();
        for (const auto& d : session->snapshot()->channel(t)->devices)
            if (d.id == modulator && d.modulator != nullptr)
            {
                held = d.modulator;
                return *held;
            }
        FAIL("no modulator " << modulator);
        return *held;
    }

    /// A module's panel in the chain: the chain shows the snapshot's order, left to right.
    fd::ui::DevicePanel* panelOf(std::uint64_t module)
    {
        std::vector<fd::ui::DevicePanel*> panels;
        collect(*chain, panels);
        std::sort(panels.begin(), panels.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
        const auto ids = order();
        for (size_t i = 0; i < panels.size() && i < ids.size(); ++i)
            if (ids[i] == module)
                return panels[i];
        return nullptr;
    }

    fd::ui::ParamKnob* knob(std::uint64_t module, std::string_view param)
    {
        auto* panel = panelOf(module);
        REQUIRE(panel != nullptr);
        auto handle = session->openDevice(module);
        REQUIRE(handle != nullptr);
        const auto& params = handle->context().info.params;
        std::vector<fd::ui::ParamKnob*> knobs;
        collect(*panel, knobs);
        const auto shown = [&](juce::Component* c)
        {
            for (; c != nullptr && c != panel; c = c->getParentComponent())
                if (!c->isVisible())
                    return false;
            return true;
        };
        for (auto* k : knobs)
            if (shown(k) && params[k->param()].id == param)
                return k;
        FAIL("no knob " << param);
        return nullptr;
    }

    Session::ParamTarget target(std::uint64_t module, const std::string& param) const
    {
        return {t, "m" + std::to_string(module) + "/" + param};
    }

    void refreshAll()
    {
        std::vector<juce::Component*> all;
        collect(*chain, all);
        for (auto* c : all)
            if (auto* pc = dynamic_cast<fd::ui::ParamControl*>(c))
                pc->refresh();
    }

    void shot(const juce::String& name) { dump("modulator-chain-" + name, chain->content()); }

    std::shared_ptr<const model::ModulatorInfo> held;
};
} // namespace

TEST_CASE(
    "modulators in the chain view: a panel among the modules; its handle dragged onto a knob routes it; "
    "lines on hover; lit handles; the route list",
    "[integration][modulators][ui]")
{
    Chain f;
    auto& s = *f.session;
    const auto filter = s.addDevice(f.t, "fd:filter");
    const auto chorus = s.addDevice(f.t, "fd:chorus");
    const auto lfo = s.addModulator(f.t, Kind::Lfo, 0); // before the Filter
    REQUIRE(lfo != 0);
    f.show();

    // Its panel, among the modules in the chain's order, apart by the chain's gap.
    auto* panel = f.chain->modulatorPanel(lfo);
    REQUIRE(panel != nullptr);
    CHECK(panel->kind() == Kind::Lfo);
    auto* filterPanel = f.panelOf(filter);
    REQUIRE(filterPanel != nullptr);
    CHECK(panel->getRight() + fd::app::DeviceChain::gap <= filterPanel->getX());
    CHECK(panel->getWidth() == panel->preferredWidth());
    f.shot("among");

    // Folded and unfolded, kept with the project as a module's view is.
    panel->setFolded(true, true);
    CHECK(s.deviceViewState(lfo).find("folded") != std::string::npos);
    CHECK(panel->getWidth() < filterPanel->getWidth());
    panel->setFolded(false, true);
    CHECK(s.deviceViewState(lfo).empty());

    // The handle dragged onto the Filter's Freq: the knob takes it, then a route at 50 %.
    auto* freq = f.knob(filter, "freq");
    const auto centre = freq->getLocalBounds().getCentre().toFloat();
    REQUIRE(panel->onHandleDrag);
    panel->onHandleDrag(mouseAt(*freq, centre, centre, true));
    CHECK(s.modulatorPointer().dragging);
    CHECK(s.modulatorPointer().modulator == lfo);
    REQUIRE(s.modulatorPointer().over);
    CHECK(*s.modulatorPointer().over == f.target(filter, "freq"));
    CHECK(panel->handle().isLit());
    f.refreshAll(); // as the panels' refresh tick does
    CHECK(freq->isDropTarget());
    f.chain->content().repaint();
    f.shot("drag");
    panel->onHandleRelease(mouseAt(*freq, centre, centre, true), false);
    CHECK_FALSE(s.modulatorPointer().dragging);
    {
        const auto& info = f.info(lfo);
        REQUIRE(info.routes.size() == 1);
        CHECK(info.routes[0].key == f.target(filter, "freq").key);
        CHECK(info.routes[0].amount == Approx(0.5f));
    }
    CHECK(s.undoDescription() == "Route LFO");
    f.show();
    f.refreshAll();
    CHECK(freq->ring().has_value());
    CHECK(panel->handle().getTooltip().contains("moves 1 control"));

    // Dropped on its own knob, or nowhere: nothing.
    {
        std::vector<fd::ui::ParamKnob*> own;
        collect(*panel, own);
        REQUIRE_FALSE(own.empty());
        const auto c = own.front()->getLocalBounds().getCentre().toFloat();
        panel->onHandleDrag(mouseAt(*own.front(), c, c, true));
        CHECK_FALSE(s.modulatorPointer().over.has_value());
        panel->onHandleRelease(mouseAt(*own.front(), c, c, true), false);
        CHECK(f.info(lfo).routes.size() == 1);
    }
    // Escape cancels a drag: no route to the Chorus's Mix.
    auto* mix = f.knob(chorus, "mix");
    {
        const auto c = mix->getLocalBounds().getCentre().toFloat();
        panel->onHandleDrag(mouseAt(*mix, c, c, true));
        CHECK(s.modulatorPointer().over.has_value());
        panel->onHandleCancel();
        CHECK_FALSE(s.modulatorPointer().dragging);
        CHECK(f.info(lfo).routes.size() == 1);
    }

    // Hovering its handle: a line from the handle's foot to the knob it moves; the knob's ring is
    // the one shown, the others' dimmed.
    f.show();
    auto& handle = panel->handle();
    f.chain->mouseMove(mouseAt(handle, {4.0f, 4.0f}, {4.0f, 4.0f}, false));
    CHECK(s.modulatorPointer().modulator == lfo);
    const auto cables = f.chain->cables();
    REQUIRE(cables.size() == 1);
    const auto knobCentre =
        f.chain->content().getLocalArea(freq, freq->getLocalBounds()).toFloat().getCentre();
    CHECK(cables[0].to.getDistanceFrom(knobCentre) < 0.5f);
    const auto chip = f.chain->content().getLocalArea(&handle, handle.getLocalBounds()).toFloat();
    CHECK(cables[0].from.y == Approx(chip.getBottom()));
    f.chain->content().repaint();
    f.shot("hover");
    f.chain->mouseExit(mouseAt(handle, {4.0f, 4.0f}, {4.0f, 4.0f}, false));
    CHECK(f.chain->cables().empty());
    CHECK(s.modulatorPointer().modulator == 0);

    // Hovering the knob it moves lights its handle; the Chorus's Mix doesn't.
    f.chain->mouseMove(mouseAt(*freq, centre, centre, false));
    CHECK(panel->handle().isLit());
    f.chain->mouseMove(mouseAt(*mix, {4.0f, 4.0f}, {4.0f, 4.0f}, false));
    CHECK_FALSE(panel->handle().isLit());

    // The route list: its route, the amount set, Show, Remove.
    s.routeModulator(lfo, f.target(chorus, "mix"), -0.25f);
    f.show();
    f.chain->showRoutes(lfo);
    auto* list = f.chain->routeList();
    REQUIRE(list != nullptr);
    CHECK(list->getParentComponent() == &f.window);
    CHECK(f.window.getLocalBounds().contains(
        list->getBounds().reduced(juce::roundToInt(fd::ui::Theme::current().shell.shadowRadius))));
    REQUIRE(list->rows().size() == 2);
    // Above the modulator's panel (the chain is the window's foot), the panel left in view.
    CHECK(list->inner().translated(list->getX(), list->getY()).getBottom() +
              fd::ui::Theme::current().shell.itemGap <=
          f.window.getLocalArea(panel, panel->getLocalBounds()).getY());
    // Each target as its control names it.
    CHECK(list->rows().size() == 2);
    dump("modulator-chain-routes", f.window);
    list->onAmountGesture(true);
    list->onAmount(0, 1.0f);
    list->onAmount(0, 1.25f);
    list->onAmountGesture(false);
    CHECK(f.info(lfo).routes[0].amount == Approx(1.25f));
    s.undo(); // the drag, one step
    CHECK(f.info(lfo).routes[0].amount == Approx(0.5f));
    list->rows()[1].remove->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(f.info(lfo).routes.size() == 1);
    list = f.chain->routeList();
    REQUIRE(list != nullptr); // open again, its rows the routes left
    CHECK(list->rows().size() == 1);
    // Show: the route's module selected, the list closed.
    f.chain->selectDevice(lfo);
    list->onShow(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(f.chain->selectedDevice() == filter);
    CHECK(f.chain->routeList() == nullptr);
    // Escape closes it.
    f.chain->showRoutes(lfo);
    list = f.chain->routeList();
    REQUIRE(list != nullptr);
    CHECK(list->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK(f.chain->routeList() == nullptr);

    // A drag resting at the chain's right edge scrolls it (§6), the chip still at the pointer.
    f.chain->setSize(600, f.chain->getHeight());
    auto& content = f.chain->content();
    REQUIRE(content.getX() == 0);
    const juce::Point<float> edge(static_cast<float>(f.chain->getWidth() - 4), 40.0f);
    for (int i = 0; i < 4; ++i)
        panel->onHandleDrag(mouseAt(content, edge - content.getPosition().toFloat(), edge, true));
    CHECK(content.getX() < 0);
    panel->onHandleCancel();
}

TEST_CASE("modulators in the chain view: Modulate with on a control's right-click, New before its module; a "
          "ring's drag sets the route; Add modulator after the selected module",
          "[integration][modulators][ui]")
{
    Chain f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    const auto filter = s.addDevice(f.t, "fd:filter");
    const auto lfo = s.addModulator(f.t, Kind::Lfo); // at the end
    f.show();
    REQUIRE(s.modulatorMenu);

    // The Filter's Freq: Modulate with ▸ LFO, then New ▸ the five.
    const auto freq = f.target(filter, "freq");
    juce::PopupMenu menu, with;
    s.modulatorMenu(freq, menu, with);
    CHECK(menu.getNumItems() == 0); // no routes yet
    std::vector<juce::String> names;
    juce::PopupMenu* fresh = nullptr;
    for (juce::PopupMenu::MenuItemIterator it(with); it.next();)
    {
        auto& item = it.getItem();
        if (item.isSeparator)
            continue;
        names.push_back(item.text);
        if (item.subMenu != nullptr)
            fresh = item.subMenu.get();
    }
    CHECK(names == std::vector<juce::String>{"LFO", "New"});
    REQUIRE(fresh != nullptr);
    std::vector<juce::String> kinds;
    std::function<void()> addRandom;
    for (juce::PopupMenu::MenuItemIterator it(*fresh); it.next();)
    {
        kinds.push_back(it.getItem().text);
        if (it.getItem().text == "Random")
            addRandom = it.getItem().action;
    }
    CHECK(kinds == std::vector<juce::String>{"LFO", "Random", "Envelope", "Follower", "Steps"});

    // New ▸ Random: just before the Filter, routed at 50 %, one undo step.
    REQUIRE(addRandom);
    addRandom();
    const auto ids = f.order();
    REQUIRE(ids.size() == 4);
    const auto random = ids[1];
    CHECK(ids == std::vector<std::uint64_t>{utility, random, filter, lfo});
    CHECK(f.info(random).kind == Kind::Random);
    REQUIRE(f.info(random).routes.size() == 1);
    CHECK(f.info(random).routes[0].key == freq.key);
    CHECK(s.undoDescription() == "Modulate with Random");
    s.undo();
    CHECK(f.order() == std::vector<std::uint64_t>{utility, filter, lfo});
    s.redo();
    f.show();

    // Now its route is listed, with Remove; the LFO's Modulate with item is ticked once routed.
    s.routeModulator(lfo, freq, 0.5f);
    f.show();
    {
        juce::PopupMenu m, w;
        s.modulatorMenu(freq, m, w);
        std::vector<juce::String> routes;
        for (juce::PopupMenu::MenuItemIterator it(m); it.next();)
            routes.push_back(it.getItem().text);
        CHECK(routes.size() == 2);
        CHECK(routes[0].startsWith("Random"));
        CHECK(routes[0].endsWith("50 %"));
        bool ticked = false;
        for (juce::PopupMenu::MenuItemIterator it(w); it.next();)
            if (it.getItem().text == "LFO")
                ticked = it.getItem().isTicked;
        CHECK(ticked);
    }

    // The Freq knob's ring dragged: it sets the route of the modulator hovered last (the LFO),
    // one undo step from start to end.
    s.setModulatorPointer({.modulator = lfo});
    s.setModulatorPointer({});
    auto handle = s.openDevice(filter);
    REQUIRE(handle != nullptr);
    auto& params = handle->context().params;
    const auto& specs = handle->context().info.params;
    const auto index = static_cast<size_t>(
        std::find_if(specs.begin(), specs.end(), [](const auto& p) { return p.id == "freq"; }) -
        specs.begin());
    REQUIRE(params.modulated(index).has_value());
    REQUIRE(params.beginModulatedDrag(index));
    params.dragModulated(index, 0.1f);
    params.dragModulated(index, 0.15f);
    params.endModulatedDrag(index);
    const auto amountOf = [&](std::uint64_t m)
    {
        for (const auto& r : f.info(m).routes)
            if (r.key == freq.key)
                return r.amount;
        return 0.0f;
    };
    CHECK(amountOf(lfo) == Approx(0.75f));
    CHECK(amountOf(random) == Approx(0.5f));
    CHECK(s.undoDescription() == "Route amount");
    s.undo();
    CHECK(amountOf(lfo) == Approx(0.5f));
    handle.reset();

    // Add modulator with the Utility selected: right after it; selected.
    f.show();
    f.chain->selectDevice(utility);
    f.chain->addModulator(Kind::Steps);
    const auto after = f.order();
    REQUIRE(after.size() == 5);
    CHECK(after[0] == utility);
    CHECK(f.info(after[1]).kind == Kind::Steps);
    f.show();
    REQUIRE(f.chain->selectedModulator() != nullptr);
    CHECK(f.chain->selectedModulator()->kind == Kind::Steps);
}

TEST_CASE("modulators in the chain view: each panel's header takes its presets without widening it (§14)",
          "[integration][modulators][ui]")
{
    Chain f;
    for (const auto kind : model::allModulators)
        REQUIRE(f.session->addModulator(f.t, kind) != 0);
    f.show();
    for (const auto id : f.order())
    {
        auto* panel = f.chain->modulatorPanel(id);
        REQUIRE(panel != nullptr);
        INFO(model::modulatorInfo(panel->kind()).name);
        REQUIRE(panel->presetBar() != nullptr);
        CHECK(panel->headerMinimumWidth() < panel->preferredWidth()); // its sections set its width
        // Fire (the Envelope's, Steps'): on while it's held, as a pad (§12.3).
        std::vector<fd::ui::ParamToggle*> toggles;
        collect(*panel, toggles);
        for (auto* t : toggles)
            if (t->getButtonText() == "Fire")
            {
                auto handle = f.session->openModulator(id);
                REQUIRE(handle != nullptr);
                const auto index = t->param();
                CHECK(t->isMomentary());
                t->setState(juce::Button::buttonDown);
                CHECK(handle->context().params.getPlain(index) == Approx(1.0f));
                t->setState(juce::Button::buttonNormal);
                CHECK(handle->context().params.getPlain(index) == Approx(0.0f));
            }
    }
    f.shot("all-five");
}

TEST_CASE(
    "modulators in the chain view: one dropped from the browser on a knob is added before its module and "
    "routed, with its preset; presets load into a modulator and keep a Steps pattern",
    "[integration][modulators][ui]")
{
    Chain f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.t, "fd:utility");
    const auto filter = s.addDevice(f.t, "fd:filter");
    f.show();

    // The Tremolo preset dragged over the Filter's Freq: the track's controls would take it.
    auto* freq = f.knob(filter, "freq");
    const auto at = f.chain->getLocalPoint(freq, freq->getLocalBounds().getCentre());
    const juce::var item(juce::String("segno-browser:preset:fd:mod-lfo|factory/Tremolo"));
    const juce::DragAndDropTarget::SourceDetails details(item, nullptr, at);
    REQUIRE(f.chain->isInterestedInDragSource(details));
    f.chain->itemDragMove(details);
    CHECK(s.modulatorPointer().dragging);
    REQUIRE(s.modulatorPointer().over);
    CHECK(*s.modulatorPointer().over == f.target(filter, "freq"));
    f.refreshAll();
    CHECK(freq->isDropTarget());
    f.chain->itemDropped(details);
    CHECK_FALSE(s.modulatorPointer().dragging);
    const auto ids = f.order();
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == utility);
    CHECK(ids[2] == filter);
    const auto lfo = ids[1];
    CHECK(f.info(lfo).kind == Kind::Lfo);
    REQUIRE(f.info(lfo).routes.size() == 1);
    CHECK(f.info(lfo).routes[0].key == f.target(filter, "freq").key);
    CHECK(s.undoDescription() == "Modulate with LFO");
    {
        auto handle = s.openModulator(lfo);
        REQUIRE(handle != nullptr);
        CHECK(handle->context().params.getPlain(fd::model::modulator::Lfo::Rate) == Approx(6.0f));
        REQUIRE(handle->context().presets != nullptr);
        CHECK(handle->context().presets->memo().preset == "factory/Tremolo");
    }
    s.undo(); // added, set up and routed: one step
    CHECK(f.order() == std::vector<std::uint64_t>{utility, filter});

    // Steps' presets carry its pattern; Default settings brings back the default one.
    const auto steps = s.addModulator(f.t, Kind::Steps);
    auto host = s.presetHost(steps);
    REQUIRE(host != nullptr);
    const auto& info = fd::model::modulatorInfo(Kind::Steps);
    auto& library = s.presetLibrary();
    const auto* gate = library.find(info, "factory/Trance gate");
    REQUIRE(gate != nullptr);
    const auto before = host->state();
    fd::PresetController(info, *host).load(*gate);
    auto handle = s.openModulator(steps);
    REQUIRE(handle != nullptr);
    CHECK(handle->host().stepValues()[2] == Approx(-1.0f));
    CHECK(fd::sameSound(info, host->state(), gate->state));
    CHECK(s.undoDescription().find("Trance gate") != std::string::npos);
    s.undo();
    CHECK(fd::sameSound(info, host->state(), before));
}

TEST_CASE("modulators in the chain: places count them as modules move, group into racks and come out",
          "[integration][modulators]")
{
    Chain f;
    auto& s = *f.session;
    const auto filter = s.addDevice(f.t, "fd:filter");
    const auto chorus = s.addDevice(f.t, "fd:chorus");
    const auto lfo = s.addModulator(f.t, Kind::Lfo, 0);
    CHECK(f.order() == std::vector<std::uint64_t>{lfo, filter, chorus});
    CHECK(s.placeOf(lfo)->index == 0);
    CHECK(s.placeOf(filter)->index == 1);
    CHECK(s.placeOf(chorus)->index == 2);

    // Dragged along the chain (the chain view's drop: before the place it lands at).
    REQUIRE(s.moveDevice(lfo, Session::ChainPlace{.channel = f.t, .index = 2}));
    CHECK(f.order() == std::vector<std::uint64_t>{filter, lfo, chorus});
    REQUIRE(s.moveDevice(chorus, Session::ChainPlace{.channel = f.t, .index = 0}));
    CHECK(f.order() == std::vector<std::uint64_t>{chorus, filter, lfo});
    REQUIRE(s.moveDevice(lfo, Session::ChainPlace{.channel = f.t, .index = 0}));
    CHECK(f.order() == std::vector<std::uint64_t>{lfo, chorus, filter});
    CHECK_FALSE(s.moveDevice(lfo, Session::ChainPlace{.channel = f.t, .rack = filter, .index = 0}));

    // Grouped: the rack where the first module was, the LFO where it sits.
    const auto rack = s.groupIntoRack({chorus, filter});
    REQUIRE(rack != 0);
    CHECK(f.order() == std::vector<std::uint64_t>{lfo, rack});
    // A rack added at the front: before the LFO.
    const auto empty = s.addRack(f.t, 0);
    CHECK(f.order() == std::vector<std::uint64_t>{empty, lfo, rack});
    s.removeDevice(empty);
    // Ungrouped: its modules where it was.
    const auto out = s.ungroupRack(rack);
    CHECK(out.size() == 2);
    CHECK(f.order() == std::vector<std::uint64_t>{lfo, chorus, filter});
    // A module out of a rack lands where it's dropped, the modulator counted.
    const auto rack2 = s.groupIntoRack({filter});
    CHECK(f.order() == std::vector<std::uint64_t>{lfo, chorus, rack2});
    REQUIRE(s.moveDevice(filter, Session::ChainPlace{.channel = f.t, .index = 1}));
    CHECK(f.order() == std::vector<std::uint64_t>{lfo, filter, chorus, rack2});
}

TEST_CASE(
    "modulators in the chain view: 8 modulators and 32 routes moving, refreshed and drawn within a frame "
    "(§20)",
    "[integration][modulators][ui]")
{
    Chain f;
    auto& s = *f.session;
    // Four modules, eight modulators among them, each moving four of the modules' knobs.
    std::vector<std::uint64_t> modules;
    for (const auto* type : {"fd:filter", "fd:chorus", "fd:delay", "fd:saturator"})
        modules.push_back(s.addDevice(f.t, type));
    std::vector<std::uint64_t> modulators;
    for (int i = 0; i < 8; ++i)
        modulators.push_back(s.addModulator(f.t, fd::model::allModulators[static_cast<size_t>(i % 5)], i));
    s.updateSnapshotNow();
    int routes = 0;
    for (const auto& p : s.automatableParameters(f.t))
    {
        if (routes >= 32)
            break;
        if (p.key.rfind('m', 0) != 0)
            continue;
        const auto modulator = modulators[static_cast<size_t>(routes % 8)];
        if (std::find(modules.begin(), modules.end(), std::stoull(p.key.substr(1, p.key.find('/') - 1))) ==
            modules.end())
            continue;
        if (s.routeModulator(modulator, {f.t, p.key}, 0.4f))
            ++routes;
    }
    REQUIRE(routes == 32);
    f.show(); // the chain at a window's width (1600 px), scrolled along
    s.play(); // they move
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // A frame: every panel's tick (each control's ring and dot), then what's on screen painted.
    juce::Image image(juce::Image::ARGB, f.chain->getWidth(), f.chain->getHeight(), true);
    double best = 1e9;
    for (int pass = 0; pass < 5; ++pass)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        f.refreshAll();
        for (const auto id : modulators)
            if (auto* panel = f.chain->modulatorPanel(id))
                panel->refreshFromHost();
        juce::Graphics g(image);
        f.chain->paintEntireComponent(g, false);
        best = std::min(best, juce::Time::getMillisecondCounterHiRes() - start);
    }
    s.stop();
    UNSCOPED_INFO("refresh and paint " << best << " ms");
#if defined(NDEBUG)
    CHECK(best < 16.0); // a frame at 60 Hz
#else
    CHECK(best < 120.0); // a Debug build: an order of magnitude slower, still no stall
#endif
    WARN("8 modulators, 32 routes: refresh and paint " << best << " ms");
}
