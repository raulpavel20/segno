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
    /// Shows whether its parameter is automated (the indicator; automation.md §6).
    void setAutomationState(AutomationState state);
    [[nodiscard]] AutomationState automationState() const noexcept { return automation_; }
    /// A right-click: the control's menu (automation, reset).
    std::function<void()> onPopupMenu;

    // ---- modulation (docs/design/device-panels.md, "Modulation") ------------------------------

    /// The range its modulation moves it over (normalised, `from` <= `to`): on a rotary knob a ring
    /// just outside its arc, on a bar a line along its bottom edge, in the modulation gradient;
    /// `dimmed` while another source is shown. With `now`, a dot (a tick on a bar) where the
    /// modulation has it now (modulators.md §7.4). None: no ring.
    struct Ring
    {
        float from = 0.0f, to = 0.0f;
        bool dimmed = false;
        std::optional<float> now;
        bool operator==(const Ring&) const = default;
    };
    void setRing(std::optional<Ring> ring);
    [[nodiscard]] const std::optional<Ring>& ring() const noexcept { return ring_; }
    /// Set elsewhere (a rack's macro, racks.md §4): the knob moves as that moves and isn't
    /// dragged, wheeled, typed into or reset; its right-click still opens its menu.
    void setFollowing(bool following);
    [[nodiscard]] bool isFollowing() const noexcept { return following_; }
    /// Lights the knob where a dragged source would land: a ring in `active` over its sweep.
    void setDropTarget(bool target);
    [[nodiscard]] bool isDropTarget() const noexcept { return dropTarget_; }
    /// While a source is dragged, a knob it can move shows a faint ring over its sweep.
    void setDropCandidate(bool candidate);
    [[nodiscard]] bool isDropCandidate() const noexcept { return dropCandidate_; }
    /// The square the ring's outer edge fits in (for the layout lint): inside the knob, clear of
    /// its name and value.
    [[nodiscard]] juce::Rectangle<float> ringBounds() const;
    /// The name and value strips (for the layout lint).
    [[nodiscard]] juce::Rectangle<float> nameStrip() const { return geometry().name; }
    [[nodiscard]] juce::Rectangle<float> valueStrip() const { return geometry().value; }
    /// Whether `position` (in the knob) is on the ring's band, outside the arc, where dragging
    /// sets the modulation's amount (while there's a ring and onRingDrag).
    [[nodiscard]] bool isOnRing(juce::Point<float> position) const;
    std::function<void()> onRingStart, onRingEnd;
    /// Each step of a drag on the ring: normalised, positive up and right (Shift finely).
    std::function<void(float delta)> onRingDrag;
    /// A knob that's itself a modulation source (a macro): its name is the source's handle (the
    /// pointer a hand over it), lit in `effectEnd` while hovered or while `lit`.
    void setSourceHandle(bool shown, bool lit = false);
    [[nodiscard]] bool isHandleLit() const noexcept { return handleLit_; }
    /// Where the source's handle is (the name strip), in the knob; empty when it isn't one.
    [[nodiscard]] juce::Rectangle<float> handleArea() const;
    std::function<void(const juce::MouseEvent&)> onHandleDrag, onHandleRelease;
    std::function<void(bool hovered)> onHandleHover;
    /// A double-click on the source's handle (a rack's macro: rename it).
    std::function<void()> onHandleDoubleClick;
    /// A lock after its name (a rack's macro that recall, Morph and Vary leave alone, racks.md
    /// §18.4); the name is cut to leave room for it.
    void setLocked(bool locked);
    [[nodiscard]] bool isLocked() const noexcept { return locked_; }

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
    void mouseExit(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusLost(FocusChangeType) override { repaint(); }
    void focusGained(FocusChangeType) override { repaint(); }

private:
    /// The rotary knob's layout in its bounds: a fixed block, centred (device-panels.md).
    struct Geometry
    {
        juce::Rectangle<float> name, value;
        juce::Point<float> centre;
        float radius = 0.0f, thickness = 0.0f;
    };
    [[nodiscard]] Geometry geometry() const;
    void paintRing(juce::Graphics& g, const Geometry& k) const;
    void paintBarRing(juce::Graphics& g, juce::Rectangle<float> r) const;
    /// The radius of the dot on a ring where the modulation has the knob now.
    [[nodiscard]] static float ringDotRadius() noexcept;
    void setHandleHovered(bool hovered);
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
    AutomationState automation_ = AutomationState::None;
    bool dragging_ = false;
    std::optional<Ring> ring_;
    bool dropTarget_ = false, dropCandidate_ = false, sourceHandle_ = false, handleLit_ = false,
         handleHovered_ = false, following_ = false, locked_ = false;
    enum class Press
    {
        Value,
        Ring,
        Handle,
        None // a following knob pressed: nothing to do
    } press_ = Press::Value;
    std::unique_ptr<juce::TextEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};
} // namespace fd::ui
