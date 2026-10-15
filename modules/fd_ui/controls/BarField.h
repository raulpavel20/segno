#pragma once

namespace fd::ui
{
/// A value set from a bar (docs/design/ui-foundations.md, "Shared components"; the audio editor's
/// Gain: audio-editor.md §4): its name in the Label style, then a NumberField an item gap after it.
/// As wide as both with the value it shows, so the gap after it is the bar's own: its owner calls
/// fit() when the value changes and lays the bar out again.
class BarField final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BarField(const juce::String& label);

    [[nodiscard]] NumberField& field() noexcept { return field_; }
    [[nodiscard]] const juce::String& label() const noexcept { return label_; }
    /// As wide as its label and the value it shows now (its height kept).
    void fit();
    void setTooltip(const juce::String& tip) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    [[nodiscard]] int labelWidth() const;

    juce::String label_;
    NumberField field_;
};
} // namespace fd::ui
