#pragma once

namespace fd::ui
{
/// What MIDI effects' panels share (docs/design/midi-effects.md §12; devices/pitch/SPEC.md, "UI
/// layout"): the note display, the step display, the HARMONY section, and the header's Listen
/// (the sidechain picker, captioned for notes). One look in every MIDI effect.
class MidiEffectPanel : public DevicePanel
{
public:
    explicit MidiEffectPanel(DeviceUiContext& context);

protected:
    /// NOTES [columns]: the note display over the whole section.
    NoteDisplay& addNotesSection(int columns);
    /// STEPS [columns]: the lane chooser along its first row (`lanes`, one cell each), the step
    /// display under it.
    StepDisplay& addStepsSection(int columns, const juce::StringArray& lanes, StepFeed* steps);
    /// HARMONY [3] from the module's harmony parameters (devices/midi-shared/dsp/Harmony.h, from
    /// `first`): Follow, Key, Root, Scale, Own chord, Split, each dimmed while it doesn't apply.
    /// Collapsed by default: the song is what it follows, and the display says so.
    PanelSection& addHarmonySection(std::size_t first);
    /// Listen (the header's picker) shows while it matters: Follow on Track, or while `when` says
    /// (Melody's Answer). Otherwise it takes no room, and the presets have it.
    void listenAlsoWhen(std::function<bool()> when) { listenAlso_ = std::move(when); }
    /// A button that acts (Learn, Vary), not a setting: a toggle's look that doesn't stay on.
    Toggle& addButton(PanelSection& section, const juce::String& text, const juce::String& tooltip,
                      int column, int row, int columns);

private:
    /// Every frame on screen (the panel's refresh covers hosts without a frame clock).
    void tick();

    std::vector<ParamControl*> displays_;
    std::function<bool()> listenAlso_;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};
} // namespace fd::ui
