#pragma once

namespace fd::ui
{
/// Gives `component` the keyboard focus and keeps it there until the user presses the mouse
/// again (or for `focusKeptFor`): when its window gets the system's focus back (after a menu, at
/// launch), JUCE hands the focus to what had it before without telling `component`, which would
/// leave a field showing that typing never reaches (ui-foundations.md, "Shared components").
/// Without the system's focus yet, it takes it as soon as the window does.
void takeFocus(juce::Component& component);
inline constexpr int focusKeptFor = 2000; // ms
/// Whether `component` took the focus with `takeFocus` during the mouse press being handled (a
/// double-click that opened a field there).
[[nodiscard]] bool tookFocusOnThisPress(const juce::Component& component);

/// A press anywhere else in the window ends typing in a text field and keeps what was typed
/// (ui-principles.md §1): the field's window hands the focus `home`, and the field's focus loss
/// keeps it. Presses in other windows (a menu) don't count, nor does the press that put the focus
/// in the field (a double-click that starts a rename). One, installed by the shell.
class EndEditingOnClickAway final : private juce::MouseListener, private juce::FocusChangeListener
{
public:
    /// `home`: where the focus goes, holding the fields it watches (the shell).
    explicit EndEditingOnClickAway(juce::Component& home);
    ~EndEditingOnClickAway() override;
    EndEditingOnClickAway(const EndEditingOnClickAway&) = delete;
    EndEditingOnClickAway& operator=(const EndEditingOnClickAway&) = delete;

    /// The rule: a press on `component` ends the typing unless it's in the field, in another
    /// window, or the field took the focus during this press.
    void clickedOn(juce::Component& component);

private:
    void mouseDown(const juce::MouseEvent& e) override;
    void globalFocusChanged(juce::Component* focused) override;

    juce::Component& home_;
    /// The focus as last reported (reports come after the event that moved it; pending events
    /// are handled in one go, so `tookFocusOnThisPress` covers what this misses).
    juce::Component::SafePointer<juce::Component> settled_;
};
} // namespace fd::ui
