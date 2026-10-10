#include "ui/ShellMenus.h"

namespace fd::app
{
ShellMenus::ShellMenus(const ActionRegistry& actions, model::MenuPlatform platform)
    : actions_(actions), menus_(model::menuBar(platform))
{
}

void ShellMenus::add(juce::PopupMenu& menu, const std::vector<model::MenuEntry>& entries) const
{
    // A separator only between items: an action the app doesn't have (assistants without a
    // server) leaves none at either end or two together.
    bool separate = false;
    auto item = [&](auto&& addIt)
    {
        if (separate && menu.getNumItems() > 0)
            menu.addSeparator();
        separate = false;
        addIt();
    };
    for (const auto& e : entries)
        switch (e.kind)
        {
        case model::MenuEntry::Kind::Action:
            if (actions_.find(e.id) != nullptr)
                item([&] { actions_.addMenuItem(menu, e.id); });
            break;
        case model::MenuEntry::Kind::Separator:
            separate = true;
            break;
        case model::MenuEntry::Kind::Submenu:
        {
            juce::PopupMenu sub;
            add(sub, e.entries);
            if (sub.getNumItems() > 0)
                item([&] { menu.addSubMenu(juce::String::fromUTF8(e.title.c_str()), sub); });
            break;
        }
        case model::MenuEntry::Kind::List:
            if (fillList)
            {
                auto items = fillList(e.id);
                for (juce::PopupMenu::MenuItemIterator it(items); it.next();)
                    item([&] { menu.addItem(it.getItem()); });
            }
            break;
        }
}

juce::PopupMenu ShellMenus::menu(int index) const
{
    juce::PopupMenu m;
    if (juce::isPositiveAndBelow(index, static_cast<int>(menus_.size())))
        add(m, menus_[static_cast<size_t>(index)].entries);
    return m;
}

juce::PopupMenu ShellMenus::appMenu() const
{
    juce::PopupMenu m;
    add(m, model::macAppMenu());
    return m;
}

int ShellMenus::menuForMnemonic(juce::juce_wchar letter) const
{
    return letter > 0 && letter < 128 ? model::menuForMnemonic(menus_, static_cast<char>(letter)) : -1;
}

juce::StringArray ShellMenus::getMenuBarNames()
{
    juce::StringArray names;
    for (const auto& m : menus_)
        names.add(juce::String::fromUTF8(m.title.c_str()));
    return names;
}

juce::PopupMenu ShellMenus::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    return menu(topLevelMenuIndex);
}

int ShellMenus::mnemonicIndex(int menu) const
{
    if (!juce::isPositiveAndBelow(menu, static_cast<int>(menus_.size())))
        return -1;
    const auto& m = menus_[static_cast<size_t>(menu)];
    return juce::String(m.title).toLowerCase().indexOfChar(static_cast<juce::juce_wchar>(m.mnemonic));
}
} // namespace fd::app
