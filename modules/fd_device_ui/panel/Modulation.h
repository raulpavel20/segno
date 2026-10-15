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
    struct SourceInfo
    {
        int id = 0;
        juce::String name, group; // "LFO 1", "LFOs"
    };
    /// The sources, in order, by group: what a knob's Modulate with menu offers.
    [[nodiscard]] virtual std::vector<SourceInfo> sources() const { return {}; }

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

/// A line from a modulation source's handle to a control it moves, shown while the handle is
/// hovered (modulators.md §7.4): from `from` (the handle's foot) to `to` (the control's centre),
/// in the modulation gradient over a dark halo so it reads where it crosses other panels, sagging
/// as a cable does, more the further it goes, a dot at each end.
void paintModulationCable(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to);

/// A modulation source's handle in a display's corner (an LFO's shape, an envelope): a chip, the
/// four-way arrow and the source's name on `surface3`, `deviceGrid.sourceHandle` tall, lit in
/// `effectEnd` while hovered or while the knob under the pointer is moved by it. Dragged onto a
/// knob, it makes a route; Escape cancels.
class ModulationHandle final : public juce::Component, public ParamControl, public juce::TooltipClient
{
public:
    ModulationHandle(ModulationModel& model, int source);
    /// The source it drags (the selected LFO changes).
    void setSource(int source);
    [[nodiscard]] int source() const noexcept { return source_; }
    /// How many routes the source has, after its name, fainter ("LFO · 3"; modulators.md §7.1);
    /// 0 shows none.
    void setCount(int routes);
    /// A lock after the name: the source plays a roll it keeps (modulators.md §13).
    void setLocked(bool locked);
    /// The arrow alone (and no name): where there's no room, a folded module's strip.
    void setCompact(bool compact);
    /// Who takes the drag: a module's own panel by default; a host whose sources reach other
    /// modules (the DAW's modulators) takes it with these. `onRelease` says whether it was
    /// cancelled (no drag); `onCancel` is Escape during the drag.
    std::function<void(const juce::MouseEvent&)> onDrag;
    std::function<void(const juce::MouseEvent&, bool cancelled)> onRelease;
    std::function<void()> onCancel;
    /// A right-click (a modulator's route list).
    std::function<void()> onPopupMenu;

    void refresh() override;
    /// Whether it's lit now (hovered, dragged, or moving the control under the pointer).
    [[nodiscard]] bool isLit() const noexcept { return lit_; }
    /// Its width: the arrow, its name, and padding.
    [[nodiscard]] int preferredWidth() const;
    juce::String getTooltip() override
    {
        return tooltip_.isNotEmpty() ? tooltip_ : model_.handleTooltip(source_);
    }
    /// Its own tooltip in place of the model's (a modulator's, saying what it moves).
    void setTooltip(juce::String text) { tooltip_ = std::move(text); }
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
    int count_ = 0;
    juce::String tooltip_;
    bool lit_ = false, cancelled_ = false, locked_ = false, compact_ = false;
};
} // namespace fd::ui
