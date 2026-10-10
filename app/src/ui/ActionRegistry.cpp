#include "ui/ActionRegistry.h"

namespace fd::app
{
void ActionRegistry::add(Action action)
{
    jassert(find(action.id) == nullptr); // ids are unique
    actions_.push_back(std::move(action));
    jassert(conflicts().empty()); // shortcuts are unique
}

const Action* ActionRegistry::find(std::string_view id) const
{
    for (const auto& a : actions_)
        if (a.id == id)
            return &a;
    return nullptr;
}

bool ActionRegistry::perform(std::string_view id) const
{
    const auto* a = find(id);
    if (a == nullptr || !a->perform)
        return false;
    if (a->isEnabled && !a->isEnabled())
    {
        const auto why = a->whyNot ? a->whyNot() : juce::String();
        if (why.isEmpty() || !onRefused)
            return false;
        onRefused(why);
        return true;
    }
    a->perform();
    return true;
}

bool ActionRegistry::handleKey(const juce::KeyPress& key) const
{
    for (const auto& a : actions_)
        for (const auto& k : a.keys)
            if (k == key)
                return perform(a.id);
    return false;
}

bool ActionRegistry::isToggled(std::string_view id) const
{
    const auto* a = find(id);
    return a != nullptr && a->isToggled && a->isToggled();
}

bool ActionRegistry::isEnabled(std::string_view id) const
{
    const auto* a = find(id);
    return a != nullptr && (!a->isEnabled || a->isEnabled());
}

juce::String ActionRegistry::label(std::string_view id) const
{
    const auto* a = find(id);
    if (a == nullptr)
        return {};
    if (a->label)
        if (auto now = a->label(); now.isNotEmpty())
            return now;
    return a->name;
}

juce::String ActionRegistry::shortcutText(const juce::KeyPress& key)
{
#if JUCE_MAC
    return key.getTextDescriptionWithIcons();
#else
    juce::String out;
    const auto mods = key.getModifiers();
    if (mods.isCtrlDown())
        out << "Ctrl+";
    if (mods.isAltDown())
        out << "Alt+";
    if (mods.isShiftDown())
        out << "Shift+";
    const int code = key.getKeyCode();
    using K = juce::KeyPress;
    const std::pair<int, const char*> named[] = {
        {K::spaceKey, "Space"},
        {K::escapeKey, "Esc"},
        {K::returnKey, "Enter"},
        {K::tabKey, "Tab"},
        {K::deleteKey, "Del"},
        {K::backspaceKey, "Backspace"},
        {K::homeKey, "Home"},
        {K::endKey, "End"},
        {K::pageUpKey, "PgUp"},
        {K::pageDownKey, "PgDn"},
        {K::upKey, "\xE2\x86\x91"},    // ↑
        {K::downKey, "\xE2\x86\x93"},  // ↓
        {K::leftKey, "\xE2\x86\x90"},  // ←
        {K::rightKey, "\xE2\x86\x92"}, // →
    };
    for (const auto& [k, name] : named)
        if (code == k)
            return out + juce::String::fromUTF8(name);
    const int f[] = {K::F1Key, K::F2Key, K::F3Key, K::F4Key, K::F5Key, K::F6Key,
                     K::F7Key, K::F8Key, K::F9Key, K::F10Key, K::F11Key, K::F12Key};
    for (int i = 0; i < 12; ++i)
        if (code == f[i])
            return out + "F" + juce::String(i + 1);
    return out + juce::String::charToString(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(code)));
#endif
}

juce::String ActionRegistry::shortcut(std::string_view id) const
{
    const auto* a = find(id);
    return a == nullptr || a->keys.empty() ? juce::String() : shortcutText(a->keys.front());
}

juce::String ActionRegistry::tooltip(std::string_view id) const
{
    const auto* a = find(id);
    if (a == nullptr)
        return {};
    if (a->keys.empty())
        return label(id);
    return label(id) + " (" + shortcut(id) + ")";
}

void ActionRegistry::addMenuItem(juce::PopupMenu& menu, std::string_view id) const
{
    const auto* a = find(id);
    if (a == nullptr)
        return;
    juce::PopupMenu::Item item(label(id));
    item.setEnabled(isEnabled(id));
    item.setTicked(isToggled(id));
    if (!a->keys.empty())
        item.shortcutKeyDescription = shortcut(id);
    item.setAction([this, key = std::string(id)] { perform(key); });
    menu.addItem(std::move(item));
}

std::vector<std::pair<std::string, std::string>> ActionRegistry::conflicts() const
{
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i < actions_.size(); ++i)
        for (size_t j = i + 1; j < actions_.size(); ++j)
            for (const auto& a : actions_[i].keys)
                for (const auto& b : actions_[j].keys)
                    if (a == b)
                        out.emplace_back(actions_[i].id, actions_[j].id);
    return out;
}
} // namespace fd::app
