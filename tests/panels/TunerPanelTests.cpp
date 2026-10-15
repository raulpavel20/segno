// The Tuner's panel with a note playing (devices/tuner/SPEC.md, "UI layout"): no signal, in tune,
// flat and sharp with the arrow that says which way, the strobe, In key with the song's key and
// without one, a voice's history with vibrato, and Muted in the header. Drawn for review with
// FD_DUMP_DIR.
#include "../../devices/tuner/dsp/Tuner.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <numbers>

using T = fd::devices::Tuner;

namespace
{
constexpr double kRate = 48000.0;

double hzOf(double midi)
{
    return 440.0 * std::exp2((midi - 69.0) / 12.0);
}

/// The panel on a Tuner, played into.
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI init;
    fd::ui::LookAndFeel look;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<fd::ui::LocalParamBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<fd::ui::DevicePanel> panel;
    double phase = 0.0, time = 0.0;

    Fixture()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&look);
        const auto* factory = fd::findDevice("tuner");
        REQUIRE(factory != nullptr);
        device = factory->create();
        device->prepare({kRate, 512});
        binding = std::make_unique<fd::ui::LocalParamBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, &device->analysis(), device.get()});
        for (const auto* p : fd::ui::allPanels())
            if (p->deviceId == "tuner")
                panel = p->create(*context);
        REQUIRE(panel != nullptr);
        panel->onEnabledChange = [](bool) {};
        panel->setFoldable(true);
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
    }
    ~Fixture() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    /// Plays `seconds` of a saw-like tone whose pitch (a fractional MIDI number) `pitchAt` gives,
    /// or silence for a NaN pitch; then refreshes the panel, as its frame clock does.
    void play(double seconds, const std::function<double(double)>& pitchAt, fd::KeyInfo key = {})
    {
        std::vector<float> l(512), r(512);
        for (double done = 0.0; done < seconds; done += 512.0 / kRate)
        {
            for (size_t i = 0; i < 512; ++i)
            {
                const double p = pitchAt(time);
                if (std::isfinite(p))
                {
                    phase += 2.0 * std::numbers::pi * hzOf(p) / kRate;
                    double v = 0.0;
                    for (int k = 1; k <= 8; ++k)
                        v += std::sin(phase * k) / k;
                    l[i] = r[i] = static_cast<float>(0.2 * v);
                }
                else
                    l[i] = r[i] = 0.0f;
                time += 1.0 / kRate;
            }
            float* channels[2] = {l.data(), r.data()};
            fd::ProcessContext ctx;
            ctx.input = {channels, 2};
            ctx.output = {channels, 2};
            ctx.numSamples = 512;
            ctx.transport.key = key;
            device->process(ctx);
        }
        refresh();
    }

    void refresh()
    {
        std::vector<juce::Component*> all{panel.get()};
        for (size_t i = 0; i < all.size(); ++i)
            for (auto* c : all[i]->getChildren())
                all.push_back(c);
        for (auto* c : all)
            if (auto* p = dynamic_cast<fd::ui::ParamControl*>(c))
                p->refresh();
        panel->resized();
    }

    void dump(const juce::String& name)
    {
        if (const char* dir = std::getenv("FD_DUMP_DIR"))
            for (const float scale : {1.0f, 2.0f})
            {
                const auto file =
                    juce::File(dir).getChildFile("tuner-" + name + (scale > 1.0f ? "@2x.png" : "@1x.png"));
                file.deleteFile();
                juce::FileOutputStream stream(file);
                juce::PNGImageFormat().writeImageToStream(
                    panel->createComponentSnapshot(panel->getLocalBounds(), true, scale), stream);
            }
    }

    [[nodiscard]] fd::ui::HeaderBadge* badge() const
    {
        for (auto* c : panel->getChildren())
            if (auto* b = dynamic_cast<fd::ui::HeaderBadge*>(c))
                return b;
        return nullptr;
    }
};
} // namespace

TEST_CASE("tuner panel: no signal, in tune, flat and sharp, the strobe, In key, Muted; drawn for review",
          "[ui]")
{
    Fixture f;
    f.refresh();
    f.dump("empty");
    CHECK_FALSE(std::isfinite(f.device->analysis().read(T::Note)));

    // A guitar's A string, in tune; then 12 cents flat; then 30 sharp.
    f.play(0.8, [](double) { return 45.0; });
    CHECK(f.device->analysis().read(T::Note) == 45.0f);
    f.dump("in-tune");
    f.play(0.8, [](double) { return 44.88; });
    f.dump("flat");
    f.play(0.8, [](double) { return 45.30; });
    f.dump("sharp");

    // The strobe.
    f.binding->setPlain(T::View, 1.0f);
    f.play(0.3, [](double) { return 45.06; });
    f.dump("strobe");
    f.binding->setPlain(T::View, 0.0f);

    // A sung phrase in F major, In key: a B flat spelled as such, and the history's vibrato.
    f.binding->setPlain(T::Notes, 1.0f);
    const auto fMajorNotes = static_cast<std::uint16_t>((1 << 5) | (1 << 7) | (1 << 9) | (1 << 10) |
                                                        (1 << 0) | (1 << 2) | (1 << 4));
    const fd::KeyInfo key{.root = 5, .notes = fMajorNotes};
    f.play(1.5, [](double t) { return 70.0 + 0.3 * std::sin(2.0 * std::numbers::pi * 5.5 * t); }, key);
    f.play(0.3, [](double) { return NAN; }, key);
    f.play(
        1.5, [](double t)
        { return 72.0 - std::max(0.0, 0.5 - t) + 0.25 * std::sin(2.0 * std::numbers::pi * 5.0 * t); }, key);
    CHECK(f.device->analysis().read(T::KeyRoot) == 5.0f);
    f.dump("in-key");
    // Without a key (a plugin host), In key says it measures every note.
    f.play(0.5, [](double) { return 66.0; });
    f.dump("in-key-no-key");
    f.binding->setPlain(T::Notes, 0.0f);

    // Mute: the header says so.
    REQUIRE(f.badge() != nullptr);
    CHECK(f.badge()->text().isEmpty());
    f.binding->setPlain(T::Mute, 1.0f);
    f.play(0.2, [](double) { return 45.0; });
    CHECK(f.badge()->text() == "Muted");
    CHECK(f.badge()->getTooltip().contains("silences"));
    f.dump("muted");

    // Silence: the last note dims for a second, then a dash.
    f.binding->setPlain(T::Mute, 0.0f);
    f.play(0.4, [](double) { return NAN; });
    f.dump("fading");
    f.play(1.2, [](double) { return NAN; });
    f.dump("silent");
}
