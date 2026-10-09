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
    [[nodiscard]] float param(size_t index) const { return context_.params.getPlain(index); }

private:
    DeviceUiContext& context_;
    juce::String label_;
    juce::Component* accessory_ = nullptr;
    int accessoryWidth_ = 0;
    std::vector<size_t> params_;
    std::vector<float> last_;
};

/// ADSR shape drawn by running dsp::Adsr itself. Expects four consecutive parameters: attack,
/// decay, release in ms and sustain in percent (A, D, S, R), plus an optional cycle toggle.
/// Segment widths follow the log of their times and fill the display.
class EnvelopeDisplay final : public MiniDisplay
{
public:
    EnvelopeDisplay(DeviceUiContext& context, juce::String label, size_t attackIndex,
                    std::optional<size_t> cycleIndex = std::nullopt);

private:
    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override;

    size_t attack_;
    std::optional<size_t> cycle_;
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
