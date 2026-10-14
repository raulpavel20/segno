#pragma once

#include "ui/PianoRoll.h"

#include <fd_ui/fd_ui.h>
#include <memory>

namespace fd::app
{
/// Make chords (docs/design/piano-roll.md §20.3, §25), the Tools pane's Make chords tile: a row
/// per note in time order, each the chord that note becomes (a kind on it, a chord of the key with
/// it on top, or the note left alone) and its voicing, each opening its menu; the All row at the
/// top sets every note at once. The notes change live in the clip; Apply keeps them as one undo
/// step, cancel() puts them back. Hovering a row plays its chord. The list scrolls past its height.
class MakeChordsForm final : public juce::Component
{
public:
    explicit MakeChordsForm(PianoRoll& roll);
    ~MakeChordsForm() override;

    /// For the selected notes, or every note: a row each, previewed.
    void start();
    /// Keeps what it shows (one undo step); its rows go.
    void apply();
    /// Puts the notes back; its rows go.
    void cancel();
    /// Its Apply button: its owner keeps it, as Return does (the pane lets the tool go after);
    /// unset, the button applies it here.
    std::function<void()> onApply;

    [[nodiscard]] std::size_t cardCount() const noexcept { return choices_.size(); }
    [[nodiscard]] const std::vector<model::NoteChord>& choices() const noexcept { return choices_; }
    /// What a row's chord menu and its voicing menu choose.
    void setChoice(std::size_t card, const model::NoteChord& choice);
    /// Every row at once: a kind on its note, or a voicing.
    void setAllKinds(music::ChordShape shape);
    void setAllVoicings(music::Voicing voicing);
    /// The kind and voicing every row shares, if they share one (the All row shows it).
    [[nodiscard]] std::optional<music::ChordShape> allKind() const;
    [[nodiscard]] std::optional<music::Voicing> allVoicing() const;
    /// A row's chord as it reads ("Am7", "F", "No chord").
    [[nodiscard]] juce::String chordText(std::size_t card) const;
    /// A row's menus (the chord: the kinds on its note, the key's chords with it on top, Leave the
    /// note; the voicing: the voicings, then the inversions), and the All row's.
    [[nodiscard]] juce::PopupMenu chordMenu(std::size_t card);
    [[nodiscard]] juce::PopupMenu voicingMenu(std::size_t card);
    [[nodiscard]] juce::PopupMenu allKindsMenu();
    [[nodiscard]] juce::PopupMenu allVoicingsMenu();
    /// A row hovered (none: none): its chord heard.
    void hoverCard(std::optional<std::size_t> card);
    /// The rows (for tests and screenshots).
    [[nodiscard]] juce::Component* card(std::size_t index) const;
    /// "Make chords · 4 notes".
    [[nodiscard]] juce::String title() const;
    /// As wide as its widest row.
    [[nodiscard]] int preferredWidth() const;
    [[nodiscard]] ui::Toggle& applyButton() noexcept { return apply_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Row;

    void changed();
    void clearRows();
    [[nodiscard]] const music::Key* key() const noexcept;
    [[nodiscard]] juce::String nameOf(const music::Chord& chord) const;
    /// The columns: the note's label, its chord, its voicing.
    [[nodiscard]] int labelWidth() const;
    [[nodiscard]] int chordWidth() const;
    [[nodiscard]] int voicingWidth() const;

    PianoRoll& roll_;
    std::vector<std::size_t> order_;        // the notes, in time order
    std::vector<model::NoteChord> choices_; // what each becomes
    std::unique_ptr<Row> all_;              // the All row
    std::vector<std::unique_ptr<Row>> rows_;
    juce::Component holder_; // the rows, scrolled
    juce::Viewport viewport_;
    ui::Toggle apply_{"Apply"};
};
} // namespace fd::app
