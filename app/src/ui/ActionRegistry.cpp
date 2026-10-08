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
    if (a == nullptr || !a->perform || (a->isEnabled && !a->isEnabled()))
        return false;
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

juce::String ActionRegistry::tooltip(std::string_view id) const
{
    const auto* a = find(id);
    if (a == nullptr)
        return {};
    if (a->keys.empty())
        return a->name;
    return a->name + " (" + a->keys.front().getTextDescriptionWithIcons() + ")";
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
