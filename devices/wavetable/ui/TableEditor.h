#pragma once

#include "AudioToTable.h"
#include "TableRecipe.h"
#include "WavetableSynth.h"

#include <fd_device_ui/fd_device_ui.h>

/// The Wavetable's table editor (docs/design/table-editor.md §4, ADR 0053): an oscillator's table
/// as a recipe. Its layers on the left with the selected one's settings, the table in the middle
/// (in 3D, a frame in 2D, a frame's harmonics) over the keyframe strip, its changes on the
/// right. Each edit is kept on release: the recipe and the frames baked from it go to the module
/// through the host, one undo step. Everything it draws comes from the same bake the module
/// plays.
namespace fd::devices::wavetable_ui
{
class TableEditor;

/// The middle of the editor: the table in 3D (dragged up and down to go through its frames), the
/// shown frame in 2D (a Draw layer's keyframe drawn on: points dragged, segments bent,
/// double-click adds or removes a point, or freehand), or its harmonics (a Harmonics layer's
/// keyframe set by dragging across the bars, its phases in the strip below them).
class TableView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TableView(TableEditor& editor);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    /// The editor's state changed: draw again.
    void update();

    /// Where the drawing is (2D), and the harmonics' bars and phases (Harmonics).
    [[nodiscard]] juce::Rectangle<float> plot() const;
    [[nodiscard]] juce::Rectangle<float> barsArea() const;
    [[nodiscard]] juce::Rectangle<float> phasesArea() const;

private:
    /// The keyframe the view edits: the selected layer's at the shown frame, if it's a Draw or
    /// Harmonics layer with one there (else null).
    [[nodiscard]] const wavetable::Recipe::Keyframe* editable() const;
    void paint3d(juce::Graphics& g, juce::Rectangle<float> area);
    void paint2d(juce::Graphics& g, juce::Rectangle<float> area);
    void paintHarmonics(juce::Graphics& g, juce::Rectangle<float> area);
    void drawFreehand(juce::Point<float> from, juce::Point<float> to);
    void setBar(juce::Point<float> at, bool phases);

    TableEditor& editor_;
    wavetable::Recipe editing_; // the recipe while a drag edits it
    bool dragging_ = false;
    int dragged_ = -1, bending_ = -1, startFrame_ = 0;
    float bendStart_ = 0.0f, bendY_ = 0.0f;
    bool phasesDrag_ = false;
    juce::Point<float> last_;
    juce::String undoName_;
    const dsp::Wavetable* builtFor_ = nullptr; // the 3D frames' table
    std::vector<juce::Path> frames_;           // the 3D frames, back to front
    juce::Point<float> depth_;
};

/// The table's frames from left to right: the shown one marked, a ◆ for each keyframe of the
/// selected layer. Click a frame to show it, double-click to add a keyframe there, drag a ◆ to
/// move it; its menu removes it.
class KeyframeStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit KeyframeStrip(TableEditor& editor);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    /// The frame at `x`, and where a frame is drawn.
    [[nodiscard]] int frameAt(float x) const;
    [[nodiscard]] float xOf(int frame) const;
    /// The selected layer's keyframe whose ◆ is at `x` (-1: none).
    [[nodiscard]] int keyframeAt(float x) const;

private:
    [[nodiscard]] juce::Rectangle<float> track() const;
    TableEditor& editor_;
    int dragged_ = -1, dragFrame_ = -1;
};

/// A line of text in an editor's pane: a layer's note, what the audio held, a formula's error.
class NoteText final : public juce::Component
{
public:
    void setText(juce::String text, bool warning = false);
    [[nodiscard]] const juce::String& text() const noexcept { return text_; }
    void paint(juce::Graphics& g) override;

private:
    juce::String text_;
    bool warning_ = false;
};

class TableEditor final : public ui::ModuleEditor, public juce::FileDragAndDropTarget
{
public:
    using Recipe = wavetable::Recipe;
    enum class View
    {
        ThreeD,
        TwoD,
        Harmonics
    };
    enum class DrawTool
    {
        Points,
        Freehand
    };

    TableEditor(ui::DeviceUiContext& context, int oscillator);
    ~TableEditor() override;

    [[nodiscard]] juce::String tabName() const override { return "Table"; }
    [[nodiscard]] juce::String title() const override;
    void closing() override;

    /// The oscillator it edits; another (the one OSC shows) loads that one's table.
    [[nodiscard]] int oscillator() const noexcept { return osc_; }
    void setOscillator(int n);

    // ---- what it shows -------------------------------------------------------------------------

    [[nodiscard]] const Recipe& recipe() const noexcept { return recipe_; }
    /// The table baked from the recipe as it is now (during a drag, before it's kept).
    [[nodiscard]] const dsp::Wavetable& baked() const noexcept { return baked_; }
    [[nodiscard]] int selectedLayer() const noexcept { return layer_; }
    [[nodiscard]] const Recipe::Layer* layer() const noexcept;
    [[nodiscard]] int shownFrame() const noexcept { return frame_; }
    [[nodiscard]] View view() const noexcept { return view_; }
    [[nodiscard]] DrawTool drawTool() const noexcept { return tool_; }
    /// The drawing's grid: steps across a cycle that points snap to (0: none).
    [[nodiscard]] int grid() const noexcept { return grid_; }
    /// Whether the note Preview holds is sounding.
    [[nodiscard]] bool previewing() const noexcept { return previewNote_ >= 0; }

    void selectLayer(int index);
    void showFrame(int frame);
    void setView(View view);

    // ---- edits: each kept at once, one undo step -------------------------------------------------

    /// While a drag edits: the recipe as it is now, shown but not kept.
    void preview(Recipe recipe);
    /// Kept: the recipe, its baked frames, one undo step named `what`.
    void keep(Recipe recipe, const juce::String& what);

    void addLayer(Recipe::Layer layer);
    void removeLayer(int index);
    void moveLayer(int index, int delta);
    void setLayerOn(int index, bool on);
    /// The selected layer's keyframes.
    void addKeyframe(int frame);
    void moveKeyframe(int index, int frame);
    void removeKeyframe(int index);
    void addChange(Recipe::Change change);
    void removeChange(int index);
    void moveChange(int index, int delta);
    void setChangeOn(int index, bool on);
    /// A change's amount: at the shown frame while it varies from frame to frame, else for the
    /// whole table.
    void setChangeAmount(int index, float amount);
    void setChangeVaries(int index, bool varies);
    void setFrames(int count);

    // ---- audio to wavetable (§5) -----------------------------------------------------------------

    /// Audio to make a table from: the way chooser opens, a new table from it (or, `asLayer`, a
    /// layer added to this one), heard at once and kept or put back.
    void takeAudio(std::vector<float> mono, double sampleRate, juce::String name, bool asLayer = false);
    [[nodiscard]] bool choosingAudio() const noexcept { return audio_.has_value(); }
    [[nodiscard]] wavetable::AudioWay audioWay() const noexcept;
    void chooseWay(wavetable::AudioWay way);
    void chooseAudioFrames(int count);
    /// Keeps the table made from the audio, or puts back the one before.
    void useAudio();
    void cancelAudio();

    // ---- files -------------------------------------------------------------------------------------

    /// A file: a table loads on the oscillator as it is; audio opens the way chooser.
    void openFile(const juce::File& file, bool asLayer = false);
    /// Import Wavetable…: a file chooser for tables and audio.
    void startImport(bool asLayer = false);
    /// Export Wavetable…: the oscillator's table as WAV (`clm `) or .wt, into Segno/Wavetables.
    void startExport();

    // ---- for the layout lint and tests -------------------------------------------------------------

    [[nodiscard]] TableView& tableView() noexcept { return view3d_; }
    [[nodiscard]] KeyframeStrip& strip() noexcept { return strip_; }
    [[nodiscard]] juce::Button& previewButton() noexcept { return preview_; }
    [[nodiscard]] juce::Button& addLayerButton() noexcept { return addLayer_; }
    [[nodiscard]] juce::Button& addChangeButton() noexcept { return addChange_; }
    /// Each change's amount bar, in order (the ones showing).
    [[nodiscard]] std::vector<ui::Knob*> changeBars() const;

    /// Once a display frame (the tests call it): bakes what a drag changed, follows the module.
    void frame();

    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    /// The rows the panes need: four layers, a label, their settings; at its least height.
    static constexpr int kRows = 10;

    struct PendingAudio
    {
        std::vector<float> samples;
        double sampleRate = 0.0;
        std::string name;
        wavetable::AudioWay way = wavetable::AudioWay::Pitched;
        int frames = 64;
        bool asLayer = false;
        Recipe before; // put back on Cancel
        std::string message;
        bool failed = false;
        bool started = false; // its undo step is open
    };

    /// Loads the oscillator's recipe from the module (or a recipe of its table).
    void load();
    /// Follows the module: undo, an assistant or the table picker changed the table.
    void follow();
    void bakeNow();
    void commit(const juce::String& what, bool newStep = true);
    void rebuildAll();
    /// The layers and the selected one's settings (or the way chooser).
    void rebuildLeft();
    void rebuildChanges();
    void updateTitle();
    void showAddLayerMenu();
    void showAddChangeMenu();
    void showLayerMenu(int index);
    void showChangeMenu(int index);
    void runAudio();
    void togglePreview();
    void importImage(bool asHarmonics);
    [[nodiscard]] juce::String changeName(const Recipe::Change& change) const;
    template <typename T>
    static T& make(std::vector<std::unique_ptr<juce::Component>>& owner, std::unique_ptr<T> control)
    {
        auto& ref = *control;
        owner.push_back(std::move(control));
        return ref;
    }

    ui::DeviceUiContext& ctx_;
    int osc_ = 0;
    Recipe recipe_;
    dsp::Wavetable baked_;
    bool rebake_ = false, editing_ = false;
    nlohmann::json kept_;                  // the recipe as loaded or last kept
    const dsp::Wavetable* seen_ = nullptr; // the module's table when last followed...
    nlohmann::json seenRecipe_;            // ...and its recipe
    int layer_ = 0, frame_ = 0, firstChange_ = 0;
    View view_ = View::ThreeD;
    DrawTool tool_ = DrawTool::Points;
    int grid_ = 16;
    int previewNote_ = -1;
    std::optional<PendingAudio> audio_;
    juce::String shownTitle_;

    int layersPane_ = 0, viewPane_ = 0, changesPane_ = 0;
    ui::Toggle preview_{"Preview"};
    ui::IconButton addLayer_{ui::Icon::Plus}, addChange_{ui::Icon::Plus};
    ui::Selector views_{{"3D", "2D", "Harmonics"}, 3};
    ui::HeaderReadout frameReadout_;
    TableView view3d_{*this};
    KeyframeStrip strip_{*this};
    ui::Knob frames_;
    std::vector<std::unique_ptr<juce::Component>> leftControls_, changeControls_;
    std::vector<ui::Knob*> changeBars_;
    juce::TextEditor* formula_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::VBlankAttachment vblank_{this, [this] { frame(); }};
};
} // namespace fd::devices::wavetable_ui
