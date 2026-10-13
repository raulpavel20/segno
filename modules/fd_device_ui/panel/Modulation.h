#pragma once

namespace fd::ui
{
/// A module's own modulation as the shared UI sees it (docs/design/device-panels.md,
/// "Modulation"): its sources, and its routes to parameters. The module keeps its routes in its
/// parameters (a matrix); the model edits them through the host binding, like any edit. It also
/// holds what the pointer is showing, which every ring and handle reads.
class ModulationModel
{
public:
    virtual ~ModulationModel() = default;

    struct Route
    {
        int index = 0; // the route's slot (its row in the matrix)
        int source = 0;
        size_t param = 0;
        float amount = 0.0f;  // -1 ... +1 of the parameter's range
        bool bipolar = false; // the source swings both ways (an LFO): so does the range
    };
    /// Every route to a parameter, in the matrix's order.
    [[nodiscard]] virtual std::vector<Route> routes() const = 0;
    [[nodiscard]] virtual bool canModulate(size_t param) const = 0;
    [[nodiscard]] virtual juce::String sourceName(int source) const = 0;
    /// A route from `source` to `param` at 50 %, in the first free slot: false when none is.
    virtual bool addRoute(int source, size_t param) = 0;
    [[nodiscard]] virtual bool isFull() const = 0;
    virtual void removeRoute(int index) = 0;
    /// A drag of a ring: one undo step from begin to end.
    virtual void beginAmount(int index) = 0;
    virtual void setAmount(int index, float amount) = 0;
    virtual void endAmount(int index) = 0;
    /// The route a parameter belongs to, when it's one of a route's own (a matrix row's source,
    /// target, amount): its right-click menu offers to remove the route.
    [[nodiscard]] virtual std::optional<int> routeOfParam(size_t /*param*/) const { return std::nullopt; }

    // What the pointer shows (never the module's state).
    int hoveredSource = -1; // a source's handle under the pointer
    int lastSource = -1;    // the source last hovered or dragged: a ring drags its route
    int draggedSource = -1; // a source being dragged onto a knob
    std::optional<size_t> hoveredParam, dropParam;

    /// The ring a knob of `param` at `value` (normalised) shows: the range its routes move it
    /// over; only the hovered source's while one is (the others dimmed). None without routes.
    [[nodiscard]] std::optional<Knob::Ring> ringFor(size_t param, float value) const;
    /// The route a drag on `param`'s ring sets: the last source's, else the first.
    [[nodiscard]] std::optional<Route> routeToDrag(size_t param) const;
    /// Whether `source` moves the knob under the pointer (its handle lights).
    [[nodiscard]] bool movesHovered(int source) const;
    /// What a source's handle says: how to use it, and when the matrix is full.
    [[nodiscard]] juce::String handleTooltip(int source) const;
};

/// A modulation source's handle in a display's corner (an LFO's shape, an envelope): a four-way
/// arrow, `deviceGrid.sourceHandle` square, lit in `effectEnd` while hovered or while the knob
/// under the pointer is moved by it. Dragged onto a knob, it makes a route; Escape cancels.
class ModulationHandle final : public juce::Component, public ParamControl, public juce::TooltipClient
{
public:
    ModulationHandle(ModulationModel& model, int source);
    /// The source it drags (the selected LFO changes).
    void setSource(int source);
    [[nodiscard]] int source() const noexcept { return source_; }

    void refresh() override;
    juce::String getTooltip() override { return model_.handleTooltip(source_); }
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    ModulationModel& model_;
    int source_;
    bool lit_ = false, cancelled_ = false;
};
} // namespace fd::ui
