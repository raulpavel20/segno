#pragma once

#include "ui/ToolHost.h"

#include <fd_ui/fd_ui.h>
#include <memory>

namespace fd::app
{
/// A tool's settings in the Tools pane (docs/design/piano-roll.md §23, §25; audio-editor.md §11),
/// built from its description (model/NoteToolkit, model/AudioToolkit): its name and what it acts on
/// in caps with Apply, then a row per setting the tool's choices show: a short choice as segments,
/// a long one as a choice field, a number as a number field. What it acts on (its host: the notes,
/// or audio clips) shows the result as it's set; Apply keeps it as one undo step named for the tool
/// and remembers its settings; cancel() puts it back. Shorter than its rows, they scroll under the
/// title and Apply.
class ToolForm final : public juce::Component
{
public:
    explicit ToolForm(ToolHost& host);
    ~ToolForm() override;

    /// What its tools act on; a tool shown lets go first.
    void setHost(ToolHost& host);
    /// Shows `tool` (its last settings) and previews it; none: nothing, its host as it was.
    void setTool(const model::ToolSpec* tool);
    [[nodiscard]] const model::ToolSpec* tool() const noexcept { return tool_; }
    /// Keeps what it shows (one undo step) and remembers the settings.
    void apply();
    /// Puts the notes back.
    void cancel();
    /// Previews again (the notes changed under it).
    void preview();

    [[nodiscard]] const model::ToolValues& values() const noexcept { return values_; }
    /// A setting changed, as its control changes it.
    void setValue(std::string_view setting, double value);
    /// A setting changed from elsewhere (the audio Tempo tool's downbeat dragged on the
    /// waveform): its control shows it, and it previews.
    void show(std::string_view setting, double value);
    /// A setting's control (for tests and screenshots); null while it isn't shown.
    [[nodiscard]] juce::Component* control(std::string_view setting) const;
    /// "STRUM · 14 NOTES", "CHORDS · 4 BARS".
    [[nodiscard]] juce::String title() const;
    /// As wide as its widest row and its title, for `tool`; as tall as the rows shown (for `tool`,
    /// the most any of its choices shows).
    [[nodiscard]] int preferredWidth(const model::ToolSpec& tool) const;
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] static int preferredHeight(const model::ToolSpec& tool);
    /// Whether its rows scroll (for tests).
    [[nodiscard]] bool scrolls() const;
    /// The rows changed (a choice others show for): the pane lays out again.
    std::function<void()> onLayoutChanged;
    /// Its Apply button: its owner keeps it, as Return does (the pane lets the tool go after);
    /// unset, the button applies it here.
    std::function<void()> onApply;
    [[nodiscard]] ui::Toggle& applyButton() noexcept { return apply_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Rows;
    class HalveDouble;
    struct Row
    {
        const model::ToolSetting* setting = nullptr;
        std::unique_ptr<juce::Component> control;
        bool shown = true;
    };

    void build();
    [[nodiscard]] static int heightFor(int rows);
    void paintLabels(juce::Graphics& g) const;
    [[nodiscard]] int controlWidth(const model::ToolSetting& setting) const;

    ToolHost* host_;
    const model::ToolSpec* tool_ = nullptr;
    model::ToolValues values_;
    std::vector<Row> rows_;
    std::unique_ptr<Rows> holder_; // the rows, scrolled
    juce::Viewport viewport_;
    ui::Toggle apply_{"Apply"};
    // Never shown: they measure what a field of each kind needs for a text, as the rows' fields do.
    mutable ui::NumberField measureNumber_;
    mutable ui::ChoiceField measureChoice_;
};
} // namespace fd::app
