#pragma once

#include "ui/PianoRoll.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// Make chords… (docs/design/piano-roll.md §20.3): a card for each note, in time order, each the
/// chord that note becomes (a kind on it, a chord of the key with it on top, or the note left
/// alone) and its voicing; a row of kinds and of voicings sets every card at once. The notes change
/// live in the clip as the cards are set; Apply (Return) keeps them as one undo step, Escape puts
/// them back. Hovering a card plays its chord. Sized to its content, beside the notes.
class MakeChordsPopover final : public juce::Component, private juce::KeyListener
{
public:
    explicit MakeChordsPopover(PianoRoll& roll);
    ~MakeChordsPopover() override;

    /// Shows inside `parent` beside `anchor` (the notes' place, in `parent`'s coordinates), for the
    /// selected notes, or every note.
    void open(juce::Component& parent, juce::Rectangle<int> anchor);
    /// Keeps what it shows (one undo step) and closes.
    void apply();
    /// Puts the notes back and closes.
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    [[nodiscard]] std::size_t cardCount() const noexcept { return choices_.size(); }
    [[nodiscard]] const std::vector<model::NoteChord>& choices() const noexcept { return choices_; }
    /// What a card's chord menu and its voicing menu choose.
    void setChoice(std::size_t card, const model::NoteChord& choice);
    /// Every card at once: a kind on its note, or a voicing.
    void setAllKinds(music::ChordShape shape);
    void setAllVoicings(music::Voicing voicing);
    /// A card's chord as it reads ("Am7", "F", "No chord").
    [[nodiscard]] juce::String chordText(std::size_t card) const;
    /// A card's menus (the chord: the kinds on its note, the key's chords with it on top, Leave the
    /// note; the voicing: the voicings, then the inversions).
    [[nodiscard]] juce::PopupMenu chordMenu(std::size_t card);
    [[nodiscard]] juce::PopupMenu voicingMenu(std::size_t card);
    /// A card hovered (none: none): its chord heard.
    void hoverCard(std::optional<std::size_t> card);
    [[nodiscard]] ui::Segments& allKinds() noexcept { return allKinds_; }
    [[nodiscard]] ui::Segments& allVoicings() noexcept { return allVoicings_; }
    /// The cards (for tests and screenshots).
    [[nodiscard]] juce::Component* card(std::size_t index) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    using juce::Component::keyPressed;

private:
    class Card;

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void changed();
    void reposition();
    [[nodiscard]] juce::Rectangle<int> inner() const;
    [[nodiscard]] int cardWidth() const;
    [[nodiscard]] int cardHeight() const;
    [[nodiscard]] int cardsPerRow() const;
    [[nodiscard]] int preferredWidth() const;
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] int cardsHeight() const;
    [[nodiscard]] juce::String title() const;
    [[nodiscard]] const music::Key* key() const noexcept;
    [[nodiscard]] juce::String nameOf(const music::Chord& chord) const;

    PianoRoll& roll_;
    std::vector<std::size_t> order_;        // the notes, in time order
    std::vector<model::NoteChord> choices_; // what each becomes
    std::vector<std::unique_ptr<Card>> cards_;
    juce::Component holder_; // the cards, scrolled past three rows
    juce::Viewport viewport_;
    ui::Segments allKinds_, allVoicings_;
    ui::Toggle apply_{"Apply"};
    juce::Component::SafePointer<juce::Component> parent_;
    juce::Rectangle<int> anchor_;
    ui::OutsideClick outside_;
};
} // namespace fd::app
