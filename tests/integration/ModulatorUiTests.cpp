// Modulators step 0 (docs/design/modulators.md §19): the modulators' UI drawn for review before
// they're built, with the real panel kit, their real panels and our modules' panels, from a stub
// of what the engine will hold. Modulators among the modules, before the instrument and inside a
// rack; each panel, unfolded and folded; Per Target's traces on and off; a handle dragged, its
// routes' lines on hover, a moved knob, bar and switch hovered; the route list; Keep. Each panel
// passes the module panel lint (tests/panels/PanelLint.h), the chain keeps its gaps, and each
// state is drawn for review with FD_DUMP_DIR, at 1x and 2x.
#include "../panels/PanelLint.h"
#include "engine/Session.h"
#include "model/Modulators.h"
#include "model/Racks.h"
#include "ui/DeviceChain.h"
#include "ui/ModulatorPanels.h"
#include "ui/RackPanel.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd/dsp/Modulators.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>
#include <map>

namespace model = fd::model;
using fd::app::ModulatorPanel;
using fd::ui::DevicePanel;
using Kind = fd::model::ModulatorKind;
using Modulated = fd::ui::ParamBinding::Modulated;

namespace
{
void dump(const juce::String& name, juce::Component& c, juce::Rectangle<int> area = {})
{
    if (const char* dir = std::getenv("FD_DUMP_DIR"))
        for (const float scale : {1.0f, 2.0f})
        {
            const auto file = juce::File(dir).getChildFile(name + (scale > 1.0f ? "@2x.png" : "@1x.png"));
            file.deleteFile();
            juce::FileOutputStream stream(file);
            juce::PNGImageFormat().writeImageToStream(
                c.createComponentSnapshot(area.isEmpty() ? c.getLocalBounds() : area, true, scale), stream);
        }
}

/// Segno's look while it lives, as the app sets it.
struct SegnoLook
{
    fd::ui::LookAndFeel lnf;
    SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(&lnf); }
    ~SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    SegnoLook(const SegnoLook&) = delete;
    SegnoLook& operator=(const SegnoLook&) = delete;
};

/// Parameters as the DAW will bind them: some moved by the track's modulators; while a handle is
/// dragged, every control a candidate and one under the pointer.
struct StubBinding final : fd::ui::ParamBinding
{
    explicit StubBinding(fd::ParamStore& s) : store(s) {}
    float getPlain(size_t i) const override { return store.get(i); }
    void setPlain(size_t i, float v) override { store.set(i, v); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json&) override {}
    std::optional<Modulated> modulated(size_t i) const override
    {
        if (const auto it = moved.find(i); it != moved.end())
            return it->second;
        return std::nullopt;
    }
    MacroDrag modulatorDrag(size_t i) const override { return {dragging, dropOn == i}; }
    fd::ParamStore& store;
    std::map<size_t, Modulated> moved;
    bool dragging = false;
    std::optional<size_t> dropOn;
};

/// A module's presets as the DAW has them, none loaded.
struct StubPresets final : fd::PresetHost
{
    fd::PresetLibrary presets{{},
                              std::filesystem::temp_directory_path() / "fd-modulator-ui-tests-no-presets"};
    fd::PresetMemo current;
    const fd::Device* device = nullptr;
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override
    {
        return device != nullptr ? fd::captureState(*device) : fd::DeviceState{};
    }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

/// The Follower's Listen picker: where it sits (Here), or the track's others.
struct StubListen final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"Here", "Kick", "Bass"}; }
    int selected() const override { return choice; }
    void select(int index) override { choice = index; }
    int choice = 0;
};

/// What the engine keeps for a modulator: its routes, where it is, its roll, what it heard.
struct StubHost final : fd::app::ModulatorHost
{
    int routes = 0;
    double at = 0.0;
    std::vector<double> plays;
    std::uint64_t dice = 7;
    bool keep = false;
    std::vector<float> levels, values, chances;
    int routeCount() const override { return routes; }
    double position() const override { return at; }
    std::vector<double> playing() const override { return plays; }
    std::uint64_t roll() const override { return dice; }
    bool kept() const override { return keep; }
    void setKept(bool k) override { keep = k; }
    std::vector<float> heard() const override { return levels; }
    std::vector<float> stepValues() const override { return values; }
    std::vector<float> stepChances() const override { return chances; }
};

/// One of our modules with its panel, as the chain shows it.
struct Module
{
    StubPresets presets;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<StubBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<DevicePanel> panel;

    explicit Module(std::string_view deviceId)
    {
        const auto* factory = fd::findDevice(deviceId);
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<StubBinding>(device->params());
        presets.device = device.get();
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, &presets});
        const auto* panelFactory = fd::ui::findPanel(deviceId);
        REQUIRE(panelFactory != nullptr);
        panel = panelFactory->create(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setFoldable(true);
    }

    [[nodiscard]] size_t param(std::string_view paramId) const
    {
        for (size_t i = 0; i < context->info.params.size(); ++i)
            if (context->info.params[i].id == paramId)
                return i;
        FAIL("no parameter " << paramId);
        return 0;
    }

    /// The control that sets `paramId`: a knob, a bar or a switch.
    [[nodiscard]] juce::Component& control(std::string_view paramId) const
    {
        const auto p = param(paramId);
        for (auto* s : panel->sections())
            for (const auto& pl : s->placements())
            {
                if (auto* k = dynamic_cast<fd::ui::ParamKnob*>(pl.component); k != nullptr && k->param() == p)
                    return *k;
                if (auto* t = dynamic_cast<fd::ui::ParamToggle*>(pl.component);
                    t != nullptr && t->param() == p)
                    return *t;
            }
        FAIL("no control for " << paramId);
        throw std::logic_error("unreachable");
    }

    /// Moved by modulators: from the knob's value, `swing` either way (or up, one way), now at `now`.
    void move(std::string_view paramId, float swing, bool bothWays, float now, juce::String by)
    {
        const auto p = param(paramId);
        const float v = device->params().getNormalized(p);
        binding->moved[p] = {.from = std::clamp(bothWays ? v - swing : v, 0.0f, 1.0f),
                             .to = std::clamp(v + swing, 0.0f, 1.0f),
                             .now = std::clamp(now, 0.0f, 1.0f),
                             .by = std::move(by)};
    }

    void refresh() const
    {
        for (auto* s : panel->sections())
            for (const auto& pl : s->placements())
                if (auto* c = dynamic_cast<fd::ui::ParamControl*>(pl.component))
                    c->refresh();
    }
};

/// A modulator with its panel, as the chain shows it.
struct Modulator
{
    Kind kind;
    StubHost host;
    fd::ParamStore store;
    StubBinding binding{store};
    StubPresets presets;
    StubListen listen;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<ModulatorPanel> panel;

    explicit Modulator(Kind k) : kind(k), store(model::modulatorInfo(k).params)
    {
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{model::modulatorInfo(k), binding, nullptr, nullptr,
                                    k == Kind::Follower ? &listen : nullptr, &presets});
        panel = std::make_unique<ModulatorPanel>(*context, k, host);
        panel->onEnabledChange = [](bool) {};
        panel->setFoldable(true);
    }

    void set(size_t param, float plain) { store.set(param, plain); }

    void refresh() const
    {
        for (auto* s : panel->sections())
            for (const auto& pl : s->placements())
                if (auto* c = dynamic_cast<fd::ui::ParamControl*>(pl.component))
                    c->refresh();
        panel->refreshFromHost();
    }
};

/// The track's chain, laid out as the chain view lays it out: panels left to right, a rack's
/// frame around its panel and its chain's modules (racks.md §5), the chain's +. Over it, while a
/// handle is hovered, a line from the handle to each control it moves; while one is dragged, its
/// chip at the pointer.
class ChainStub final : public juce::Component
{
public:
    struct Slot
    {
        DevicePanel* panel = nullptr;
        bool inRack = false;
    };
    std::vector<Slot> slots;
    fd::app::RackPanel* rack = nullptr;
    fd::app::RackFrame frame;
    fd::ui::IconButton endCap{fd::ui::Icon::Plus}, add{fd::ui::Icon::Plus};
    juce::Component* selected = nullptr;
    // Hover: the handle and the controls its routes go to.
    juce::Component* lineFrom = nullptr;
    std::vector<juce::Component*> lineTo;
    // A drag: the handle and where the pointer is.
    juce::Component* dragged = nullptr;
    juce::Point<int> dragAt;

    ChainStub()
    {
        addChildComponent(endCap);
        addAndMakeVisible(add);
    }

    void build()
    {
        for (auto& s : slots)
            addAndMakeVisible(*s.panel);
        layout();
    }

    void layout()
    {
        const auto& t = fd::ui::Theme::current();
        const int gap = fd::app::DeviceChain::gap, inset = t.shell.rackInset;
        const int top = inset, h = t.devicePanelHeight, button = t.shell.buttonSize;
        int x = gap;
        int frameX = -1;
        const auto put = [&](DevicePanel& p)
        {
            p.setBounds(x, top, p.preferredWidth(), h);
            x += p.getWidth() + gap;
        };
        endCap.setVisible(false);
        frame = {};
        for (size_t i = 0; i < slots.size(); ++i)
        {
            auto& s = slots[i];
            if (s.panel == rack)
            {
                frameX = x;
                x += inset;
                put(*s.panel);
                continue;
            }
            put(*s.panel);
            const bool lastInRack = s.inRack && (i + 1 == slots.size() || !slots[i + 1].inRack);
            if (lastInRack)
            {
                endCap.setVisible(true);
                endCap.setBounds(x, top, button, button);
                x += button;
                frame.bounds = {frameX, 0, x + inset - frameX, top + h + t.shell.rackLabelHeight};
                x = frame.bounds.getRight() + gap;
            }
        }
        add.setBounds(x, top, button, button);
        x += button + gap;
        setSize(x, (frame.bounds.isEmpty() ? top + h : frame.bounds.getBottom()) + inset);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = fd::ui::Theme::current();
        g.fillAll(t.surface1); // the detail panel
        if (!frame.bounds.isEmpty())
            fd::app::paintRackFrame(g, frame);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        const auto& t = fd::ui::Theme::current();
        if (selected != nullptr)
        {
            g.setColour(t.active);
            g.drawRoundedRectangle(selected->getBounds().toFloat().reduced(0.5f), t.radiusPanel, 1.0f);
        }
        // Hovering a handle (modulators.md §7.4): a line from it to each control it moves, from the
        // chip's foot to a dot on the control, in the modulation gradient over a dark halo (it
        // crosses other panels), sagging as a cable does, more the further it goes.
        if (lineFrom != nullptr)
            for (auto* target : lineTo)
            {
                const auto chip = getLocalArea(lineFrom, lineFrom->getLocalBounds()).toFloat();
                fd::ui::paintModulationCable(
                    g, {chip.getCentreX(), chip.getBottom()},
                    getLocalArea(target, target->getLocalBounds()).toFloat().getCentre());
            }
        // A drag: the handle's chip at the pointer.
        if (dragged != nullptr)
        {
            const auto image = dragged->createComponentSnapshot(dragged->getLocalBounds(), true, 2.0f);
            g.setOpacity(0.9f);
            g.drawImage(image, juce::Rectangle<int>(dragged->getWidth(), dragged->getHeight())
                                   .withPosition(dragAt.translated(t.shell.itemGap, t.shell.itemGap))
                                   .toFloat());
        }
    }
};

/// The chain's rules (the chain view's): panels apart by its gap, the rack's inside its frame,
/// nothing touching; each modulator's panel on the module grid (the module panel lint), its
/// handle inside its display's label line, its folded strip's handle at the foot.
void lint(ChainStub& chain, const std::vector<std::pair<ModulatorPanel*, const fd::DeviceInfo*>>& modulators)
{
    const auto& t = fd::ui::Theme::current();
    const int gap = fd::app::DeviceChain::gap;
    std::vector<juce::Rectangle<int>> placed;
    for (const auto& s : chain.slots)
    {
        INFO("panel " << s.panel->getName());
        const auto r = s.panel->getBounds();
        for (const auto& other : placed)
            CHECK_FALSE(other.expanded(gap - 1, 0).intersects(r));
        placed.push_back(r);
        if (s.inRack || s.panel == chain.rack)
            CHECK(chain.frame.bounds.reduced(t.shell.rackInset).contains(r));
        else if (!chain.frame.bounds.isEmpty())
            CHECK_FALSE(chain.frame.bounds.expanded(gap - 1, 0).intersects(r));
    }
    for (const auto& [panel, info] : modulators)
    {
        INFO("modulator " << info->name);
        fd::test::lintPanel(*panel, *info, "Here");
        auto& handle = panel->handle();
        if (panel->isFolded())
        {
            CHECK(handle.isVisible());
            CHECK(panel->getLocalBounds().reduced(t.shell.itemGap, 0).contains(handle.getBounds()));
            CHECK(handle.getY() > t.deviceHeaderHeight * 2); // under the chevron and power
            continue;
        }
        // The handle: inside the display, on its label line, as wide as its name and count need.
        auto& display = panel->display();
        CHECK(display.getLocalBounds().contains(handle.getBounds()));
        CHECK(handle.getHeight() == t.deviceGrid.sourceHandle);
        CHECK(handle.getWidth() == handle.preferredWidth());
        if (auto* keep = panel->keep())
            CHECK(keep->isVisible());
    }
}

/// The route list: rows inside it, under its title, their parts apart, the names fitting.
void lint(const fd::app::RouteList& list, const std::vector<juce::String>& names)
{
    const auto& t = fd::ui::Theme::current();
    const auto inside = list.inner().reduced(t.shell.groupGap);
    CHECK(inside.contains(list.titleArea()));
    for (size_t i = 0; i < list.rows().size(); ++i)
    {
        const auto& row = list.rows()[i];
        INFO("route " << names[i]);
        std::vector<juce::Rectangle<int>> parts{row.name, row.amount->getBounds(), row.show->getBounds(),
                                                row.remove->getBounds()};
        for (size_t a = 0; a < parts.size(); ++a)
        {
            CHECK(inside.contains(parts[a]));
            CHECK(parts[a].getY() >= list.titleArea().getBottom());
            for (size_t b = a + 1; b < parts.size(); ++b)
                CHECK_FALSE(parts[a].intersects(parts[b]));
        }
        CHECK(juce::GlyphArrangement::getStringWidth(fd::ui::font(fd::ui::TextStyle::Body), names[i]) <=
              static_cast<float>(row.name.getWidth()) + fd::test::kTextMargin);
    }
}

/// The modulators drawn, routed to our modules: an LFO beside the Filter it moves (its Freq, and
/// the Chorus's Mix), Random moving four controls with Per Target (the Filter's Res, the Chorus's
/// Depth, its Feedback bar and its Invert switch), after a Utility; an Envelope before the
/// Wavetable it opens; a Follower after it, ducking; Steps inside a rack beside a Utility.
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SegnoLook look;
    Module utility{"utility"}, filter{"filter"}, chorus{"chorus"}, wavetable{"wavetable"},
        rackUtility{"utility"}, delay{"delay"};
    Modulator lfo{Kind::Lfo}, random{Kind::Random}, envelope{Kind::Envelope}, follower{Kind::Follower},
        steps{Kind::Steps};
    fd::ParamStore macroStore{fd::engine::Session::rackMacros().params};
    StubBinding macroBinding{macroStore};
    fd::ui::DeviceUiContext macroContext{
        fd::engine::Session::rackMacros(), macroBinding, nullptr, nullptr, nullptr, nullptr};
    std::unique_ptr<fd::app::RackPanel> rack;
    ChainStub chain;

    Fixture()
    {
        using namespace model::modulator;
        rack = std::make_unique<fd::app::RackPanel>(macroContext);
        rack->onEnabledChange = [](bool) {};
        rack->setFoldable(true);
        model::RackInfo info;
        info.chains = {{.name = "Main", .devices = {}}};
        rack->setRack("Pump", info);

        // The LFO: 0.5 Hz, a triangle slanted late, moving the Filter's Freq and the Chorus's Mix.
        lfo.set(Lfo::Shape, 1.0f);
        lfo.set(Lfo::Rate, 0.5f);
        lfo.set(Lfo::Slant, 40.0f);
        lfo.host.routes = 2;
        lfo.host.at = 0.3;
        // Random: 4 Hz, gliding, each of its four targets its own way.
        random.set(Random::Rate, 4.0f);
        random.set(Random::Smooth, 70.0f);
        random.host.routes = 4;
        random.host.at = 13.4;
        // The Envelope: a pluck, each copy on its own chance; two of three playing.
        envelope.set(Envelope::Attack, 4.0f);
        envelope.set(Envelope::Decay, 420.0f);
        envelope.set(Envelope::Sustain, 35.0f);
        envelope.set(Envelope::Release, 600.0f);
        envelope.set(Envelope::Chance, 70.0f);
        envelope.host.routes = 3;
        envelope.host.plays = {120.0, -1.0, 40.0};
        // The Follower: what it heard, a kick a beat.
        follower.set(Follower::Release, 220.0f);
        for (int i = 0; i < 160; ++i)
        {
            const double beat = std::fmod(i / 40.0, 1.0);
            follower.host.levels.push_back(static_cast<float>(0.02 + 0.85 * std::exp(-beat * 9.0)));
        }
        follower.host.routes = 1;
        // Steps: sixteen, a rising figure with gaps; some steps left to chance.
        steps.host.routes = 1;
        steps.host.at = 5.5;
        for (int i = 0; i < 16; ++i)
        {
            steps.host.values.push_back(static_cast<float>(std::sin(i * 0.7) * 0.8));
            steps.host.chances.push_back(i % 4 == 3 ? 0.5f : 1.0f);
        }

        // Where they move our modules (normalised ranges, as the engine will report them).
        filter.move("freq", 0.18f, true, 0.62f, "LFO, Envelope");
        filter.move("res", 0.12f, true, 0.34f, "Random");
        chorus.move("mix", 0.2f, true, 0.42f, "LFO");
        chorus.move("depth", 0.15f, true, 0.6f, "Random");
        chorus.move("feedback", 0.25f, true, 0.52f, "Random");
        chorus.move("invert", 1.0f, true, 1.0f, "Random");
        wavetable.move("osc1.pos", 0.35f, false, 0.48f, "Envelope");
        rackUtility.move("gain", 0.2f, true, 0.55f, "Steps");

        chain.slots = {{utility.panel.get()},
                       {lfo.panel.get()},
                       {filter.panel.get()},
                       {random.panel.get()},
                       {chorus.panel.get()}};
        chain.build();
        refresh();
    }

    std::vector<std::pair<ModulatorPanel*, const fd::DeviceInfo*>> shownModulators() const
    {
        std::vector<std::pair<ModulatorPanel*, const fd::DeviceInfo*>> out;
        for (const auto* m : {&lfo, &random, &envelope, &follower, &steps})
            for (const auto& s : chain.slots)
                if (s.panel == m->panel.get())
                    out.emplace_back(m->panel.get(), &m->context->info);
        return out;
    }

    /// What every panel's refresh tick does (no message loop here).
    void refresh() const
    {
        for (const auto* m : {&utility, &filter, &chorus, &wavetable, &rackUtility, &delay})
            m->refresh();
        for (const auto* m : {&lfo, &random, &envelope, &follower, &steps})
            m->refresh();
    }

    void shot(const juce::String& name)
    {
        refresh();
        chain.layout();
        lint(chain, shownModulators());
        dump("modulators-" + name, chain);
    }

    /// A closer look at one component, with a little of the chain around it.
    void crop(const juce::String& name, juce::Component& c)
    {
        const int beside = fd::app::DeviceChain::gap * 2;
        dump("modulators-" + name, chain,
             chain.getLocalArea(&c, c.getLocalBounds())
                 .expanded(beside, 0)
                 .withY(0)
                 .withHeight(chain.getHeight())
                 .getIntersection(chain.getLocalBounds()));
    }
};
} // namespace

TEST_CASE(
    "modulators, drawn for review: among the modules, before the instrument, in a rack (modulators.md §19)",
    "[ui]")
{
    Fixture f;
    using namespace model::modulator;

    // Among the modules: the LFO beside the Filter it moves, Random after it.
    f.shot("chain");
    f.crop("lfo", *f.lfo.panel);
    f.crop("random-per-target-on", *f.random.panel);
    // The chain at a compact width: the detail panel shows what fits, and scrolls.
    {
        f.refresh();
        f.chain.layout();
        const auto from = f.lfo.panel->getX() - fd::app::DeviceChain::gap;
        dump("modulators-chain-compact", f.chain, {from, 0, 1100, f.chain.getHeight()});
    }

    // Per Target off: one trace.
    f.random.set(Random::PerTarget, 0.0f);
    f.shot("random-per-target-off");
    f.crop("random-per-target-off", *f.random.panel);
    f.random.set(Random::PerTarget, 1.0f);

    // Keep: the roll it played, kept; a lock on its handle.
    f.random.host.keep = true;
    f.shot("keep");
    f.crop("random-keep", *f.random.panel);
    f.random.host.keep = false;

    // Before the instrument: the Envelope, then the Wavetable it opens; a Follower after it.
    f.chain.slots = {{f.envelope.panel.get()}, {f.wavetable.panel.get()}, {f.follower.panel.get()}};
    for (juce::Component* m : std::initializer_list<juce::Component*>{
             f.utility.panel.get(), f.lfo.panel.get(), f.filter.panel.get(), f.random.panel.get(),
             f.chorus.panel.get()})
        m->setVisible(false);
    f.chain.build();
    f.shot("before-instrument");
    f.crop("envelope", *f.envelope.panel);
    f.crop("follower", *f.follower.panel);

    // Inside a rack: Steps beside the Utility it moves, after a Delay.
    f.chain.slots = {{f.delay.panel.get()},
                     {f.rack.get()},
                     {f.steps.panel.get(), true},
                     {f.rackUtility.panel.get(), true}};
    for (juce::Component* m : std::initializer_list<juce::Component*>{
             f.envelope.panel.get(), f.wavetable.panel.get(), f.follower.panel.get()})
        m->setVisible(false);
    f.chain.rack = f.rack.get();
    f.chain.build();
    f.shot("in-rack");
    f.crop("steps", *f.steps.panel);
    f.chain.rack = nullptr;
}

TEST_CASE("modulators, drawn for review: each panel folded", "[ui]")
{
    Fixture f;
    f.chain.slots = {};
    for (auto* m : {&f.lfo, &f.random, &f.envelope, &f.follower, &f.steps})
    {
        m->panel->setFolded(true);
        f.chain.slots.push_back({m->panel.get()});
    }
    for (auto* m : {f.utility.panel.get(), f.filter.panel.get(), f.chorus.panel.get()})
        m->setVisible(false);
    f.chain.build();
    f.shot("folded");
}

TEST_CASE("modulators, drawn for review: a handle dragged, hovered, and moved controls hovered", "[ui]")
{
    Fixture f;
    // A drag of Random's handle: every control it could move faintly ringed, the Filter's Drive lit
    // under the pointer, the chip following it.
    for (auto* m : {&f.utility, &f.filter, &f.chorus})
        m->binding->dragging = true;
    f.filter.binding->dropOn = f.filter.param("drive");
    f.random.panel->source().draggedSource = 0;
    auto& drive = f.filter.control("drive");
    f.chain.dragged = &f.random.panel->handle();
    f.chain.dragAt = f.chain.getLocalArea(&drive, drive.getLocalBounds()).getCentre();
    f.shot("drag");
    f.crop("drag-filter", *f.filter.panel);
    f.chain.dragged = nullptr;
    f.random.panel->source().draggedSource = -1;
    for (auto* m : {&f.utility, &f.filter, &f.chorus})
    {
        m->binding->dragging = false;
        m->binding->dropOn.reset();
    }

    // Hovering the LFO's handle: a line to each control it moves; the other modulators' rings dim.
    f.lfo.panel->source().hoveredSource = 0;
    for (auto* m : {&f.filter, &f.chorus})
        for (auto& [p, moved] : m->binding->moved)
            moved.dimmed = !moved.by.contains("LFO");
    f.chain.lineFrom = &f.lfo.panel->handle();
    f.chain.lineTo = {&f.filter.control("freq"), &f.chorus.control("mix")};
    f.shot("hover-handle");
    f.chain.lineFrom = nullptr;
    f.chain.lineTo.clear();
    f.lfo.panel->source().hoveredSource = -1;
    for (auto* m : {&f.filter, &f.chorus})
        for (auto& [p, moved] : m->binding->moved)
            moved.dimmed = false;

    // Hovering a moved knob (the Filter's Freq): the handles of what moves it light (the chain
    // lights them; here, as it will).
    f.lfo.panel->source().hoveredSource = 0;
    f.shot("hover-knob");
    f.lfo.panel->source().hoveredSource = -1;
    // A moved bar and a moved switch (the Chorus's Feedback and Invert): Random's handle lights.
    f.random.panel->source().hoveredSource = 0;
    f.shot("hover-bar-switch");
    f.crop("chorus-moved", *f.chorus.panel);
}

TEST_CASE("modulators, drawn for review: the route list", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SegnoLook look;
    const auto dotted = [](const char* a, const char* b)
    { return juce::String(a) + juce::String::fromUTF8(" \xC2\xB7 ") + b; };
    const std::vector<juce::String> names{dotted("Filter", "Freq"), dotted("Chorus", "Mix"),
                                          dotted("Track", "Volume")};
    fd::app::RouteList list("LFO", {{names[0], 0.4f}, {names[1], -0.3f}, {names[2], 1.25f}});
    list.setBounds(list.preferredBounds());
    lint(list, names);
    dump("modulators-routes", list);
    fd::app::RouteList none("Random", {});
    none.setBounds(none.preferredBounds());
    dump("modulators-routes-empty", none);
}
