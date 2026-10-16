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

/// What the shared synth parts (ui::synth, SynthParts.h) need to know of the Wavetable beyond
/// its parameters' layout.
struct WavetableParts
{
    using Synth = WavetableSynth;
    /// The group a source is listed under.
    [[nodiscard]] static const char* sourceGroup(int source) noexcept
    {
        using S = W::Source;
        const auto s = static_cast<S>(source);
        return s < S::Lfo1       ? "Envelopes"
               : s < S::Random1  ? "LFOs"
               : s < S::Macro1   ? "Random"
               : s < S::Velocity ? "Macros"
                                 : "Playing";
    }
    /// Whether a source swings both ways (an LFO, a random source, the note, the bend).
    [[nodiscard]] static bool bipolar(int source) noexcept
    {
        using S = W::Source;
        const auto s = static_cast<S>(source);
        return (s >= S::Lfo1 && s < S::Macro1) || s == S::Note || s == S::PitchBend || s == S::NoteRandom;
    }
    [[nodiscard]] static int envSource(int n) noexcept { return static_cast<int>(W::Source::AmpEnv) + n; }
    [[nodiscard]] static dsp::RandomSource::Mode randomMode(int choice) noexcept
    {
        return static_cast<dsp::RandomSource::Mode>(choice);
    }
};

/// The synth's matrix (32 routes in its parameters) for rings, handles and dragged sources.
using Modulation = ui::synth::SynthModulation<WavetableParts>;

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

/// The factory tables by category, each category a submenu (nine at a glance, table-editor.md
/// §11): what a table's menu offers. `current` is ticked; choosing one calls `chosen` with its ID.
[[nodiscard]] juce::PopupMenu factoryTableMenu(std::string_view current,
                                               std::function<void(std::string_view id)> chosen);

/// A table's dropdown: its name shown as a combo shows its choice, its menu built when it opens
/// (the factory tables by category, and what else the owner offers).
class TableField final : public ui::ChoiceBox
{
public:
    /// What the menu offers, each time it opens.
    std::function<void(juce::PopupMenu& menu)> buildMenu;
    void showPopup() override;
};

/// Oscillator n's table: previous, its name (a menu of the factory tables by category, then
/// Edit… and Import…), next.
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
    [[nodiscard]] TableField& field() noexcept { return field_; }
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
    TableField field_;
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

/// Both filters' responses, an envelope drawn and dragged, an LFO's shape or a random source
/// drawn, the matrix: the parts every synth shares (SynthParts.h).
using FilterResponse = ui::synth::SynthFilterResponse<WavetableParts>;
using EnvelopeEditor = ui::synth::SynthEnvelopeEditor<WavetableParts>;
using ShapeEditor = ui::synth::SynthShapeEditor<WavetableParts>;
using MatrixList = ui::synth::SynthMatrixList<WavetableParts>;
} // namespace fd::devices::wavetable_ui
