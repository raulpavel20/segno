#pragma once

#include "ui/MakeChordsForm.h"
#include "ui/PianoRoll.h"
#include "ui/ToolForm.h"

#include <fd_ui/fd_ui.h>
#include <memory>

namespace fd::app
{
/// The clip view's Tools pane (docs/design/piano-roll.md §25): docked at its right edge (floating
/// over the notes in a narrow clip view), opened by the header's Transform and Generate. Its tabs,
/// every tool of the tab as a tile, then the chosen tool: its settings (ToolForm), or Make chords'
/// rows (MakeChordsForm). Choosing a tile shows the tool on the notes as it's set; Apply (Return)
/// keeps it as one undo step; choosing it again, another tile or Escape puts the notes back. A tile
/// that can't run is dimmed, its tooltip saying why. Where the pane is too short for the tab's
/// tiles and its tallest tool, the tiles show their icons alone, in a row.
///
/// For an audio clip (audio-editor.md §11) it shows the audio's tools instead, without tabs, on
/// the host the audio editor gives it: the same tiles, settings and Apply.
class ToolsPane final : public juce::Component
{
public:
    ToolsPane(PianoRoll& roll, engine::Session& session);
    ~ToolsPane() override;

    /// The audio's tools on `host` (the audio editor's), or the notes' (null).
    void setAudio(ToolHost* host);
    [[nodiscard]] bool showsAudio() const noexcept { return audio_ != nullptr; }

    /// The tab's tiles, nothing chosen.
    void setTab(model::ToolKind tab);
    [[nodiscard]] model::ToolKind tab() const noexcept { return tab_; }
    /// Chooses a tool by id ("strum"; "makeChords" for Make chords), or none (empty), the notes
    /// back first; a tool of the other tab switches to it.
    void choose(std::string_view id);
    [[nodiscard]] const std::string& chosen() const noexcept { return chosen_; }
    /// Keeps the chosen tool's result (one undo step); nothing is chosen after.
    void apply();
    /// Nothing chosen: the notes back.
    void disarm() { choose({}); }
    /// Once a frame while shown: tiles dimmed as the notes allow, the chosen tool shown again when
    /// the notes or the selection changed under it.
    void refresh();

    /// Floating over the notes (a narrow clip view): a popover's look, else docked.
    void setFloating(bool floating);
    [[nodiscard]] bool isFloating() const noexcept { return floating_; }
    /// As wide as the widest of its tiles, every tool's settings and Make chords' rows, so it keeps
    /// its width as tools are chosen; the margin around a floating one included.
    [[nodiscard]] int preferredWidth() const;

    [[nodiscard]] ToolForm& form() noexcept { return form_; }
    [[nodiscard]] MakeChordsForm& makeChords() noexcept { return makeChords_; }
    [[nodiscard]] ui::Segments& tabs() noexcept { return tabs_; }
    [[nodiscard]] ui::Tile* tile(std::string_view id) const;
    /// The tiles show their icons alone (a short pane).
    [[nodiscard]] bool isCompact() const noexcept { return compact_; }
    /// Escape with nothing chosen: its owner closes it.
    std::function<void()> onEscape;
    /// Its own tabs chose another (the header's buttons and the view state follow).
    std::function<void(model::ToolKind)> onTabChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    /// Return applies; Escape lets the tool go, or with none chosen closes the pane. From its
    /// fields too: their keys come up to it.
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct TileItem
    {
        std::string id;
        model::ToolKind kind;
        std::unique_ptr<ui::Tile> tile;
        bool audio = false; // one of the audio's tools
    };
    /// Whether a tile shows now: the audio's, or the notes' of the tab.
    [[nodiscard]] bool shows(const TileItem& item) const noexcept;
    /// The tools shown now, and a tool of them by id.
    [[nodiscard]] const std::vector<model::ToolSpec>& catalogue() const;
    [[nodiscard]] const model::ToolSpec* findTool(std::string_view id) const;
    [[nodiscard]] ToolHost& host() const;

    /// Why a tile can't run now (empty: it can).
    [[nodiscard]] juce::String unavailable(const TileItem& item) const;
    /// Its tooltip: why it can't run, else what it does (its name first while compact).
    [[nodiscard]] juce::String tipFor(const TileItem& item) const;
    /// The tiles' height, with names (two columns) or compact (rows of icons).
    [[nodiscard]] int tilesHeight(bool compact) const;
    /// The height the tab's tallest tool needs (Make chords: its title, the All row and four rows).
    [[nodiscard]] int toolHeight() const;
    [[nodiscard]] juce::Rectangle<int> content() const;
    [[nodiscard]] juce::Rectangle<int> formArea() const;

    PianoRoll& roll_;
    engine::Session& session_;
    std::unique_ptr<ToolHost> notes_; // the piano roll's notes, as the tools see them
    ToolHost* audio_ = nullptr;       // the audio editor's, while it's shown
    ToolForm form_;
    MakeChordsForm makeChords_;
    ui::Segments tabs_{juce::StringArray{"Transform", "Generate"}};
    std::vector<TileItem> tiles_;
    model::ToolKind tab_ = model::ToolKind::Transform;
    std::string chosen_;
    std::uint64_t seen_ = 0; // the notes and selection the chosen tool was last shown on
    bool floating_ = false;
    bool compact_ = false;
};
} // namespace fd::app
