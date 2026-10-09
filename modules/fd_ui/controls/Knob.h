#pragma once

namespace fd::ui
{
/// Rotary control: a flat value arc (ui-principles §4, §7). The Bar style draws the same control
/// as a compact horizontal bar with the name and value inside, for dense rows (mod amounts). The
/// Fader style is a vertical fader (the mixer view): a thin track, filled up to a flat cap; its
/// travel follows the mouse one to one.
///
/// Interaction (identical everywhere):
///   drag vertically to change (a bar also follows horizontal drags); Shift = fine;
///   double-click = reset to default;
///   mouse wheel adjusts; when focused, typing a number opens an inline editor; arrows nudge.
///
/// The knob works in normalised units (0..1). Owners map to plain values and text.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style
    {
        Rotary,
        Bar,
        Fader
    };

    Knob();

    void setStyle(Style style);
    [[nodiscard]] Style style() const noexcept { return style_; }
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
    void paintBar(juce::Graphics& g);
    void paintFader(juce::Graphics& g);
    /// A fader's travel in pixels: the cap's centre moves over it.
    [[nodiscard]] float faderTravel() const noexcept;
    void changeTo(float normalized);
    [[nodiscard]] float quantize(float normalized) const noexcept;
    void showEditor(const juce::String& initialText);

    /// The value text's size, fitted once per width to the widest text the knob can show, so a
    /// value never changes size while it changes: smaller first (down to 9.5 px), then without
    /// the spaces before units (down to 9 px).
    struct ValueFit
    {
        float width = -1.0f;
        float size = 0.0f;
        bool compact = false;
    };
    [[nodiscard]] const ValueFit& valueFit(float width);
    [[nodiscard]] juce::String valueText() const;
    ValueFit fit_;

    juce::String name_;
    float value_ = 0.0f;
    float default_ = 0.0f;
    float origin_ = 0.0f;
    int steps_ = 0;
    float dragAccumulator_ = 0.0f;
    float lastDragY_ = 0.0f;
    float lastDragX_ = 0.0f;
    Style style_ = Style::Rotary;
    bool dragging_ = false;
    std::unique_ptr<juce::TextEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};
} // namespace fd::ui
