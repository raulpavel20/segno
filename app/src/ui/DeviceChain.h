#pragma once

#include "engine/Session.h"
#include "ui/ActionRegistry.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
/// The selected track's module chain in the detail panel (C6): our modules as their panels,
/// third-party plugins as compact cards, left to right; add with the + button, reorder by
/// dragging a header (or a folded strip), bypass with the power toggle, fold a module or
/// collapse its sections (ADR 0022), delete the selected module.
class DeviceChain final : public juce::Component
{
public:
    static constexpr int gap = 8;
    static constexpr int cardWidth = 200;

    DeviceChain(engine::Session& session, const ActionRegistry& actions);
    ~DeviceChain() override;

    /// Shows `track`'s chain; panels of devices still present are kept.
    void setTrack(std::optional<model::TrackInfo> track);

    [[nodiscard]] bool hasSelection() const noexcept { return selected_.has_value(); }
    void deleteSelected();
    /// The selected module's panel (null for none, or a third-party plugin's card).
    [[nodiscard]] ui::DevicePanel* selectedPanel() const;
    /// Folds or unfolds the selected module (ADR 0022).
    void foldSelected();
    /// Unfolds the selected module and expands all of its sections.
    void expandSelected();
    /// Called when the chain is clicked (the shell routes Delete to the area used last).
    std::function<void()> onActivated;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    struct Slot
    {
        model::DeviceSlot info;
        std::unique_ptr<engine::DeviceHandle> handle; // our devices
        std::unique_ptr<juce::Component> view;        // panel or card
    };
    class Content;

    void rebuild(const std::vector<model::DeviceSlot>& devices);
    void layoutSlots();
    void showAddMenu();
    [[nodiscard]] int slotIndexOf(const juce::Component* c) const;
    [[nodiscard]] int insertIndexAt(int contentX) const;

    engine::Session& session_;
    const ActionRegistry& actions_;
    std::optional<std::uint64_t> track_;
    std::vector<Slot> slots_;
    std::optional<std::uint64_t> selected_;

    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    ui::IconButton add_{ui::Icon::Plus};

    // Reordering by dragging a header.
    int dragSlot_ = -1;
    bool dragging_ = false;
    int insertAt_ = -1;
};
} // namespace fd::app
