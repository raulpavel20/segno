#pragma once

#include "ui/PianoRoll.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// The Chord popover (docs/design/piano-roll.md §7.3, ADR 0058): the chords to write at a place in
/// the chord lane. The key's chords on each step, the usual next ones after the chord before, and
/// borrowed ones; hovering one plays it and shows its notes in place, a click writes it (one undo
/// step each). Its Kind, Voicing and Inversion are its own, for the chord there (§20.4). Sized to
/// its content, beside the place it writes so the chord before stays in view.
class ChordPopover final : public juce::Component, private juce::KeyListener
{
public:
    explicit ChordPopover(PianoRoll& roll);
    ~ChordPopover() override;

    /// Shows inside `parent` beside `anchor` (the place in the lane, in `parent`'s coordinates).
    void open(juce::Component& parent, juce::Rectangle<int> anchor, const PianoRoll::ChordPlace& place);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    /// A row of chords offered, under its caps title.
    struct Group
    {
        juce::String title;
        std::vector<music::Chord> chords;
    };
    [[nodiscard]] const std::vector<Group>& groups() const noexcept { return groups_; }
    /// What a click and a hover on a chord do.
    void choose(const music::Chord& chord);
    void hover(const std::optional<music::Chord>& chord);
    /// Where it writes.
    [[nodiscard]] const std::optional<PianoRoll::ChordPlace>& place() const noexcept { return place_; }
    [[nodiscard]] ui::Segments& kind() noexcept { return kind_; }
    [[nodiscard]] ui::Segments& voicing() noexcept { return voicing_; }
    [[nodiscard]] ui::Segments& inversion() noexcept { return inversion_; }
    /// The chord buttons, in the groups' order (for tests).
    [[nodiscard]] std::vector<ui::Toggle*> chordButtons() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    using juce::Component::keyPressed;

private:
    class Chip;

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void show(juce::Component& parent, juce::Rectangle<int> anchor);
    void rebuild();
    void styleChanged(bool kindChanged);
    void reposition();
    [[nodiscard]] juce::Rectangle<int> inner() const;
    /// As wide as its content: the widest row of the key's chords in any kind, the titles, the
    /// three choices under their labels.
    [[nodiscard]] int preferredWidth() const;
    [[nodiscard]] int preferredHeight() const;
    /// The chips' rows: each group's title row and its chips' row(s), wrapped to the width.
    int chipRows(int width, std::vector<int>* rowsPerGroup = nullptr) const;
    [[nodiscard]] juce::String nameOf(const music::Chord& chord) const;
    [[nodiscard]] juce::String title() const;

    PianoRoll& roll_;
    std::optional<PianoRoll::ChordPlace> place_;
    model::ChordStyle style_; // its Kind, Voicing and Inversion
    std::vector<Group> groups_;
    std::vector<std::unique_ptr<Chip>> chips_;
    ui::Segments kind_, voicing_, inversion_;
    juce::Component::SafePointer<juce::Component> parent_;
    juce::Rectangle<int> anchor_;
    ui::OutsideClick outside_;
};
} // namespace fd::app
