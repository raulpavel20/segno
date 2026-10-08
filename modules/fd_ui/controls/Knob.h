#pragma once

namespace fd::ui
{
/// Rotary control: a flat value arc with a gradient fill (ui-principles §4, §7).
///
/// Interaction (identical everywhere):
///   drag vertically to change; Shift = fine; double-click = reset to default;
///   mouse wheel adjusts; when focused, typing a number opens an inline editor; arrows nudge.
///
/// The knob works in normalised units (0..1). Owners map to plain values and text.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob();

    void setName(const juce::String& name) override;
    void setValue(float normalized, juce::NotificationType notification);
    [[nodiscard]] float getValue() const noexcept { return value_; }
    void setDefaultValue(float normalized) noexcept { default_ = normalized; }
    /// Arc grows from this normalised position (0 = from the start, 0.5 = bipolar).
    void setArcOrigin(float normalized) noexcept { origin_ = normalized; }
    /// For stepped parameters: number of discrete positions (0 = continuous).
    void setNumSteps(int steps) noexcept { steps_ = steps; }

    std::function<void(float)> onValueChange;
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;
    std::function<juce::String(float)> textFromValue;
    std::function<std::optional<float>(const juce::String&)> valueFromText;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    bool keyPressed(const juce::KeyPress& key) override;
    void focusLost(FocusChangeType) override { repaint(); }
    void focusGained(FocusChangeType) override { repaint(); }

private:
    void changeTo(float normalized);
    [[nodiscard]] float quantize(float normalized) const noexcept;
    void showEditor(const juce::String& initialText);

    juce::String name_;
    float value_ = 0.0f;
    float default_ = 0.0f;
    float origin_ = 0.0f;
    int steps_ = 0;
    float dragAccumulator_ = 0.0f;
    float lastDragY_ = 0.0f;
    bool dragging_ = false;
    std::unique_ptr<juce::TextEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};
} // namespace fd::ui
