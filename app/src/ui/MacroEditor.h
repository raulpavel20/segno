#pragma once

#include "model/Snapshot.h"

#include <fd_device_ui/fd_device_ui.h>

namespace fd::app
{
/// A rack's macros and what they move (docs/design/racks.md §6): a module's editor in the detail
/// panel (ADR 0053), its tab "Macros". Panes, left to right:
///   - MACROS: the eight, to choose one; one that maps nothing yet recedes;
///   - MAPPINGS: the chosen macro's, a row each: its module and parameter, From and To as bars in
///     the parameter's unit, Curve, and remove; + adds one;
///   - CURVES: every one of them drawn across the macro's travel, in its row's colour, the
///     macro's position a line through them, and the macro itself as a bar under them.
/// Edits go to the host as they're made, each an undo step there.
class MacroEditor final : public ui::ModuleEditor
{
public:
    /// What a mapping's From and To say at `normalised`, in its parameter's unit ("1.20 kHz").
    using ValueText = std::function<juce::String(const model::MacroMappingInfo&, float normalised)>;

    explicit MacroEditor(ValueText valueText);
    ~MacroEditor() override;

    /// What it shows: the rack's name (its title), macros and the modules they map.
    void setRack(const juce::String& name, const model::RackInfo& rack);
    void selectMacro(int macro);
    [[nodiscard]] int selectedMacro() const noexcept { return macro_; }

    [[nodiscard]] juce::String tabName() const override { return "Macros"; }
    [[nodiscard]] juce::String title() const override { return name_; }
    [[nodiscard]] juce::String closeTooltip() const override
    {
        return "Close the macro editor: its edits are kept as they're made";
    }

    std::function<void(int macro, int mapping, float from, float to, float curve)> onMappingChange;
    std::function<void(int macro, int mapping)> onRemoveMapping;
    /// "+": the host offers the rack's modules and their parameters.
    std::function<void(int macro)> onAddMapping;
    std::function<void(int macro, float value)> onMacroValue;

    void resized() override;

private:
    class Curves;
    class MappingName;
    void rebuild();

    ValueText valueText_;
    juce::String name_;
    model::RackInfo rack_;
    int macro_ = 0;
    int macrosPane_ = 0, mappingsPane_ = 0, curvesPane_ = 0;
    std::unique_ptr<ui::Selector> macros_;
    std::vector<std::unique_ptr<juce::Component>> rows_;
    std::unique_ptr<Curves> curves_;
    ui::Knob value_;
    ui::IconButton add_{ui::Icon::Plus};
    int shownRows_ = -1;
};
} // namespace fd::app
