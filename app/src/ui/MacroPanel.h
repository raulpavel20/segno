#pragma once

#include "model/Snapshot.h"

#include <array>
#include <fd_device_ui/fd_device_ui.h>

namespace fd::app
{
/// A panel with a rack's eight macros (racks.md §4, §5a): a rack's (RackPanel) and a Drum Kit's
/// (DrumKitPanel, drum-kit.md §10), so they look and behave the same in both.
///   - Each macro is a knob whose name is its handle: dragged onto a knob in the rack, it maps it;
///     hovered, it lights what it sets; a double-click renames it.
///   - Its right-click: Rename macro…, Edit macros, what the panel adds, then its automation.
///   - One that maps nothing yet recedes: turning it does nothing.
/// The macros are the panel's own parameters 0 … 7 (engine::Session::rackMacros(), drumKit()).
class MacroPanel : public ui::DevicePanel
{
public:
    explicit MacroPanel(ui::DeviceUiContext& context);

    [[nodiscard]] ui::ParamKnob& macroKnob(int macro) const { return *macros_[static_cast<size_t>(macro)]; }
    /// Lights a macro's name while the pointer is on a knob it sets, elsewhere in the rack (-1:
    /// none).
    void setLitMacro(int macro);
    /// Renames a macro in place: its name over its knob.
    void renameMacro(int macro);

    std::function<void(int macro, const juce::String& name)> onRenameMacro;
    /// Its right-click's Edit macros: the macro editor, that macro chosen.
    std::function<void(int macro)> onEditMacros;
    // A macro's name dragged onto a knob in the rack (racks.md §4): the host lights where it
    // would land and maps it there on release; hovered, it lights the knobs it sets.
    std::function<void(int macro, const juce::MouseEvent&)> onMacroDrag, onMacroDrop;
    std::function<void(int macro, bool hovered)> onMacroHover;

protected:
    /// The eight knobs in `section`, two rows of four.
    void addMacroKnobs(ui::PanelSection& section);
    /// Their names and their look from the rack's macros, `macros` as the snapshot has them.
    void updateMacroKnobs(const std::array<model::RackMacro, model::RackInfo::macroCount>& macros);
    /// What a panel adds to a macro's right-click, after Edit macros (a rack: Store snapshot, Lock).
    virtual void addMacroItems(int /*macro*/, juce::PopupMenu& /*menu*/) {}
    /// Whether a macro is lit for another reason too (a rack: Morph's name hovered).
    [[nodiscard]] virtual bool macroLit(int /*macro*/) const { return false; }
    /// A macro's name hovered (a rack: Morph lights while one it moves is).
    virtual void macroHovered(int /*macro*/, bool /*hovered*/) {}

    std::array<ui::ParamKnob*, model::RackInfo::macroCount> macros_{};
    std::array<model::RackMacro, model::RackInfo::macroCount> macroInfo_{};
    int litMacro_ = -1;
    ui::NameEditor nameEditor_{*this};
};
} // namespace fd::app
