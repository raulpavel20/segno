// Racks step 0 (docs/design/racks.md §5a): the rack's UI drawn for review before it's built, with
// the real panel kit and module panels, from a stub of what the engine will hold: one chain and
// two, each shown; folded; a module dragged in and one dragged out; macros mapped, hovered and
// being mapped; the macro editor; an empty rack. Each state is checked on the module grid and in
// its frame (nothing touching), and drawn for review with FD_DUMP_DIR, at 1x and 2x.
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "model/Racks.h"
#include "ui/ChannelControls.h"
#include "ui/DeviceChain.h"
#include "ui/MacroEditor.h"
#include "ui/RackPanel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>
#include <map>
#include <set>
#include <tracktion_engine/tracktion_engine.h>

namespace model = fd::model;
using fd::app::RackPanel;
using fd::ui::DevicePanel;
using MacroMapping = fd::ui::ParamBinding::MacroMapping;

namespace
{
constexpr float kTextMargin = 1.5f; // font rasterisers differ slightly across platforms

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

float textWidth(const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth(font, text);
}

/// A module's parameters as the DAW will bind them inside a rack: some set by a macro.
struct RackBinding final : fd::ui::ParamBinding
{
    explicit RackBinding(fd::ParamStore& s) : store(s) {}
    float getPlain(size_t i) const override { return store.get(i); }
    void setPlain(size_t i, float v) override { store.set(i, v); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json&) override {}
    std::optional<MacroMapping> macroMapping(size_t i) const override
    {
        if (const auto it = mapped.find(i); it != mapped.end())
            return it->second;
        return std::nullopt;
    }
    fd::ui::AutomationState automationState(size_t i) const override
    {
        const auto it = automated.find(i);
        return it != automated.end() ? it->second : fd::ui::AutomationState::None;
    }
    fd::ParamStore& store;
    std::map<size_t, MacroMapping> mapped;
    std::map<size_t, fd::ui::AutomationState> automated;
};

/// A module's presets as the DAW has them, none loaded: its header has its preset bar there.
struct StubPresets final : fd::PresetHost
{
    fd::PresetLibrary presets{{}, std::filesystem::temp_directory_path() / "fd-rack-ui-tests-no-presets"};
    fd::PresetMemo current;
    const fd::Device* device = nullptr;
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override { return fd::captureState(*device); }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

/// Segno's look while it lives, as the app sets it (combos and fields in its fonts).
struct SegnoLook
{
    fd::ui::LookAndFeel lnf;
    SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(&lnf); }
    ~SegnoLook() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    SegnoLook(const SegnoLook&) = delete;
    SegnoLook& operator=(const SegnoLook&) = delete;
};

/// One of our modules with its panel, as the chain shows it.
struct Module
{
    std::uint64_t id = 0;
    StubPresets presets;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<RackBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<DevicePanel> panel;

    Module(std::uint64_t moduleId, std::string_view deviceId) : id(moduleId)
    {
        const auto* factory = fd::findDevice(deviceId);
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<RackBinding>(device->params());
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
    [[nodiscard]] model::DeviceSlot slot() const
    {
        return {.id = id, .name = std::string(context->info.name), .deviceId = std::string(context->info.id)};
    }
    /// The knob that sets `paramId`.
    [[nodiscard]] fd::ui::ParamKnob& knob(std::string_view paramId) const
    {
        const auto p = param(paramId);
        for (auto* s : panel->sections())
            for (const auto& pl : s->placements())
                if (auto* k = dynamic_cast<fd::ui::ParamKnob*>(pl.component); k != nullptr && k->param() == p)
                    return *k;
        FAIL("no knob for " << paramId);
        throw std::logic_error("unreachable");
    }
};

/// The track's chain with a rack in it, laid out as the chain view will lay it out (racks.md §5):
/// modules before it, the rack's panel and its shown chain's modules in its frame with the end
/// cap, modules after it, the chain's +.
class ChainStub final : public juce::Component
{
public:
    std::vector<Module*> before, after, inner;
    RackPanel* rack = nullptr;
    model::RackInfo info;
    juce::Component* selected = nullptr;
    // A drag: where it lands (in the track's chain, after `dropAfter`; or in the rack's chain at
    // `dropInside`, before that module or, at its end, before the end cap).
    juce::Component* dropAfter = nullptr;
    std::optional<int> dropInside;

    fd::ui::IconButton endCap{fd::ui::Icon::Plus}, add{fd::ui::Icon::Plus};
    fd::app::RackFrame frame;
    juce::Rectangle<int> emptyHint; // an empty chain's room in the frame

    ChainStub()
    {
        endCap.setTooltip("Add a module to the rack's chain shown (or a chain beside it)");
        addAndMakeVisible(endCap);
        addAndMakeVisible(add);
    }

    void build()
    {
        for (auto* m : before)
            addAndMakeVisible(*m->panel);
        addAndMakeVisible(*rack);
        for (auto* m : inner)
            addAndMakeVisible(*m->panel);
        for (auto* m : after)
            addAndMakeVisible(*m->panel);
        layout();
    }

    void layout()
    {
        const auto& t = fd::ui::Theme::current();
        const int gap = fd::app::DeviceChain::gap, inset = t.shell.rackInset;
        const int top = inset, h = t.devicePanelHeight;
        const int button = t.shell.buttonSize;
        int x = gap;
        const auto put = [&](DevicePanel& p)
        {
            p.setBounds(x, top, p.preferredWidth(), h);
            x += p.getWidth() + gap;
        };
        for (auto* m : before)
            put(*m->panel);
        // The rack's frame: its panel, its chain's modules, the end cap.
        const int frameX = x;
        x += inset;
        put(*rack);
        const bool folded = rack->isFolded();
        const int chainX = x;
        emptyHint = {};
        for (auto* m : inner)
        {
            m->panel->setVisible(!folded);
            if (!folded)
                put(*m->panel);
        }
        if (!folded && inner.empty())
        {
            emptyHint = {x, top, fd::app::DeviceChain::cardWidth, h};
            x += emptyHint.getWidth() + gap;
        }
        endCap.setVisible(!folded);
        if (!folded)
        {
            endCap.setBounds(x, top, button, button);
            x += button;
        }
        else
            x -= gap;
        frame.bounds = {frameX, 0, x + inset - frameX, top + h + (folded ? inset : t.shell.rackLabelHeight)};
        frame.label = folded ? juce::Rectangle<int>()
                             : juce::Rectangle<int>(chainX, top + h, x - chainX, t.shell.rackLabelHeight);
        const auto& shown = info.chains[static_cast<size_t>(info.shownChain)];
        frame.text =
            info.chains.size() > 1 && !folded ? fd::app::chainLabel(info, info.shownChain) : juce::String();
        frame.chainOff = !shown.on && info.chains.size() > 1;
        frame.soloed = shown.solo && info.chains.size() > 1;
        for (auto* m : inner)
            m->panel->setAlpha(frame.chainOff ? t.roles.fadedAlpha : 1.0f);
        x = frame.bounds.getRight() + gap;
        for (auto* m : after)
            put(*m->panel);
        add.setBounds(x, top, button, button);
        x += button + gap;
        setSize(x, frame.bounds.getBottom() + inset);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = fd::ui::Theme::current();
        g.fillAll(t.surface1); // the detail panel
        frame.dropInside = dropInside.has_value();
        fd::app::paintRackFrame(g, frame);
        if (!emptyHint.isEmpty())
            fd::ui::paintEmptyState(g, emptyHint, "Drop modules here");
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        const auto& t = fd::ui::Theme::current();
        if (selected != nullptr)
        {
            g.setColour(t.active);
            g.drawRoundedRectangle(selected->getBounds().toFloat().reduced(0.5f), t.radiusPanel, 1.0f);
        }
        const int gap = fd::app::DeviceChain::gap;
        // Where a drop lands, before it lands (racks.md §5a): in the rack's chain, the line runs
        // the frame's panels high; in the track's chain, the chain's whole height.
        if (dropInside)
        {
            const int i = *dropInside;
            const int x = i < static_cast<int>(inner.size())
                              ? inner[static_cast<size_t>(i)]->panel->getX() - gap / 2
                              : endCap.getX() - gap / 2;
            g.setColour(t.active);
            g.fillRect(x - t.shell.dropLine / 2, rack->getY(), t.shell.dropLine, rack->getHeight());
        }
        if (dropAfter != nullptr)
        {
            const int x = (dropAfter == rack ? frame.bounds.getRight() : dropAfter->getRight()) + gap / 2;
            g.setColour(t.active);
            g.fillRect(x - t.shell.dropLine / 2, 0, t.shell.dropLine, getHeight());
        }
    }
};

/// The rack's panel on the module grid (the panel lint's rules, device-panels.md) and its frame:
/// its panels inside it and apart, its label fitting, nothing touching outside it.
void lint(ChainStub& chain)
{
    const auto& t = fd::ui::Theme::current();
    const auto& grid = t.deviceGrid;
    auto& panel = *chain.rack;
    REQUIRE(panel.getHeight() == t.devicePanelHeight);
    CHECK(panel.preferredWidth() % t.grid == 0);
    if (!panel.isFolded())
    {
        CHECK(panel.headerMinimumWidth() <= panel.getWidth());
        CHECK(textWidth(fd::ui::font(fd::ui::TextStyle::Title), panel.getName()) >= 0.0f);
        int x = 0;
        for (auto* s : panel.shownSections())
        {
            INFO("section " << s->id());
            CHECK(s->getX() == x);
            CHECK(s->getWidth() == s->preferredWidth());
            CHECK(s->getHeight() == grid.bodyHeight());
            x += s->getWidth();
            std::set<std::pair<int, int>> taken;
            for (const auto& pl : s->placements())
            {
                INFO("'" << pl.component->getName() << "' at (" << pl.column << ", " << pl.row << ")");
                CHECK(pl.column + pl.columns <= s->columns());
                CHECK(pl.row + pl.rows <= grid.rows);
                CHECK(pl.component->getBounds() ==
                      fd::ui::PanelSection::cellBounds(pl.column, pl.row, pl.columns, pl.rows));
                for (int c = pl.column; c < pl.column + pl.columns; ++c)
                    for (int r = pl.row; r < pl.row + pl.rows; ++r)
                        CHECK(taken.insert({c, r}).second);
                if (auto* knob = dynamic_cast<fd::ui::Knob*>(pl.component))
                {
                    const auto nameFont = fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium);
                    if (knob->style() == fd::ui::Knob::Style::Rotary)
                        CHECK(textWidth(nameFont, knob->getName()) <= 46.0f + kTextMargin);
                    else
                    {
                        float widest = 0.0f;
                        for (int i = 0; i <= 32; ++i)
                            widest = std::max(widest,
                                              textWidth(fd::ui::fonts::mono(t.textXs),
                                                        knob->textFromValue(static_cast<float>(i) / 32.0f)));
                        CHECK(textWidth(nameFont, knob->getName()) + widest + 16.0f <=
                              static_cast<float>(knob->getWidth()) + kTextMargin);
                    }
                }
                const auto toggleFont = fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::SemiBold);
                if (auto* selector = dynamic_cast<fd::ui::Selector*>(pl.component))
                    for (auto* b : selector->buttons())
                        CHECK(textWidth(toggleFont, b->getButtonText()) + 6.0f <=
                              static_cast<float>(b->getWidth()) + kTextMargin);
                if (auto* toggle = dynamic_cast<fd::ui::Toggle*>(pl.component))
                    CHECK(textWidth(toggleFont, toggle->getButtonText()) + 8.0f <=
                          static_cast<float>(toggle->getWidth()) + kTextMargin);
            }
        }
    }

    // The frame: its panels and end cap inside it, an inset from its edges; nothing outside it
    // touches it; its label fits its strip.
    const auto inner = chain.frame.bounds.reduced(t.shell.rackInset);
    std::vector<juce::Component*> in{chain.rack};
    for (auto* m : chain.inner)
        if (m->panel->isVisible())
            in.push_back(m->panel.get());
    if (chain.endCap.isVisible())
        in.push_back(&chain.endCap);
    for (auto* c : in)
    {
        INFO("in the frame: " << c->getBounds().toString().toStdString());
        CHECK(inner.contains(c->getBounds()));
    }
    for (auto* m : chain.before)
        CHECK(m->panel->getRight() + fd::app::DeviceChain::gap <= chain.frame.bounds.getX());
    for (auto* m : chain.after)
        CHECK(m->panel->getX() >= chain.frame.bounds.getRight() + fd::app::DeviceChain::gap);
    CHECK(chain.add.getX() >= chain.frame.bounds.getRight() + fd::app::DeviceChain::gap);
    if (chain.frame.text.isNotEmpty())
    {
        CHECK(chain.frame.label.getY() >= chain.rack->getBottom());
        CHECK(chain.frame.label.getBottom() <= chain.frame.bounds.getBottom());
        CHECK(textWidth(fd::ui::capsLabelFont(), (chain.frame.text + " · off   solo").toUpperCase()) <=
              static_cast<float>(chain.frame.label.getWidth()) + kTextMargin);
    }
}

/// The macro editor on the grid: every control inside its pane, none overlapping, text fitting.
void lint(const fd::app::MacroEditor& editor)
{
    const auto& t = fd::ui::Theme::current();
    const auto bounds = editor.getLocalBounds();
    for (int pane = 0; pane < editor.paneCount(); ++pane)
    {
        INFO("pane " << pane);
        CHECK(bounds.contains(editor.paneBounds(pane)));
        const auto content = editor.paneContent(pane);
        std::vector<juce::Rectangle<int>> placed;
        for (const auto& p : editor.placements())
        {
            if (p.pane != pane || !p.component->isVisible())
                continue;
            const auto r = p.component->getBounds();
            INFO("'" << p.component->getName() << "' at (" << p.column << ", " << p.row << ") "
                     << r.toString());
            CHECK(!r.isEmpty());
            CHECK(content.contains(r));
            for (const auto& other : placed)
                CHECK_FALSE(other.intersects(r));
            placed.push_back(r);
            if (auto* knob = dynamic_cast<fd::ui::Knob*>(p.component); knob != nullptr && knob->textFromValue)
            {
                float widest = 0.0f;
                for (int i = 0; i <= 32; ++i)
                    widest = std::max(widest, textWidth(fd::ui::fonts::mono(t.textXs),
                                                        knob->textFromValue(static_cast<float>(i) / 32.0f)));
                CHECK(
                    textWidth(fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium), knob->getName()) +
                        widest + 16.0f <=
                    static_cast<float>(knob->getWidth()) + kTextMargin);
            }
        }
        for (auto* c : editor.titleComponents(pane))
        {
            CHECK(editor.paneTitleArea(pane).contains(c->getBounds()));
            CHECK_FALSE(c->getBounds().intersects(editor.paneTitleText(pane)));
        }
    }
}

/// The rack drawn: "Drop riser" after a Utility and before a Limiter; its chains "Main" (Filter,
/// Delay) and "Crushed" (Saturator, Chorus); macros Cut (the Filter's frequency), Wash (the
/// Delay's mix and feedback) and Drive (the Saturator's drive); five macros unmapped.
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SegnoLook look;
    Module utility{1, "utility"}, filter{11, "filter"}, delay{12, "delay"}, saturator{21, "saturator"},
        chorus{22, "chorus"}, limiter{2, "limiter"};
    fd::ParamStore macroStore{fd::engine::Session::rackMacros().params};
    RackBinding macroBinding{macroStore};
    fd::ui::DeviceUiContext macroContext{
        fd::engine::Session::rackMacros(), macroBinding, nullptr, nullptr, nullptr, nullptr};
    std::unique_ptr<RackPanel> rack;
    ChainStub chain;
    model::RackInfo info;

    Fixture()
    {
        rack = std::make_unique<RackPanel>(macroContext);
        rack->onEnabledChange = [](bool) {};
        rack->setFoldable(true);
        info.chains = {{.name = "Main", .devices = {filter.slot(), delay.slot()}},
                       {.name = "Crushed", .volumeDb = -6.0f, .devices = {saturator.slot(), chorus.slot()}}};
        info.macros[0] = {.name = "Cut",
                          .value = 0.3f,
                          .mappings = {{.module = 11,
                                        .param = "freq",
                                        .paramName = "Freq",
                                        .from = 1.0f,
                                        .to = 0.3f,
                                        .curve = 0.4f}}};
        info.macros[1] = {
            .name = "Wash",
            .value = 0.55f,
            .mappings = {{.module = 12, .param = "mix", .paramName = "Dry/Wet", .from = 0.1f, .to = 0.6f},
                         {.module = 12,
                          .param = "feedback",
                          .paramName = "Feedback",
                          .from = 0.2f,
                          .to = 0.75f,
                          .curve = -0.5f}}};
        info.macros[2] = {.name = "Drive",
                          .value = 0.7f,
                          .mappings = {{.module = 21,
                                        .param = "drive",
                                        .paramName = "Drive",
                                        .from = 0.0f,
                                        .to = 0.8f,
                                        .curve = 0.0f}}};
        for (int i = 0; i < model::RackInfo::macroCount; ++i)
            macroStore.set(static_cast<size_t>(i), info.macros[static_cast<size_t>(i)].value * 100.0f);
        // Each mapped parameter where its macro puts it, set by it.
        for (const auto& macro : info.macros)
            for (const auto& m : macro.mappings)
                for (auto* mod : {&filter, &delay, &saturator})
                    if (mod->id == m.module)
                    {
                        const auto p = mod->param(m.param);
                        mod->binding->mapped[p] = {juce::String(macro.name), m.from, m.to};
                        mod->device->params().setNormalized(
                            p, model::macroMapped(macro.value, m.from, m.to, m.curve));
                    }
        chain.before = {&utility};
        chain.after = {&limiter};
        chain.rack = rack.get();
        show(info);
        chain.build();
    }

    /// Shows `rack` as the chain view will: its panel, its shown chain's modules in the frame.
    void show(const model::RackInfo& r)
    {
        for (auto* m : {&filter, &delay, &saturator, &chorus})
            m->panel->setVisible(false);
        chain.inner.clear();
        if (!r.chains.empty())
            for (const auto& slot : r.chains[static_cast<size_t>(r.shownChain)].devices)
                for (auto* m : {&filter, &delay, &saturator, &chorus})
                    if (m->id == slot.id)
                        chain.inner.push_back(m);
        for (auto* m : chain.inner)
        {
            if (m->panel->getParentComponent() != &chain)
                chain.addAndMakeVisible(*m->panel);
            m->panel->setVisible(true);
        }
        chain.info = r;
        rack->setRack("Drop riser", r);
        refreshPanels();
        chain.layout();
    }

    /// What every panel's refresh tick does (no message loop here).
    void refreshPanels()
    {
        for (auto* m : {&utility, &filter, &delay, &saturator, &chorus, &limiter})
            for (auto* s : m->panel->sections())
                for (const auto& pl : s->placements())
                    if (auto* c = dynamic_cast<fd::ui::ParamControl*>(pl.component))
                        c->refresh();
        for (int i = 0; i < model::RackInfo::macroCount; ++i)
            rack->macroKnob(i).refresh();
        rack->morphKnob().refresh();
    }

    /// The pointer on macro `macro`'s name: the knobs it sets lit, the other macros' rings dimmed
    /// (-1: none).
    void hoverMacro(int macro)
    {
        for (auto* m : {&filter, &delay, &saturator})
            for (auto& [p, mapping] : m->binding->mapped)
                mapping.dimmed =
                    macro >= 0 && mapping.macro != juce::String(info.macros[static_cast<size_t>(macro)].name);
        refreshPanels();
        if (macro >= 0)
            rack->macroKnob(macro).setSourceHandle(true, true);
    }

    void shot(const juce::String& name)
    {
        chain.repaint();
        lint(chain);
        dump("rack-" + name, chain);
        // The frame and what's beside it, for a closer look.
        const int beside = fd::app::DeviceChain::gap * 4;
        dump("rack-" + name + "-frame", chain,
             chain.frame.bounds.expanded(beside, 0)
                 .withY(0)
                 .withHeight(chain.getHeight())
                 .getIntersection(chain.getLocalBounds()));
    }
};
} // namespace

TEST_CASE("racks, drawn for review: the chain, its frame and the rack's panel in every state (racks.md §5a)",
          "[ui]")
{
    Fixture f;
    const auto& t = fd::ui::Theme::current();

    // One chain: a group. No chain list; the frame names no chain.
    {
        auto one = f.info;
        one.chains.resize(1);
        f.show(one);
        CHECK_FALSE(f.rack->findSection("chains")->isVisible());
        CHECK(f.chain.frame.text.isEmpty());
        CHECK(f.chain.inner.size() == 2);
        f.shot("one-chain");
        // Selected, as clicked in its header.
        f.chain.selected = f.rack.get();
        f.shot("one-chain-selected");
        f.chain.selected = nullptr;
    }

    // Two chains, each shown in turn: the list marks the same chain the frame names.
    for (int shown : {0, 1})
    {
        auto two = f.info;
        two.shownChain = shown;
        f.show(two);
        CHECK(f.rack->findSection("chains")->isVisible());
        CHECK(f.chain.frame.text == fd::app::chainLabel(two, shown));
        f.shot("two-chains-" + juce::String(shown + 1));
    }
    // The shown chain soloed, then off: lit and dimmed wherever it shows.
    {
        auto solo = f.info;
        solo.shownChain = 1;
        solo.chains[1].solo = true;
        f.show(solo);
        CHECK(f.chain.frame.soloed);
        f.shot("two-chains-solo");
        solo.chains[1].solo = false;
        solo.chains[1].on = false;
        f.show(solo);
        CHECK(f.chain.frame.chainOff);
        f.shot("two-chains-off");
        f.show(f.info);
    }

    // Folded: one strip that says what it holds; its modules folded away with it.
    {
        auto two = f.info;
        f.show(two);
        f.rack->setFolded(true);
        f.chain.layout();
        CHECK(f.rack->getWidth() == t.deviceGrid.foldedWidth);
        CHECK(fd::app::rackContents(two) == "2 chains");
        f.shot("folded");
        auto one = two;
        one.chains.resize(1);
        f.show(one);
        CHECK(fd::app::rackContents(one) == "2 modules");
        f.shot("folded-one-chain");
        f.rack->setFolded(false);
        f.show(f.info);
    }

    // A module dragged in: the Limiter, after the rack, between the Filter and the Delay.
    {
        auto one = f.info;
        one.chains.resize(1);
        f.show(one);
        f.chain.dropInside = 1;
        f.shot("drag-in");
        f.chain.dropInside.reset();
        // Dragged out: the Delay, past the frame's end, into the track's chain.
        f.chain.dropAfter = f.rack.get();
        f.shot("drag-out");
        f.chain.dropAfter = nullptr;
    }

    // Mapped knobs: each shows its range as a ring and follows its macro; nothing drags it.
    {
        auto two = f.info;
        f.show(two);
        auto& freq = f.filter.knob("freq");
        CHECK(freq.isFollowing());
        CHECK(freq.ring().has_value());
        CHECK(freq.getTooltip().startsWith("Set by Cut."));
        CHECK_FALSE(f.filter.knob("res").isFollowing());
        // Hovering a macro: the knobs it sets lit, the rest dimmed.
        f.hoverMacro(1);
        CHECK(f.delay.knob("mix").ring()->dimmed == false);
        CHECK(f.filter.knob("freq").ring()->dimmed);
        f.shot("hover-macro");
        f.hoverMacro(-1);
        // Hovering a mapped knob: its macro lit.
        f.rack->setLitMacro(0);
        f.shot("hover-knob");
        f.rack->setLitMacro(-1);
        f.refreshPanels();
    }

    // A macro being mapped: its name dragged; the rack's knobs that could take it are marked, the
    // one under the pointer lit; nothing outside the rack is.
    {
        auto two = f.info;
        f.show(two);
        for (auto* m : f.chain.inner)
            for (auto* s : m->panel->sections())
                for (const auto& pl : s->placements())
                    if (auto* k = dynamic_cast<fd::ui::ParamKnob*>(pl.component);
                        k != nullptr && k->style() == fd::ui::Knob::Style::Rotary && !k->isFollowing())
                        k->setDropCandidate(true);
        f.filter.knob("res").setDropTarget(true);
        f.rack->macroKnob(3).setAlpha(1.0f);
        f.rack->macroKnob(3).setSourceHandle(true, true);
        f.shot("mapping");
        for (auto* m : f.chain.inner)
            for (auto* s : m->panel->sections())
                for (const auto& pl : s->placements())
                    if (auto* k = dynamic_cast<fd::ui::ParamKnob*>(pl.component))
                    {
                        k->setDropCandidate(false);
                        k->setDropTarget(false);
                    }
        f.show(f.info);
    }

    // An empty rack (Add module ▸ Rack): its panel, an empty chain to drop modules into.
    {
        model::RackInfo empty;
        empty.chains = {{.name = "Chain 1"}};
        for (int i = 0; i < model::RackInfo::macroCount; ++i)
            f.macroStore.set(static_cast<size_t>(i), 0.0f);
        f.show(empty);
        CHECK(fd::app::rackContents(empty) == "0 modules");
        CHECK_FALSE(f.chain.emptyHint.isEmpty());
        f.shot("empty");
    }
}

TEST_CASE("racks, drawn for review: the macro editor, narrow and wide (racks.md §6)", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SegnoLook look;
    Module filter{11, "filter"}, delay{12, "delay"}, saturator{21, "saturator"};
    model::RackInfo info;
    info.chains = {{.name = "Main", .devices = {filter.slot(), delay.slot()}},
                   {.name = "Crushed", .devices = {saturator.slot()}}};
    info.macros[1] = {
        .name = "Wash",
        .value = 0.55f,
        .mappings = {
            {.module = 12, .param = "mix", .paramName = "Dry/Wet", .from = 0.1f, .to = 0.6f},
            {.module = 12,
             .param = "feedback",
             .paramName = "Feedback",
             .from = 0.2f,
             .to = 0.75f,
             .curve = -0.5f},
            {.module = 11, .param = "freq", .paramName = "Freq", .from = 1.0f, .to = 0.35f, .curve = 0.6f}}};
    info.macros[0] = {.name = "Cut", .mappings = {{.module = 11, .param = "freq", .paramName = "Freq"}}};
    fd::app::MacroEditor editor(
        [&](const model::MacroMappingInfo& m, float v)
        {
            for (auto* mod : {&filter, &delay, &saturator})
                if (mod->id == m.module)
                {
                    const auto& spec = mod->context->info.params[mod->param(m.param)];
                    return juce::String(spec.toText(spec.fromNormalized(v)));
                }
            return juce::String();
        });
    editor.setRack("Drop riser", info);
    editor.selectMacro(1);
    CHECK(editor.tabName() == "Macros");
    CHECK(editor.title() == "Drop riser");
    CHECK(editor.minimumWidth() <= 1000); // in the DAW's narrowest window (1 024 px)

    for (const auto& [w, name] :
         {std::pair{editor.minimumWidth(), juce::String("compact")}, std::pair{1400, juce::String("wide")}})
    {
        editor.setSize(w, editor.minimumHeight());
        lint(editor);
        dump("rack-macro-editor-" + name, editor);
    }
    // A macro that maps nothing yet: what to do says so.
    editor.selectMacro(4);
    editor.setSize(editor.minimumWidth(), editor.minimumHeight());
    lint(editor);
    dump("rack-macro-editor-none", editor);
}

// ---- step 2: snapshots, Morph and locks (racks.md §18) ------------------------------------------

namespace
{
/// `rack` with snapshots named `names`, the first where the macros are; each next one moves the
/// three mapped macros along (Cut, Wash and Drive).
model::RackInfo withSnapshots(model::RackInfo rack, const std::vector<std::string>& names)
{
    for (size_t n = 0; n < names.size(); ++n)
    {
        model::RackInfo::Snapshot s{.name = names[n]};
        for (size_t i = 0; i < 3; ++i)
            s.macros[i] = std::clamp(
                rack.macros[i].value + 0.12f * static_cast<float>(n) * (i == 1 ? -1.0f : 1.0f), 0.0f, 1.0f);
        rack.snapshots.push_back(s);
    }
    rack.currentSnapshot = 0;
    rack.morph.a = names.size() > 1 ? 0 : -1;
    rack.morph.b = names.size() > 1 ? 1 : -1;
    return rack;
}
} // namespace

TEST_CASE("racks step 2, drawn for review: snapshots, Morph and locks in every state (racks.md §18.11)",
          "[ui]")
{
    Fixture f;
    const auto morphParam = static_cast<size_t>(model::RackInfo::morphParam);
    // As the host shows the macros while Morph sets them: each follows it, its range A to B a ring.
    const auto showMorph = [&](const model::RackInfo& r)
    {
        f.macroBinding.mapped.clear();
        for (int i = 0; i < model::RackInfo::macroCount; ++i)
        {
            if (!model::morphSets(r, i))
                continue;
            const auto& a = r.snapshots[static_cast<size_t>(r.morph.a)];
            const auto& b = r.snapshots[static_cast<size_t>(r.morph.b)];
            f.macroBinding.mapped[static_cast<size_t>(i)] = {
                juce::String::fromUTF8(("Morph: " + a.name + " \xe2\x86\x92 " + b.name).c_str()),
                a.macros[static_cast<size_t>(i)], b.macros[static_cast<size_t>(i)]};
            f.macroStore.set(static_cast<size_t>(i), 100.0f * model::morphPosition(r, i, r.morph.position));
        }
        f.macroStore.set(morphParam, 100.0f * r.morph.position);
    };
    const auto shotOf = [&](const model::RackInfo& r, const juce::String& name)
    {
        showMorph(r);
        f.show(r);
        f.shot(name);
    };

    // No snapshots: the panel as step 1 left it.
    f.show(f.info);
    CHECK_FALSE(f.rack->findSection("snapshots")->isVisible());
    CHECK_FALSE(f.rack->findSection("morph")->isVisible());

    // One: SNAPSHOTS shows, the current one lit; no MORPH yet (it needs two).
    {
        const auto one = withSnapshots(f.info, {"Verse"});
        f.show(one);
        CHECK(f.rack->findSection("snapshots")->isVisible());
        CHECK_FALSE(f.rack->findSection("morph")->isVisible());
        CHECK(f.rack->snapshotButton(0).getToggleState());
        CHECK_FALSE(f.rack->snapshotButton(0).isModified());
        CHECK(f.rack->storeButton().isVisible());
        shotOf(one, "snapshots-one");
    }

    // Three, the current one changed since: its mark. MORPH shows, off: the macros are free.
    {
        auto three = withSnapshots(f.info, {"Verse", "Build", "Drop"});
        three.currentSnapshot = 1;
        three.morph.b = 2;
        f.show(three);
        CHECK(f.rack->findSection("morph")->isVisible());
        CHECK(f.rack->snapshotButton(1).getToggleState());
        CHECK(f.rack->snapshotButton(1).isModified()); // Build moves the macros; they're where Verse is
        CHECK_FALSE(f.rack->findSection("morph")->isPoweredOn());
        CHECK(f.rack->morphA().getText() == "Verse");
        CHECK(f.rack->morphB().getText() == "Drop");
        CHECK_FALSE(f.rack->macroKnob(0).isFollowing());
        shotOf(three, "snapshots-three-changed");
    }

    // Eight: room for no more, so no "+ Store".
    {
        const auto eight = withSnapshots(
            f.info, {"Intro", "Verse", "Build", "Drop", "Break", "Verse 2", "Build 2", "Outro"});
        f.show(eight);
        CHECK_FALSE(f.rack->storeButton().isVisible());
        shotOf(eight, "snapshots-eight");
    }

    // Morph on, Verse to Drop at 40 %: every macro it sets follows it, its range a ring; no
    // snapshot is lit, Morph puts the macros where they are.
    auto morph = withSnapshots(f.info, {"Verse", "Build", "Drop"});
    morph.morph = {.on = true, .a = 0, .b = 2, .position = 0.4f};
    {
        showMorph(morph);
        f.show(morph);
        CHECK(f.rack->findSection("morph")->isPoweredOn());
        for (int i : {0, 1, 2})
        {
            INFO("macro " << i);
            CHECK(f.rack->macroKnob(i).isFollowing());
            CHECK(f.rack->macroKnob(i).ring().has_value());
            CHECK(f.rack->macroKnob(i).getTooltip().startsWith(
                juce::String::fromUTF8("Set by Morph: Verse \xe2\x86\x92 Drop.")));
        }
        CHECK_FALSE(f.rack->snapshotButton(0).getToggleState());
        CHECK(f.rack->morphKnob().getValue() == Catch::Approx(0.4f));
        f.shot("morph-on");
        // Automated: Morph's knob says so, as any automated parameter's does.
        f.macroBinding.automated[morphParam] = fd::ui::AutomationState::On;
        f.refreshPanels();
        f.rack->morphKnob().refresh();
        CHECK(f.rack->morphKnob().automationState() == fd::ui::AutomationState::On);
        f.shot("morph-automated");
        f.macroBinding.automated.clear();
        f.refreshPanels();
        // Hovering Morph's name: it's lit, and so are the macros it moves.
        f.rack->setMorphLit(true);
        for (int i : {0, 1, 2})
            f.rack->macroKnob(i).setSourceHandle(true, true);
        f.shot("morph-hover");
        f.rack->setMorphLit(false);
    }

    // Drive locked: a lock after its name; Morph leaves it alone, so it's free, without a ring.
    {
        auto locked = morph;
        locked.macros[2].locked = true;
        showMorph(locked);
        f.show(locked);
        f.rack->macroKnob(2).refresh();
        CHECK(f.rack->macroKnob(2).isLocked());
        CHECK_FALSE(f.rack->macroKnob(2).isFollowing());
        CHECK(f.rack->macroKnob(0).isFollowing());
        f.shot("locked");
        // Off again: the macros stay where Morph left them, free.
        locked.morph.on = false;
        showMorph(locked);
        f.show(locked);
        f.refreshPanels();
        CHECK_FALSE(f.rack->macroKnob(0).isFollowing());
        CHECK_FALSE(f.rack->findSection("morph")->isPoweredOn());
        f.shot("morph-off-locked");
    }

    // Folded: what it holds, as before; its snapshots don't fit the strip, and aren't what tells
    // racks apart there.
    {
        const auto three = withSnapshots(f.info, {"Verse", "Build", "Drop"});
        showMorph(three);
        f.show(three);
        f.rack->setFolded(true);
        f.chain.layout();
        CHECK(fd::app::rackContents(three) == "2 chains");
        f.shot("folded-snapshots");
        f.rack->setFolded(false);
    }
    f.macroBinding.mapped.clear();
    f.show(f.info);
}

TEST_CASE(
    "racks step 2, drawn for review: the macro editor's locks, and A and B in its curves (racks.md §18.7)",
    "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SegnoLook look;
    Module filter{11, "filter"}, delay{12, "delay"};
    model::RackInfo info;
    info.chains = {{.name = "Main", .devices = {filter.slot(), delay.slot()}}};
    info.macros[0] = {
        .name = "Cut",
        .value = 0.44f,
        .mappings = {{.module = 11, .param = "freq", .paramName = "Freq", .from = 1.0f, .to = 0.3f}}};
    info.macros[1] = {
        .name = "Wash",
        .value = 0.46f,
        .mappings = {{.module = 12, .param = "mix", .paramName = "Dry/Wet", .from = 0.1f, .to = 0.6f},
                     {.module = 12,
                      .param = "feedback",
                      .paramName = "Feedback",
                      .from = 0.2f,
                      .to = 0.75f,
                      .curve = -0.5f}}};
    info.macros[2] = {.name = "Drive", .value = 0.7f, .locked = true};
    info.snapshots = {{.name = "Verse", .macros = {0.2f, 0.25f, 0.7f}},
                      {.name = "Drop", .macros = {0.8f, 0.8f, 0.7f}}};
    info.morph = {.on = true, .a = 0, .b = 1, .position = 0.4f};
    fd::app::MacroEditor editor(
        [&](const model::MacroMappingInfo& m, float v)
        {
            for (auto* mod : {&filter, &delay})
                if (mod->id == m.module)
                {
                    const auto& spec = mod->context->info.params[mod->param(m.param)];
                    return juce::String(spec.toText(spec.fromNormalized(v)));
                }
            return juce::String();
        });
    editor.setRack("Verse to drop", info);
    editor.selectMacro(1);
    CHECK(editor.lockButton(2).getToggleState());
    CHECK_FALSE(editor.lockButton(1).getToggleState());
    for (const auto& [w, name] :
         {std::pair{editor.minimumWidth(), juce::String("compact")}, std::pair{1400, juce::String("wide")}})
    {
        editor.setSize(w, editor.minimumHeight());
        lint(editor);
        dump("rack-macro-editor-morph-" + name, editor);
    }
    // A click on a lock asks the host to lock it.
    std::optional<std::pair<int, bool>> asked;
    editor.onLockMacro = [&](int macro, bool locked) { asked = {macro, locked}; };
    editor.lockButton(0).triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(asked);
    CHECK(asked->first == 0);
    CHECK(asked->second);
}

// ---- the chain view itself, on a session (step 1) ---------------------------------------------

namespace
{
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

struct ChainOnSession
{
    juce::ScopedJuceInitialiser_GUI init;
    SegnoLook look;
    tracktion::Engine engine{"fd-rack-ui-tests", nullptr, fd::engine::makeEngineBehaviour()};
    std::unique_ptr<fd::engine::Session> session;
    fd::app::ActionRegistry actions;
    std::unique_ptr<fd::app::DeviceChain> chain;
    std::uint64_t track = 0;

    ChainOnSession()
    {
        fd::engine::registerDevices(engine);
        session = std::make_unique<fd::engine::Session>(engine);
        session->updateSnapshotNow();
        track = session->snapshot()->tracks[0].id;
        actions.add({.id = "app.scanPlugins", .name = "Scan", .perform = [] {}});
        actions.add({.id = "module.ungroupRack", .name = "Ungroup rack", .perform = [] {}});
        chain = std::make_unique<fd::app::DeviceChain>(*session, actions);
        chain->setSize(2400, fd::app::DeviceChain::preferredHeight());
    }

    ~ChainOnSession()
    {
        chain.reset();
        session.reset();
    }

    void show()
    {
        session->updateSnapshotNow();
        chain->setTrack(*session->snapshot()->channel(track));
    }

    fd::app::RackPanel* rackPanel()
    {
        std::vector<fd::app::RackPanel*> found;
        collect(*chain, found);
        return found.empty() ? nullptr : found.front();
    }

    /// The modules' panels shown (not the rack's), left to right.
    std::vector<DevicePanel*> modulePanels()
    {
        std::vector<DevicePanel*> panels, out;
        collect(*chain, panels);
        for (auto* p : panels)
            if (dynamic_cast<fd::app::RackPanel*>(p) == nullptr && p->isVisible())
                out.push_back(p);
        std::sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
        return out;
    }

    void shot(const juce::String& name)
    {
        auto& content = chain->content();
        dump("rack-chain-" + name, content);
    }
};
} // namespace

TEST_CASE(
    "racks in the chain view: a rack's frame holds its chain; modules dragged in; a macro dragged onto a "
    "knob maps it",
    "[ui]")
{
    ChainOnSession f;
    auto& s = *f.session;
    const auto eq = s.addDevice(f.track, "fd:eq");
    const auto saturator = s.addDevice(f.track, "fd:saturator");
    const auto delay = s.addDevice(f.track, "fd:delay");
    const auto rack = s.groupIntoRack({saturator, delay});
    REQUIRE(rack != 0);
    f.show();
    const auto& t = fd::ui::Theme::current();

    // The rack's panel, then its chain's modules in its frame; the EQ before it, outside.
    auto* panel = f.rackPanel();
    REQUIRE(panel != nullptr);
    auto modules = f.modulePanels();
    REQUIRE(modules.size() == 3);
    auto frames = f.chain->frames();
    REQUIRE(frames.size() == 1);
    const auto inner = frames[0].bounds.reduced(t.shell.rackInset);
    CHECK(inner.contains(panel->getBounds()));
    CHECK_FALSE(frames[0].bounds.intersects(modules[0]->getBounds())); // the EQ
    CHECK(inner.contains(modules[1]->getBounds()));
    CHECK(inner.contains(modules[2]->getBounds()));
    CHECK(modules[0]->getRight() + fd::app::DeviceChain::gap <= frames[0].bounds.getX());
    CHECK(frames[0].text.isEmpty()); // one chain: a group
    CHECK(f.chain->content().getHeight() <= fd::app::DeviceChain::preferredHeight());
    f.shot("one-chain");

    // A second chain: the list shows, and the frame names the chain shown (the new, empty one).
    REQUIRE(s.addRackChain(rack) == 1);
    f.show();
    frames = f.chain->frames();
    CHECK(frames[0].text == "Chain 2");
    CHECK(f.modulePanels().size() == 1); // the EQ alone: chain 2 is empty
    f.shot("two-chains-empty");
    s.showRackChain(rack, 0);
    f.show();
    CHECK(f.chain->frames()[0].text == "Chain 1");
    CHECK(f.modulePanels().size() == 3);

    // The EQ dragged by its header into the frame, between the Saturator and the Delay.
    modules = f.modulePanels();
    auto* eqPanel = modules[0];
    const juce::Point<float> grab(60.0f, 10.0f);
    const auto between = static_cast<float>(modules[2]->getX() - eqPanel->getX()) - 2.0f;
    const auto to = grab + juce::Point<float>(between, 0.0f);
    f.chain->mouseDown(mouseAt(*eqPanel, grab, grab, false));
    f.chain->mouseDrag(mouseAt(*eqPanel, to, grab, true));
    f.chain->content().repaint();
    f.shot("drag-in");
    f.chain->mouseUp(mouseAt(*eqPanel, to, grab, true));
    {
        const auto place = s.placeOf(eq);
        REQUIRE(place);
        CHECK(place->rack == rack);
        CHECK(place->chain == 0);
        CHECK(place->index == 1);
    }
    f.show();

    // Cut's name dragged onto the Delay's first knob: mapped, From where it was.
    modules = f.modulePanels();
    std::vector<fd::ui::ParamKnob*> knobs;
    collect(*modules.back(), knobs);
    knobs.erase(std::remove_if(knobs.begin(), knobs.end(), [](auto* k)
                               { return !k->isVisible() || k->style() != fd::ui::Knob::Style::Rotary; }),
                knobs.end());
    REQUIRE_FALSE(knobs.empty());
    auto* target = knobs.front();
    const auto centre = target->getLocalBounds().getCentre().toFloat();
    REQUIRE(panel->onMacroDrag);
    panel->onMacroDrag(0, mouseAt(*target, centre, centre, true));
    CHECK(s.macroPointer().dragging);
    CHECK(s.macroPointer().module == delay);
    for (auto* k : knobs) // as the panel's refresh tick does
        k->refresh();
    CHECK(target->isDropTarget());
    f.shot("mapping");
    panel->onMacroDrop(0, mouseAt(*target, centre, centre, true));
    CHECK_FALSE(s.macroPointer().dragging);
    s.updateSnapshotNow();
    const auto* info = model::findDevice(s.snapshot()->channel(f.track)->devices, rack);
    REQUIRE(info != nullptr);
    REQUIRE(info->rack->macros[0].mappings.size() == 1);
    CHECK(info->rack->macros[0].mappings[0].module == delay);
    f.show();
    // Its knob now follows the macro, its range a ring.
    for (auto* k : knobs)
        k->refresh();
    CHECK(target->isFollowing());
    CHECK(target->ring().has_value());
    f.shot("mapped");

    // Folded: one strip in its frame; its modules away with it.
    panel->setFolded(true, true);
    f.show();
    CHECK(f.modulePanels().empty());
    f.shot("folded");
}

TEST_CASE(
    "racks in the chain view, step 2: snapshots stored and recalled from the rack's panel, Morph switched "
    "on there, and a macro locked from the macro editor (racks.md §18)",
    "[ui]")
{
    ChainOnSession f;
    auto& s = *f.session;
    const auto utility = s.addDevice(f.track, "fd:utility");
    const auto rack = s.groupIntoRack({utility});
    REQUIRE(rack != 0);
    REQUIRE(s.mapMacro(rack, 0, utility, "gain", 0.2f, 0.8f));
    REQUIRE(s.setMacro(rack, 0, 0.3f));
    f.show();
    auto* panel = f.rackPanel();
    REQUIRE(panel != nullptr);
    CHECK_FALSE(panel->findSection("snapshots")->isVisible());

    // "+ Store" isn't there before the first: the rack's right-click and the Module menu store it.
    REQUIRE(s.storeSnapshot(rack) == 0);
    f.show();
    panel = f.rackPanel();
    CHECK(panel->findSection("snapshots")->isVisible());
    CHECK_FALSE(panel->findSection("morph")->isVisible());
    // The second, from its "+ Store": MORPH shows, A and B the two.
    REQUIRE(s.setMacro(rack, 0, 0.9f));
    f.show();
    panel = f.rackPanel();
    REQUIRE(panel->storeButton().onClick);
    panel->storeButton().onClick();
    f.show();
    panel = f.rackPanel();
    CHECK(panel->findSection("morph")->isVisible());
    CHECK(panel->morphA().getText() == "Snapshot 1");
    CHECK(panel->morphB().getText() == "Snapshot 2");
    CHECK(panel->snapshotButton(1).getToggleState());

    // A click on the first recalls it.
    panel->snapshotButton(0).onClick();
    f.show();
    panel = f.rackPanel();
    CHECK(panel->snapshotButton(0).getToggleState());
    CHECK(panel->macroKnob(0).getValue() == Catch::Approx(0.3f).margin(1e-3));

    // MORPH's power: on, the macro follows it, its range from A to B a ring; the knob won't drag.
    auto* power = dynamic_cast<fd::ui::Toggle*>(panel->findSection("morph")->powerToggle());
    REQUIRE(power != nullptr);
    power->setToggleState(true, juce::dontSendNotification);
    power->onClick();
    f.show();
    panel = f.rackPanel();
    CHECK(model::findDevice(s.snapshot()->channel(f.track)->devices, rack)->rack->morph.on);
    CHECK(panel->findSection("morph")->isPoweredOn());
    panel->macroKnob(0).refresh();
    CHECK(panel->macroKnob(0).isFollowing());
    REQUIRE(panel->macroKnob(0).ring().has_value());
    CHECK(panel->macroKnob(0).ring()->from == Catch::Approx(0.3f).margin(1e-3));
    CHECK(panel->macroKnob(0).ring()->to == Catch::Approx(0.9f).margin(1e-3));
    CHECK_FALSE(panel->snapshotButton(0).getToggleState()); // Morph puts the macros where they are
    f.shot("step2-morph-on");

    // Locked from the macro editor: Morph lets go of it.
    std::unique_ptr<juce::Component> shown;
    f.chain->onShowEditor = [&](std::uint64_t, std::unique_ptr<fd::ui::ModuleEditor> editor)
    { shown = std::move(editor); };
    panel->onEditMacros(0);
    auto* editor = dynamic_cast<fd::app::MacroEditor*>(shown.get());
    REQUIRE(editor != nullptr);
    REQUIRE(editor->onLockMacro);
    editor->onLockMacro(0, true);
    f.show();
    panel = f.rackPanel();
    panel->macroKnob(0).refresh();
    CHECK(panel->macroKnob(0).isLocked());
    CHECK_FALSE(panel->macroKnob(0).isFollowing());
    f.shot("step2-locked");
}
