#include "ui/MacroPanel.h"

namespace fd::app
{
MacroPanel::MacroPanel(ui::DeviceUiContext& context) : DevicePanel(context) {}

void MacroPanel::addMacroKnobs(ui::PanelSection& section)
{
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
    {
        auto& knob = section.knob(static_cast<size_t>(i), i % 4, i < 4 ? 0 : 3);
        knob.setSourceHandle(true);
        knob.onHandleDrag = [this, i](const juce::MouseEvent& e)
        {
            if (onMacroDrag)
                onMacroDrag(i, e);
        };
        knob.onHandleRelease = [this, i](const juce::MouseEvent& e)
        {
            if (onMacroDrop)
                onMacroDrop(i, e);
        };
        knob.onHandleHover = [this, i](bool hovered)
        {
            if (onMacroHover)
                onMacroHover(i, hovered);
            macroHovered(i, hovered);
        };
        knob.onHandleDoubleClick = [this, i] { renameMacro(i); };
        // Its right-click: rename it, edit the macros, the panel's own, then its automation.
        knob.onPopupMenu = [this, &knob, i]
        {
            ui::showParamMenu(this->context(), static_cast<size_t>(i), knob,
                              [this, i](juce::PopupMenu& menu)
                              {
                                  menu.addItem(juce::String::fromUTF8("Rename macro\xe2\x80\xa6"),
                                               [this, i] { renameMacro(i); });
                                  menu.addItem("Edit macros",
                                               [this, i]
                                               {
                                                   if (onEditMacros)
                                                       onEditMacros(i);
                                               });
                                  addMacroItems(i, menu);
                              });
        };
        macros_[static_cast<size_t>(i)] = &knob;
    }
}

void MacroPanel::updateMacroKnobs(const std::array<model::RackMacro, model::RackInfo::macroCount>& macros)
{
    macroInfo_ = macros;
    const auto& t = ui::Theme::current();
    const auto& specs = context().info.params;
    for (size_t i = 0; i < macros_.size(); ++i)
    {
        if (macros_[i] == nullptr)
            continue;
        const auto& macro = macros[i];
        auto& knob = *macros_[i];
        knob.setName(macro.name.empty() ? juce::String(std::string(specs[i].name))
                                        : juce::String::fromUTF8(macro.name.c_str()));
        // Lit while its own knob's pointed at, or for the panel's own reason.
        const int index = static_cast<int>(i);
        knob.setSourceHandle(true, index == litMacro_ || macroLit(index));
        // One that maps nothing yet recedes: turning it does nothing (racks.md §5a).
        knob.setAlpha(macro.mappings.empty() && index != litMacro_ ? t.roles.fadedAlpha : 1.0f);
    }
}

void MacroPanel::setLitMacro(int macro)
{
    if (macro == litMacro_)
        return;
    litMacro_ = macro;
    updateMacroKnobs(macroInfo_);
}

void MacroPanel::renameMacro(int macro)
{
    if (macro < 0 || macro >= model::RackInfo::macroCount || isFolded() ||
        macros_[static_cast<size_t>(macro)] == nullptr)
        return;
    auto& knob = *macros_[static_cast<size_t>(macro)];
    const auto strip = knob.nameStrip().toNearestInt() + getLocalPoint(&knob, juce::Point<int>());
    nameEditor_.show(strip, knob.getName(), ui::TextStyle::Label,
                     [this, macro](const juce::String& name)
                     {
                         if (onRenameMacro)
                             onRenameMacro(macro, name);
                     });
}
} // namespace fd::app
