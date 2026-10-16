// The Drum Kit, step 0 (docs/design/drum-kit.md §17): the kit's UI drawn for review before it's
// built, with the real panel kit and module panels, from a stub of what the engine will hold: a
// new, empty kit; a kit of 12 pads with its kick's Sampler and Compressor in the frame; a pad with
// a synth; a pad being renamed; the pads shown moved up; drops onto pads; pads choked, off and
// soloed; pads playing; MACROS shown; folded. Each state passes the module panel lint, the pads'
// own checks and the frame's (nothing touching), and is drawn for review with FD_DUMP_DIR, at 1x
// and 2x.
#include "../panels/PanelLint.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "fd/testkit/WavIO.h"
#include "model/DrumKit.h"
#include "ui/DeviceChain.h"
#include "ui/DrumKitPanel.h"
#include "ui/RackPanel.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <filesystem>
#include <tracktion_engine/tracktion_engine.h>

namespace model = fd::model;
using fd::app::DrumKitPanel;
using fd::ui::DevicePanel;

namespace
{
constexpr float kTextMargin = 1.5f;

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

/// Parameters as the DAW binds them, held in a store.
struct StoreBinding final : fd::ui::ParamBinding
{
    explicit StoreBinding(fd::ParamStore& s) : store(s) {}
    float getPlain(size_t i) const override { return store.get(i); }
    void setPlain(size_t i, float v) override { store.set(i, v); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json&) override {}
    fd::ParamStore& store;
};

/// A module's presets as the DAW has them, none loaded.
struct StubPresets final : fd::PresetHost
{
    fd::PresetLibrary presets{{}, std::filesystem::temp_directory_path() / "fd-kit-ui-tests-no-presets"};
    fd::PresetMemo current;
    const fd::Device* device = nullptr;
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override { return fd::captureState(*device); }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

/// The kit's presets: one factory kit, "808 Kit", loaded (§9: kits are the Drum Kit's presets).
struct KitPresets final : fd::PresetHost
{
    static constexpr std::string_view kitFile =
        R"({"segnoPreset": 1, "tags": ["Electronic"], "author": "Segno",)"
        R"( "state": {"device": "drum-kit", "extra": {}, "params": {}, "stateVersion": 1}})";
    std::array<fd::FactoryPresetFile, 1> factory{{{"808 Kit", kitFile}}};
    fd::PresetLibrary presets{factory, std::filesystem::temp_directory_path() / "fd-kit-ui-tests-no-presets"};
    fd::PresetMemo current{.preset = "factory/808 Kit"};
    fd::PresetLibrary& library() override { return presets; }
    fd::DeviceState state() const override { return {.deviceId = "drum-kit", .stateVersion = 1}; }
    fd::PresetMemo memo() const override { return current; }
    void apply(const fd::DeviceState&, const fd::PresetMemo& m, const std::string&) override { current = m; }
    void setMemo(const fd::PresetMemo& m) override { current = m; }
    bool trash(const std::filesystem::path&) override { return false; }
};

/// Segno's look while it lives, as the app sets it.
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
    std::unique_ptr<StoreBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<DevicePanel> panel;

    Module(std::uint64_t moduleId, std::string_view deviceId) : id(moduleId)
    {
        const auto* factory = fd::findDevice(deviceId);
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<StoreBinding>(device->params());
        presets.device = device.get();
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, &presets});
        const auto* panelFactory = fd::ui::findPanel(deviceId);
        REQUIRE(panelFactory != nullptr);
        panel = panelFactory->create(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setFoldable(true);
    }

    [[nodiscard]] model::DeviceSlot slot() const
    {
        return {.id = id, .name = std::string(context->info.name), .deviceId = std::string(context->info.id)};
    }
};

/// A short kick: a falling sine, gone in 300 ms, for the Sampler's display.
juce::File kickFile()
{
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fd-kit-ui-kick.wav");
    fd::testkit::AudioBuffer kick(1, 14'400);
    double phase = 0.0;
    for (int i = 0; i < kick.numSamples(); ++i)
    {
        const double t = i / 48'000.0;
        phase += 2.0 * juce::MathConstants<double>::pi * (45.0 + 110.0 * std::exp(-t * 30.0)) / 48'000.0;
        kick.at(0, i) = static_cast<float>(0.8 * std::exp(-t * 9.0) * std::sin(phase));
    }
    REQUIRE(fd::testkit::writeWav(file.getFullPathName().toStdString(), kick, 48'000.0));
    return file;
}

/// The track's chain with a kit in it, laid out as the chain view will lay it out (§8): the kit's
/// panel and the selected pad's modules in its frame with the end cap, then the track's modules
/// after it, then the chain's +.
class ChainStub final : public juce::Component
{
public:
    std::vector<Module*> after, inner;
    DrumKitPanel* kit = nullptr;
    model::RackInfo info;
    fd::ui::IconButton endCap{fd::ui::Icon::Plus}, add{fd::ui::Icon::Plus};
    fd::app::RackFrame frame;
    juce::Rectangle<int> emptyHint; // an empty pad's room in the frame

    ChainStub()
    {
        endCap.setTooltip("Add a module to the pad");
        addAndMakeVisible(endCap);
        addAndMakeVisible(add);
    }

    void build()
    {
        addAndMakeVisible(*kit);
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
        const int frameX = x;
        x += inset;
        put(*kit);
        const bool folded = kit->isFolded();
        const int padX = x;
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
                             : juce::Rectangle<int>(padX, top + h, x - padX, t.shell.rackLabelHeight);
        // A kit's frame always names its pad: there's one shown even with a single pad.
        frame.text = folded ? juce::String() : juce::String::fromUTF8(model::kit::frameLabel(info).c_str());
        const auto* pad = model::kit::padOn(info, info.selectedPad);
        frame.chainOff = pad != nullptr && !pad->on;
        frame.soloed = pad != nullptr && pad->solo;
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
        g.fillAll(fd::ui::Theme::current().surface1); // the detail panel
        fd::app::paintRackFrame(g, frame);
        if (!emptyHint.isEmpty())
            fd::ui::paintEmptyState(g, emptyHint, "Drop a sample or an instrument here");
    }
};

/// The pads: inside the grid, apart by the gap, on whole pixels; each one's name and note inside
/// it and apart; the note's text and the choke group's number side by side without touching; the
/// overview's rows inside it, its frame round the 16 shown.
void lintPads(DrumKitPanel& panel, const model::RackInfo& kit)
{
    const auto& t = fd::ui::Theme::current();
    auto& pads = panel.pads();
    const auto bounds = pads.getLocalBounds();
    for (int i = 0; i < fd::ui::PadGrid::shown; ++i)
    {
        const int note = pads.from() + i;
        INFO("pad " << note);
        const auto r = pads.padBounds(note);
        CHECK(bounds.contains(r));
        CHECK(r.getWidth() > 0);
        if (i % fd::ui::PadGrid::columns > 0)
            CHECK(pads.padBounds(note - 1).getRight() + t.deviceGrid.padGap == r.getX());
        if (i >= fd::ui::PadGrid::columns)
            CHECK(r.getBottom() + t.deviceGrid.padGap ==
                  pads.padBounds(note - fd::ui::PadGrid::columns).getY());
        const auto name = pads.nameArea(note), noteRow = pads.noteArea(note);
        CHECK(r.reduced(t.deviceGrid.padInset).contains(name));
        CHECK(r.reduced(t.deviceGrid.padInset).contains(noteRow));
        CHECK(name.getBottom() <= noteRow.getY());
        // The note's name and, at its right, the choke group's number: room for both.
        const auto noteText = juce::String::fromUTF8(model::kit::noteName(note).c_str());
        const float chokeText = textWidth(fd::ui::font(fd::ui::TextStyle::Micro), "16");
        CHECK(textWidth(fd::ui::font(fd::ui::TextStyle::ValueSmall), noteText) + chokeText +
                  static_cast<float>(t.shell.itemGap) <=
              static_cast<float>(noteRow.getWidth()) + kTextMargin);
        CHECK(pads.padAt(r.getCentre()) == note);
    }
    // The overview: every row inside it, the frame inside it, round the 16 shown.
    auto& overview = panel.overview();
    for (int row = 0; row < 32; ++row)
        CHECK(overview.getLocalBounds().contains(overview.rowBounds(row)));
    CHECK(overview.getLocalBounds().contains(overview.frameBounds()));
    CHECK(overview.frameBounds().contains(overview.rowBounds(kit.padsFrom / 4)));
    CHECK(overview.frameBounds().contains(overview.rowBounds(kit.padsFrom / 4 + 3)));
    if (const int above = kit.padsFrom / 4 + 4; above < 32)
        CHECK_FALSE(overview.frameBounds().intersects(overview.rowBounds(above)));
    // PAD: dimmed and disabled while the selected pad is empty.
    const bool empty = model::kit::padOn(kit, kit.selectedPad) == nullptr;
    for (const auto& pl : panel.padSection().placements())
        CHECK(pl.component->isEnabled() == !empty);
}

/// The frame: the kit's panel, its pad's modules and the end cap inside it, an inset from its
/// edges; nothing after it touches it; its label fits its strip.
void lintFrame(ChainStub& chain)
{
    const auto& t = fd::ui::Theme::current();
    const auto inner = chain.frame.bounds.reduced(t.shell.rackInset);
    std::vector<juce::Component*> in{chain.kit};
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
    for (auto* m : chain.after)
        CHECK(m->panel->getX() >= chain.frame.bounds.getRight() + fd::app::DeviceChain::gap);
    CHECK(chain.add.getX() >= chain.frame.bounds.getRight() + fd::app::DeviceChain::gap);
    if (chain.frame.text.isNotEmpty())
    {
        CHECK(chain.frame.label.getY() >= chain.kit->getBottom());
        CHECK(chain.frame.label.getBottom() <= chain.frame.bounds.getBottom());
        CHECK(textWidth(fd::ui::capsLabelFont(), (chain.frame.text + " · off   solo").toUpperCase()) <=
              static_cast<float>(chain.frame.label.getWidth()) + kTextMargin);
    }
}

/// A General MIDI kit of 12 pads on C1 to C♯2, and two higher up: the kick a Sampler with a
/// Compressor after it, the hats in choke group 1, a synthesised tom.
model::RackInfo gmKit(const Module& sampler, const Module& compressor, const Module& synth)
{
    model::RackInfo kit;
    kit.kit = true;
    const auto pad = [&](int note, std::string name, int choke = 0)
    {
        model::RackChain c{.name = std::move(name), .note = note, .choke = choke};
        c.devices = {sampler.slot()};
        return c;
    };
    kit.chains = {pad(36, "Kick 1"),        pad(37, "Rim"),          pad(38, "Snare"),
                  pad(39, "Clap"),          pad(40, "Snare 2"),      pad(41, "Tom floor"),
                  pad(42, "Hat closed", 1), pad(44, "Hat pedal", 1), pad(45, "Tom low"),
                  pad(46, "Hat open", 1),   pad(49, "Crash"),        pad(51, "Ride"),
                  pad(56, "Cowbell"),       pad(63, "Conga high")};
    kit.chains[0].devices.push_back(compressor.slot());
    // A pad that plays a synth: the tom on G1, a Subtractive playing G1.
    model::RackChain tom{.name = "Sub tom", .note = 43, .plays = 43};
    tom.devices = {synth.slot()};
    kit.chains.push_back(tom);
    kit.selectedPad = 36;
    kit.padsFrom = 36;
    return kit;
}

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SegnoLook look;
    Module sampler{101, "sampler"}, compressor{102, "compressor"}, synth{201, "subtractive-synth"},
        reverb{3, "reverb"};
    fd::ParamStore macroStore{fd::engine::Session::drumKit().params};
    StoreBinding macroBinding{macroStore};
    KitPresets kitPresets;
    fd::ui::DeviceUiContext kitContext{
        fd::engine::Session::drumKit(), macroBinding, nullptr, nullptr, nullptr, &kitPresets};
    std::unique_ptr<DrumKitPanel> kit;
    ChainStub chain;
    model::RackInfo info;
    juce::File kick = kickFile();

    Fixture()
    {
        sampler.device->readExtraState({{"sample.path", kick.getFullPathName().toStdString()}});
        sampler.device->params().set(*sampler.device->params().indexOf("mode"), 1.0f); // One-shot
        kit = std::make_unique<DrumKitPanel>(kitContext);
        kit->onEnabledChange = [](bool) {};
        kit->setFoldable(true);
        info = gmKit(sampler, compressor, synth);
        chain.kit = kit.get();
        chain.after = {&reverb};
        show(info);
        chain.build();
    }

    ~Fixture() { kick.deleteFile(); }

    /// Shows `k` as the chain view will: its panel, the selected pad's modules in the frame.
    void show(const model::RackInfo& k)
    {
        for (auto* m : {&sampler, &compressor, &synth})
            m->panel->setVisible(false);
        chain.inner.clear();
        if (const auto* pad = model::kit::padOn(k, k.selectedPad))
            for (const auto& slot : pad->devices)
                for (auto* m : {&sampler, &compressor, &synth})
                    if (m->id == slot.id)
                        chain.inner.push_back(m);
        for (auto* m : chain.inner)
        {
            if (m->panel->getParentComponent() != &chain)
                chain.addAndMakeVisible(*m->panel);
            m->panel->setVisible(true);
        }
        chain.info = k;
        kit->setKit(k);
        refreshPanels();
        chain.layout();
    }

    /// What every panel's refresh tick does (no message loop here).
    void refreshPanels()
    {
        for (auto* m : {&sampler, &compressor, &synth, &reverb})
            for (auto* s : m->panel->sections())
                for (const auto& pl : s->placements())
                    if (auto* c = dynamic_cast<fd::ui::ParamControl*>(pl.component))
                        c->refresh();
        if (auto* bar = kit->presetBar())
            bar->refresh(); // the kit's name, as the header's tick shows it
        for (auto* s : kit->sections())
        {
            s->refresh();
            for (const auto& pl : s->placements())
                if (auto* c = dynamic_cast<fd::ui::ParamControl*>(pl.component))
                    c->refresh();
        }
    }

    void shot(const juce::String& name, const model::RackInfo& k)
    {
        chain.repaint();
        fd::test::lintPanel(*kit, fd::engine::Session::drumKit(), {});
        if (!kit->isFolded())
            lintPads(*kit, k);
        lintFrame(chain);
        dump("kit-" + name, chain);
        // The kit's panel alone, for a closer look.
        dump("kit-" + name + "-panel", chain, kit->getBounds().expanded(fd::app::DeviceChain::gap));
    }
};
} // namespace

TEST_CASE(
    "drum kit, drawn for review: the kit's panel, its pads and its frame in every state (drum-kit.md §17)",
    "[ui]")
{
    Fixture f;
    const auto& t = fd::ui::Theme::current();

    // A new, empty kit: every pad a well, PAD dimmed, the frame waiting for a sound.
    {
        model::RackInfo empty;
        empty.kit = true;
        f.kitPresets.current = {}; // Default settings: no kit loaded
        f.show(empty);
        CHECK(f.chain.inner.empty());
        CHECK_FALSE(f.chain.emptyHint.isEmpty());
        CHECK(f.chain.frame.text == "Pad C1");
        CHECK_FALSE(f.kit->findSection("macros")->isVisible());
        f.shot("new", empty);
        f.kitPresets.current = {.preset = "factory/808 Kit"};
    }

    // Twelve pads on C1 to C♯2, the kick selected: its Sampler and Compressor in the frame.
    {
        f.show(f.info);
        CHECK(f.chain.inner.size() == 2);
        CHECK(f.chain.frame.text == juce::String::fromUTF8("Pad C1 \xc2\xb7 Kick 1"));
        // The panel's width: PADS and PAD only, as narrow as the design says (§8).
        CHECK(f.kit->preferredWidth() ==
              f.kit->findSection("pads")->preferredWidth() + f.kit->findSection("pad")->preferredWidth());
        f.shot("kit", f.info);
    }

    // A pad that plays a synth, selected: the Subtractive in the frame.
    {
        auto synth = f.info;
        synth.selectedPad = 43;
        f.show(synth);
        REQUIRE(f.chain.inner.size() == 1);
        CHECK(f.chain.inner[0] == &f.synth);
        f.shot("synth-pad", synth);
    }

    // The pads shown moved up a page: C♯2 … E3 (the overview's frame follows), the kick still
    // selected (its modules still in the frame).
    {
        auto up = f.info;
        up.padsFrom = 52;
        f.show(up);
        CHECK(f.kit->pads().from() == 52);
        CHECK(f.kit->overview().from() == 52);
        f.shot("shown-up", up);
    }

    // Drops: a sample onto an empty pad, a pad dragged onto another with a sound (they'd swap).
    {
        f.show(f.info);
        f.kit->pads().setDropTarget(47);
        f.shot("drop-on-empty", f.info);
        f.kit->pads().setDropTarget(45);
        f.shot("drop-on-pad", f.info);
        f.kit->pads().setDropTarget(-1);
    }

    // Choke groups (the hats), a pad off (the floor tom), a pad soloed (the snare, selected):
    // each shown on its pad, and the snare's frame says SOLO.
    {
        auto mixed = f.info;
        for (auto& c : mixed.chains)
        {
            if (c.note == 41)
                c.on = false;
            if (c.note == 38)
                c.solo = true;
        }
        mixed.selectedPad = 38;
        f.show(mixed);
        CHECK(f.chain.frame.soloed);
        f.shot("choke-off-solo", mixed);
        // The off pad selected: its frame says OFF, its modules faded.
        mixed.selectedPad = 41;
        f.show(mixed);
        CHECK(f.chain.frame.chainOff);
        f.shot("pad-off", mixed);
    }

    // Pads playing: the kick and the closed hat lit.
    {
        f.show(f.info);
        std::bitset<128> playing;
        playing.set(36);
        playing.set(42);
        f.kit->setPlaying(playing);
        f.shot("playing", f.info);
        f.kit->setPlaying({});
    }

    // MACROS, once one is mapped: "Toms" tuning the toms.
    {
        auto mapped = f.info;
        mapped.macros[0] = {.name = "Toms",
                            .value = 0.4f,
                            .mappings = {{.module = 201, .param = "osc1.pitch", .paramName = "Pitch"}}};
        f.macroStore.set(0, 40.0f);
        f.show(mapped);
        CHECK(f.kit->findSection("macros")->isVisible());
        f.shot("macros", mapped);
        f.macroStore.set(0, 0.0f);
    }

    // Folded: one strip that says what it holds, its pad's modules folded away with it.
    {
        f.show(f.info);
        f.kit->setFolded(true);
        f.chain.layout();
        CHECK(f.kit->getWidth() == t.deviceGrid.foldedWidth);
        CHECK(model::kit::contents(f.info) == "15 pads");
        f.shot("folded", f.info);
        f.kit->setFolded(false);
        f.show(f.info);
    }

    // A pad being renamed, in place on the pad (last: the field stays until it's kept or left).
    {
        f.show(f.info);
        f.kit->renamePad(38);
        f.shot("renaming", f.info);
    }
}
