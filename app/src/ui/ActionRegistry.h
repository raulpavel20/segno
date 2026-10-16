#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
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
    std::vector<juce::KeyPress> keys; // default shortcuts (yours, where changed, are the registry's)
    std::function<void()> perform;
    std::function<bool()> isEnabled; // null = always
    std::function<bool()> isToggled; // null = not a toggle
    /// While it can't run: why, said when its shortcut is pressed anyway (null or empty: nothing
    /// to say, the key goes elsewhere).
    std::function<juce::String()> whyNot;
    /// The name as menus and tooltips show it now ("Undo Add track"); null: `name`.
    std::function<juce::String()> label;
    /// An action whose key it shows and shares, having none of its own: the key goes to that
    /// action, which acts on whatever kind of clip the clip view shows (the Notes and Audio menus'
    /// Quantise and Transpose, audio-editor.md §13).
    std::string keysFrom;
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
    /// Adds a condition to an action already added: it runs only while `condition` holds too, and
    /// `why` says why when it doesn't (a view where it means nothing: a Drum Kit's pads).
    void require(std::string_view id, std::function<bool()> condition, std::function<juce::String()> why);

    /// Performs `id` if it exists and is enabled. Returns true if it ran, or if it couldn't and
    /// said why (onRefused).
    bool perform(std::string_view id) const;
    /// Tells the user why an action couldn't run (a toast).
    std::function<void(const juce::String&)> onRefused;

    /// Performs the action bound to `key`, if any. Returns true if one was found and enabled.
    bool handleKey(const juce::KeyPress& key) const;

    [[nodiscard]] bool isToggled(std::string_view id) const;
    /// True if `id` exists and can run now (buttons bound to it dim otherwise).
    [[nodiscard]] bool isEnabled(std::string_view id) const;

    /// The name menus and tooltips show now (Action::label, else its name).
    [[nodiscard]] juce::String label(std::string_view id) const;
    /// A shortcut as menus, tooltips and the palette show it: "Ctrl+Shift+E", "Alt+↑", "Space" on
    /// Linux and Windows (the system's symbols on macOS).
    [[nodiscard]] static juce::String shortcutText(const juce::KeyPress& key);
    /// `id`'s first shortcut as shortcutText shows it (an action's it shares, Action::keysFrom);
    /// empty without one.
    [[nodiscard]] juce::String shortcut(std::string_view id) const;
    /// "Name (Shortcut)" for tooltips.
    [[nodiscard]] juce::String tooltip(std::string_view id) const;

    /// Adds `id` to a right-click menu: its name and shortcut, dimmed while it can't run, ticked
    /// while on. Choosing it performs it.
    void addMenuItem(juce::PopupMenu& menu, std::string_view id) const;

    /// Pairs of action ids that share a shortcut (empty when the map is clean).
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> conflicts() const;

    // ---- your shortcuts (Settings > Keys, ADR 0045) -----------------------------------------

    /// An action's keys now: yours where you changed them, else its defaults.
    [[nodiscard]] std::vector<juce::KeyPress> keysOf(std::string_view id) const;
    /// The action a key runs now, if any.
    [[nodiscard]] const Action* actionOn(const juce::KeyPress& key) const;
    /// Sets your keys for `id` (empty: none). A key another action has is taken from it (the page
    /// asks first). Tells onKeysChanged.
    void setKeys(std::string_view id, std::vector<juce::KeyPress> keys);
    /// Back to the defaults: one action's, or every one's.
    void resetKeys(std::string_view id);
    void resetAllKeys();
    [[nodiscard]] bool keysChanged(std::string_view id) const;
    [[nodiscard]] bool anyKeysChanged() const { return !overrides_.empty(); }
    /// Your changes as Settings keeps them (each key's text description), and back; keys for
    /// actions that don't exist (any more) are kept, so they come back if the action does.
    [[nodiscard]] std::map<std::string, std::vector<std::string>> keyMap() const;
    void setKeyMap(const std::map<std::string, std::vector<std::string>>& map);
    std::function<void()> onKeysChanged;

private:
    /// The keys `id` shows: its own, else those it shares.
    [[nodiscard]] std::vector<juce::KeyPress> shownKeys(std::string_view id) const;

    std::vector<Action> actions_;
    std::map<std::string, std::vector<juce::KeyPress>, std::less<>> overrides_;
    std::map<std::string, std::vector<std::string>> unknownKeys_; // for actions this build lacks
};
} // namespace fd::app
