#pragma once

namespace fd::ui
{
// Drawn curves on a module's displays (an LFO's shape: the Wavetable's, an LFO modulator's; a
// Remap warp's): one look and one way to edit them (device-panels.md, "Displays").

/// A display's well: flat, a step below the module's body.
void paintDisplayWell(juce::Graphics& g, juce::Rectangle<float> r);
/// A point dragged on a display: filled `surface3`, ringed in the value colour; `active` while held
/// or hovered.
void paintDisplayHandle(juce::Graphics& g, juce::Point<float> at, bool hot);
/// A drawn curve across `r` (0 at the bottom, 1 at the top) from the table that plays it, filled
/// below in the value colour; its points as handles, `hot` the one held.
void paintDrawnCurve(juce::Graphics& g, const dsp::DrawnCurve& curve, juce::Rectangle<float> r, bool handles,
                     int hot = -1);

namespace curves
{
/// The point of `points` under `at` on `r` (within a handle's reach), or -1.
[[nodiscard]] int pointNear(const std::vector<std::array<float, 3>>& points, juce::Rectangle<float> r,
                            juce::Point<float> at);
/// Moves point `i` to `at`, between its neighbours (the first and last stay at the ends).
void movePoint(std::vector<std::array<float, 3>>& points, size_t i, juce::Rectangle<float> r,
               juce::Point<float> at);
/// Adds a point on the curve at `at`'s x, or removes the one there (keeping the ends).
void addOrRemove(dsp::DrawnCurve& curve, juce::Rectangle<float> r, juce::Point<float> at);
/// A named shape's name in a menu: "saw-up" reads "Saw up".
[[nodiscard]] juce::String shapeName(std::string_view id);
} // namespace curves

/// A drawn curve edited on a display: its points dragged, a double-click adds or removes one, a
/// segment dragged up or down bends it, a named shape from the menu at its top left; while it
/// plays, a line where it is and a dot on the curve. Each drag, point or shape is one change
/// (`onChange`: the owner makes it one undo step). The owner's accessory (a source's handle) sits
/// at its top right.
class CurveEditor : public juce::Component, public ParamControl, public juce::SettableTooltipClient
{
public:
    CurveEditor();
    ~CurveEditor() override;

    /// What it shows, what it sets, and where it plays now (0..1; below 0 while it doesn't).
    std::function<dsp::DrawnCurve()> curve;
    std::function<void(const dsp::DrawnCurve&)> onChange;
    std::function<float()> phase;

    /// One control at the top right (a source's handle), `width` wide.
    void setAccessory(juce::Component* accessory, int width);
    /// Whether it edits (and shows its shapes' menu); not: it only shows.
    void setEditable(bool editable);
    /// Without its well and top row: the curve and its handles on `plot` (in its own coordinates),
    /// over a display that draws the rest (an LFO modulator's, which names the shape in its Shape).
    void setChrome(bool chrome, juce::Rectangle<float> plot = {});
    [[nodiscard]] bool isEditable() const noexcept { return editable_; }

    /// Where the curve is drawn: below the top row, the handles whole inside the well.
    [[nodiscard]] juce::Rectangle<float> plot() const;
    /// The shapes' menu button (tests).
    [[nodiscard]] juce::Component& shapesButton() noexcept { return shapes_; }

    void refresh() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

protected:
    /// Draws what's in the well; by default the curve, its handles and where it plays.
    virtual void paintContent(juce::Graphics& g, juce::Rectangle<float> plot);

private:
    void showShapes();
    [[nodiscard]] dsp::DrawnCurve current() const;

    IconButton shapes_{Icon::ChevronDown};
    juce::Component* accessory_ = nullptr;
    int accessoryWidth_ = 0;
    bool editable_ = true, chrome_ = true;
    juce::Rectangle<float> plot_; // without chrome: where the curve is
    int dragged_ = -1, bending_ = -1;
    float bendStart_ = 0.0f, bendY_ = 0.0f;
    dsp::DrawnCurve editing_;
    std::string lastState_;
    float phase_ = -1.0f;
};
} // namespace fd::ui
