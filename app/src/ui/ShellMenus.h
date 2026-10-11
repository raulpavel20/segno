#pragma once

#include "model/MenuBar.h"
#include "ui/ActionRegistry.h"

#include <fd_ui/fd_ui.h>
#include <functional>
#include <string>

namespace fd::app
{
/// The menu bar (M2 step C10, ADR 0040, docs/design/menu-bar.md §5) over the action registry:
/// model::menuBar's menus, each item its action's (name, shortcut, dimmed, ticked), built when the
/// menu opens. The title bar shows it on Linux and Windows; on macOS it is the system menu bar.
class ShellMenus final : public juce::MenuBarModel, public ui::MenuMnemonics
{
public:
    ShellMenus(const ActionRegistry& actions, model::MenuPlatform platform);

    /// The items of a named list ("recent", "countIn", "midiLoop"), when its menu opens.
    std::function<juce::PopupMenu(const std::string& list)> fillList;

    [[nodiscard]] const std::vector<model::Menu>& menus() const noexcept { return menus_; }
    /// Menu `index` as it is now.
    [[nodiscard]] juce::PopupMenu menu(int index) const;
    /// macOS: the application menu's items (About, Settings).
    [[nodiscard]] juce::PopupMenu appMenu() const;
    /// The menu Alt+`letter` opens, or -1.
    [[nodiscard]] int menuForMnemonic(juce::juce_wchar letter) const;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int, int) override {} // items run their actions
    [[nodiscard]] int mnemonicIndex(int menu) const override;

private:
    void add(juce::PopupMenu& menu, const std::vector<model::MenuEntry>& entries) const;

    const ActionRegistry& actions_;
    std::vector<model::Menu> menus_;
};
} // namespace fd::app
