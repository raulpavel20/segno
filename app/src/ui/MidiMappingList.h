#pragma once

#include "engine/Session.h"
#include "model/MidiMap.h"

#include <fd_ui/fd_ui.h>
#include <functional>
#include <memory>
#include <vector>

namespace fd::app
{
/// The project's MIDI mappings (docs/design/midi-learn.md §6), in the inspector while MIDI map
/// mode is on, in place of the selection's sections: under "MIDI MAPPINGS · 6", each mapping its
/// message and input, its target as automation names it (Show selects it), Min and Max in the
/// control's own units, how it moves (Absolute, Toggle, Relative…) and Remove. Each change one undo
/// step; Min and Max dragged, one a drag. The transport's are in Settings > Controllers.
class MidiMappingList final : public juce::Component
{
public:
    explicit MidiMappingList(engine::Session& session);
    ~MidiMappingList() override;

    /// Reads the mappings again where they changed (each frame while shown).
    void update();
    /// A mapping's Show: its target's track, and its module (0: the mixer's or a button).
    std::function<void(std::uint64_t channel, std::uint64_t module)> onShow;

    /// The rows' parts (tests and the layout check).
    struct Row
    {
        model::MidiMapping mapping;
        juce::Rectangle<int> source, target, minCaption, maxCaption, modeCaption;
        std::unique_ptr<ui::IconButton> show, remove;
        std::unique_ptr<ui::NumberField> min, max;
        std::unique_ptr<ui::ChoiceField> mode;
    };
    [[nodiscard]] const std::vector<std::unique_ptr<Row>>& rows() const noexcept { return rows_; }
    [[nodiscard]] int preferredHeight(int width) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuild();
    [[nodiscard]] static int rowHeight();
    /// Where its section goes, `width` wide.
    [[nodiscard]] static juce::Rectangle<int> area(int width);

    engine::Session& session_;
    std::vector<model::MidiMapping> shown_;
    std::vector<std::unique_ptr<Row>> rows_;
    ui::SectionStack stack_;
    std::unique_ptr<engine::Session::EditGroup> gesture_; // a Min or Max dragged: one undo step
};
} // namespace fd::app
