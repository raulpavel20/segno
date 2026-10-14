#pragma once

namespace fd::ui
{
/// A number you drag or type (ui-principles §1 "drag numbers to change them"): tempo, time
/// signature, later any numeric field. Same interaction as Knob: drag vertically (Shift =
/// fine), mouse wheel, arrows, type a number while focused or double-click to edit.
class NumberField : public juce::Component, public juce::SettableTooltipClient
{
public:
    /// Its text from its edges (mono values sit tighter than a combo's text); a ChoiceField's too.
    static constexpr int textInset = 4;

    NumberField();

    void setRange(double min, double max, double step) noexcept;
    void setValue(double value, juce::NotificationType notification);
    [[nodiscard]] double getValue() const noexcept { return value_; }
    /// Value change per pixel of vertical drag (before Shift's 1/10).
    void setDragSensitivity(double perPixel) noexcept { perPixel_ = perPixel; }
    void setFontSize(float size) noexcept { fontSize_ = size; }
    /// Where its text sits (left by default; a numerator sits right, against its slash).
    void setJustification(juce::Justification justification);
    /// The width it needs to show `sample` (its widest value).
    [[nodiscard]] int widthFor(const juce::String& sample) const;

    std::function<juce::String(double)> textFromValue;
    std::function<std::optional<double>(const juce::String&)> valueFromText;
    std::function<void(double)> onValueChange;
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;

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

private:
    [[nodiscard]] double constrain(double v) const noexcept;
    void change(double v);
    void showEditor(const juce::String& initial);
    [[nodiscard]] juce::String text() const;

    double min_ = 0.0, max_ = 1.0, step_ = 0.0, value_ = 0.0;
    double perPixel_ = 0.01;
    double accumulator_ = 0.0;
    float lastY_ = 0.0f;
    float fontSize_ = 13.0f;
    juce::Justification justification_ = juce::Justification::centredLeft;
    bool dragging_ = false;
    std::unique_ptr<juce::TextEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NumberField)
};
} // namespace fd::ui
