#pragma once

#include "engine/Session.h"
#include "model/Snapshot.h"
#include "ui/MacroPanel.h"

#include <array>
#include <fd_device_ui/fd_device_ui.h>

namespace fd::app
{
/// A rack's panel in the chain (docs/design/racks.md §5, §5a), on the module grid like any
/// module's:
///   - MACROS: the eight macros as knobs whose names are their handles; one that maps nothing
///     yet shows faded;
///   - SNAPSHOTS, from its first snapshot on (racks.md §18.2): their names two a row, the current
///     one lit (with the changed mark once the macros move), "+ Store" in the next place;
///   - MORPH, from its second snapshot on (§18.3): its power dot, the Morph knob, and the
///     snapshots it goes between, A and B;
///   - CHAINS, from the rack's second chain on: a row each (on, name, solo), "+ Chain", and the
///     shown chain's volume and pan; choosing a chain's name shows its modules in the frame. A
///     rack with one chain is a group: no list.
/// The macros are the rack's own parameters (engine::Session::rackMacros()), set through the
/// host's binding as a module's are; the chains are the host's, edited through the callbacks below.
class RackPanel final : public MacroPanel
{
public:
    /// The chains CHAINS has rows for (racks.md §16).
    static constexpr int maxChains = 4;
    /// The snapshots SNAPSHOTS has room for (racks.md §18.2).
    static constexpr int maxSnapshots = model::RackInfo::maxSnapshots;

    explicit RackPanel(ui::DeviceUiContext& context);
    ~RackPanel() override;

    /// What it shows: the rack's name, its chains (and the one its frame shows) and its macros.
    void setRack(const juce::String& name, const model::RackInfo& rack);
    [[nodiscard]] ui::ParamKnob& morphKnob() const { return *morph_; }
    [[nodiscard]] ui::Toggle& snapshotButton(int snapshot)
    {
        return snapshots_[static_cast<size_t>(snapshot)];
    }
    [[nodiscard]] ui::Toggle& storeButton() noexcept { return store_; }
    [[nodiscard]] ui::ChoiceBox& morphA() noexcept { return morphA_; }
    [[nodiscard]] ui::ChoiceBox& morphB() noexcept { return morphB_; }

    // The chains' edits, recorded by the host (each one undo step).
    std::function<void(int chain)> onShowChain;
    std::function<void(int chain, bool on)> onChainOn;
    std::function<void(int chain, bool solo)> onChainSolo;
    std::function<void(int chain, float volumeDb)> onChainVolume;
    std::function<void(int chain, float pan)> onChainPan;
    std::function<void(int chain)> onChainMenu; // its right-click: Rename, Solo, Remove chain
    std::function<void()> onAddChain;
    /// A drag of the shown chain's Vol or Pan begins (true) and ends: one undo step between.
    std::function<void(bool started)> onChainGesture;
    std::function<void(int chain, const juce::String& name)> onRenameChain;
    /// Renames a chain in place: its name in the list.
    void renameChain(int chain);
    /// Renames the rack in place: its name in the header.
    void renameRack();
    std::function<void(const juce::String& name)> onRenameRack;

    // Snapshots, locks and Morph (racks.md §18), recorded by the host, each one undo step.
    std::function<void(int snapshot)> onRecallSnapshot;
    std::function<void()> onStoreSnapshot;
    std::function<void(int snapshot)> onSnapshotMenu; // its right-click: Rename, Update, Remove
    std::function<void(int snapshot, const juce::String& name)> onRenameSnapshot;
    std::function<void(int macro, bool locked)> onLockMacro;
    std::function<void(bool on)> onMorphOn;
    std::function<void(int a, int b)> onMorphBetween;
    /// Renames a snapshot in place: its name in SNAPSHOTS.
    void renameSnapshot(int snapshot);
    /// Lights Morph's name while the pointer is on a macro it moves.
    void setMorphLit(bool lit);

private:
    void placeChains();
    void placeSnapshots();
    void updateMacros();
    // Its macros' extras (racks.md §18): Store snapshot and Lock on a macro's right-click; a macro
    // Morph moves lit while Morph's name is hovered, and the other way round.
    void addMacroItems(int macro, juce::PopupMenu& menu) override;
    [[nodiscard]] bool macroLit(int macro) const override;
    void macroHovered(int macro, bool hovered) override;

    // CHAINS: a row a chain, as the table editor's layers (an on dot, the names to choose from),
    // and its solo.
    std::array<ui::Toggle, maxChains> on_, solo_;
    std::unique_ptr<ui::Selector> names_;
    juce::StringArray placedNames_;
    int placedShown_ = -1;
    ui::Toggle addChain_{"+ Chain"};
    ui::Knob volume_, pan_;
    ui::PanelSection* chains_ = nullptr;
    // SNAPSHOTS: a name each, chosen to recall it, and "+ Store".
    std::array<ui::Toggle, maxSnapshots> snapshots_;
    ui::Toggle store_{"+ Store"};
    ui::PanelSection* snapshotsSection_ = nullptr;
    int placedSnapshots_ = -1;
    // MORPH: the knob, and the two snapshots it goes between.
    ui::PanelSection* morphSection_ = nullptr;
    ui::ParamKnob* morph_ = nullptr;
    ui::ChoiceBox morphA_, morphB_;
    juce::StringArray morphNames_;
    bool morphHovered_ = false; // the pointer on Morph's name: the macros it moves are lit
    model::RackInfo rack_;
    juce::String rackName_;
    bool chainGesture_ = false; // Vol or Pan being dragged: not set from the snapshot meanwhile
};

/// A rack's frame in the chain (racks.md §5, §5a): a tray behind the rack's panel and the shown
/// chain's modules, to its end cap; under them, with several chains, the shown chain's name.
/// The chain view lays it out; this is what it draws.
struct RackFrame
{
    juce::Rectangle<int> bounds; // the tray
    juce::Rectangle<int> label;  // the label strip under the chain's modules
    juce::String text;           // "Chain 2 · Crushed"; empty with one chain
    bool chainOff = false;       // the shown chain is off: dimmed, and says so
    bool soloed = false;         // it's soloed: says so, in `active`
    bool dropInside = false;     // a drag would land in the shown chain: the tray is outlined
};
void paintRackFrame(juce::Graphics& g, const RackFrame& frame);
/// "Chain 2 · Crushed" ("Chain 2" until it's renamed), for the frame and the chain's tooltip.
[[nodiscard]] juce::String chainLabel(const model::RackInfo& rack, int chain);
/// What a folded rack says it holds: "4 modules", "2 chains".
[[nodiscard]] juce::String rackContents(const model::RackInfo& rack);
} // namespace fd::app
