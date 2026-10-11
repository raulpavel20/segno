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
    const auto* a = actionOn(key);
    return a != nullptr && perform(a->id);
}

std::vector<juce::KeyPress> ActionRegistry::keysOf(std::string_view id) const
{
    if (const auto it = overrides_.find(id); it != overrides_.end())
        return it->second;
    const auto* a = find(id);
    return a != nullptr ? a->keys : std::vector<juce::KeyPress>{};
}

const Action* ActionRegistry::actionOn(const juce::KeyPress& key) const
{
    for (const auto& a : actions_)
        for (const auto& k : keysOf(a.id))
            if (k == key)
                return &a;
    return nullptr;
}

void ActionRegistry::setKeys(std::string_view id, std::vector<juce::KeyPress> keys)
{
    const auto* action = find(id);
    if (action == nullptr)
        return;
    // A key moves: whoever had it loses it.
    for (const auto& other : actions_)
        if (other.id != id)
        {
            auto theirs = keysOf(other.id);
            const auto before = theirs.size();
            std::erase_if(theirs, [&](const juce::KeyPress& k)
                          { return std::find(keys.begin(), keys.end(), k) != keys.end(); });
            if (theirs.size() != before)
            {
                if (theirs == other.keys)
                    overrides_.erase(other.id);
                else
                    overrides_[other.id] = std::move(theirs);
            }
        }
    if (keys == action->keys)
        overrides_.erase(std::string(id));
    else
        overrides_[std::string(id)] = std::move(keys);
    if (onKeysChanged)
        onKeysChanged();
}

void ActionRegistry::resetKeys(std::string_view id)
{
    const auto* action = find(id);
    if (action == nullptr || !keysChanged(id))
        return;
    setKeys(id, action->keys); // its keys come back, taken from whoever had them since
}

void ActionRegistry::resetAllKeys()
{
    if (overrides_.empty() && unknownKeys_.empty())
        return;
    overrides_.clear();
    unknownKeys_.clear();
    if (onKeysChanged)
        onKeysChanged();
}

bool ActionRegistry::keysChanged(std::string_view id) const
{
    return overrides_.find(id) != overrides_.end();
}

std::map<std::string, std::vector<std::string>> ActionRegistry::keyMap() const
{
    auto out = unknownKeys_;
    for (const auto& [id, keys] : overrides_)
    {
        auto& list = out[id];
        list.clear();
        for (const auto& k : keys)
            list.push_back(k.getTextDescription().toStdString());
    }
    return out;
}

void ActionRegistry::setKeyMap(const std::map<std::string, std::vector<std::string>>& map)
{
    overrides_.clear();
    unknownKeys_.clear();
    for (const auto& [id, keys] : map)
    {
        if (find(id) == nullptr)
        {
            unknownKeys_[id] = keys;
            continue;
        }
        std::vector<juce::KeyPress> list;
        for (const auto& k : keys)
            if (const auto key = juce::KeyPress::createFromDescription(juce::String::fromUTF8(k.c_str()));
                key.isValid())
                list.push_back(key);
        if (list != find(id)->keys)
            overrides_[id] = std::move(list);
    }
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
    const auto keys = keysOf(id);
    return keys.empty() ? juce::String() : shortcutText(keys.front());
}

juce::String ActionRegistry::tooltip(std::string_view id) const
{
    const auto* a = find(id);
    if (a == nullptr)
        return {};
    if (keysOf(id).empty())
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
    if (!keysOf(id).empty())
        item.shortcutKeyDescription = shortcut(id);
    item.setAction([this, key = std::string(id)] { perform(key); });
    menu.addItem(std::move(item));
}

std::vector<std::pair<std::string, std::string>> ActionRegistry::conflicts() const
{
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t i = 0; i < actions_.size(); ++i)
        for (size_t j = i + 1; j < actions_.size(); ++j)
            for (const auto& a : keysOf(actions_[i].id))
                for (const auto& b : keysOf(actions_[j].id))
                    if (a == b)
                        out.emplace_back(actions_[i].id, actions_[j].id);
    return out;
}
} // namespace fd::app
