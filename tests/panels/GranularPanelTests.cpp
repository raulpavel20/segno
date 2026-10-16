// Granular's panel (devices/granular/SPEC.md, "UI layout"), drawn in every state for the visual
// mockup (G18) and checked by the layout lint in each: the modes, both sources, an empty source,
// the live input, every page, the routes' rings, collapsed and folded (FD_DUMP_DIR).
#include "GranularUi.h"
#include "PanelLint.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>

using G = fd::devices::Granular;
using fd::devices::GrainSource;

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

/// A host's tracks for the Input picker.
struct Tracks final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Guitar", "Vocals"}; }
    int selected() const override { return choice; }
    void select(int index) override { choice = index; }
    int choice = 0;
};

/// A host's presets (ADR 0044), so the header shows its preset field.
struct Presets final : fd::PresetHost
{
    fd::PresetLibrary presets{{}, std::filesystem::temp_directory_path() / "fd-granular-panel-no-presets"};
    fd::PresetMemo current{.preset = "factory/Pad/Strings in the air"};
    const fd::Device* device = nullptr;
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override { return fd::captureState(*device); }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

/// Plucks: decaying hits of a few partials, four a second, for Source 2.
fd::audiofile::DecodedAudio plucks(double seconds, double sampleRate)
{
    fd::audiofile::DecodedAudio audio;
    audio.sampleRate = sampleRate;
    const auto frames = static_cast<size_t>(seconds * sampleRate);
    audio.channels.assign(2, std::vector<float>(frames, 0.0f));
    constexpr double notes[] = {440.0, 659.25, 554.37, 880.0};
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const int hit = static_cast<int>(t * 4.0);
        const double since = t - hit / 4.0, hz = notes[hit % 4];
        const double x = std::exp(-since * 9.0) *
                         (std::sin(6.2831853 * hz * t) + 0.3 * std::sin(6.2831853 * 2.0 * hz * t));
        audio.channels[0][i] = static_cast<float>(0.5 * x);
        audio.channels[1][i] = static_cast<float>(0.5 * x * (hit % 2 == 0 ? 0.6 : 1.0));
    }
    return audio;
}

/// Grains as the engine would publish them: `count` around `position` (0 … 1 of the file) with a
/// spread, pitched within `pitchSpread` semitones (on octaves and fifths when `stepped`).
std::vector<G::GrainMark> grains(int count, float position, float spread, float pitchSpread, bool stepped,
                                 std::uint8_t source, std::uint32_t seed)
{
    std::vector<G::GrainMark> out;
    auto next = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < count; ++i)
    {
        float pitch = (next() * 2.0f - 1.0f) * pitchSpread;
        if (stepped)
        {
            constexpr float steps[] = {-12.0f, -5.0f, 0.0f, 7.0f, 12.0f, 19.0f};
            pitch = steps[static_cast<int>(next() * 6.0f) % 6];
        }
        out.push_back({std::clamp(position + (next() - 0.5f) * spread, 0.0f, 1.0f), pitch,
                       0.2f + 0.8f * next(), next() * 2.0f - 1.0f, source});
    }
    return out;
}

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    Tracks tracks;
    Presets presets;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<fd::ui::DevicePanel> panel;

    Fixture()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        const auto* factory = fd::findDevice("granular");
        REQUIRE(factory != nullptr);
        device = factory->create();
        granular().setSourceData(0, GrainSource::build(GrainSource::chord(6.0, 48000.0), "Strings chord",
                                                       "/Library/Strings chord.flac"));
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        presets.device = device.get();
        context = std::make_unique<fd::ui::DeviceUiContext>(fd::ui::DeviceUiContext{
            factory->info(), *binding, &device->analysis(), device.get(), &tracks, &presets});
        panel = fd::ui::findPanel("granular")->create(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setFoldable(true);
        panel->setVisible(true);
        // What the engine reports while it plays (until step 1 builds it).
        device->analysis().publish(G::CpuLoad, 0.031f);
        device->analysis().publish(G::GrainsPlaying, 212.0f);
        device->analysis().publish(G::ActiveVoices, 3.0f);
        device->analysis().publish(G::Scan1, 0.42f);
        layout();
    }
    ~Fixture() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }

    [[nodiscard]] G& granular() const { return dynamic_cast<G&>(*device); }
    void set(size_t p, float v) const { binding->setPlain(p, v); }
    void layout() const
    {
        refresh(*panel);
        // What a refresh set off (GRAIN placed again for a new mode) happens after it.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        refresh(*panel);
        panel->resized();
    }
    /// Lints the panel as it stands, on every page, then draws the page shown.
    void shot(const juce::String& name, int page = 0) const
    {
        for (int p = 0; p < panel->pageCount(); ++p)
        {
            INFO(name << ", page " << p);
            panel->setPage(p);
            layout();
            fd::test::lintPanel(*panel, context->info, tracks.sources()[static_cast<size_t>(tracks.choice)]);
        }
        panel->setPage(page);
        layout();
        dump("granular-" + name + "@2x.png",
             panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f));
    }
};
} // namespace

TEST_CASE("granular panel: drawn in every state for the mockup, each on the module grid", "[ui]")
{
    Fixture f;

    // Classic: a stretch through a chord, the grains at the position.
    f.set(G::Pos, 30.0f);
    f.set(G::PosSpray, 12.0f);
    f.set(G::ScanRate, 25.0f);
    f.set(G::PanSpray, 60.0f);
    f.granular().publishGrains(grains(40, 0.32f, 0.08f, 0.3f, false, 0, 1));
    f.shot("grains-classic");

    // Cloud: a dense cloud on octaves and fifths, wide.
    f.set(G::GrainMode, static_cast<float>(G::Mode::Cloud));
    f.set(G::GrainDensity, 180.0f);
    f.set(G::PitchSpray, 12.0f);
    f.set(G::PitchQuant, 4.0f);
    f.set(G::PanSpray, 90.0f);
    f.set(G::PosSpray, 35.0f);
    f.granular().publishGrains(grains(260, 0.45f, 0.35f, 0.0f, true, 0, 2));
    f.shot("grains-cloud");

    // Loop, Pitched, Rhythm, Spectral: GRAIN shows what each uses.
    f.set(G::PitchQuant, 0.0f);
    f.set(G::PitchSpray, 0.0f);
    f.set(G::PosSpray, 8.0f);
    for (const auto& [mode, name] :
         {std::pair{G::Mode::Loop, "grains-loop"}, std::pair{G::Mode::Pitched, "grains-pitched"},
          std::pair{G::Mode::Rhythm, "grains-rhythm"}, std::pair{G::Mode::Spectral, "grains-spectral"}})
    {
        f.set(G::GrainMode, static_cast<float>(mode));
        f.granular().publishGrains(
            grains(mode == G::Mode::Pitched ? 90 : 24, 0.5f, 0.06f, 0.0f, false, 0, 3));
        f.shot(name);
    }

    // Source 2: plucks beside the chord, half the grains each.
    f.set(G::GrainMode, static_cast<float>(G::Mode::Cloud));
    f.set(G::source(1, G::SrcOn), 1.0f);
    f.granular().setSourceData(
        1, GrainSource::build(plucks(4.0, 48000.0), "Glass plucks", "/Library/Glass plucks.wav"));
    auto both = grains(60, 0.4f, 0.3f, 5.0f, false, 0, 4);
    const auto second = grains(60, 0.55f, 0.4f, 5.0f, false, 1, 5);
    both.insert(both.end(), second.begin(), second.end());
    f.granular().publishGrains(both);
    auto* selector =
        find<fd::ui::Selector>(*f.panel, [](fd::ui::Selector& s) { return s.buttons().size() == 2; });
    REQUIRE(selector != nullptr);
    selector->setSelected(1, juce::sendNotification);
    f.shot("grains-source2");
    selector->setSelected(0, juce::sendNotification);

    // An empty source.
    f.granular().setSourceData(0, nullptr);
    f.shot("grains-empty");
    f.granular().setSourceData(0, GrainSource::build(GrainSource::chord(6.0, 48000.0), "Strings chord",
                                                     "/Library/Strings chord.flac"));

    // The live input (step 2): Source 1 on Live, the buffer rolling; LIVE on the Sound page.
    f.set(G::source(0, G::SrcInput), 1.0f);
    f.tracks.choice = 1;
    f.device->analysis().publish(G::InputLevelNow, 0.45f);
    f.granular().setLiveView(GrainSource::build(plucks(8.0, 48000.0), "Live", {}));
    f.granular().publishGrains(grains(50, 0.85f, 0.1f, 0.0f, false, 0, 6));
    f.shot("grains-live");
    f.shot("sound-live", 1);
    f.set(G::source(0, G::SrcInput), 0.0f);
    f.tracks.choice = 0;

    // Sound: a resonator on every grain, cutoff spread over two octaves, some ring.
    f.set(G::GFilterOn, 1.0f);
    f.set(G::GFilterType, 3.0f);
    f.set(G::GFilterCutoff, 1200.0f);
    f.set(G::GFilterRes, 70.0f);
    f.set(G::GFilterSpray, 2.0f);
    f.set(G::GColorRing, 40.0f);
    f.set(G::filter(0, G::FilterCutoff), 6000.0f);
    f.shot("sound", 1);

    // Modulation: routes in the matrix, the knobs they reach ringed; LFO 1, then Random 1 per grain.
    struct Route
    {
        G::Source source;
        int target;
        float amount;
    };
    int n = 0;
    for (const auto& r : {Route{G::Source::Lfo1, G::TargetPosition, 30.0f},
                          Route{G::Source::Random1, G::TargetGrainCutoff, 60.0f},
                          Route{G::Source::Pressure, G::TargetDensity, 40.0f},
                          Route{G::Source::Macro1, G::TargetPitchSpray, 80.0f}})
    {
        f.set(G::route(n, G::RouteSource), static_cast<float>(r.source));
        f.set(G::route(n, G::RouteTarget), static_cast<float>(r.target));
        f.set(G::route(n, G::RouteAmount), r.amount);
        ++n;
    }
    f.set(G::random(0, G::RandomMode), static_cast<float>(G::RandomPerGrain));
    f.shot("modulation", 2);
    auto* lfoSelector =
        find<fd::ui::Selector>(*f.panel, [](fd::ui::Selector& s) { return s.buttons().size() == 6; });
    REQUIRE(lfoSelector != nullptr);
    lfoSelector->setSelected(4, juce::sendNotification);
    f.shot("modulation-random", 2);
    f.shot("grains-rings");

    // Collapsed sections, and folded.
    f.panel->setAllSectionsCollapsed(true);
    f.shot("collapsed");
    f.panel->setAllSectionsCollapsed(false);
    f.panel->setFolded(true);
    f.layout();
    dump("granular-folded@2x.png", f.panel->createComponentSnapshot(f.panel->getLocalBounds(), true, 2.0f));
    f.panel->setFolded(false);
}

TEST_CASE("granular panel: GRAIN shows what each mode uses", "[ui]")
{
    Fixture f;
    const auto knobFor = [&f](size_t param)
    {
        return find<fd::ui::ParamKnob>(*f.panel,
                                       [param](fd::ui::ParamKnob& k) { return k.param() == param; });
    };
    CHECK(knobFor(G::GrainOverlap) != nullptr); // Classic
    CHECK(knobFor(G::GrainDensity) == nullptr);
    f.set(G::GrainMode, static_cast<float>(G::Mode::Cloud));
    f.layout();
    CHECK(knobFor(G::GrainDensity) != nullptr);
    CHECK(knobFor(G::GrainOverlap) == nullptr);
}
