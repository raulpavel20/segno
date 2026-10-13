// Pages for very wide modules (ADR 0051, docs/design/device-panels.md, "Pages"), on a panel made
// for the test from the Utility's parameters: one width, tabs after the name, turning by click,
// by menu and to a parameter, and the page in the view state.
#include <catch2/catch_test_macros.hpp>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

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

/// Two pages: a narrow one, then a wider one of two sections.
class PagedPanel final : public fd::ui::DevicePanel
{
public:
    explicit PagedPanel(fd::ui::DeviceUiContext& c) : DevicePanel(c)
    {
        addPage("main", "Main");
        auto& level = addSection("level", "Level", 2);
        level.knob(param(c, "gain"), 0, 0);
        level.knob(param(c, "balance"), 1, 0);
        addPage("image", "Stereo Image");
        auto& image = addSection("width", "Width", 2);
        image.knob(param(c, "width"), 0, 0);
        auto& fix = addSection("fix", "Fix", 3);
        fix.toggle(param(c, "mono"), 0, 0, 2);
        fix.toggle(param(c, "dc"), 0, 1, 2);
    }
};

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<PagedPanel> panel;

    Fixture()
    {
        const auto* factory = fd::findDevice("utility");
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, nullptr});
        panel = std::make_unique<PagedPanel>(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setFoldable(true);
        layout();
    }
    void layout() const
    {
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    }
};

int sectionsWidth(const std::vector<fd::ui::PanelSection*>& sections)
{
    int w = 0;
    for (auto* s : sections)
        w += s->preferredWidth();
    return w;
}
} // namespace

TEST_CASE("pages: one width, the widest page's; only the page's sections show", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    REQUIRE(panel.pageCount() == 2);
    CHECK(panel.page() == 0);
    const int wide = sectionsWidth(panel.pageSections(1));
    CHECK(sectionsWidth(panel.pageSections(0)) < wide);
    CHECK(panel.preferredWidth() == std::max(wide, panel.headerMinimumWidth()));
    CHECK(panel.findSection("level")->isVisible());
    CHECK_FALSE(panel.findSection("width")->isVisible());

    panel.setPage(1);
    f.layout();
    CHECK(panel.preferredWidth() == std::max(wide, panel.headerMinimumWidth()));
    CHECK_FALSE(panel.findSection("level")->isVisible());
    CHECK(panel.findSection("width")->isVisible());
    CHECK(panel.findSection("width")->getX() == 0);
    CHECK(panel.findSection("fix")->getX() == panel.findSection("width")->getRight());

    // Collapsing a section of the wide page narrows the module, whichever page shows.
    panel.findSection("fix")->setCollapsed(true);
    panel.setPage(0);
    f.layout();
    CHECK(panel.preferredWidth() ==
          std::max(std::max(sectionsWidth(panel.pageSections(0)), sectionsWidth(panel.pageSections(1))),
                   panel.headerMinimumWidth()));
}

TEST_CASE("pages: the tabs follow the name, and a click turns the page", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    const auto* tabs = panel.pageTabs();
    REQUIRE(tabs != nullptr);
    CHECK(tabs->isVisible());
    CHECK(tabs->tabs().joinIntoString(",") == "Main,Stereo Image");
    CHECK(panel.nameArea().getRight() < tabs->getX());
    if (const auto* presets = panel.presetBar())
        CHECK(tabs->getRight() < presets->getX());
    CHECK_FALSE(panel.isDragHandle(tabs->getBounds().getCentre()));

    int changes = 0;
    panel.onViewChanged = [&] { ++changes; };
    auto& row = const_cast<fd::ui::TabRow&>(*tabs);
    const auto second = row.tabAreas()[1].getCentre().toFloat();
    const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), second,
                                 juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &row,
                                 &row, juce::Time::getCurrentTime(), second, juce::Time::getCurrentTime(), 1,
                                 false);
    row.mouseUp(click);
    CHECK(panel.page() == 1);
    CHECK(changes == 1);
    CHECK(row.selected() == 1);

    // Folded, a module shows its strip whatever its page.
    panel.setFolded(true);
    f.layout();
    CHECK_FALSE(tabs->isVisible());
}

TEST_CASE("pages: turned from the menu, round from the last to the first", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    panel.turnPage(+1, true);
    CHECK(panel.page() == 1);
    panel.turnPage(+1, true);
    CHECK(panel.page() == 0);
    panel.turnPage(-1, true);
    CHECK(panel.page() == 1);
}

TEST_CASE("pages: showing a parameter turns to its page", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    CHECK(panel.showParameter(param(*f.context, "mono")));
    CHECK(panel.page() == 1);
    CHECK(panel.showParameter(param(*f.context, "gain")));
    CHECK(panel.page() == 0);
    CHECK_FALSE(panel.showParameter(param(*f.context, "mute"))); // on no page
    CHECK(panel.page() == 0);
}

TEST_CASE("pages: the page is view state, by its id", "[ui]")
{
    Fixture f;
    auto& panel = *f.panel;
    CHECK(panel.viewState().empty()); // the first page is the default
    panel.setPage(1, true);
    CHECK(panel.viewState() == R"({"page":"image"})");
    Fixture g;
    g.panel->setViewState(panel.viewState());
    CHECK(g.panel->page() == 1);
    CHECK(g.panel->pageTabs()->selected() == 1);
    g.panel->setViewState("{}");
    CHECK(g.panel->page() == 0);
}
