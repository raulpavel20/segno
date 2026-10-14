#pragma once

#include "ui/PianoRoll.h"

#include <fd_ui/fd_ui.h>
#include <memory>

namespace fd::app
{
/// A tool of Notes ▸ Transform ▸ and Generate ▸ (docs/design/piano-roll.md §12, §23), built from
/// its description (model/NoteToolkit): its name and what it acts on in caps, Apply in the title
/// row, then a row per setting: a short choice as segments, a long one as a choice field, a number
/// as a number field. The notes change live as it's set; Apply (Return) keeps them as one undo
/// step named for the tool, Escape puts them back. Its settings are remembered for next time.
/// Sized to its content, at the top right of the clip view's notes.
class ToolPopover final : public juce::Component, private juce::KeyListener
{
public:
    ToolPopover(PianoRoll& roll, engine::Session& session);
    ~ToolPopover() override;

    /// Shows inside `parent`, at the top right of `notes` (the grid, in `parent`'s coordinates).
    void open(juce::Component& parent, juce::Rectangle<int> notes, const model::ToolSpec& tool);
    /// Keeps what it shows (one undo step) and closes.
    void apply();
    /// Puts the notes back and closes.
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    [[nodiscard]] const model::ToolSpec* tool() const noexcept { return tool_; }
    [[nodiscard]] const model::ToolValues& values() const noexcept { return values_; }
    /// A setting changed, as its control changes it.
    void setValue(std::string_view setting, double value);
    /// A setting's control (for tests and screenshots); null while it isn't shown.
    [[nodiscard]] juce::Component* control(std::string_view setting) const;
    /// "STRUM · 14 NOTES", "CHORDS · 4 BARS".
    [[nodiscard]] juce::String title() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    using juce::Component::keyPressed;

private:
    struct Row
    {
        const model::ToolSetting* setting = nullptr;
        std::unique_ptr<juce::Component> control;
        bool shown = true;
    };

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void build();
    void preview();
    void place();
    [[nodiscard]] model::ToolRun run() const;
    [[nodiscard]] juce::Rectangle<int> inner() const;
    [[nodiscard]] int controlWidth(const Row& row) const;
    [[nodiscard]] int preferredWidth() const;
    [[nodiscard]] int preferredHeight() const;

    PianoRoll& roll_;
    engine::Session& session_;
    const model::ToolSpec* tool_ = nullptr;
    model::ToolValues values_;
    std::vector<Row> rows_;
    ui::Toggle apply_{"Apply"};
    juce::Component::SafePointer<juce::Component> parent_;
    juce::Rectangle<int> notes_;
    ui::OutsideClick outside_;
};
} // namespace fd::app
