#pragma once

#include "WavetableSynth.h"

#include <fd_device_ui/fd_device_ui.h>

/// The Wavetable Synth's panel parts (devices/wavetable/SPEC.md, "UI layout"): its modulation as
/// the shared modulation UI sees it, and its displays. Each display draws with the functions the
/// audio path uses: the cycle renderer, the filters' magnitudes, the envelope's and the curves'
/// shapes.
namespace fd::devices::wavetable_ui
{
using W = WavetableSynth;

/// The look the Wavetable's displays share: the panel's and the table editor's.
namespace look
{
/// A display's well: flat, a step below the module's body.
void paintWell(juce::Graphics& g, juce::Rectangle<float> r);
/// A display's text and marks, from its well's edges.
[[nodiscard]] float inset();
/// A point dragged on a display (device-panels.md, "Display handle"), lit while `hot`.
void paintHandle(juce::Graphics& g, juce::Point<float> at, bool hot);
/// A cycle's samples as a line across `r`, its middle at the centre.
[[nodiscard]] juce::Path cyclePath(const float* cycle, int size, juce::Rectangle<float> r, int points);
} // namespace look

/// Where the user's tables go: the Segno folder's Wavetables (docs/design/table-editor.md §7).
[[nodiscard]] juce::File wavetablesFolder(const ui::DeviceUiContext& ctx);
/// The files Import Wavetable… offers: tables and audio.
inline constexpr const char* tableAndAudioFiles = "*.wav;*.wt;*.aif;*.aiff;*.flac;*.mp3";
/// Puts the table in `file` on oscillator n through the host, one undo step; says why when it
/// can't read it.
bool loadTableFile(ui::DeviceUiContext& ctx, int n, const juce::File& file);
/// Export Wavetable…: oscillator n's table as WAV (`clm `) or .wt, into Segno/Wavetables by
/// default. `owner` keeps the chooser's answer from outliving the panel or editor that asked.
void exportTable(juce::Component& owner, ui::DeviceUiContext& ctx, int n,
                 std::unique_ptr<juce::FileChooser>& chooser);

/// The synth's matrix (32 routes in its parameters) for rings, handles and dragged sources.
class Modulation final : public ui::ModulationModel
{
public:
    explicit Modulation(ui::DeviceUiContext& context) : ctx_(context) {}

    [[nodiscard]] std::vector<Route> routes() const override;
    [[nodiscard]] bool canModulate(size_t param) const override { return W::targetOf(param).has_value(); }
    [[nodiscard]] juce::String sourceName(int source) const override;
    bool addRoute(int source, size_t param) override;
    [[nodiscard]] bool isFull() const override;
    void removeRoute(int index) override;
    void beginAmount(int index) override;
    void setAmount(int index, float amount) override;
    void endAmount(int index) override;
    [[nodiscard]] std::optional<int> routeOfParam(size_t param) const override;
    [[nodiscard]] std::vector<SourceInfo> sources() const override;

    /// A route from the matrix's add button: the source last hovered (else LFO 1), no target yet.
    bool addFromMatrix();
    /// The routes the matrix lists: those with a source, in slot order.
    [[nodiscard]] std::vector<int> listed() const;
    /// Whether a source swings both ways (an LFO, a random source, the note, the bend).
    [[nodiscard]] static bool bipolar(int source) noexcept;

private:
    [[nodiscard]] int freeSlot() const;
    ui::DeviceUiContext& ctx_;
};

/// Oscillator n's table in 3D: its frames from front to back, the cycle it plays (Position, its
/// warps) highlighted where it is, from the newest note while one plays. Dragged, it moves
/// Position; double-clicked, it opens the table editor; a table or audio file dropped on it
/// loads.
class TableDisplay final : public juce::Component,
                           public ui::ParamControl,
                           public juce::SettableTooltipClient,
                           public juce::FileDragAndDropTarget
{
public:
    /// A double-click: edit the table.
    std::function<void()> onEdit;
    /// A file dropped on it.
    std::function<void(const juce::File&)> onFile;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    explicit TableDisplay(ui::DeviceUiContext& context);
    void setOscillator(int n);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void resized() override { frames_.clear(); }
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    /// The position drawn (0..1): the newest note's while one plays, else Position's.
    [[nodiscard]] float shownPosition() const noexcept { return shown_; }

private:
    [[nodiscard]] const dsp::Wavetable* table() const;
    void buildFrames(juce::Rectangle<float> area);

    ui::DeviceUiContext& ctx_;
    int osc_ = 0;
    const dsp::Wavetable* builtFor_ = nullptr;
    std::vector<juce::Path> frames_; // back to front, in the well
    juce::Point<float> depth_;       // from the front frame to the back one
    dsp::CycleRenderer renderer_;
    std::vector<float> cycle_;
    float shown_ = -1.0f, dragStart_ = 0.0f;
    std::vector<float> last_;
};

/// Oscillator n's table: previous, its name (a menu of the factory tables by category, and
/// Import a file…), next.
class TablePicker final : public juce::Component, public ui::ParamControl
{
public:
    explicit TablePicker(ui::DeviceUiContext& context);
    void setOscillator(int n);
    void refresh() override;
    void resized() override;
    /// Steps through the factory tables (wrapping round).
    void step(int delta);
    /// Puts factory table `id` on the oscillator, through the host.
    void choose(std::string_view id);
    [[nodiscard]] juce::ComboBox& field() noexcept { return field_; }
    /// Its menu's Edit… (shown when set): edit the table.
    std::function<void()> onEdit;
    /// Its menu's Import…: a table or audio (the panel's Import Wavetable…; without it, tables
    /// only).
    std::function<void()> onImport;
    /// Shows Edit… in its menu.
    void setEditable(bool editable);

private:
    void importFile();
    void rebuildMenu();

    ui::DeviceUiContext& ctx_;
    int osc_ = 0;
    ui::IconButton previous_{ui::Icon::ChevronLeft}, next_{ui::Icon::ChevronRight};
    juce::ComboBox field_;
    std::string shownId_, shownName_;
    bool editable_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
};

/// The cycle oscillator n's warps make, from its table at Position; while a warp is Remap, its
/// curve over it, edited by dragging points (double-click adds or removes one).
class CycleDisplay final : public juce::Component, public ui::ParamControl, public juce::SettableTooltipClient
{
public:
    explicit CycleDisplay(ui::DeviceUiContext& context);
    void setOscillator(int n);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    /// The warp (0 or 1) whose Remap curve shows, or -1.
    [[nodiscard]] int remapWarp() const;

private:
    [[nodiscard]] juce::Rectangle<float> plot() const;
    [[nodiscard]] W::Curve curve() const;
    void setCurve(const W::Curve& c);

    ui::DeviceUiContext& ctx_;
    int osc_ = 0, dragged_ = -1;
    W::Curve editing_; // the curve while a point is dragged (set once, on release)
    dsp::CycleRenderer renderer_;
    std::vector<float> cycle_;
    std::vector<float> last_;
};

/// The sub's wave (Shape), or a moment of the noise (Type, Color): the source drawn.
class SourceWave final : public ui::MiniDisplay
{
public:
    SourceWave(ui::DeviceUiContext& context, bool noise);

private:
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override;
    bool noise_;
};

/// Both filters' responses (the selected one bright, the other faint), 20 Hz to 20 kHz. Dragged,
/// the selected filter's Cutoff sideways and Resonance up and down.
class FilterResponse final : public juce::Component,
                             public ui::ParamControl,
                             public juce::SettableTooltipClient
{
public:
    explicit FilterResponse(ui::DeviceUiContext& context);
    void setFilter(int n);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] ui::SpectrumPlot plot() const;
    ui::DeviceUiContext& ctx_;
    int filter_ = 0;
    float startCutoff_ = 0.0f, startRes_ = 0.0f;
    std::vector<float> last_;
};

/// Envelope n drawn from its times, sustain and curves, and dragged: the attack's peak sideways
/// (Attack), the decay's end both ways (Decay, Sustain), the release's end sideways (Release); a
/// segment dragged up or down bends it (its curve). Its source handle in the corner.
class EnvelopeEditor final : public juce::Component,
                             public ui::ParamControl,
                             public juce::SettableTooltipClient
{
public:
    EnvelopeEditor(ui::DeviceUiContext& context, Modulation& modulation);
    void setEnvelope(int n);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /// What a press at `p` drags: a point (0 attack, 1 decay, 2 release) or a segment's bend
    /// (3 attack, 4 decay, 5 release), or -1.
    [[nodiscard]] int partAt(juce::Point<float> p) const;
    [[nodiscard]] ui::ModulationHandle& handle() noexcept { return handle_; }

private:
    struct Layout
    {
        juce::Rectangle<float> area;
        float attackEnd = 0.0f, holdEnd = 0.0f, decayEnd = 0.0f, sustainEnd = 0.0f, releaseEnd = 0.0f;
    };
    [[nodiscard]] Layout layout() const;
    [[nodiscard]] juce::Point<float> pointOf(int part, const Layout& l) const;
    [[nodiscard]] float param(W::EnvParam p) const;

    ui::DeviceUiContext& ctx_;
    ui::ModulationHandle handle_;
    int env_ = 0, dragged_ = -1;
    juce::Point<float> start_;
    std::array<float, 3> startValues_{};
    std::vector<float> last_;
};

/// LFO n's shape drawn and edited (points dragged, double-click adds or removes one, a segment
/// dragged up or down bends it, a named shape from the menu in its corner), or random source n's
/// way of moving drawn. Its phase while one plays; its source handle in the corner.
class ShapeEditor final : public juce::Component, public ui::ParamControl, public juce::SettableTooltipClient
{
public:
    ShapeEditor(ui::DeviceUiContext& context, Modulation& modulation);
    /// 0-7: an LFO; 8-9: a random source.
    void setSource(int n);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    [[nodiscard]] juce::Rectangle<float> plot() const;
    [[nodiscard]] W::Curve curve() const;
    void setCurve(const W::Curve& c);
    [[nodiscard]] ui::ModulationHandle& handle() noexcept { return handle_; }

private:
    [[nodiscard]] bool isLfo() const noexcept { return source_ < W::numLfos; }
    [[nodiscard]] int pointAt(juce::Point<float> p) const;
    void showShapes();

    ui::DeviceUiContext& ctx_;
    ui::ModulationHandle handle_;
    ui::IconButton shapes_{ui::Icon::ChevronDown};
    int source_ = 0, dragged_ = -1, bending_ = -1;
    float bendStart_ = 0.0f, bendY_ = 0.0f;
    W::Curve editing_;
    std::string lastState_;
    float phase_ = -1.0f;
};

/// The modulation matrix: a caption row with Add, then a row a route (source, target, amount,
/// curve, scale) on the module grid, five showing and the wheel scrolling the rest (the section's
/// title says which). Right-click a route's control to remove the route.
class MatrixList final : public juce::Component, public ui::ParamControl
{
public:
    MatrixList(ui::DeviceUiContext& context, Modulation& modulation);
    ~MatrixList() override;
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    /// The routes listed (with a source), and the first one showing (for tests).
    [[nodiscard]] const std::vector<int>& rows() const noexcept { return listed_; }
    [[nodiscard]] int firstShown() const noexcept { return first_; }
    [[nodiscard]] juce::Button& addButton() noexcept { return add_; }

private:
    void rebuild();

    ui::DeviceUiContext& ctx_;
    Modulation& modulation_;
    ui::IconButton add_{ui::Icon::Plus};
    std::vector<std::unique_ptr<juce::Component>> controls_;
    std::vector<int> listed_;
    int first_ = 0;
};
} // namespace fd::devices::wavetable_ui
