#pragma once

#include "engine/Session.h"
#include "model/Fuzzy.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// "Add automation lane" (docs/design/automation.md §3): a searchable list of a channel's
/// parameters, like the command palette: name, module and current value; lanes already shown
/// are marked. Choosing one shows its lane.
class LanePicker final : public juce::Component, private juce::KeyListener, private juce::TextEditor::Listener
{
public:
    static constexpr int maxRows = 12;

    explicit LanePicker(engine::Session& session);

    /// Shows the picker inside `parent` (top centre) for a channel's parameters.
    void open(juce::Component& parent, std::uint64_t channel);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }
    /// Shows the selected parameter's lane (Enter).
    void chooseSelected();
    void setQuery(const juce::String& text);
    [[nodiscard]] std::vector<std::string> shownLanes() const;

    /// A lane was shown from the picker.
    std::function<void(const engine::Session::LaneTarget&)> onChosen;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    using juce::Component::keyPressed;

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void textEditorTextChanged(juce::TextEditor&) override { update(); }
    void textEditorReturnKeyPressed(juce::TextEditor&) override { chooseSelected(); }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { close(); }
    void textEditorFocusLost(juce::TextEditor&) override;
    void update();
    [[nodiscard]] juce::Rectangle<int> rowArea(int row) const;

    engine::Session& session_;
    std::uint64_t channel_ = 0;
    juce::TextEditor search_;
    std::vector<engine::Session::LaneChoice> choices_;
    std::vector<model::PaletteItem> items_;
    std::vector<size_t> shown_;
    int selected_ = 0;
};
} // namespace fd::app
