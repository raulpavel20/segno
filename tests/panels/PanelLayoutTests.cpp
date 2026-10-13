// The module panel layout lint (ADR 0022, docs/design/device-panels.md). Builds every
// registered panel and checks it against the module grid: expanded, with each section
// collapsed, with all collapsed, and folded. A module isn't done until this passes.

#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

using fd::ui::DevicePanel;
using fd::ui::PanelSection;

namespace
{
// Text measurements carry a margin: font rasterisers differ slightly across platforms.
constexpr float kTextMargin = 1.5f;
constexpr float kKnobNameMax = 46.0f;

/// A host's sidechain sources (ADR 0025), with a long track name: the picker must still fit.
struct StubSidechain final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Drums", "A very long track name indeed"}; }
    int selected() const override { return choice; }
    void select(int index) override { choice = index; }
    int choice = 2;
};

/// A host's presets (ADR 0044) with a long name loaded: the header must still lay out.
struct StubPresets final : fd::PresetHost
{
    fd::PresetLibrary presets{{}, std::filesystem::temp_directory_path() / "fd-panel-tests-no-presets"};
    fd::PresetMemo current{.preset = "user/Somewhere/A preset with a name far too long for its field"};
    const fd::Device* device = nullptr;
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override { return fd::captureState(*device); }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

struct PanelUnderTest
{
    StubSidechain sidechain;
    StubPresets presets;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<DevicePanel> panel;

    PanelUnderTest(const fd::DeviceFactory& factory, const fd::ui::PanelFactory& panelFactory, bool foldable,
                   const std::function<void(fd::Device&)>& setup = {})
    {
        device = factory.create();
        if (factory.testSetup != nullptr)
            factory.testSetup(*device);
        if (setup)
            setup(*device);
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        presets.device = device.get();
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory.info(), *binding, nullptr, device.get(), &sidechain, &presets});
        panel = panelFactory.create(*context);
        panel->onEnabledChange = [](bool) {}; // the power toggle shows, as in hosts
        panel->setFoldable(foldable);
        layout();
    }

    void layout() const { panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight); }
};

float width(const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth(font, text);
}

/// Every descendant of `root`, depth first.
void descendants(juce::Component& root, std::vector<juce::Component*>& out)
{
    for (auto* c : root.getChildren())
    {
        out.push_back(c);
        descendants(*c, out);
    }
}

void checkPlacementText(const PanelSection::Placement& p)
{
    const auto& t = fd::ui::Theme::current();
    auto* c = p.component;
    INFO("control '" << c->getName() << "' / " << c->getTitle() << " at (" << p.column << ", " << p.row << ")");
    if (auto* knob = dynamic_cast<fd::ui::Knob*>(c))
    {
        const auto nameFont = fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium);
        if (knob->style() == fd::ui::Knob::Style::Rotary)
        {
            // A source's name is cut to leave room for its handle (a macro named by you).
            if (knob->handleArea().isEmpty())
                CHECK(width(nameFont, knob->getName()) <= kKnobNameMax + kTextMargin);
            // A modulated knob's ring (device-panels.md, "Modulation"): inside its cell, clear of
            // its name and value.
            const auto ring = knob->ringBounds();
            CHECK(knob->getLocalBounds().toFloat().contains(ring));
            CHECK_FALSE(ring.intersects(knob->nameStrip()));
            CHECK_FALSE(ring.intersects(knob->valueStrip()));
            return;
        }
        // A bar: the name on the left, the widest value on the right.
        float widest = 0.0f;
        for (int i = 0; i <= 32 && knob->textFromValue; ++i)
            widest = std::max(widest, width(fd::ui::fonts::mono(t.textXs), knob->textFromValue(static_cast<float>(i) / 32.0f)));
        CHECK(width(nameFont, knob->getName()) + widest + 16.0f <= static_cast<float>(c->getWidth()) + kTextMargin);
        return;
    }
    if (auto* combo = dynamic_cast<juce::ComboBox*>(c))
    {
        if (combo->getName() == "sidechain")
            return; // track names: user content, cut to the picker's fixed width
        const auto font = combo->getLookAndFeel().getComboBoxFont(*combo);
        for (int i = 0; i < combo->getNumItems(); ++i)
        {
            INFO("choice " << combo->getItemText(i));
            CHECK(width(font, combo->getItemText(i)) + 26.0f <= static_cast<float>(c->getWidth()) + kTextMargin);
        }
        return;
    }
    const auto toggleFont = fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::SemiBold);
    if (auto* segmented = dynamic_cast<fd::ui::ParamSegmented*>(c))
    {
        for (auto* b : segmented->buttons())
        {
            INFO("segment " << b->getButtonText());
            CHECK(width(toggleFont, b->getButtonText()) + 6.0f <= static_cast<float>(b->getWidth()) + kTextMargin);
            CHECK(b->getHeight() == fd::ui::Theme::current().deviceGrid.row);
        }
        return;
    }
    if (auto* toggle = dynamic_cast<fd::ui::Toggle*>(c))
        CHECK(width(toggleFont, toggle->getButtonText()) + 8.0f <= static_cast<float>(c->getWidth()) + kTextMargin);
}

/// The grid rules for one panel in its current state.
void lint(const PanelUnderTest& p)
{
    const auto& t = fd::ui::Theme::current();
    const auto& grid = t.deviceGrid;
    auto& panel = *p.panel;

    REQUIRE(panel.getHeight() == t.devicePanelHeight);
    CHECK(panel.preferredWidth() % t.grid == 0);

    if (panel.isFolded())
    {
        CHECK(panel.preferredWidth() == grid.foldedWidth);
        for (auto* s : panel.sections())
            CHECK_FALSE(s->isVisible());
        CHECK_FALSE(panel.presetBar()->isVisible());
        return;
    }
    CHECK(panel.headerMinimumWidth() <= panel.getWidth());
    // A sidechain bus brings the host's picker into the header (ADR 0025), inside the panel.
    if (p.context->info.buses.sidechainChannels > 0)
    {
        REQUIRE_FALSE(panel.headerControls().empty());
        const auto* picker = panel.headerControls().front();
        CHECK(picker->isVisible());
        CHECK(panel.getLocalBounds().contains(picker->getBounds()));
        CHECK(dynamic_cast<const juce::ComboBox*>(picker)->getText() == "A very long track name indeed");
    }

    // Width: the widest page's sections and the meter strip, at least what the header needs.
    int widest = 0;
    for (int page = 0; page < std::max(1, panel.pageCount()); ++page)
    {
        int sum = 0;
        for (auto* s : panel.pageSections(page))
            sum += s->preferredWidth();
        widest = std::max(widest, sum);
    }
    const auto meters = panel.meters();
    if (!meters.empty())
        widest += grid.meterStripWidth(static_cast<int>(meters.size()));
    CHECK(panel.preferredWidth() == std::max(widest, panel.headerMinimumWidth()));

    // Only the page's sections show (ADR 0051).
    const auto shown = panel.shownSections();
    for (auto* s : panel.sections())
        CHECK(s->isVisible() == (std::find(shown.begin(), shown.end(), s) != shown.end()));

    // Sections: side by side from the left, the full body high.
    int x = 0;
    for (auto* s : shown)
    {
        INFO("section " << s->id());
        CHECK(s->getX() == x);
        CHECK(s->getY() == t.deviceHeaderHeight);
        CHECK(s->getWidth() == s->preferredWidth());
        CHECK(s->getHeight() == grid.bodyHeight());
        x += s->getWidth();

        if (s->isCollapsed())
        {
            CHECK(s->getWidth() == grid.collapsedWidth);
            continue;
        }
        // The title fits, with the power toggle.
        const float titleWidth = width(fd::ui::capsLabelFont(), s->sectionTitle().toUpperCase()) +
                                 (s->powerToggle() != nullptr ? 18.0f : 0.0f);
        CHECK(titleWidth <= static_cast<float>(grid.width(s->columns())) + kTextMargin);

        // Placements: whole cells inside the section, never sharing a cell.
        std::set<std::pair<int, int>> taken;
        for (const auto& pl : s->placements())
        {
            INFO("control '" << pl.component->getName() << "' at (" << pl.column << ", " << pl.row << ") span "
                             << pl.columns << " x " << pl.rows);
            CHECK(pl.column >= 0);
            CHECK(pl.row >= 0);
            CHECK(pl.column + pl.columns <= s->columns());
            CHECK(pl.row + pl.rows <= grid.rows);
            CHECK(pl.component->getBounds() == PanelSection::cellBounds(pl.column, pl.row, pl.columns, pl.rows));
            for (int c = pl.column; c < pl.column + pl.columns; ++c)
                for (int r = pl.row; r < pl.row + pl.rows; ++r)
                    CHECK(taken.insert({c, r}).second); // no overlaps
            switch (pl.kind)
            {
            case PanelSection::Kind::Knob:
                CHECK(pl.component->getWidth() == grid.column);
                CHECK(pl.component->getHeight() == grid.knobHeight());
                CHECK((pl.row == 0 || pl.row == grid.knobRows));
                break;
            case PanelSection::Kind::Line:
                if (dynamic_cast<juce::ComboBox*>(pl.component) != nullptr)
                    CHECK(pl.columns >= 2);
                if (auto* k = dynamic_cast<fd::ui::Knob*>(pl.component))
                    CHECK((k->style() == fd::ui::Knob::Style::Bar && pl.columns >= 2));
                if (dynamic_cast<fd::ui::ParamSegmented*>(pl.component) == nullptr)
                    CHECK(pl.rows == 1);
                break;
            case PanelSection::Kind::Display:
            case PanelSection::Kind::Label: break;
            }
            checkPlacementText(pl);
        }
    }

    // The meter strip.
    for (auto* m : meters)
    {
        CHECK(m->getWidth() == grid.meterWidth);
        CHECK(m->getHeight() == grid.height(grid.rows));
        CHECK(m->getX() >= x);
        CHECK(m->getRight() <= panel.getWidth());
    }

    // Header settings stay in the header, clear of the name.
    for (auto* c : panel.headerControls())
    {
        if (!c->isVisible())
            continue; // a latency badge while there's no delay: no room taken
        if (auto* badge = dynamic_cast<fd::ui::LatencyBadge*>(c))
            CHECK(width(fd::ui::font(fd::ui::TextStyle::Control), badge->text()) <=
                  static_cast<float>(c->getWidth() - 2 * grid.displayInset) + kTextMargin);
        CHECK(c->getY() >= 0);
        CHECK(c->getBottom() <= t.deviceHeaderHeight);
        CHECK(c->getHeight() == grid.row);
        CHECK(c->getRight() <= panel.getWidth());
        checkPlacementText({c, PanelSection::Kind::Line, 0, 0, 2, 1});
    }

    // The presets (ADR 0044): in the header between the name and the settings, touching neither,
    // the field between its narrowest and widest, its parts apart.
    {
        INFO("presets");
        auto* bar = panel.presetBar();
        REQUIRE(bar != nullptr);
        CHECK(bar->isVisible());
        CHECK(bar->getY() >= 0);
        CHECK(bar->getBottom() <= t.deviceHeaderHeight);
        CHECK(bar->getWidth() >= bar->minimumWidth());
        CHECK(bar->getWidth() <= bar->maximumWidth());
        CHECK(bar->getRight() <= panel.getWidth() - grid.headerPadding);
        const auto name =
            width(fd::ui::font(fd::ui::TextStyle::Title), juce::String(std::string(p.context->info.name)));
        CHECK(name <= static_cast<float>(panel.nameArea().getWidth()) + kTextMargin);
        CHECK(panel.nameArea().getRight() < bar->getX());
        for (auto* c : panel.headerControls())
            if (c->isVisible())
                CHECK(bar->getRight() < c->getX());
        // The page tabs: right after the name, before the presets, touching neither.
        if (const auto* tabs = panel.pageTabs())
        {
            INFO("page tabs");
            CHECK(tabs->isVisible());
            CHECK(tabs->getY() >= 0);
            CHECK(tabs->getBottom() <= t.deviceHeaderHeight);
            CHECK(tabs->getWidth() == tabs->preferredWidth());
            CHECK(panel.nameArea().getRight() < tabs->getX());
            CHECK(tabs->getRight() < bar->getX());
            CHECK(tabs->tabs().size() == panel.pageCount());
            CHECK(tabs->selected() == panel.page());
        }
        const auto field = bar->field().getWidth();
        CHECK(field >= grid.presetFieldMin);
        CHECK(field <= grid.presetFieldMax);
        CHECK(bar->field().getHeight() == grid.row);
        std::vector<juce::Rectangle<int>> parts;
        for (auto* c : bar->getChildren())
        {
            CHECK(bar->getLocalBounds().contains(c->getBounds()));
            for (const auto& other : parts)
                CHECK_FALSE(other.intersects(c->getBounds()));
            parts.push_back(c->getBounds());
        }
        CHECK(width(fd::ui::font(fd::ui::TextStyle::Control), "B") + 8.0f <=
              static_cast<float>(bar->compareButton().getWidth()) + kTextMargin);
    }

    // Every bound control lives in a section or the header: nothing hand-placed.
    std::vector<juce::Component*> all;
    descendants(panel, all);
    const auto& header = panel.headerControls();
    for (auto* c : all)
        if (dynamic_cast<fd::ui::ParamControl*>(c) != nullptr && c->isVisible())
        {
            INFO("control '" << c->getName() << "' / " << c->getTitle());
            const bool inHeader = std::find(header.begin(), header.end(), c) != header.end();
            CHECK((inHeader || c->findParentComponentOfClass<PanelSection>() != nullptr));
        }
}
} // namespace

TEST_CASE("module grid tokens add up to the panel height", "[ui]")
{
    const auto& t = fd::ui::Theme::current();
    const auto& grid = t.deviceGrid;
    CHECK(t.deviceHeaderHeight + grid.bodyHeight() == t.devicePanelHeight);
    CHECK(grid.height(grid.knobRows) == grid.knobName + 44 + grid.knobValue); // the knob block
    CHECK(grid.width(1) == grid.column);
    CHECK(grid.height(2 * grid.knobRows) == grid.height(grid.rows)); // two knob rows fill the content
    CHECK(grid.meterStripWidth(1) == 24);
    CHECK(grid.meterStripWidth(2) == 44);
}

TEST_CASE("every module panel is laid out on the module grid", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);

    for (const auto* panelFactoryPtr : fd::ui::allPanels())
    {
        const auto& panelFactory = *panelFactoryPtr;
        const auto* factory = fd::findDevice(panelFactory.deviceId);
        REQUIRE(factory != nullptr);
        DYNAMIC_SECTION(std::string(panelFactory.deviceId))
        {
            PanelUnderTest p(*factory, panelFactory, true);
            REQUIRE_FALSE(p.panel->sections().empty()); // every panel is declared as sections
            const auto ids = [&] {
                std::set<std::string> out;
                for (auto* s : p.panel->sections())
                    out.insert(s->id());
                return out;
            }();
            CHECK(ids.size() == p.panel->sections().size()); // unique ids

            // Every page (ADR 0051), at one width.
            const int pages = std::max(1, p.panel->pageCount());
            const int width = p.panel->preferredWidth();
            for (int page = 0; page < pages; ++page)
            {
                INFO("page " << page);
                p.panel->setPage(page);
                p.layout();
                CHECK(p.panel->preferredWidth() == width);
                INFO("expanded");
                lint(p);
                for (auto* s : p.panel->shownSections())
                {
                    INFO("collapsed alone: " << s->id());
                    s->setCollapsed(true);
                    p.layout();
                    lint(p);
                    s->setCollapsed(false);
                }
                {
                    INFO("all collapsed");
                    p.panel->setAllSectionsCollapsed(true);
                    p.layout();
                    lint(p);
                    p.panel->setAllSectionsCollapsed(false);
                }
                {
                    INFO("folded");
                    p.panel->setFolded(true);
                    p.layout();
                    lint(p);
                    p.panel->setFolded(false);
                    p.layout();
                }
            }
            p.panel->setPage(0);
            p.layout();
            if (p.panel->pageCount() > 0)
            {
                // Every section on exactly one page, page ids unique.
                std::set<std::string> pageIds;
                size_t onPages = 0;
                for (int page = 0; page < p.panel->pageCount(); ++page)
                {
                    pageIds.insert(p.panel->pageId(page));
                    onPages += p.panel->pageSections(page).size();
                }
                CHECK(pageIds.size() == static_cast<size_t>(p.panel->pageCount()));
                CHECK(onPages == p.panel->sections().size());
            }
            // The EQ delaying its sound (linear phase, a spectral band selected): the latency badge
            // in the header, Detail in DYNAMICS.
            if (panelFactory.deviceId == "eq")
            {
                INFO("linear phase, a spectral band");
                PanelUnderTest q(*factory, panelFactory, true,
                                 [](fd::Device& d)
                                 {
                                     auto& params = d.params();
                                     for (const auto* id : {"phase", "band2.dyn", "band2.spectral"})
                                         params.set(*params.indexOf(id), 1.0f);
                                 });
                const auto& header = q.panel->headerControls();
                CHECK(std::any_of(
                    header.begin(), header.end(), [](auto* c)
                    { return dynamic_cast<fd::ui::LatencyBadge*>(c) != nullptr && c->isVisible(); }));
                lint(q);
            }
        }
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

TEST_CASE("panel view state round-trips and tolerates bad input", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (const auto* panelFactoryPtr : fd::ui::allPanels())
    {
        const auto& panelFactory = *panelFactoryPtr;
        const auto* factory = fd::findDevice(panelFactory.deviceId);
        PanelUnderTest p(*factory, panelFactory, true);
        const auto sections = p.panel->sections();
        if (sections.empty())
            continue;
        DYNAMIC_SECTION(std::string(panelFactory.deviceId))
        {
            CHECK(p.panel->viewState().empty()); // all default

            int changes = 0;
            p.panel->onViewChanged = [&] { ++changes; };
            sections.front()->setCollapsed(true, true);
            p.panel->setFolded(true, true);
            CHECK(changes == 2);
            const auto state = p.panel->viewState();
            CHECK(state.find(sections.front()->id()) != std::string::npos);

            PanelUnderTest other(*factory, panelFactory, true);
            other.panel->setViewState(state);
            CHECK(other.panel->viewState() == state);
            CHECK(other.panel->isFolded());
            CHECK(other.panel->sections().front()->isCollapsed());

            // Not foldable (a plugin editor): the fold is ignored, collapsed sections stay.
            PanelUnderTest plugin(*factory, panelFactory, false);
            plugin.panel->setViewState(state);
            CHECK_FALSE(plugin.panel->isFolded());
            CHECK(plugin.panel->sections().front()->isCollapsed());

            // The page shown (ADR 0051): stored by id, the first page left out.
            if (p.panel->pageCount() > 1)
            {
                p.panel->setPage(1, true);
                CHECK(changes == 3);
                const auto paged = p.panel->viewState();
                CHECK(paged.find(p.panel->pageId(1)) != std::string::npos);
                PanelUnderTest turned(*factory, panelFactory, true);
                turned.panel->setViewState(paged);
                CHECK(turned.panel->page() == 1);
                turned.panel->setViewState(R"({"page": "no-such-page"})");
                CHECK(turned.panel->page() == 0);
            }

            for (const auto* bad : {"", "nonsense", "[1,2]", R"({"collapsed": "shape", "folded": 3})",
                                    R"({"collapsed": ["no-such-section"]})", R"({"page": 2})"})
            {
                INFO(bad);
                other.panel->setViewState(bad);
                CHECK(other.panel->viewState().empty());
            }
        }
    }
}
