#pragma once

#include "engine/Session.h"
#include "model/Fuzzy.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// The parameter picker (docs/design/automation.md §3, §9): assigns a parameter to a lane. A
/// searchable list of a channel's parameters, like the command palette (name, module and current
/// value), with a channel row at its top that switches to another track, return or the master.
/// Parameters on another lane are marked ("on Filter sweep"), and choosing one moves it here.
class LanePicker final : public juce::Component, private juce::KeyListener, private juce::TextEditor::Listener
{
public:
    static constexpr int maxRows = 12;

    explicit LanePicker(engine::Session& session);

    /// Shows inside `parent` (top centre) to assign one of `channel`'s parameters to `lane`.
    void open(juce::Component& parent, std::uint64_t lane, std::uint64_t channel);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }
    /// The channel whose parameters it lists (its channel row's menu switches it).
    void setChannel(std::uint64_t channel);
    [[nodiscard]] std::uint64_t channel() const noexcept { return channel_; }
    /// Assigns the selected row's parameter (Enter).
    void chooseSelected();
    void setQuery(const juce::String& text);
    /// The rows shown, by parameter key.
    [[nodiscard]] std::vector<std::string> shownLanes() const;
    /// Selects a shown row (as the arrows or the mouse do).
    void setSelectedRow(int row);

    /// A parameter was assigned to the lane.
    std::function<void(std::uint64_t lane)> onChosen;

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
    void update();
    void showChannelMenu();
    [[nodiscard]] int rowCount() const noexcept { return static_cast<int>(shown_.size()); }
    [[nodiscard]] juce::Rectangle<int> channelArea() const;
    [[nodiscard]] juce::Rectangle<int> rowArea(int row) const;

    engine::Session& session_;
    std::uint64_t channel_ = 0, lane_ = 0;
    juce::TextEditor search_;
    std::vector<engine::Session::ParamChoice> choices_;
    std::vector<model::PaletteItem> items_;
    std::vector<size_t> shown_;
    int selected_ = 0;
    ui::OutsideClick outside_{*this, [this] { close(); }}; // a click anywhere else closes it
};
} // namespace fd::app
