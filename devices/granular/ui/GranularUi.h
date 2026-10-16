#pragma once

#include "GrainWindow.h"
#include "Granular.h"

#include <fd_device_ui/fd_device_ui.h>

/// Granular's panel parts (devices/granular/SPEC.md, "UI layout"): its modulation and the parts
/// every synth shares (SynthParts.h), and its own displays. Each display draws with what the
/// audio path uses: the grains it publishes, the window it plays, the grain filter's magnitude.
namespace fd::devices::granular_ui
{
using G = Granular;

/// What the shared synth parts (ui::synth) need to know of Granular beyond its parameters'
/// layout.
struct GranularParts
{
    using Synth = Granular;
    [[nodiscard]] static const char* sourceGroup(int source) noexcept
    {
        using S = G::Source;
        const auto s = static_cast<S>(source);
        return s < S::Lfo1          ? "Envelopes"
               : s < S::Random1     ? "LFOs"
               : s < S::Macro1      ? "Random"
               : s < S::Velocity    ? "Macros"
               : s < S::GrainRandom ? "Playing"
                                    : "Grains";
    }
    /// Whether a source swings both ways (an LFO, a random source, the note, the bend, the grain's
    /// random and alternate).
    [[nodiscard]] static bool bipolar(int source) noexcept
    {
        using S = G::Source;
        const auto s = static_cast<S>(source);
        return (s >= S::Lfo1 && s < S::Macro1) || s == S::Note || s == S::PitchBend || s == S::GrainRandom ||
               s == S::GrainAlternate;
    }
    [[nodiscard]] static int envSource(int n) noexcept { return static_cast<int>(G::Source::AmpEnv) + n; }
    /// Per Grain draws as steps: a new value with each grain.
    [[nodiscard]] static dsp::RandomSource::Mode randomMode(int choice) noexcept
    {
        return choice == G::RandomPerGrain ? dsp::RandomSource::Mode::Steps
                                           : static_cast<dsp::RandomSource::Mode>(choice);
    }
};

using Modulation = ui::synth::SynthModulation<GranularParts>;
using FilterResponse = ui::synth::SynthFilterResponse<GranularParts>;
using EnvelopeEditor = ui::synth::SynthEnvelopeEditor<GranularParts>;
using ShapeEditor = ui::synth::SynthShapeEditor<GranularParts>;
using MatrixList = ui::synth::SynthMatrixList<GranularParts>;

/// The audio files a source takes.
inline constexpr const char* audioFiles = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.sfz";
/// Puts `file` into source n through the host, one undo step.
void loadSourceFile(ui::DeviceUiContext& ctx, int n, const juce::File& file);

/// The shown source: its waveform (both channels), the part grains come from (Start and End
/// dragged), Position (dragged) with its spray, the scan's playhead, and every grain playing:
/// where it reads, its pitch (up and down), its level, its source. With the Input on Live, the
/// input buffer, its newest moment at the right. Empty, what to drop on it.
class SourceDisplay final : public juce::Component,
                            public ui::ParamControl,
                            public juce::SettableTooltipClient,
                            public juce::FileDragAndDropTarget
{
public:
    explicit SourceDisplay(ui::DeviceUiContext& context);
    void setSource(int n);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    /// A click on an empty display: browse for a file.
    std::function<void()> onBrowse;

private:
    [[nodiscard]] bool live() const;
    [[nodiscard]] std::shared_ptr<const GrainSource> shown() const;
    [[nodiscard]] juce::Rectangle<float> waveArea() const;
    [[nodiscard]] juce::Rectangle<int> textLine() const;
    /// Where a place in the file (0 … 1) is drawn.
    [[nodiscard]] float xOf(double place) const;
    [[nodiscard]] double placeAt(float x) const;
    /// The part's start and end, and the position, as places in the file (0 … 1).
    [[nodiscard]] double partStart() const;
    [[nodiscard]] double partEnd() const;
    [[nodiscard]] double position() const;
    /// The marker a press at `x` drags: Start, End or Position (a parameter), or none.
    [[nodiscard]] std::optional<size_t> partAt(float x) const;
    void paintWave(juce::Graphics& g, const GrainSource& s, juce::Rectangle<float> lane, int channel,
                   juce::Colour inside) const;
    void paintMarker(juce::Graphics& g, double place, const char* tag, bool right) const;

    ui::DeviceUiContext& ctx_;
    int source_ = 0;
    std::optional<size_t> dragging_;
    bool dragOver_ = false;
    std::vector<float> last_;
    std::shared_ptr<const GrainSource> lastData_;
    std::vector<G::GrainMark> grains_;
};

/// The shown source's name, with the previous and next audio file in its folder; its menu loads
/// another or clears it.
class SourcePicker final : public juce::Component, public ui::ParamControl
{
public:
    explicit SourcePicker(ui::DeviceUiContext& context);
    void setSource(int n);
    void refresh() override;
    void resized() override;
    /// Load…: browse for a file.
    void browse();

private:
    void step(int delta);
    ui::DeviceUiContext& ctx_;
    int source_ = 0;
    ui::IconButton previous_{ui::Icon::ChevronLeft}, next_{ui::Icon::ChevronRight};
    ui::ChoiceBox field_;
    juce::String shownName_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

/// The grains' window as the renderer plays it (GrainWindow), drawn small: dragged sideways for
/// Skew, up and down for Flatness.
class WindowDisplay final : public juce::Component,
                            public ui::ParamControl,
                            public juce::SettableTooltipClient
{
public:
    explicit WindowDisplay(ui::DeviceUiContext& context);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /// The window as the current settings make it.
    [[nodiscard]] granular::GrainWindow window() const;

private:
    ui::DeviceUiContext& ctx_;
    float startSkew_ = 0.0f, startFlat_ = 0.0f;
    std::vector<float> last_;
    mutable dsp::CurveTable drawn_{}; // the Drawn window's table, made as it's drawn
};

/// The grain filter's response, its cutoff's spray as a band around it: dragged sideways for the
/// cutoff, up and down for the resonance.
class GrainFilterResponse final : public juce::Component,
                                  public ui::ParamControl,
                                  public juce::SettableTooltipClient
{
public:
    explicit GrainFilterResponse(ui::DeviceUiContext& context);
    void refresh() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /// The grain filter's magnitude at `hz`, as each grain's filter has it.
    [[nodiscard]] static double magnitude(int type, double hz, double cutoff, double res) noexcept;

private:
    [[nodiscard]] ui::SpectrumPlot plot() const;
    ui::DeviceUiContext& ctx_;
    float startCutoff_ = 0.0f, startRes_ = 0.0f;
    std::vector<float> last_;
};
} // namespace fd::devices::granular_ui
