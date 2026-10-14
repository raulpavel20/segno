#pragma once

#include "engine/Session.h"
#include "model/Snapshot.h"

#include <array>
#include <fd_device_ui/fd_device_ui.h>

namespace fd::app
{
/// A rack's panel in the chain (docs/design/racks.md §5, §5a), on the module grid like any
/// module's:
///   - MACROS: the eight macros as knobs whose names are their handles; one that maps nothing
///     yet shows faded;
///   - CHAINS, from the rack's second chain on: a row each (on, name, solo), "+ Chain", and the
///     shown chain's volume and pan; choosing a chain's name shows its modules in the frame. A
///     rack with one chain is a group: no list.
/// The macros are the rack's own parameters (engine::Session::rackMacros()), set through the
/// host's binding as a module's are; the chains are the host's, edited through the callbacks below.
class RackPanel final : public ui::DevicePanel
{
public:
    /// The chains CHAINS has rows for (racks.md §16).
    static constexpr int maxChains = 4;

    explicit RackPanel(ui::DeviceUiContext& context);
    ~RackPanel() override;

    /// What it shows: the rack's name, its chains (and the one its frame shows) and its macros.
    void setRack(const juce::String& name, const model::RackInfo& rack);
    /// Lights a macro's name while the pointer is on a knob it sets, elsewhere in the rack (-1:
    /// none).
    void setLitMacro(int macro);
    [[nodiscard]] ui::ParamKnob& macroKnob(int macro) const { return *macros_[static_cast<size_t>(macro)]; }

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
    std::function<void(int macro, const juce::String& name)> onRenameMacro;
    /// Its right-click's Edit macros: the macro editor, that macro chosen.
    std::function<void(int macro)> onEditMacros;
    /// Renames in place: a macro (its name over its knob) or a chain (its name in the list).
    void renameMacro(int macro);
    void renameChain(int chain);
    /// Renames the rack in place: its name in the header.
    void renameRack();
    std::function<void(const juce::String& name)> onRenameRack;
    // A macro's name dragged onto a knob in the rack (racks.md §4): the host lights where it
    // would land and maps it there on release; hovered, it lights the knobs it sets.
    std::function<void(int macro, const juce::MouseEvent&)> onMacroDrag, onMacroDrop;
    std::function<void(int macro, bool hovered)> onMacroHover;

private:
    void placeChains();
    void updateMacros();

    std::array<ui::ParamKnob*, model::RackInfo::macroCount> macros_{};
    // CHAINS: a row a chain, as the table editor's layers (an on dot, the names to choose from),
    // and its solo.
    std::array<ui::Toggle, maxChains> on_, solo_;
    std::unique_ptr<ui::Selector> names_;
    juce::StringArray placedNames_;
    int placedShown_ = -1;
    ui::Toggle addChain_{"+ Chain"};
    ui::Knob volume_, pan_;
    ui::PanelSection* chains_ = nullptr;
    ui::NameEditor nameEditor_{*this};
    model::RackInfo rack_;
    juce::String rackName_;
    int litMacro_ = -1;
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
