#pragma once

// The module panel layout lint (ADR 0022, docs/design/device-panels.md) for any panel on the
// module grid: every registered module's (PanelLayoutTests), and the DAW's own panels drawn in
// their tests (a modulator's, modulators.md §19).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fd_device_ui/fd_device_ui.h>
#include <set>
#include <vector>

namespace fd::test
{
using fd::ui::DevicePanel;
using fd::ui::PanelSection;

// Text measurements carry a margin: font rasterisers differ slightly across platforms.
inline constexpr float kTextMargin = 1.5f;
inline constexpr float kKnobNameMax = 46.0f;

inline float width(const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth(font, text);
}

/// Every descendant of `root`, depth first.
inline void descendants(juce::Component& root, std::vector<juce::Component*>& out)
{
    for (auto* c : root.getChildren())
    {
        out.push_back(c);
        descendants(*c, out);
    }
}

inline void checkPlacementText(const PanelSection::Placement& p)
{
    const auto& t = fd::ui::Theme::current();
    auto* c = p.component;
    INFO("control '" << c->getName() << "' / " << c->getTitle() << " at (" << p.column << ", " << p.row
                     << ")");
    if (auto* knob = dynamic_cast<fd::ui::Knob*>(c))
    {
        const auto nameFont = fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium);
        if (knob->style() == fd::ui::Knob::Style::Rotary)
        {
            // A source's name is its handle; a name of your own is cut to fit (a macro), but the
            // defaults fit whole.
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
            widest = std::max(widest, width(fd::ui::fonts::mono(t.textXs),
                                            knob->textFromValue(static_cast<float>(i) / 32.0f)));
        CHECK(width(nameFont, knob->getName()) + widest + 16.0f <=
              static_cast<float>(c->getWidth()) + kTextMargin);
        return;
    }
    if (auto* combo = dynamic_cast<juce::ComboBox*>(c))
    {
        if (combo->getName() == "sidechain")
            return; // track names: user content, cut to the picker's fixed width
        const auto font = combo->getLookAndFeel().getComboBoxFont(*combo);
        // A combo with its name inside: the name, a text button's padding, then the choice.
        float name = 0.0f;
        if (auto* param = dynamic_cast<fd::ui::ChoiceBox*>(combo);
            param != nullptr && param->inlineName().isNotEmpty())
            name = width(fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium), param->inlineName()) +
                   static_cast<float>(t.shell.buttonTextPadding);
        for (int i = 0; i < combo->getNumItems(); ++i)
        {
            INFO("choice " << combo->getItemText(i));
            CHECK(name + width(font, combo->getItemText(i)) + 26.0f <=
                  static_cast<float>(c->getWidth()) + kTextMargin);
        }
        return;
    }
    const auto toggleFont = fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::SemiBold);
    const auto checkSegments = [&](const juce::OwnedArray<fd::ui::Toggle>& buttons)
    {
        for (auto* b : buttons)
        {
            INFO("segment " << b->getButtonText());
            CHECK(width(toggleFont, b->getButtonText()) + 6.0f <=
                  static_cast<float>(b->getWidth()) + kTextMargin);
            CHECK(b->getHeight() == fd::ui::Theme::current().deviceGrid.row);
        }
    };
    if (auto* segmented = dynamic_cast<fd::ui::ParamSegmented*>(c))
        return checkSegments(segmented->buttons());
    if (auto* selector = dynamic_cast<fd::ui::Selector*>(c))
        return checkSegments(selector->buttons());
    if (auto* toggle = dynamic_cast<fd::ui::Toggle*>(c))
        CHECK(width(toggleFont, toggle->getButtonText()) + 8.0f <=
              static_cast<float>(c->getWidth()) + kTextMargin);
}

/// The grid rules for one panel in its current state.
inline void lintPanel(DevicePanel& panel, const fd::DeviceInfo& info, const juce::String& sidechainText)
{
    const auto& t = fd::ui::Theme::current();
    const auto& grid = t.deviceGrid;

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
    if (info.buses.sidechainChannels > 0)
    {
        REQUIRE_FALSE(panel.headerControls().empty());
        const auto* picker = panel.headerControls().front();
        CHECK(picker->isVisible());
        CHECK(panel.getLocalBounds().contains(picker->getBounds()));
        CHECK(dynamic_cast<const juce::ComboBox*>(picker)->getText() == sidechainText);
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
            INFO("control '" << pl.component->getName() << "' at (" << pl.column << ", " << pl.row
                             << ") span " << pl.columns << " x " << pl.rows);
            CHECK(pl.column >= 0);
            CHECK(pl.row >= 0);
            CHECK(pl.column + pl.columns <= s->columns());
            CHECK(pl.row + pl.rows <= grid.rows);
            CHECK(pl.component->getBounds() ==
                  PanelSection::cellBounds(pl.column, pl.row, pl.columns, pl.rows));
            for (int c = pl.column; c < pl.column + pl.columns; ++c)
                for (int r = pl.row; r < pl.row + pl.rows; ++r)
                    CHECK(taken.insert({c, r}).second); // no overlaps
            switch (pl.kind)
            {
            case PanelSection::Kind::Knob:
                CHECK((pl.columns == 1 || pl.columns == 2)); // two: a lone knob, centred
                CHECK(pl.component->getWidth() == grid.width(pl.columns));
                CHECK(pl.component->getHeight() == grid.knobHeight());
                CHECK((pl.row == 0 || pl.row == grid.knobRows));
                break;
            case PanelSection::Kind::Line:
                if (dynamic_cast<juce::ComboBox*>(pl.component) != nullptr)
                    CHECK(pl.columns >= 2);
                if (auto* k = dynamic_cast<fd::ui::Knob*>(pl.component))
                    CHECK((k->style() == fd::ui::Knob::Style::Bar && pl.columns >= 2));
                if (dynamic_cast<fd::ui::ParamSegmented*>(pl.component) == nullptr &&
                    dynamic_cast<fd::ui::Selector*>(pl.component) == nullptr)
                    CHECK(pl.rows == 1);
                break;
            case PanelSection::Kind::Display:
            case PanelSection::Kind::Label:
                break;
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
        if (auto* readout = dynamic_cast<fd::ui::HeaderReadout*>(c))
            CHECK(width(fd::ui::font(fd::ui::TextStyle::ValueSmall), readout->text()) <=
                  static_cast<float>(c->getWidth()) + kTextMargin);
        CHECK(c->getY() >= 0);
        CHECK(c->getBottom() <= t.deviceHeaderHeight);
        CHECK(c->getHeight() == grid.row);
        CHECK(c->getRight() <= panel.getWidth());
        checkPlacementText({c, PanelSection::Kind::Line, 0, 0, 2, 1});
    }

    // A host's notice (DevicePanel::setHostNotice): at the header's right end, its text whole,
    // touching nothing.
    if (const auto* notice = panel.hostNotice())
    {
        INFO("host notice: " << notice->text());
        CHECK(notice->text().isNotEmpty());
        CHECK(width(fd::ui::font(fd::ui::TextStyle::Control), notice->text()) <=
              static_cast<float>(notice->getWidth() - 2 * grid.displayInset) + kTextMargin);
        CHECK(notice->getY() >= 0);
        CHECK(notice->getBottom() <= t.deviceHeaderHeight);
        CHECK(notice->getHeight() == grid.row);
        CHECK(notice->getRight() <= panel.getWidth() - grid.headerPadding);
        for (auto* c : panel.headerControls())
            if (c->isVisible())
                CHECK(c->getRight() < notice->getX());
        if (const auto* bar = panel.presetBar(); bar != nullptr && bar->isVisible())
            CHECK(bar->getRight() < notice->getX());
        CHECK(panel.nameArea().getRight() < notice->getX());
    }

    // The presets (ADR 0044): in the header between the name and the settings, touching neither,
    // the field between its narrowest and widest, its parts apart.
    // A host's notice takes the field's place on a header too tight for both (the name whole).
    if (panel.presetBar() != nullptr && !panel.presetBar()->isVisible() && panel.hostNotice() != nullptr)
    {
        INFO("presets, given up to a host's notice");
        const auto name = width(fd::ui::font(fd::ui::TextStyle::Title), juce::String(std::string(info.name)));
        CHECK(name <= static_cast<float>(panel.nameArea().getWidth()) + kTextMargin);
    }
    else
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
        const auto name = width(fd::ui::font(fd::ui::TextStyle::Title), juce::String(std::string(info.name)));
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
            const bool inHeader = std::find(header.begin(), header.end(), c) != header.end() ||
                                  c == panel.hostNotice(); // the host's, laid out with the header
            CHECK((inHeader || c->findParentComponentOfClass<PanelSection>() != nullptr));
        }
}
} // namespace fd::test
