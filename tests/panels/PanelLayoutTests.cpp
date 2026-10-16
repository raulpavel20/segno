// The module panel layout lint (ADR 0022, docs/design/device-panels.md). Builds every
// registered panel and checks it against the module grid: expanded, with each section
// collapsed, with all collapsed, and folded. A module isn't done until this passes.

#include "PanelLint.h"

#include <catch2/catch_test_macros.hpp>
#include <fd/audiofile/AudioFile.h>
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
using fd::test::kTextMargin;
using fd::test::width;
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

void lint(const PanelUnderTest& p)
{
    fd::test::lintPanel(*p.panel, p.context->info, "A very long track name indeed");
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
                {
                    // The DAW's "Bypassed while monitoring" (recording.md §22.1): no room of its
                    // own, whole or short as it fits, and gone when folded.
                    INFO("a host's notice");
                    p.panel->setHostNotice("Bypassed while monitoring", "Bypassed", "Why");
                    p.layout();
                    CHECK(p.panel->preferredWidth() == width);
                    REQUIRE(p.panel->hostNotice() != nullptr);
                    lint(p);
                    p.panel->setFolded(true);
                    p.layout();
                    CHECK(p.panel->hostNotice() == nullptr);
                    p.panel->setFolded(false);
                    p.panel->setHostNotice({}, {}, {});
                    p.layout();
                    CHECK(p.panel->hostNotice() == nullptr);
                }
            }
            p.panel->setPage(0);
            p.layout();
            if (p.panel->pageCount() > 0)
            {
                // Every section on exactly one page (one that shows only sometimes too), page ids
                // unique.
                std::set<std::string> pageIds;
                for (int page = 0; page < p.panel->pageCount(); ++page)
                    pageIds.insert(p.panel->pageId(page));
                CHECK(pageIds.size() == static_cast<size_t>(p.panel->pageCount()));
                for (const auto* section : p.panel->sections())
                {
                    INFO(section->id());
                    const int page = p.panel->sectionPage(*section);
                    CHECK(page >= 0);
                    CHECK(page < p.panel->pageCount());
                }
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
                // Bypassed while monitoring: the notice in the badge's place, the badge hidden.
                q.panel->setHostNotice("Bypassed while monitoring", "Bypassed", "Why");
                q.layout();
                CHECK(std::none_of(
                    header.begin(), header.end(), [](auto* c)
                    { return dynamic_cast<fd::ui::LatencyBadge*>(c) != nullptr && c->isVisible(); }));
                lint(q);
            }
            // The Sampler with one sample: its waveform fills the section (a multisample is its
            // test setup).
            if (panelFactory.deviceId == "sampler")
            {
                INFO("one sample");
                const auto wav = std::filesystem::temp_directory_path() / "fd-panel-tests-one-sample.wav";
                REQUIRE(fd::audiofile::writeWav(wav.string(), {std::vector<float>(4800, 0.1f)}, 48000.0));
                PanelUnderTest q(*factory, panelFactory, true, [&wav](fd::Device& d)
                                 { d.readExtraState({{"sample.path", wav.string()}}); });
                lint(q);
                std::filesystem::remove(wav);
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
