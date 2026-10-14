#pragma once

#include "model/Fuzzy.h"
#include "ui/ActionRegistry.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// The command palette (M2 step C1, ui-principles §1): every action by fuzzy search, with its
/// shortcut, recently used first. A popover at the top of the window: Escape or a click
/// elsewhere closes it; it never blocks anything.
class CommandPalette final : public juce::Component, private juce::KeyListener, private juce::TextEditor::Listener
{
public:
    static constexpr int maxRows = 12;

    explicit CommandPalette(const ActionRegistry& actions);

    /// Shows the palette inside `parent` (top centre) with an empty query.
    void open(juce::Component& parent);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    /// Entries besides the actions (each setting, "Settings: Buffer size"): asked for each time
    /// the palette opens. Their ids mustn't be actions'.
    struct Entry
    {
        std::string id, name, category;
        std::function<void()> perform;
    };
    std::function<std::vector<Entry>()> entries;
    /// Action ids performed from the palette, most recent first.
    [[nodiscard]] const std::vector<std::string>& recent() const noexcept { return recent_; }
    /// Runs the selected action (Enter).
    void performSelected();
    /// The query, for tests.
    void setQuery(const juce::String& text);
    [[nodiscard]] std::vector<std::string> shownIds() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void inputAttemptWhenModal() override { close(); }

    using juce::Component::keyPressed;

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void textEditorTextChanged(juce::TextEditor&) override { update(); }
    void textEditorReturnKeyPressed(juce::TextEditor&) override { performSelected(); }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { close(); }
    void update();
    [[nodiscard]] juce::Rectangle<int> rowArea(int row) const;

    const ActionRegistry& actions_;
    ui::SearchField search_;
    std::vector<model::PaletteItem> items_;
    std::vector<Entry> entries_; // the extra entries, as the palette opened
    [[nodiscard]] const Entry* entry(const std::string& id) const;
    std::vector<size_t> shown_;
    std::vector<std::string> recent_;
    int selected_ = 0;
    ui::OutsideClick outside_{*this, [this] { close(); }}; // a click anywhere else closes it
};
} // namespace fd::app
