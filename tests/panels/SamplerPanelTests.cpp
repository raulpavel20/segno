// The Sampler's panel with a multisample (devices/sampler/SPEC.md, "The panel", S9): the zone map
// above the last zone's waveform, each zone named on hover and played on a click, missing samples
// and ignored settings said; one sample keeps its waveform alone. Drawn for review (FD_DUMP_DIR).
#include "Sampler.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/audiofile/AudioFile.h>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>
#include <fstream>
#include <set>

using S = fd::devices::Sampler;
namespace fs = std::filesystem;

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

juce::Component* named(juce::Component& root, const juce::String& name)
{
    for (auto* c : root.getChildren())
    {
        if (c->getName() == name && c->isVisible())
            return c;
        if (auto* deeper = named(*c, name))
            return deeper;
    }
    return nullptr;
}

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> pos)
{
    return {juce::Desktop::getInstance().getMainMouseSource(),
            pos,
            juce::ModifierKeys::leftButtonModifier,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &c,
            &c,
            juce::Time::getCurrentTime(),
            pos,
            juce::Time::getCurrentTime(),
            1,
            false};
}

/// A violin-like instrument: 16 zones of three keys from G2, soft and loud, the loud ones a
/// round robin of two; one sample missing and one setting the Sampler doesn't play.
fs::path writeInstrument(const fs::path& folder)
{
    fs::remove_all(folder);
    fs::create_directories(folder / "samples");
    std::ofstream sfz(folder / "Strings.sfz");
    sfz << "<control> default_path=samples\\\n<global> ampeg_release=0.4 pan=10\n";
    for (int z = 0; z < 16; ++z)
    {
        const int low = 55 + 3 * z, root = low + 1;
        for (const auto* layer : {"soft", "loud 1", "loud 2"})
        {
            const auto name = std::string(layer) + " " + std::to_string(root) + ".wav";
            if (!(z == 5 && std::string(layer) == "soft"))
            {
                std::vector<float> tone(2400);
                for (size_t i = 0; i < tone.size(); ++i)
                    tone[i] =
                        0.3f * static_cast<float>(std::sin(0.01 * (root - 40) * static_cast<double>(i)));
                REQUIRE(fd::audiofile::writeWav((folder / "samples" / name).string(), {tone, tone}, 48000.0,
                                                std::nullopt, 16));
            }
            sfz << "<region> sample=" << name << " lokey=" << low << " hikey=" << low + 2
                << " pitch_keycenter=" << root
                << (std::string(layer) == "soft"
                        ? " lovel=0 hivel=63"
                        : std::string(" lovel=64 hivel=127 seq_length=2 seq_position=") +
                              (std::string(layer) == "loud 1" ? "1" : "2"))
                << "\n";
        }
    }
    return folder / "Strings.sfz";
}

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<fd::ui::DevicePanel> panel;
    std::vector<std::pair<int, float>> notes;

    explicit Fixture(const std::string& path)
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        const auto* factory = fd::findDevice("sampler");
        REQUIRE(factory != nullptr);
        device = factory->create();
        device->readExtraState({{"sample.path", path}});
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, nullptr});
        context->playNote = [this](int note, float velocity) { notes.emplace_back(note, velocity); };
        panel = fd::ui::findPanel("sampler")->create(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setVisible(true);
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        refresh(*panel);
    }
    ~Fixture() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }

    void shot(const juce::String& name) const
    {
        refresh(*panel);
        const auto sample = panel->findSection("sample")->getBoundsInParent().expanded(8);
        dump(name + "@2x.png", panel->createComponentSnapshot(sample, true, 2.0f));
        dump(name + "@1x.png", panel->createComponentSnapshot(sample, true, 1.0f));
    }

    /// Every distinct tooltip over the zone map, and a point that shows each.
    std::map<juce::String, juce::Point<float>> tooltips(juce::Component& zones) const
    {
        std::map<juce::String, juce::Point<float>> out;
        auto* tips = dynamic_cast<juce::TooltipClient*>(&zones);
        REQUIRE(tips != nullptr);
        for (float y = 0.5f; y < static_cast<float>(zones.getHeight()); y += 1.0f)
            for (float x = 0.5f; x < static_cast<float>(zones.getWidth()); x += 1.0f)
            {
                zones.mouseMove(mouse(zones, {x, y}));
                if (const auto tip = tips->getTooltip(); tip.isNotEmpty())
                    out.emplace(tip, juce::Point<float>{x, y});
            }
        return out;
    }
};
} // namespace

TEST_CASE("sampler panel: a multisample shows its zones above the last zone's waveform", "[ui]")
{
    const auto sfz = writeInstrument(fs::temp_directory_path() / "fd-sampler-panel-strings");
    Fixture f(sfz.string());
    const auto& sampler = dynamic_cast<const S&>(*f.device);
    const auto map = sampler.map();
    REQUIRE(map != nullptr);
    CHECK(map->zones.size() == 48);
    CHECK(map->missing == 1);
    CHECK(map->ignored == std::vector<std::string>{"pan"});

    auto* zones = named(*f.panel, "Zones");
    auto* wave = named(*f.panel, "Sample");
    REQUIRE(zones != nullptr);
    REQUIRE(wave != nullptr);
    // On the grid: the map's four rows, the waveform's two below them.
    CHECK(zones->getBottom() < wave->getY());
    CHECK(zones->getX() == wave->getX());
    CHECK(zones->getWidth() == wave->getWidth());

    // A zone a rectangle, the round robins as one: 16 soft and 16 loud; the line above names
    // what's missing and ignored.
    const auto tips = f.tooltips(*zones);
    CHECK(tips.size() == 33);
    CHECK(tips.contains("1 of 48 samples missing: soft 71.wav\nIn the file but not played: pan"));
    const auto has = [&tips](const juce::String& text)
    {
        return std::any_of(tips.begin(), tips.end(),
                           [&text](const auto& t) { return t.first.contains(text); });
    };
    CHECK(has("loud 1 56.wav"));
    CHECK(has("2 take turns"));
    CHECK(has("velocity 0" + juce::String::fromUTF8("\xE2\x80\x93") + "63"));
    CHECK(has("soft 71.wav (missing)"));
    CHECK(has("root G#2")); // 56, as Segno names notes (C3 = 60)
    f.shot("sampler-zones");

    // A click plays the zone's root at the middle of its velocities, and lets go.
    const auto at =
        std::find_if(tips.begin(), tips.end(), [](const auto& t) { return t.first.contains("soft 59.wav"); });
    REQUIRE(at != tips.end());
    zones->mouseDown(mouse(*zones, at->second));
    zones->mouseUp(mouse(*zones, at->second));
    REQUIRE(f.notes.size() == 2);
    CHECK(f.notes[0].first == 59);
    CHECK(f.notes[0].second > 0.2f);
    CHECK(f.notes[0].second < 0.3f);
    CHECK(f.notes[1] == std::pair<int, float>{59, 0.0f});

    // Root Key dims: the zones have their own.
    fs::remove_all(sfz.parent_path());
}

TEST_CASE("sampler panel: one sample keeps the whole section for its waveform", "[ui]")
{
    const auto wav = fs::temp_directory_path() / "fd-sampler-panel-one.wav";
    std::vector<float> tone(24000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.4f * static_cast<float>(std::sin(0.02 * static_cast<double>(i)) *
                                            std::exp(-0.0002 * static_cast<double>(i)));
    REQUIRE(fd::audiofile::writeWav(wav.string(), {tone}, 48000.0));
    Fixture f(wav.string());
    CHECK(named(*f.panel, "Zones") == nullptr);
    auto* wave = named(*f.panel, "Sample");
    REQUIRE(wave != nullptr);
    CHECK(wave->getHeight() > 150);
    f.shot("sampler-one");
    fs::remove(wav);
}

TEST_CASE("sampler panel: the Library's instruments, drawn for review", "[library][ui]")
{
    const auto library = fs::path(FD_SOURCE_DIR) / ".cache" / "library";
    if (!fs::exists(library / "library.json"))
        SKIP("The Library isn't built: cmake --build --preset dev --target segno-library");
    for (const auto& [name, sfz] :
         {std::pair<const char*, const char*>{"violins", "Strings/Violin section/Sustain.sfz"},
          {"grand-piano", "Keys/Grand piano/Grand piano.sfz"},
          {"kit", "Percussion/Orchestral kit/Orchestral kit.sfz"}})
    {
        INFO(name);
        Fixture f((library / "Samples" / sfz).string());
        const auto map = dynamic_cast<const S&>(*f.device).map();
        REQUIRE(map != nullptr);
        CHECK(map->missing == 0);
        CHECK(map->ignored.empty());
        f.shot(juce::String("sampler-library-") + name);
    }
}
