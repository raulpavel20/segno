#pragma once

#include "ui/PianoRoll.h"

#include <fd_ui/fd_ui.h>

namespace fd::app
{
/// Quantise… (docs/design/piano-roll.md §9.5): how far notes move to the grid, its swing, and
/// whether their starts, ends or both move. The notes change live as it's set; Apply (Return)
/// keeps the result as one undo step, Escape puts everything back. Its settings are kept with the
/// project for Quantise (Ctrl+U) too. Sized to its content, under the clip view's Quantise.
class QuantisePopover final : public juce::Component, private juce::KeyListener
{
public:
    explicit QuantisePopover(PianoRoll& roll);
    ~QuantisePopover() override;

    /// Shows inside `parent` under `anchor` (in `parent`'s coordinates).
    void open(juce::Component& parent, juce::Rectangle<int> anchor);
    /// Keeps what it shows (one undo step) and closes.
    void apply();
    /// Puts everything back and closes.
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    [[nodiscard]] const model::QuantiseSettings& settings() const noexcept { return settings_; }
    void setSettings(const model::QuantiseSettings& settings);
    [[nodiscard]] ui::NumberField& strength() noexcept { return strength_; }
    [[nodiscard]] ui::NumberField& swing() noexcept { return swing_; }
    [[nodiscard]] ui::Segments& moves() noexcept { return moves_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    using juce::Component::keyPressed;

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
    void preview();
    void place();
    [[nodiscard]] juce::Rectangle<int> inner() const;
    [[nodiscard]] int preferredWidth() const;
    [[nodiscard]] int preferredHeight() const;

    PianoRoll& roll_;
    model::QuantiseSettings settings_;
    ui::NumberField strength_, swing_;
    ui::Segments moves_{juce::StringArray{"Starts", "Ends", "Both"}};
    ui::Toggle apply_{"Apply"};
    juce::Component::SafePointer<juce::Component> parent_;
    juce::Rectangle<int> anchor_;
    ui::OutsideClick outside_;
};
} // namespace fd::app
