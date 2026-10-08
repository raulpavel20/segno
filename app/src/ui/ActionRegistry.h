#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace fd::app
{
/// One user action (ADR 0018, docs/design/daw-shell.md "Actions and shortcuts").
struct Action
{
    std::string id;          // stable: "transport.playStop"
    juce::String name;       // "Play / stop"
    juce::String category;   // "Transport"
    std::vector<juce::KeyPress> keys; // default shortcuts
    std::function<void()> perform;
    std::function<bool()> isEnabled; // null = always
    std::function<bool()> isToggled; // null = not a toggle
};

/// Every action in the app, looked up by id or key press. The command palette (M2) lists it;
/// buttons take their tooltips from it.
class ActionRegistry
{
public:
    /// Adds an action. Its id and shortcuts must be unique (asserted, and checked by a test).
    void add(Action action);

    [[nodiscard]] const Action* find(std::string_view id) const;
    [[nodiscard]] const std::vector<Action>& all() const noexcept { return actions_; }

    /// Performs `id` if it exists and is enabled. Returns true if it ran.
    bool perform(std::string_view id) const;

    /// Performs the action bound to `key`, if any. Returns true if one was found and enabled.
    bool handleKey(const juce::KeyPress& key) const;

    [[nodiscard]] bool isToggled(std::string_view id) const;

    /// "Name (Shortcut)" for tooltips.
    [[nodiscard]] juce::String tooltip(std::string_view id) const;

    /// Pairs of action ids that share a shortcut (empty when the map is clean).
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> conflicts() const;

private:
    std::vector<Action> actions_;
};
} // namespace fd::app
