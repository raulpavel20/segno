#pragma once

#include "engine/Session.h"
#include "ui/ActionRegistry.h"
#include "ui/RackPanel.h"

#include <fd_ui/fd_ui.h>
#include <optional>
#include <set>

namespace fd::app
{
class MacroEditor;

/// The selected track's module chain in the detail panel (C6): our modules as their panels,
/// third-party plugins as compact cards, left to right; add with the + button, reorder by
/// dragging a header (or a folded strip), bypass with the power toggle, fold a module or
/// collapse its sections (ADR 0022), delete the selected module.
///
/// A rack (docs/design/racks.md §5, §5a) shows as its panel, then its shown chain's modules, in a
/// frame ending in a + of its own; folded, it's one strip. Modules are dragged into its frame
/// (its chain shown) and out past its end; a macro's name dragged onto a knob in the frame maps it.
class DeviceChain final : public juce::Component,
                          public juce::DragAndDropTarget,
                          private engine::Session::Listener
{
public:
    static constexpr int gap = 8;
    static constexpr int cardWidth = 200;

    DeviceChain(engine::Session& session, const ActionRegistry& actions);
    ~DeviceChain() override;

    /// Shows `track`'s chain; panels of devices still present are kept.
    void setTrack(std::optional<model::TrackInfo> track);

    [[nodiscard]] bool hasSelection() const noexcept { return selected_.has_value(); }
    /// The channel whose chain is shown, and the selected module (nullopt: none).
    [[nodiscard]] std::optional<std::uint64_t> track() const noexcept { return track_; }
    [[nodiscard]] std::optional<std::uint64_t> selectedDevice() const noexcept { return selected_; }
    void deleteSelected();
    /// Selects a module and scrolls it into view (the mixer view's module names); one in a rack's
    /// chain not shown shows its chain.
    void selectDevice(std::uint64_t id);
    /// The selected module's panel (null for none, or a third-party plugin's card).
    [[nodiscard]] ui::DevicePanel* selectedPanel() const;
    /// Folds or unfolds the selected module (ADR 0022).
    void foldSelected();
    /// Unfolds the selected module and expands all of its sections.
    void expandSelected();
    /// The selected module's next (+1) or previous (-1) page, unfolding it (ADR 0051).
    void turnSelectedPage(int delta);

    // ---- racks (racks.md §9: the Module menu's) ------------------------------------------------
    /// The modules Group into rack takes: the selected one, and those Shift-click added along its
    /// chain, in the track's own chain.
    [[nodiscard]] std::vector<std::uint64_t> pickedModules() const;
    void groupPicked();
    /// The rack selected, or holding the module selected.
    [[nodiscard]] std::optional<std::uint64_t> selectedRack() const;
    void ungroupSelectedRack();
    void addChainToSelectedRack();
    void renameSelectedRack();
    /// Renames the selected rack in place, then saves it under its new name (Save rack as…).
    void saveSelectedRackAs();
    /// A rack was renamed to be saved (Save rack as…): the shell writes its file.
    std::function<void(std::uint64_t rack)> onSaveRack;
    /// The selected rack as the snapshot has it (null: none selected).
    [[nodiscard]] const model::RackInfo* selectedRackInfo() const;
    /// Renames the selected rack's current snapshot in place (Rename snapshot…, racks.md §18.2).
    void renameCurrentSnapshot();
    /// Opens the selected rack's macro editor, `macro` chosen.
    void editMacros(int macro = 0);
    /// Whether the macro editor open is the selected rack's.
    [[nodiscard]] bool editingMacros() const;
    /// Adds an empty rack after the selected module, or at the end (Add module ▸ Rack).
    void addRack();
    /// The racks' frames as laid out, in the chain's content (tests and the layout checks).
    [[nodiscard]] std::vector<RackFrame> frames() const;
    /// The scrolled strip the modules are laid out on (tests and screenshots).
    [[nodiscard]] juce::Component& content() const;

    /// Called when the chain is clicked (the shell routes Delete to the area used last).
    std::function<void()> onActivated;
    /// A module asks to show its editor (ADR 0053): the detail panel takes it.
    std::function<void(std::uint64_t module, std::unique_ptr<ui::ModuleEditor> editor)> onShowEditor;
    /// A module's panel is about to go (the module removed, another track shown): its editor
    /// closes first, while the module's context is still there.
    std::function<void(std::uint64_t module)> onModuleGone;
    /// The chain's height in the detail panel: a module panel, a rack's frame, the scrollbar.
    [[nodiscard]] static int preferredHeight();
    /// Something from the browser dropped (browser.md §10): at `index` in the chain, or onto a
    /// module it lands on (a preset of its kind, a sample on a Sampler).
    std::function<void(const std::string& item, std::uint64_t track, int index,
                       std::optional<std::uint64_t> onModule)>
        onBrowserDrop;
    /// Something from the browser dropped into a rack's frame: into its chain shown.
    std::function<void(const std::string& item, const engine::Session::ChainPlace& place)>
        onBrowserDropInRack;
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    struct Slot
    {
        model::DeviceSlot info;
        std::unique_ptr<engine::DeviceHandle> handle; // our devices, and a rack's macros
        std::unique_ptr<juce::Component> view;        // panel or card
        std::uint64_t rack = 0;                       // shown in a rack's frame (0: the track's chain)
        int chain = 0;                                // that rack's chain
    };
    /// A rack shown in the chain: its frame, its end cap, an empty chain's room.
    struct Rack
    {
        std::uint64_t id = 0;
        std::shared_ptr<const model::RackInfo> info;
        RackFrame frame;
        std::unique_ptr<ui::IconButton> endCap;
        juce::Rectangle<int> emptyHint;
        bool folded = false;
    };
    /// Where a drag lands: in the track's chain (rack 0) or a rack's shown chain, before `index`.
    struct Drop
    {
        std::uint64_t rack = 0;
        int chain = 0, index = 0;
    };
    class Content;

    void rebuild(const std::vector<model::DeviceSlot>& devices);
    void wireRackPanel(RackPanel& panel, std::uint64_t rack);
    /// A module's view state set elsewhere (an assistant, undo): the panel follows.
    void deviceViewChanged(std::uint64_t deviceId) override;
    void layoutSlots();
    /// The add menu: our modules, then plugins by vendor; `add` gets the chosen key.
    void showAddMenu(juce::Component& target, std::function<void(const std::string& key)> add,
                     std::function<void(juce::PopupMenu&)> extra = {});
    void showRackMenu(std::uint64_t rack);
    void showChainMenu(std::uint64_t rack, int chain);
    /// A snapshot's right-click (racks.md §18.2): Rename, Update, Remove.
    void showSnapshotMenu(std::uint64_t rack, int snapshot);
    [[nodiscard]] int slotIndexOf(const juce::Component* c) const;
    [[nodiscard]] Slot* slotOf(std::uint64_t id);
    [[nodiscard]] Rack* rackOf(std::uint64_t id);
    [[nodiscard]] const Rack* rackOfConst(std::uint64_t id) const;
    [[nodiscard]] RackPanel* rackPanelOf(std::uint64_t id);
    /// Where a drag at `p` (in the content) would land; never into the rack being dragged.
    [[nodiscard]] Drop dropAt(juce::Point<int> p, std::optional<std::uint64_t> dragged) const;
    /// The knob of a module in `rack`'s frame at `p` (in the content): its module and parameter.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, std::string>> knobAt(juce::Point<int> p,
                                                                              std::uint64_t rack) const;
    void macroDragged(std::uint64_t rack, int macro, const juce::MouseEvent& e);
    void macroDropped(std::uint64_t rack, int macro);
    /// A knob's right-click in a rack: Map to macro, Unmap (session's macroMenu).
    void addMacroItems(std::uint64_t module, const std::string& param, juce::PopupMenu& menu);
    void openMacroEditor(std::uint64_t rack, int macro);
    void updateMacroEditor();
    /// The normalised value a parameter of a module in the chain has now.
    [[nodiscard]] float valueOf(std::uint64_t module, const std::string& param);
    void beginGesture(bool started, const std::string& name);

    engine::Session& session_;
    const ActionRegistry& actions_;
    std::optional<std::uint64_t> track_;
    std::vector<Slot> slots_; // in the order shown: a rack's modules after it
    std::vector<Rack> racks_;
    std::optional<std::uint64_t> selected_;
    std::set<std::uint64_t> picked_; // Shift-click added along the track's chain (Group into rack)

    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
    ui::IconButton add_{ui::Icon::Plus};

    // Reordering by dragging a header.
    int dragSlot_ = -1;
    bool dragging_ = false;
    std::optional<Drop> drop_;
    // A browser drag: where it would land, or the module it would go into.
    bool browserDrag_ = false;
    std::optional<std::uint64_t> browserOn_;
    // The macro editor while it's open, and its rack.
    juce::Component::SafePointer<MacroEditor> macroEditor_;
    std::uint64_t macroEditorRack_ = 0;
    std::optional<engine::Session::EditGroup> gesture_; // a drag of a chain's mixer or a mapping
    bool saveOnRename_ = false;                         // Save rack as…: the rename saves it
};
} // namespace fd::app
