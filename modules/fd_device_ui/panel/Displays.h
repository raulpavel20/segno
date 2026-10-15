#pragma once

namespace fd::ui
{
/// A flat well with a corner label for small device displays (oscillator, filter, envelope
/// views). Draws from pure DSP functions and repaints only when its parameters change.
class MiniDisplay : public juce::Component, public ParamControl
{
public:
    MiniDisplay(DeviceUiContext& context, juce::String label, std::vector<size_t> params);

    void refresh() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    /// One small control in the label line, at the right (e.g. the Mod envelope's Cycle).
    void setAccessory(juce::Component& accessory, int width);
    [[nodiscard]] juce::Component* accessory() const noexcept { return accessory_; }

protected:
    /// `area` excludes the label line (use the full bounds when there is no label).
    virtual void drawContent(juce::Graphics& g, juce::Rectangle<float> area) = 0;
    /// The area drawContent gets.
    [[nodiscard]] juce::Rectangle<float> contentArea() const;
    [[nodiscard]] float param(size_t index) const { return context_.params.getPlain(index); }

private:
    DeviceUiContext& context_;
    juce::String label_;
    juce::Component* accessory_ = nullptr;
    int accessoryWidth_ = 0;
    std::vector<size_t> params_;
    std::vector<float> last_;
};

/// An envelope dragged on its display (device-panels.md, "Displays"): the attack's peak sets its
/// attack, the decay's end its decay and sustain, the release's end its release; with curves, a
/// segment dragged up or down bends it. Times double for every bend travel dragged right and halve
/// dragged left, the sustain follows the pointer's height, Shift drags finely; one gesture a
/// parameter, from the press to the release. The Subtractive's, the Sampler's, the Wavetable's and
/// the Envelope modulator's displays share it.
class EnvelopeDrag
{
public:
    /// The parameters it sets: times in milliseconds, the sustain over its range, and the attack's,
    /// decay's and release's curves (bends) where it has them.
    struct Params
    {
        size_t attack = 0, decay = 0, sustain = 0, release = 0;
        std::optional<std::array<size_t, 3>> curves;
    };
    enum Part
    {
        none = -1,
        attack = 0,
        decay = 1,
        release = 2,
        attackBend = 3,
        decayBend = 4,
        releaseBend = 5
    };
    EnvelopeDrag(DeviceUiContext& context, Params params) : context_(context), params_(params) {}
    void setParams(Params params) { params_ = params; }

    /// What a press at `at` takes: the point of `points` (attack, decay and, unless it cycles,
    /// release, on screen) within a handle's reach, else with curves the segment under it
    /// (`segments`: the attack's, decay's and release's spans across, within `plot`'s height).
    [[nodiscard]] int partAt(const std::vector<juce::Point<float>>& points,
                             const std::array<juce::Range<float>, 3>& segments, juce::Rectangle<float> plot,
                             juce::Point<float> at) const;
    /// The pointer over a part: the decay's point moves both ways, the other points sideways, a
    /// segment up and down.
    [[nodiscard]] static juce::MouseCursor cursorFor(int part);
    /// Draws `points` as display handles, the one dragged lit.
    void paint(juce::Graphics& g, const std::vector<juce::Point<float>>& points) const;

    void begin(int part);
    /// The pointer `delta` from where it was pressed; `height` the plot's (the sustain's travel).
    void drag(juce::Point<float> delta, bool fine, float height);
    void end();
    [[nodiscard]] int dragged() const noexcept { return part_; }

private:
    [[nodiscard]] std::vector<size_t> paramsOf(int part) const;

    DeviceUiContext& context_;
    Params params_;
    int part_ = none;
    std::array<float, 2> start_{};
};

/// ADSR shape drawn by running dsp::Adsr itself. Expects four consecutive parameters: attack,
/// decay, release in ms and sustain in percent (A, D, S, R), plus an optional cycle toggle.
/// Segment widths follow the log of their times and fill the display. Its points are dragged
/// (EnvelopeDrag): the release's only while it doesn't cycle.
class EnvelopeDisplay final : public MiniDisplay, public juce::SettableTooltipClient
{
public:
    EnvelopeDisplay(DeviceUiContext& context, juce::String label, size_t attackIndex,
                    std::optional<size_t> cycleIndex = std::nullopt);

    /// Its points on screen: the attack's peak, the decay's end and, unless it cycles, the
    /// release's end.
    [[nodiscard]] std::vector<juce::Point<float>> points() const;
    /// The point a press at `at` drags (EnvelopeDrag::Part), or none.
    [[nodiscard]] int partAt(juce::Point<float> at) const;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    /// Where its segments end across its plot, as drawn.
    struct Layout
    {
        juce::Rectangle<float> plot;
        float attackEnd = 0.0f, decayEnd = 0.0f, sustainEnd = 0.0f, releaseEnd = 0.0f;
        bool cycle = false;
    };
    [[nodiscard]] Layout layout() const;
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override;

    size_t attack_;
    std::optional<size_t> cycle_;
    EnvelopeDrag drag_;
};
/// Magnitude response of the instruments' filter section (dsp::SynthFilter) at the panel's
/// type, cutoff (Hz) and resonance (percent). Dimmed while the optional enable toggle is off.
class FilterDisplay final : public MiniDisplay
{
public:
    FilterDisplay(DeviceUiContext& context, size_t typeIndex, size_t cutoffIndex, size_t resonanceIndex,
                  std::optional<size_t> enabledIndex = std::nullopt);

private:
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override;

    size_t type_, cutoff_, resonance_;
    std::optional<size_t> enabled_;
};

/// A low and a high cut (12 dB/oct, `dsp::FeedbackPath::magnitude`: what the audio path filters),
/// 20 Hz to 20 kHz, dimmed while its power is off; drag near a cutoff to move it (Shift: finely).
/// The Delay's feedback filter, the Reverb's filter going in.
class CutFilterCurve final : public MiniDisplay, public juce::SettableTooltipClient
{
public:
    CutFilterCurve(DeviceUiContext& context, size_t power, size_t low, size_t high);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override;
    [[nodiscard]] float xFor(double hz) const;

    DeviceUiContext& context_;
    size_t power_, low_, high_, dragged_;
    juce::Rectangle<float> area_;
    double start_ = 0.0;
    float startX_ = 0.0f;
};

/// The analysers' axes (the EQ, the Spectrum; devices/spectrum/SPEC.md): frequency on a log scale
/// from 20 Hz to 20 kHz across `area`, decibels from `topDb` at the top to `bottomDb` at the
/// bottom, curves tilted by `slope`. Both draw with it, so they read alike.
struct SpectrumPlot
{
    juce::Rectangle<float> area;
    float topDb = 0.0f, bottomDb = -90.0f, slope = 0.0f;

    static constexpr double minHz = 20.0, maxHz = 20000.0;
    /// How curves are told apart besides their colour.
    enum class Dash
    {
        Solid,
        Dashed,
        LongDashed,
        Dotted
    };

    [[nodiscard]] float xOf(double hz) const noexcept;
    [[nodiscard]] double hzAt(float x) const noexcept;
    /// A level on screen, clamped to the area.
    [[nodiscard]] float yOf(float db) const noexcept;
    /// A band of `curve` on screen: tilted by the slope, `offsetDb` added.
    [[nodiscard]] juce::Point<float> pointOf(const dsp::SpectrumCurve& curve, int band, float offsetDb = 0.0f) const noexcept;
    /// `curve` as a line across the area; bands with nothing sit on the bottom.
    [[nodiscard]] juce::Path line(const dsp::SpectrumCurve& curve, float offsetDb = 0.0f) const;
    /// `line` closed along the bottom, for the flat fill under it.
    [[nodiscard]] juce::Path fill(const juce::Path& line) const;
    /// The frequency grid with its labels along the bottom; with `dbStep`, a line every
    /// `dbStep` dB, labelled at the right.
    void paintGrid(juce::Graphics& g, float dbStep = 0.0f) const;
    /// Strokes `path` in `colour`, solid or dashed.
    static void stroke(juce::Graphics& g, const juce::Path& path, juce::Colour colour, Dash dash = Dash::Solid,
                       float width = 1.0f);
};

/// A device's FIFO through the shared analyser (devices/spectrum/SPEC.md), for a spectrum behind a
/// curve: the EQ's, the Filter's. Both read alike.
class FifoSpectrum
{
public:
    static constexpr int order = 12;
    static constexpr double averagingMs = 150.0; // lively behind a curve being shaped

    /// Takes what the FIFO holds (the UI's side of it).
    void pull(SampleFifo& fifo, double sampleRate);
    [[nodiscard]] bool hasData() const noexcept { return analyser_.hasData(); }
    [[nodiscard]] const dsp::SpectrumCurve& bands() const noexcept { return bands_; }

private:
    dsp::SpectrumAnalyser analyser_;
    dsp::SpectrumCurve bands_{};
};

/// Values a device publishes, in a well: each a caption over its value, in a grid of `columns`
/// (the Limiter's loudness, the Loudness Meter's readings). With `large`, the first spans the width
/// in a larger type. Rows are spread evenly down the well. Each value can carry a tooltip.
class Readouts : public juce::Component, public juce::TooltipClient
{
public:
    struct Value
    {
        juce::String caption, text;
        juce::Colour colour; // the value's (transparent: textPrimary; none: "-" in textTertiary)
        juce::String tooltip;
        friend bool operator==(const Value&, const Value&) = default;
    };
    /// What a value shows while there's no reading: an en dash.
    static const juce::String& none();

    Readouts(int columns, bool large);
    /// Repaints only when something changed.
    void set(std::vector<Value> values);
    void paint(juce::Graphics& g) override;
    juce::String getTooltip() override;

private:
    /// Where each value sits: its caption's line and its value's below it.
    [[nodiscard]] std::vector<juce::Rectangle<float>> cells() const;

    int columns_;
    bool large_;
    std::vector<Value> values_;
};

/// A dynamics device's last four seconds, scrolling right to left: the input level (a dim fill
/// from the bottom), the output level (a line) and the gain reduction (from the top, in the value
/// colour), with a dashed reference line (the threshold, the ceiling) and the current reduction
/// in the corner. Frames of (input dB, output dB, reduction dB) come from one of the device's
/// FIFOs, one every 10 ms (Compressor, Limiter).
class DynamicsHistory final : public juce::Component, public ParamControl
{
public:
    DynamicsHistory(DeviceUiContext& context, int fifo, std::function<float()> referenceDb, float floorDb = -60.0f);

    /// Repaints when the reference moves.
    void refresh() override;
    /// Every frame: takes the new frames. `visible` false only drains the FIFO.
    void tick(bool visible);
    void paint(juce::Graphics& g) override;

private:
    static constexpr int kFrames = 400; // 4 s at one frame per 10 ms
    [[nodiscard]] float yOf(juce::Rectangle<float> area, float db) const;

    DeviceUiContext& context_;
    int fifo_;
    std::function<float()> referenceDb_;
    float floorDb_, reference_;
    std::vector<std::array<float, 3>> frames_;
    int write_ = 0;
};
} // namespace fd::ui
