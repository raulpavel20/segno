// The module panel layout lint (ADR 0022, docs/design/device-panels.md). Builds every
// registered panel and checks it against the module grid: expanded, with each section
// collapsed, with all collapsed, and folded. A module isn't done until this passes.

#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

using fd::ui::DevicePanel;
using fd::ui::PanelSection;

namespace
{
// Text measurements carry a margin: font rasterisers differ slightly across platforms.
constexpr float kTextMargin = 1.5f;
constexpr float kKnobNameMax = 46.0f;

struct PanelUnderTest
{
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<DevicePanel> panel;

    PanelUnderTest(const fd::DeviceFactory& factory, const fd::ui::PanelFactory& panelFactory, bool foldable)
    {
        device = factory.create();
        if (factory.testSetup != nullptr)
            factory.testSetup(*device);
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(fd::ui::DeviceUiContext{factory.info(), *binding, nullptr, device.get()});
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
            CHECK(width(nameFont, knob->getName()) <= kKnobNameMax + kTextMargin);
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
        return;
    }
    CHECK(panel.headerMinimumWidth() <= panel.getWidth());

    // Width: the sections and the meter strip, at least what the header needs.
    int sum = 0;
    for (auto* s : panel.sections())
        sum += s->preferredWidth();
    const auto meters = panel.meters();
    if (!meters.empty())
        sum += grid.meterStripWidth(static_cast<int>(meters.size()));
    CHECK(panel.preferredWidth() == std::max(sum, panel.headerMinimumWidth()));

    // Sections: side by side from the left, the full body high.
    int x = 0;
    for (auto* s : panel.sections())
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
        CHECK(c->getY() >= 0);
        CHECK(c->getBottom() <= t.deviceHeaderHeight);
        CHECK(c->getHeight() == grid.row);
        CHECK(c->getRight() <= panel.getWidth());
        checkPlacementText({c, PanelSection::Kind::Line, 0, 0, 2, 1});
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
    CHECK(grid.meterStripWidth(2) == 40);
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
            if (p.panel->sections().empty())
            {
                WARN(std::string(panelFactory.deviceId) << " is not on the module grid yet");
                continue;
            }
            const auto ids = [&] {
                std::set<std::string> out;
                for (auto* s : p.panel->sections())
                    out.insert(s->id());
                return out;
            }();
            CHECK(ids.size() == p.panel->sections().size()); // unique ids

            INFO("expanded");
            lint(p);
            for (auto* s : p.panel->sections())
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

            for (const auto* bad : {"", "nonsense", "[1,2]", R"({"collapsed": "shape", "folded": 3})",
                                    R"({"collapsed": ["no-such-section"]})"})
            {
                INFO(bad);
                other.panel->setViewState(bad);
                CHECK(other.panel->viewState().empty());
            }
        }
    }
}
