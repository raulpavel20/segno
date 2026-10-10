#pragma once

#include <string>
#include <vector>

namespace fd::model
{
// The menu bar (M2 step C10, ADR 0040, docs/design/menu-bar.md §5): every action in a traditional
// menu, as data, JUCE-free. The shell turns it into menus from the action registry, so an item has
// its action's name, shortcut, dimming and tick.

struct MenuEntry
{
    enum class Kind
    {
        Action,    // `id` is an action id
        Separator, //
        Submenu,   // `title` and `entries` (one level: no submenu inside)
        List,      // `id` names items filled when the menu opens ("recent", "countIn", "midiLoop")
    };
    Kind kind = Kind::Action;
    std::string id;
    std::string title;
    std::vector<MenuEntry> entries;

    bool operator==(const MenuEntry&) const = default;
};

struct Menu
{
    std::string title;
    char mnemonic = 0; // the letter of `title` that Alt opens it with (lower case)
    std::vector<MenuEntry> entries;
};

/// Linux and Windows: the menus in the window, File first, Help last. macOS: the system menu bar,
/// whose application menu holds About, Audio settings and Quit (macAppMenu); the window's own
/// title bar option doesn't apply.
enum class MenuPlatform
{
    Windowed,
    Mac,
};

[[nodiscard]] std::vector<Menu> menuBar(MenuPlatform platform);
/// The application menu's items on macOS (Quit, Services and Hide are the system's).
[[nodiscard]] std::vector<MenuEntry> macAppMenu();

/// Every action id in `menus`, in order, submenus included.
[[nodiscard]] std::vector<std::string> menuActions(const std::vector<Menu>& menus);
/// What's wrong with `menus`, one line each: a submenu inside a submenu, an empty menu or submenu,
/// a separator first, last or after another, an action twice, a mnemonic missing from its title or
/// used twice. Empty when they're well made.
[[nodiscard]] std::vector<std::string> menuProblems(const std::vector<Menu>& menus);

/// The menu whose mnemonic is `letter` (any case), or -1.
[[nodiscard]] int menuForMnemonic(const std::vector<Menu>& menus, char letter);
} // namespace fd::model
