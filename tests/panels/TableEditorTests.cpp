// The Wavetable's table editor (docs/design/table-editor.md §4, ADR 0053): its layout lint on the
// module grid at its narrowest and a wide width, at its least height and taller, in each of its
// states; edits kept through the host, one undo step each; following the module (undo, the table
// picker); audio taken in by the way chooser; Preview holding a note. Drawn for review
// (FD_DUMP_DIR).
#include "TableEditor.h"
#include "WavetableUi.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fd/device/Registry.h>
#include <fd/dsp/Math.h>
#include <fd_device_ui/fd_device_ui.h>

using W = fd::devices::WavetableSynth;
using namespace fd::devices::wavetable_ui;
using Recipe = fd::devices::wavetable::Recipe;
using K = Recipe::SourceKind;

namespace
{
constexpr float kTextMargin = 1.5f; // font rasterisers differ slightly across platforms

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

/// The host as the editor sees it: every edit recorded, merged into the device.
struct RecordingBinding final : fd::ui::ParamBinding
{
    explicit RecordingBinding(fd::Device& d) : device(d) {}
    float getPlain(size_t i) const override { return device.params().get(i); }
    void setPlain(size_t i, float v) override { device.params().set(i, v); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json& patch) override
    {
        ++patches;
        fd::ui::mergeExtraState(device, patch);
    }
    void beginEdit(const juce::String& name) override { steps.push_back(name); }

    fd::Device& device;
    int patches = 0;
    std::vector<juce::String> steps;
};

juce::MouseEvent mouse(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged,
                       int clicks = 1)
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
            down,
            juce::Time::getCurrentTime(),
            clicks,
            dragged};
}

void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    c.mouseDown(mouse(c, from, from, false));
    c.mouseDrag(mouse(c, (from + to) * 0.5f, from, true));
    c.mouseDrag(mouse(c, to, from, true));
    c.mouseUp(mouse(c, to, from, true));
}

struct Fixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    fd::ui::LookAndFeel lnf;
    std::unique_ptr<fd::Device> device;
    std::unique_ptr<RecordingBinding> binding;
    std::unique_ptr<fd::ui::DeviceUiContext> context;
    std::unique_ptr<fd::ui::DevicePanel> panel;
    std::unique_ptr<fd::ui::ModuleEditor> editor;
    std::vector<std::pair<int, float>> notes;
    int shown = 0;

    Fixture()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        const auto* factory = fd::findDevice("wavetable");
        REQUIRE(factory != nullptr);
        device = factory->create();
        binding = std::make_unique<RecordingBinding>(*device);
        context = std::make_unique<fd::ui::DeviceUiContext>(
            fd::ui::DeviceUiContext{factory->info(), *binding, nullptr, device.get(), nullptr, nullptr});
        context->showEditor = [this](std::unique_ptr<fd::ui::ModuleEditor> e)
        {
            editor = std::move(e);
            editor->onShowRequested = [this] { ++shown; };
            ++shown;
        };
        context->playNote = [this](int note, float velocity) { notes.emplace_back(note, velocity); };
        panel = fd::ui::findPanel("wavetable")->create(*context);
        panel->onEnabledChange = [](bool) {};
        panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);
        REQUIRE(panel->performCommand("table.edit"));
        REQUIRE(editor != nullptr);
        editor->setVisible(true);
        layout(editor->minimumWidth(), editor->minimumHeight());
    }
    ~Fixture()
    {
        editor.reset(); // before the context it edits through
        panel.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void layout(int width, int height) const { editor->setSize(width, height); }
    [[nodiscard]] TableEditor& table() const { return dynamic_cast<TableEditor&>(*editor); }
    [[nodiscard]] W& synth() const { return dynamic_cast<W&>(*device); }
    void shot(const juce::String& name) const
    {
        dump(name + "@2x.png", editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f));
    }
};

float width(const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth(font, text);
}

/// Text that has to fit in its control (as the panels' lint checks).
void checkText(juce::Component& c)
{
    const auto& t = fd::ui::Theme::current();
    INFO("'" << c.getName() << "' " << c.getBounds().toString());
    if (auto* knob = dynamic_cast<fd::ui::Knob*>(&c);
        knob != nullptr && knob->style() == fd::ui::Knob::Style::Bar)
    {
        float widest = 0.0f;
        for (int i = 0; i <= 32 && knob->textFromValue; ++i)
            widest = std::max(widest, width(fd::ui::fonts::mono(t.textXs),
                                            knob->textFromValue(static_cast<float>(i) / 32.0f)));
        CHECK(width(fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium), knob->getName()) + widest +
                  16.0f <=
              static_cast<float>(c.getWidth()) + kTextMargin);
    }
    else if (auto* combo = dynamic_cast<fd::ui::ChoiceBox*>(&c))
    {
        const auto font = combo->getLookAndFeel().getComboBoxFont(*combo);
        const float name =
            combo->inlineName().isEmpty()
                ? 0.0f
                : width(fd::ui::fonts::font(t.textXs, fd::ui::fonts::Weight::Medium), combo->inlineName()) +
                      static_cast<float>(t.shell.buttonTextPadding);
        for (int i = 0; i < combo->getNumItems(); ++i)
        {
            INFO("choice " << combo->getItemText(i));
            CHECK(name + width(font, combo->getItemText(i)) + 26.0f <=
                  static_cast<float>(c.getWidth()) + kTextMargin);
        }
    }
    else if (auto* toggle = dynamic_cast<fd::ui::Toggle*>(&c);
             toggle != nullptr && toggle->getButtonText().isNotEmpty())
        CHECK(fd::ui::Toggle::widthFor(toggle->getButtonText()) <= c.getWidth());
    else if (auto* selector = dynamic_cast<fd::ui::Selector*>(&c))
        for (auto* b : selector->buttons())
        {
            INFO("segment " << b->getButtonText());
            CHECK(fd::ui::Toggle::widthFor(b->getButtonText()) <= b->getWidth());
        }
}

/// The layout lint: every placement inside its pane's content, none overlapping another in its
/// pane, the title rows' components clear of the titles and each other, every text fitting.
void lint(const fd::ui::ModuleEditor& editor)
{
    const auto bounds = editor.getLocalBounds();
    for (int pane = 0; pane < editor.paneCount(); ++pane)
    {
        INFO("pane " << pane);
        const auto content = editor.paneContent(pane);
        CHECK(bounds.contains(editor.paneBounds(pane)));
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
            checkText(*p.component);
        }
        const auto title = editor.paneTitleArea(pane), text = editor.paneTitleText(pane);
        std::vector<juce::Rectangle<int>> titled;
        for (auto* c : editor.titleComponents(pane))
        {
            const auto r = c->getBounds();
            INFO("title component " << r.toString());
            CHECK(title.contains(r));
            CHECK_FALSE(r.intersects(text));
            for (const auto& other : titled)
                CHECK_FALSE(other.intersects(r));
            titled.push_back(r);
            checkText(*c);
        }
    }
}

/// The lint at the narrowest and a wide width, at its least height and taller.
void lintAtSizes(Fixture& f, const juce::String& state)
{
    INFO("state: " << state);
    for (const auto& [w, h] : {std::pair{f.editor->minimumWidth(), f.editor->minimumHeight()},
                               std::pair{1600, f.editor->minimumHeight()},
                               std::pair{f.editor->minimumWidth(), f.editor->minimumHeight() + 240}})
    {
        INFO(w << " x " << h);
        f.layout(w, h);
        lint(*f.editor);
    }
    f.layout(f.editor->minimumWidth(), f.editor->minimumHeight());
    f.shot("table-editor-" + state + "-compact");
    f.layout(1400, f.editor->minimumHeight());
    f.shot("table-editor-" + state);
    f.layout(f.editor->minimumWidth(), f.editor->minimumHeight());
}
} // namespace

TEST_CASE("table editor: on the module grid in every state, narrow and wide, short and tall", "[ui]")
{
    Fixture f;
    auto& t = f.table();
    CHECK(f.editor->tabName() == "Table");
    CHECK(f.editor->title() == "Osc 1, Basic Shapes");
    CHECK(f.editor->minimumHeight() >= 300);
    CHECK(f.editor->minimumHeight() <= 330); // the design's 320
    CHECK(f.editor->minimumWidth() <= 900);  // beside the narrowest module windows
    REQUIRE(t.recipe().layers.size() == 1);
    CHECK(t.recipe().layers[0].kind == K::Table);
    lintAtSizes(f, "table");

    t.addLayer(fd::devices::wavetable::newLayer(K::Draw));
    CHECK(t.view() == TableEditor::View::TwoD);
    lintAtSizes(f, "draw");
    t.addLayer(fd::devices::wavetable::newLayer(K::Harmonics));
    CHECK(t.view() == TableEditor::View::Harmonics);
    lintAtSizes(f, "harmonics");
    t.addLayer(fd::devices::wavetable::newLayer(K::Formula));
    lintAtSizes(f, "formula");
    std::vector<float> pixels(64 * 32, 0.5f);
    t.selectLayer(0);
    t.removeLayer(0);
    t.addLayer(fd::devices::wavetable::imageLayer(pixels.data(), 64, 32, false, "Gradient"));
    lintAtSizes(f, "image");

    // Every change, the longest names among them, more than the rows show.
    for (int k = 0; k <= static_cast<int>(Recipe::ChangeKind::Mirror); ++k)
    {
        Recipe::Change c;
        c.kind = static_cast<Recipe::ChangeKind>(k);
        t.addChange(c);
    }
    for (int w = static_cast<int>(fd::dsp::Warp::FormantShift); w <= static_cast<int>(fd::dsp::Warp::Shepard);
         ++w)
    {
        Recipe::Change c;
        c.kind = Recipe::ChangeKind::Warp;
        c.warp = static_cast<fd::dsp::Warp>(w);
        t.addChange(c);
    }
    CHECK(t.changeBars().size() == static_cast<size_t>(f.editor->rows()));
    lintAtSizes(f, "changes");

    // The way chooser.
    std::vector<float> saw(48000);
    for (size_t i = 0; i < saw.size(); ++i)
        saw[i] =
            static_cast<float>(2.0 * std::fmod(static_cast<double>(i) * 220.0 / 48000.0, 1.0) - 1.0) * 0.5f;
    t.takeAudio(saw, 48000.0, "Saw");
    REQUIRE(t.choosingAudio());
    lintAtSizes(f, "audio");
}

TEST_CASE("table editor: each edit kept through the host, one undo step; nothing kept unchanged", "[ui]")
{
    Fixture f;
    auto& t = f.table();
    auto& steps = f.binding->steps;
    steps.clear();

    // A factory table becomes one of yours once edited.
    t.addLayer(fd::devices::wavetable::newLayer(K::Draw));
    REQUIRE(steps.size() == 1);
    CHECK(steps.back() == "Add a layer");
    CHECK(f.synth().tableId(0) == "user");
    CHECK(f.synth().tableName(0) == "Basic Shapes (edited)");
    CHECK(f.editor->title() == "Osc 1, Basic Shapes (edited)");
    CHECK(f.synth().recipe(0) == fd::devices::wavetable::toJson(t.recipe()));
    // The module plays what the editor shows.
    const auto table = f.synth().table(0);
    REQUIRE(table->frames() == t.baked().frames());
    CHECK(std::abs(table->samples(10)[100] - t.baked().samples(10)[100]) < 1e-3f);

    // A drag on the drawing: shown while it moves, one step when let go.
    f.layout(1400, f.editor->minimumHeight());
    auto& view = t.tableView();
    const auto plot = view.plot();
    const auto before = t.recipe().layers[1].keyframes[0].points;
    const auto point =
        juce::Point<float>(plot.getX() + plot.getWidth() * 0.25f, plot.getY()); // the drawing's peak
    drag(view, point, point + juce::Point<float>(0.0f, plot.getHeight() * 0.25f));
    REQUIRE(steps.size() == 2);
    CHECK(steps.back() == "Draw a wave");
    CHECK(t.recipe().layers[1].keyframes[0].points != before);
    CHECK(t.recipe().layers[1].keyframes[0].points[1][1] < 0.6f);

    // A click that changes nothing keeps nothing.
    t.setLayerOn(0, true);
    CHECK(steps.size() == 2);

    // Keyframes on the strip: a double-click adds one at the frame, as the layer is there.
    auto& strip = t.strip();
    const float x = strip.xOf(40);
    strip.mouseDoubleClick(mouse(strip, {x, 5.0f}, {x, 5.0f}, false, 2));
    REQUIRE(t.recipe().layers[1].keyframes.size() == 2);
    CHECK(t.recipe().layers[1].keyframes[1].frame == 40);
    CHECK(t.shownFrame() == 40);
    CHECK(steps.back() == "Add a keyframe");
    t.moveKeyframe(1, 50);
    CHECK(t.recipe().layers[1].keyframes[1].frame == 50);
    t.removeKeyframe(1);
    CHECK(t.recipe().layers[1].keyframes.size() == 1);
    t.removeKeyframe(0); // the last one stays
    CHECK(t.recipe().layers[1].keyframes.size() == 1);

    // A change, its amount for the whole table, then from frame to frame.
    Recipe::Change smooth;
    smooth.kind = Recipe::ChangeKind::Smooth;
    t.addChange(smooth);
    t.setChangeAmount(0, 0.3f);
    CHECK(t.recipe().changes[0].amountAt(0) == 0.3f);
    CHECK(t.recipe().changes[0].amountAt(63) == 0.3f);
    t.setChangeVaries(0, true);
    t.showFrame(63);
    t.setChangeAmount(0, 0.9f);
    CHECK(t.recipe().changes[0].amountAt(0) == 0.3f);
    CHECK(t.recipe().changes[0].amountAt(63) == 0.9f);
    CHECK(steps.back() == "Set a change's amount");

    // Fewer frames: the keyframes keep their place in the table.
    t.addKeyframe(63);
    t.setFrames(16);
    CHECK(t.recipe().frames == 16);
    CHECK(t.recipe().layers[1].keyframes.back().frame == 15);
    CHECK(f.synth().table(0)->frames() == 16);
}

TEST_CASE("table editor: follows the module, and the oscillator OSC shows", "[ui]")
{
    Fixture f;
    auto& t = f.table();
    t.addLayer(fd::devices::wavetable::newLayer(K::Harmonics));
    REQUIRE(t.recipe().layers.size() == 2);

    // Undo (the state from before) puts back the factory table: the editor shows it again.
    f.binding->applyExtraState(W::factoryTablePatch(0, "formant/vowels"));
    t.frame();
    CHECK(t.recipe().layers.size() == 1);
    CHECK(t.recipe().layers[0].tableId == "formant/vowels");
    CHECK(f.editor->title() == "Osc 1, Vowels");

    // OSC shows another oscillator: the editor edits it.
    auto* panel = f.panel.get();
    REQUIRE(panel->showParameter(W::osc(1, W::OscPos)));
    CHECK(t.oscillator() == 1);
    CHECK(f.editor->title().startsWith("Osc 2, "));
    // Edit again from the panel: the same editor, asked to show.
    const int shown = f.shown;
    REQUIRE(panel->performCommand("table.edit"));
    CHECK(f.shown == shown + 1);
}

TEST_CASE("table editor: audio becomes a table its way, heard at once, kept or put back", "[ui]")
{
    Fixture f;
    auto& t = f.table();
    auto& steps = f.binding->steps;
    steps.clear();
    std::vector<float> saw(48000);
    for (size_t i = 0; i < saw.size(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 40; ++h)
            v += std::sin(fd::dsp::twoPi * h * 220.0 * static_cast<double>(i) / 48000.0) / h;
        saw[i] = static_cast<float>(0.5 * v);
    }
    t.takeAudio(saw, 48000.0, "Saw");
    REQUIRE(t.choosingAudio());
    CHECK(t.audioWay() == fd::devices::wavetable::AudioWay::Pitched); // it has a pitch
    REQUIRE(t.recipe().layers.size() == 1);
    CHECK(t.recipe().layers[0].kind == K::Audio);
    CHECK(t.recipe().frames == 64);
    CHECK(f.synth().tableName(0) == "Saw"); // heard at once
    t.chooseWay(fd::devices::wavetable::AudioWay::Spectral);
    t.chooseAudioFrames(16);
    CHECK(t.recipe().frames == 16);
    CHECK(steps.size() == 1); // one step for the whole choosing
    t.cancelAudio();
    CHECK_FALSE(t.choosingAudio());
    CHECK(f.synth().tableName(0) == "Basic Shapes (edited)"); // what it was, as a recipe now
    CHECK(t.recipe().layers[0].kind == K::Table);

    t.takeAudio(saw, 48000.0, "Saw");
    t.useAudio();
    CHECK_FALSE(t.choosingAudio());
    CHECK(f.synth().tableName(0) == "Saw");

    // Noise has no pitch: Slices.
    std::vector<float> noise(48000);
    uint32_t seed = 1;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
    }
    t.takeAudio(noise, 48000.0, "Noise", true);
    CHECK(t.audioWay() == fd::devices::wavetable::AudioWay::Slices);
    REQUIRE(t.recipe().layers.size() == 2);                 // added as a layer
    t.chooseWay(fd::devices::wavetable::AudioWay::Pitched); // fails: put back what was there
    CHECK(t.recipe().layers.size() == 1);
    t.useAudio(); // a failed way can't be kept: as Cancel
    CHECK_FALSE(t.choosingAudio());
}

TEST_CASE("table editor: Preview holds a note on the module until clicked again, or closed", "[ui]")
{
    Fixture f;
    auto& t = f.table();
    t.previewButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE(f.notes.size() == 1);
    CHECK(f.notes[0].second > 0.0f);
    CHECK(t.previewing());
    f.editor->closing();
    REQUIRE(f.notes.size() == 2);
    CHECK(f.notes[1] == std::pair{f.notes[0].first, 0.0f});
    CHECK_FALSE(t.previewing());
}
