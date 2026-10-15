#pragma once

#include "model/Modulators.h"

#include <cstdint>
#include <fd_device_ui/fd_device_ui.h>
#include <functional>
#include <memory>
#include <vector>

namespace fd::app
{
using ModulatorHost = model::ModulatorHost;

/// A modulator's panel on the module grid (modulators.md §12): its main section with its
/// display and the handle in the display's corner, then TARGETS; the Modulator icon in its
/// header, no meters (§6). Folded, the strip keeps the handle at its foot.
class ModulatorPanel final : public ui::DevicePanel
{
public:
    ModulatorPanel(ui::DeviceUiContext& context, model::ModulatorKind kind, ModulatorHost& host);
    ~ModulatorPanel() override;

    [[nodiscard]] model::ModulatorKind kind() const noexcept { return kind_; }
    /// The modulator as the pointer sees it: its handle lit while hovered, dragged, or while the
    /// control under the pointer is one it moves. One source, itself (0).
    [[nodiscard]] ui::ModulationModel& source() noexcept;
    /// The handle in the display's corner (unfolded), or the folded strip's.
    [[nodiscard]] ui::ModulationHandle& handle() noexcept;
    /// Its display (for the drawn states' crops).
    [[nodiscard]] juce::Component& display() noexcept;
    /// The Keep toggle (Random, Envelope, Steps), else null.
    [[nodiscard]] ui::Toggle* keep() noexcept { return keep_.get(); }
    /// The LFO's drawn shape's editor over its display (shown while Shape is Drawn), else null.
    [[nodiscard]] ui::CurveEditor* drawnEditor() noexcept;

    /// Where its routes go and how the chain takes its handle's drag (the DAW's DeviceChain):
    /// set by the host.
    std::function<void(const juce::MouseEvent&)> onHandleDrag;
    std::function<void(const juce::MouseEvent&, bool cancelled)> onHandleRelease;
    std::function<void()> onHandleCancel, onRoutes;

    /// Reads the host again: the handle's count and lock, the Keep toggle, the display.
    void refreshFromHost();
    /// Lights its handle while the control under the pointer is one it moves (§7.4).
    void setLit(bool lit);
    /// Its handle is being dragged (lit, and Escape cancels the drag), or no longer.
    void setDragging(bool dragging);

private:
    struct Self;
    class Display;
    class ListenBox;
    model::ModulatorKind kind_;
    ModulatorHost& host_;
    std::unique_ptr<Self> self_;
    std::unique_ptr<ui::ModulationHandle> handle_, foldedHandle_;
    std::unique_ptr<ui::Toggle> keep_;
    std::unique_ptr<ListenBox> listen_;
    Display* display_ = nullptr;
};

/// A modulator's routes as rows (modulators.md §7.4): each the target ("Filter · Freq"), its
/// amount (-200 ... +200 %), Show (selects the target's module and scrolls to it) and Remove. On
/// one line where they fit, the name above the rest where they don't (the inspector's ROUTES,
/// selection.md §8.2). The route list and the inspector share them.
class RouteRows final : public juce::Component
{
public:
    struct Route
    {
        juce::String target; // "Filter · Freq"
        float amount = 0.5f; // -2 ... +2 of the target's range
    };
    explicit RouteRows(std::vector<Route> routes);

    std::function<void(int route, float amount)> onAmount;
    /// An amount's drag starts (true) or ends: one undo step.
    std::function<void(bool started)> onAmountGesture;
    std::function<void(int route)> onShow, onRemove;

    /// Its height `width` wide: a list row a route, two where a route's parts don't fit one.
    [[nodiscard]] int preferredHeight(int width) const;
    void paint(juce::Graphics& g) override;
    void resized() override;

    /// The rows' parts (for the drawn states' lint): name area, amount bar, Show, Remove.
    struct Row
    {
        juce::Rectangle<int> name;
        std::unique_ptr<ui::Knob> amount;
        std::unique_ptr<ui::IconButton> show, remove;
    };
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] const std::vector<Route>& routes() const noexcept { return routes_; }

private:
    /// Whether a route's parts fit one line `width` wide.
    [[nodiscard]] static bool oneLine(int width);

    std::vector<Route> routes_;
    std::vector<Row> rows_;
};

/// A modulator's routes (modulators.md §7.4): its right-click and its header's menu open it. A
/// popover of route rows (RouteRows). Escape closes it.
class RouteList final : public juce::Component
{
public:
    using Route = RouteRows::Route;
    using Row = RouteRows::Row;
    RouteList(juce::String modulator, std::vector<Route> routes);

    std::function<void(int route, float amount)> onAmount;
    /// An amount's drag starts (true) or ends: one undo step.
    std::function<void(bool started)> onAmountGesture;
    std::function<void(int route)> onShow, onRemove;
    std::function<void()> onClose;

    /// Its size for its routes: the form popover's width, a list row a route, and the shadow's
    /// margin round it.
    [[nodiscard]] juce::Rectangle<int> preferredBounds() const;
    /// The popover inside the shadow's margin.
    [[nodiscard]] juce::Rectangle<int> inner() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /// The rows' parts, in the rows' own place (rowsArea).
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_.rows(); }
    [[nodiscard]] juce::Rectangle<int> rowsArea() const noexcept { return rows_.getBounds(); }
    [[nodiscard]] juce::Rectangle<int> titleArea() const noexcept { return title_; }

private:
    juce::String modulator_;
    RouteRows rows_;
    juce::Rectangle<int> title_;
};
} // namespace fd::app
