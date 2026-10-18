#pragma once

#include "engine/Session.h"
#include "model/Fades.h"
#include "model/ModuleFacts.h"
#include "model/Selection.h"
#include "ui/ActionRegistry.h"
#include "ui/AudioClipView.h"
#include "ui/ChannelInspector.h"
#include "ui/DeviceChain.h"
#include "ui/LaneAssignments.h"
#include "ui/MidiMappingList.h"
#include "ui/PianoRoll.h"
#include "ui/ToolsPane.h"
#include "ui/TrackInputs.h"

#include <fd_ui/fd_ui.h>
#include <optional>

namespace fd::app
{
/// Thin resize handle between docked panels: side by side, or (vertical) one above the other.
class Splitter final : public juce::Component
{
public:
    /// Mouse movement along the drag direction (px, right or down positive).
    std::function<void(float delta)> onDrag;

    explicit Splitter(bool vertical = false) : vertical_(vertical)
    {
        setMouseCursor(vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    }
    void mouseDown(const juce::MouseEvent& e) override { last_ = along(e); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int now = along(e);
        if (onDrag)
            onDrag(static_cast<float>(now - last_));
        last_ = now;
    }

private:
    [[nodiscard]] int along(const juce::MouseEvent& e) const
    {
        return vertical_ ? e.getScreenPosition().y : e.getScreenPosition().x;
    }
    bool vertical_;
    int last_ = 0;
};

/// Top bar (ui-principles §2): the product's mark and name (macOS; elsewhere in the title bar),
/// the song, Undo and Redo, transport and position, CPU, views, the command palette and app.
class TransportBar final : public juce::Component
{
public:
    static constexpr int height = 40;

    TransportBar(engine::Session& session, const ActionRegistry& actions);

    /// Once per frame: position, play state and toggles.
    void frame(double positionSeconds, const model::TempoMap& tempo);

    void paint(juce::Graphics& g) override;
    void resized() override;

    ui::IconButton& settingsButton() noexcept { return settings_; }
    /// The brand at the left end: on macOS, where there's no title bar of ours (ADR 0040).
    void setBrandShown(bool shown);
    /// The count-in's lengths, and MIDI loop recording's choices: the Record button's menu, and
    /// the menu bar's Transport (one builder for both).
    [[nodiscard]] juce::PopupMenu countInMenu();
    [[nodiscard]] juce::PopupMenu midiLoopMenu();
    /// The assistants button (shown when the app has an MCP server), and whether an assistant
    /// made a request in the last few seconds (its light).
    [[nodiscard]] juce::Rectangle<int> assistantsButtonBounds() const { return assistants_.getBounds(); }
    std::function<bool()> assistantsActive;
    /// The capture button's tooltip: where a capture goes now (checked every frame).
    std::function<juce::String()> captureTooltip;
    /// The Record button's "Recording settings…": Settings, at Recording.
    std::function<void()> onRecordingSettings;
    /// The song's key (key-and-scale.md §6): the field after the time signature, where the Key
    /// popover opens (onKeyOpen) and its right-click menu (onKeyMenu).
    [[nodiscard]] ui::ChoiceField& keyField() noexcept { return keyField_; }
    std::function<void()> onKeyOpen;
    std::function<void()> onKeyMenu;

private:
    ui::IconButton& add(ui::IconButton& b, std::string id);

    engine::Session& session_;
    const ActionRegistry& actions_;
    ui::IconButton play_{ui::Icon::Play}, stop_{ui::Icon::Stop}, record_{ui::Icon::Record};
    /// The Record button's menu (recording.md §9): count-in, MIDI loop recording, settings.
    void showRecordMenu();
    /// Capture MIDI (recording.md §8): dim until something was played to capture.
    ui::IconButton capture_{ui::Icon::Capture};
    ui::IconButton loop_{ui::Icon::Loop}, follow_{ui::Icon::Follow}, metronome_{ui::Icon::Metronome},
        countIn_{ui::Icon::CountIn}, keyboard_{ui::Icon::Keyboard};
    // Automation arm (C4): touch recording, in the record colour (lanes turn themselves back on).
    ui::IconButton automationArm_{ui::Icon::AutomationArm};
    ui::NumberField tempoField_, numeratorField_, denominatorField_;
    ui::ChoiceField keyField_;
    /// The key's name, its short name ("A min"), and whether the bar shows the short one.
    juce::String keyName_, keyShort_;
    std::optional<music::Key> keyShown_;
    bool keyNarrow_ = false;
    void showKey();
    ui::IconButton browser_{ui::Icon::PanelLeft}, detail_{ui::Icon::PanelBottom}, inspector_{ui::Icon::PanelRight};
    ui::IconButton settings_{ui::Icon::Settings};
    // Undo and Redo after the song; the command palette first in the app group (ADR 0040).
    ui::IconButton undo_{ui::Icon::Undo}, redo_{ui::Icon::Redo}, search_{ui::Icon::Search, "palette"};
    bool brandShown_ = true;
    // The centre view: the arrangement or the mixer (C3c), a two-part switch.
    ui::Toggle arrangeView_{"Arrange"}, mixView_{"Mix"};
    ui::IconButton assistants_{ui::Icon::Assistant, "assistants"};
    std::vector<std::pair<ui::IconButton*, std::string>> bound_;
    model::TempoMap tempo_;
    bool tempoShown_ = false; // the fields show tempo_
    double position_ = -1.0;
    int cpu_ = -1;
    /// "Reduced latency" by the CPU load while it affects a track (recording.md §22.1).
    ui::HeaderBadge reducedLatency_{nullptr, nullptr, {}};
    bool reducing_ = false;
    double lastMidi_ = -1.0; // last time a MIDI note was held (activity light)
    bool midiLit_ = false;
    bool assistantLit_ = false;
    [[nodiscard]] juce::Rectangle<int> midiLightArea() const { return midiLight_; }
    // Laid out by resized() from model::layoutTopBar: the painted parts between the controls. The
    // brand is the mark alone when the name doesn't fit; the position sits in an inset well.
    juce::Rectangle<int> brandArea_, bpmArea_, slashArea_, barsArea_, timeArea_, wellArea_, cpuArea_, midiLight_,
        assistantLight_;
    bool markOnly_ = false;
};

/// Right panel, in the arrangement: the properties of the selection's subject in titled sections
/// (docs/design/selection.md §5): CLIP or CLIPS, then TRACK, RETURN or MASTER, then MIX and SENDS.
class InspectorPanel final : public juce::Component
{
public:
    explicit InspectorPanel(engine::Session& session);
    /// Shows a subject (model::panelContext decides the sections) with the project's tempo and
    /// return tracks.
    void setContext(const model::PanelContext& context, const model::TempoMap& tempo,
                    const std::vector<model::TrackInfo>& returns);
    /// Shows nothing and drops its meters (before the project is replaced).
    void clear();
    [[nodiscard]] const std::vector<model::InspectorSection>& sections() const noexcept { return sections_; }
    [[nodiscard]] LaneAssignments& assignments() noexcept { return assignments_; }
    /// What its sections hold (the controls in them), scrolled under its header (tests).
    [[nodiscard]] juce::Component& content() noexcept { return body_; }
    /// A section's property rows (none for MIX and SENDS, which are components); tests read them.
    [[nodiscard]] std::vector<ui::Property> properties(model::InspectorSection section) const;
    /// Puts the keyboard in the lane's name field (Rename).
    void focusLaneName();
    /// Puts the keyboard in the take's name field (Rename).
    void focusTakeName();
    /// The shell's actions, for the TAKE section's commands (they act on the selected take).
    void setActions(const ActionRegistry& actions);
    /// TIME's fields moved the time selection (time-selection.md §7): its new stretch.
    std::function<void(model::TimeSpan)> onTimeEdited;
    /// TIME's fields (tests).
    [[nodiscard]] ui::NumberField& timeField(int which) noexcept
    {
        return which == 0 ? timeStart_ : which == 1 ? timeEnd_ : timeLength_;
    }
    /// The clip view's piano roll (piano-roll.md §9.6): a NOTES section after CLIP while it shows
    /// the selected clip with notes selected, edited through it.
    void setPianoRoll(PianoRoll& roll);
    /// Its notes or their selection changed.
    void notesChanged();
    /// The NOTES section's fields (tests).
    [[nodiscard]] ui::NumberField& noteField(model::NoteField field) noexcept;
    [[nodiscard]] ui::Toggle& noteMute() noexcept { return noteMute_; }

    /// The module selected in the chain while the chain was used last (selection.md §8.2): MODULE
    /// above the channel's sections, its type, the delay it adds, what moves it and what
    /// automates it; for a modulator, ROUTES under it. None takes them away.
    struct ModuleShown
    {
        model::ModuleFacts facts;
        juce::String type;
        double latencyMs = 0.0;
    };
    void setModule(std::optional<ModuleShown> module);
    /// A modulator's ROUTES: its rows as its route list has them, and what they do.
    void setRoutes(std::vector<RouteRows::Route> routes, DeviceChain::RouteCallbacks callbacks);
    /// A "Moved by" line clicked: that modulator, to select.
    std::function<void(std::uint64_t modulator)> onSelectModule;
    /// TRACK's Group row clicked: the group, to select (groups.md §10).
    std::function<void(std::uint64_t group)> onSelectChannel;
    [[nodiscard]] ui::Toggle& groupLink() noexcept { return groupLink_; }
    [[nodiscard]] const std::optional<ModuleShown>& module() const noexcept { return module_; }
    /// MODULE's controls and ROUTES' rows (tests).
    [[nodiscard]] const juce::OwnedArray<ui::Toggle>& movers() const noexcept { return movers_; }
    [[nodiscard]] const juce::OwnedArray<ui::Toggle>& laneShows() const noexcept { return laneShows_; }
    [[nodiscard]] RouteRows* routes() noexcept { return routes_.get(); }
    /// MIDI map mode (midi-learn.md §6): the project's mappings in place of the sections, read
    /// again each frame (updateMidiMappings) as they land.
    void setMidiMappingsShown(bool shown);
    [[nodiscard]] bool midiMappingsShown() const noexcept { return midiShown_; }
    void updateMidiMappings();
    [[nodiscard]] MidiMappingList& midiMappings() noexcept { return midiList_; }
    /// CLIP's Loop (tests).
    [[nodiscard]] ui::Toggle& clipLoop() noexcept { return clipLoop_; }
    /// The AUDIO section's fields (tests).
    [[nodiscard]] ui::NumberField& audioGain() noexcept { return audioGain_; }
    [[nodiscard]] ui::Toggle& audioReverse() noexcept { return audioReverse_; }
    [[nodiscard]] ui::Toggle& audioPolarity() noexcept { return audioPolarity_; }
    [[nodiscard]] juce::ComboBox& audioChannels() noexcept { return audioChannels_; }
    [[nodiscard]] ui::NumberField& audioTranspose() noexcept { return audioTranspose_; }
    [[nodiscard]] ui::NumberField& audioFine() noexcept { return audioFine_; }
    [[nodiscard]] ui::Toggle& audioWarp() noexcept { return audioWarp_; }
    [[nodiscard]] juce::ComboBox& audioMode() noexcept { return audioMode_; }
    [[nodiscard]] ui::NumberField& audioTempo() noexcept { return audioTempo_; }
    [[nodiscard]] juce::ComboBox& audioTails() noexcept { return audioTails_; }
    [[nodiscard]] ui::Toggle& audioFormants() noexcept { return audioFormants_; }
    [[nodiscard]] ui::NumberField& audioSensitivity() noexcept { return audioSensitivity_; }
    [[nodiscard]] juce::ComboBox& audioKey() noexcept { return audioKey_; }
    [[nodiscard]] ui::Toggle& audioFollow() noexcept { return audioFollow_; }
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    /// The context's sections, MODULE and ROUTES before them while a module is shown.
    void rebuildSections();
    /// "None", "0.1 ms", "107 ms".
    [[nodiscard]] juce::String delayText() const;
    /// The selected section's fields (arranger.md §10), made once.
    void wireSection();
    /// The NOTES section's fields, made once.
    void wireNotes();
    /// Adds or removes the NOTES section and shows the notes' values.
    void updateNotes();
    [[nodiscard]] bool showsNotes() const;
    /// "Notes · 4 selected · G7".
    [[nodiscard]] juce::String notesTitle() const;
    /// Lays the sections out `width` wide from the top of the content; returns their height.
    int layoutSections(int width);
    /// Scrolls the sections so `c` (one of their controls) is in view.
    void reveal(const juce::Component& c);
    /// The sections' titles, lines and property rows, on the content.
    void paintSections(juce::Graphics& g);
    /// The row with `key` in a section's properties (-1: none), where its control goes.
    [[nodiscard]] int rowOf(model::InspectorSection section, std::string_view key) const;

    engine::Session& session_;
    ui::PanelHeader header_;
    MidiMappingList midiList_{session_};
    juce::Viewport midiViewport_;
    bool midiShown_ = false;
    /// The sections, under the header, scrolled when they're taller than the panel (the detail
    /// panel below can leave it little room).
    struct Body final : juce::Component
    {
        explicit Body(InspectorPanel& p) : panel(p) {}
        void paint(juce::Graphics& g) override { panel.paintSections(g); }
        InspectorPanel& panel;
    };
    Body body_{*this};
    juce::Viewport viewport_;
    ChannelMix mix_{session_};
    ChannelSends sends_{session_};
    ui::SectionStack stack_;
    std::vector<model::InspectorSection> sections_, contextSections_;
    std::optional<ModuleShown> module_;
    juce::OwnedArray<ui::Toggle> movers_, laneShows_;
    std::unique_ptr<RouteRows> routes_;
    std::optional<model::TrackInfo> channel_;
    std::optional<model::ClipInfo> clip_;
    int clipCount_ = 0;
    int trackCount_ = 0;                    // several tracks selected (groups.md §8)
    std::optional<model::TrackInfo> group_; // the channel's group: TRACK's Group row (§10)
    int groupTracks_ = 0;                   // a group's tracks, at any depth
    int takeCount_ = 0;
    std::optional<model::AutomationLaneInfo> lane_;
    std::optional<model::AutomationPoint> point_;
    int pointCount_ = 0;
    std::optional<size_t> pointIndex_;
    /// The selected point's value, typed or dragged (automation.md §9): one undo step each.
    ui::NumberField pointValue_;
    std::optional<engine::Session::EditGroup> pointGesture_;
    /// The lane's name, typed (one undo step); empty: named after its assignments.
    juce::TextEditor laneName_;
    LaneAssignments assignments_{session_};
    /// The selected take (recording.md §10): its name typed, and "Use this take".
    std::optional<model::TakeInfo> take_;
    juce::TextEditor takeName_;
    ui::Toggle useTake_{"Use this take"};
    // Its commands, where the take is (menu-bar.md §15): to a new track, keep only it.
    ui::Toggle copyTake_{"Copy"}, moveTake_{"Move"}, keepTake_{"Keep only this take"};
    const ActionRegistry* actions_ = nullptr;
    /// The selected clip's mute (bounce.md §6), as on a track's mute button.
    ui::Toggle clipMute_{"M"};
    /// The selected notes (piano-roll.md §9.6): pitch, start, length and velocity dragged or typed
    /// (several: a range, moved together by a drag, all set by typing), and their mute. One undo
    /// step per drag or value typed.
    PianoRoll* roll_ = nullptr;
    ui::NumberField notePitch_, noteStart_, noteLength_, noteVelocity_, noteSpread_, noteRelease_,
        noteChance_;
    ui::Toggle noteMute_{"M"};
    std::optional<engine::Session::EditGroup> notesGesture_;
    std::optional<model::NoteEdit> notesBase_;       // the notes a drag started from
    double notesFrom_ = 0.0;                         // and the field's value then
    std::map<model::NoteField, double> notesSpread_; // each field's range over the notes
    /// Its loop (ADR 0048, clip-loops.md §7): on or off, and while on its start in the content and
    /// its length, dragged in sixteenths or typed ("2.1.1", "1 bar 2 beats", for audio "1.5 s").
    ui::Toggle clipLoop_{"On"};
    ui::NumberField loopStart_, loopLength_;
    std::optional<engine::Session::EditGroup> loopGesture_;
    /// Names typed and colours chosen (tracks-and-clips.md §4, §8): a track's or return's, a
    /// clip's (with its parts), a lane's colour. Each one undo step.
    juce::TextEditor channelName_, clipName_;
    ui::ColourSwatch channelColour_, clipColour_, laneColour_;
    /// TRACK's Group row: the group's name, a click selecting it (groups.md §10).
    ui::Toggle groupLink_;
    /// The selected clip and every part of it (a comp).
    [[nodiscard]] std::vector<std::uint64_t> clipParts() const;
    /// An audio clip's fade lengths, dragged or typed in ms (fades.md §3), and beside each its
    /// curve (−100 … 100 %, 0 straight; menu-bar.md §15): one undo step each.
    ui::NumberField fadeIn_, fadeOut_, fadeInCurve_, fadeOutCurve_;
    /// The crossfades at the clip's start and end (menu-bar.md §15): the clips either side of the
    /// join, and their shape (None removes it) and position against the join, as choices.
    struct Join
    {
        model::FadeClip out, in;
    };
    std::optional<Join> crossIn_, crossOut_;
    juce::ComboBox crossInShape_, crossInPlace_, crossOutShape_, crossOutPlace_;
    void showCrossfade(juce::ComboBox& shape, juce::ComboBox& place, const std::optional<Join>& join);
    void setCrossfadeShape(const std::optional<Join>& join, int id);
    void setCrossfadePlace(const std::optional<Join>& join, int id);
    std::optional<engine::Session::EditGroup> fadeGesture_;
    /// An audio clip's sound (audio-editor.md §12): its gain dragged or typed, Reverse, Polarity,
    /// and for a stereo file how its channels play. One undo step each.
    ui::NumberField audioGain_;
    ui::Toggle audioReverse_{"On"}, audioPolarity_{"Invert"};
    juce::ComboBox audioChannels_;
    /// Its warping and transposition (§7, §8.1, §12): Transpose with Fine beside it; Warp with the
    /// mode beside it; while it warps its tempo with ×2 and ÷2, and the mode's setting (Beats'
    /// tails, Tones' formants); the transients' sensitivity.
    ui::NumberField audioTranspose_, audioFine_, audioTempo_, audioSensitivity_;
    ui::Toggle audioWarp_{"On"}, audioFormants_{"Keep"};
    ui::Toggle audioDouble_{juce::String::fromUTF8("\xC3\x97"
                                                   "2")},
        audioHalve_{juce::String::fromUTF8("\xC3\xB7"
                                           "2")};
    juce::ComboBox audioMode_, audioTails_;
    /// Its key, found or set, then Follow key under it (§8.2, §8.3). The list is made again when
    /// what it offers changes (`keyItems_`).
    juce::ComboBox audioKey_;
    ui::Toggle audioFollow_{"On"};
    juce::String keyItems_;
    std::optional<music::Key> otherKey_; // a set key of another scale, listed as its own item
    void refreshKey();
    std::optional<engine::Session::EditGroup> audioGesture_;
    TrackInputSection input_{session_};
    model::TempoMap tempo_;
    /// A time selection (time-selection.md §7): Start, End and Length dragged or typed move it,
    /// never the music; Rows says what it's on.
    std::optional<model::TimeSpan> time_;
    int timeTracks_ = 0, timeLanes_ = 0;
    ui::NumberField timeStart_, timeEnd_, timeLength_;
    void wireTime();
    /// "2 tracks, 1 lane".
    [[nodiscard]] juce::String timeRows() const;
    /// The selected section (arranger.md §10): its name typed, its colour, and its marks (Start,
    /// Length) dragged or typed; the music stays where it is. Each one undo step.
    std::optional<model::SectionInfo> section_;
    int sectionCount_ = 0;
    juce::TextEditor sectionName_;
    ui::ColourSwatch sectionColour_;
    ui::NumberField sectionStart_, sectionLength_;
    std::optional<engine::Session::EditGroup> sectionGesture_;
};

/// Bottom panel: the selected track's device chain (C6) or the clip view, which shows the
/// selected MIDI clip in the piano roll (M2 C2); Tab switches. In the mixer view it shows only
/// the chain, under a plain "Modules" title (docs/design/selection.md §4).
class DetailPanel final : public juce::Component
{
public:
    DetailPanel(engine::Session& session, const ActionRegistry& actions);

    /// Modules, Clip, and a module's editor while one is open (ADR 0053).
    enum class Mode
    {
        Devices,
        Clip,
        Editor
    };
    /// Clip is ignored while the clip view isn't available, Editor while no editor is open.
    void setMode(Mode m);
    /// The arrangement: Modules and Clip tabs. The mixer view: modules only, no tabs.
    void setClipViewAvailable(bool available);
    [[nodiscard]] bool clipViewAvailable() const noexcept { return clipViewAvailable_; }
    /// A tab was clicked. The shell owns the mode, so it decides.
    std::function<void(Mode)> onModeChosen;
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    void setSelection(std::optional<model::TrackInfo> track);
    /// The clip the clip view shows (nullopt: none): a MIDI clip in the piano roll, an audio clip
    /// in the audio clip view (clip-loops.md §6).
    void setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo);
    /// True while the clip view is showing a clip in the piano roll.
    [[nodiscard]] bool showsPianoRoll() const noexcept { return mode_ == Mode::Clip && roll_.clip().has_value(); }
    /// True while it's showing an audio clip.
    [[nodiscard]] bool showsAudioClip() const noexcept { return mode_ == Mode::Clip && audio_.clip().has_value(); }
    [[nodiscard]] DeviceChain& chain() noexcept { return chain_; }
    [[nodiscard]] const DeviceChain& chain() const noexcept { return chain_; }
    /// A module's editor (ADR 0053): a view of its own, below the module, its tab named for what
    /// it edits. Replaces one open before; its Done, or the module going, closes it.
    void showEditor(std::uint64_t module, std::unique_ptr<ui::ModuleEditor> editor);
    void closeEditor();
    [[nodiscard]] ui::ModuleEditor* editor() const noexcept { return editor_.get(); }
    [[nodiscard]] std::uint64_t editorModule() const noexcept { return editorModule_; }
    /// An editor opened or closed: the shell sizes the panel for it.
    std::function<void()> onEditorChanged;
    /// Asks the shell for `mode` (a tab, an editor asking to be seen).
    void chooseMode(Mode mode);
    /// Its header: the tabs, the editor's × (for tests).
    [[nodiscard]] ui::PanelHeader& header() noexcept { return header_; }
    /// Shows the module view with `id` selected.
    void selectModule(std::uint64_t id);
    [[nodiscard]] PianoRoll& pianoRoll() noexcept { return roll_; }
    [[nodiscard]] AudioClipView& audioClipView() noexcept { return audio_; }
    [[nodiscard]] const PianoRoll& pianoRoll() const noexcept { return roll_; }
    /// The Notes menu (piano-roll.md §4, §13): the menu bar's, built by the same function, so the
    /// header's Notes button and the menu bar never differ.
    std::function<juce::PopupMenu()> notesMenu;
    /// The header's clip view controls (for tests and the layout lint).
    [[nodiscard]] ui::MenuButton& notesButton() noexcept { return notes_; }
    /// The Tools pane (piano-roll.md §25), and the header's Transform and Generate that open it.
    [[nodiscard]] ToolsPane& toolsPane() noexcept { return tools_; }
    [[nodiscard]] ui::Toggle& transformButton() noexcept { return transform_; }
    [[nodiscard]] ui::Toggle& generateButton() noexcept { return generate_; }
    /// Opens the Tools pane on `tab`, or closes it (none); view state, kept with the project.
    void showTools(std::optional<model::ToolKind> tab);
    [[nodiscard]] bool toolsShown(model::ToolKind tab) const;
    /// Opens the pane at a tool ("strum"; "makeChords"; for an audio clip, "normalise") and
    /// chooses it.
    void showTool(std::string_view id);
    /// Over an audio clip: the pane open or closed (audio-editor.md §11); view state, kept with the
    /// project.
    void showAudioTools(bool open);
    /// The Audio menu (audio-editor.md §13): the menu bar's, built by the same function, opened
    /// from the bar's Audio button and the waveform's right-click.
    std::function<juce::PopupMenu()> audioMenu;
    /// The audio editor's bar (audio-editor.md §4, §20), for tests and the layout lint.
    [[nodiscard]] ui::MenuButton& audioButton() noexcept { return audioButton_; }
    [[nodiscard]] ui::Toggle& normaliseButton() noexcept { return normalise_; }
    [[nodiscard]] ui::BarField& gainField() noexcept { return gain_; }
    [[nodiscard]] ui::Toggle& reverseButton() noexcept { return reverse_; }
    // Its warping (§4, §7, §8.1): Quantise, the Tempo tool, Transpose, Warp, and while the clip
    // warps its mode, its tempo, ×2 and ÷2.
    [[nodiscard]] ui::Toggle& audioQuantiseButton() noexcept { return quantiseAudio_; }
    [[nodiscard]] ui::Toggle& tempoButton() noexcept { return tempoTool_; }
    [[nodiscard]] ui::Toggle& sliceButton() noexcept { return slice_; }
    [[nodiscard]] ui::BarField& transposeField() noexcept { return transpose_; }
    [[nodiscard]] ui::Toggle& warpButton() noexcept { return warp_; }
    [[nodiscard]] ui::MenuButton& warpModeButton() noexcept { return warpMode_; }
    [[nodiscard]] ui::BarField& tempoField() noexcept { return warpTempo_; }
    [[nodiscard]] ui::Toggle& doubleButton() noexcept { return tempoDouble_; }
    [[nodiscard]] ui::Toggle& halveButton() noexcept { return tempoHalf_; }
    /// A range command's button (Split, Crop, Silence, Fade, Loop): shown while a range is selected.
    [[nodiscard]] ui::Toggle& rangeButton(engine::Session::RangeAction action) noexcept;
    [[nodiscard]] ui::ViewChoice& drawChoice() noexcept { return draw_; }
    [[nodiscard]] ui::Segments& drawKind() noexcept { return drawKind_; }
    [[nodiscard]] ui::ViewChoice& drawVoicing() noexcept { return drawVoicing_; }
    [[nodiscard]] ui::ViewOption& inKeyOption() noexcept { return inKey_; }
    [[nodiscard]] ui::ViewOption& othersOption() noexcept { return others_; }
    /// Once per display frame.
    void frame(double playheadSeconds);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void updateVisibility();
    void updateHeader();
    /// The clip view's options in the header, as the piano roll has them.
    void updateOptions();
    /// In key, or Pads on a Drum Kit's clip (drum-kit.md §23.3): its text, whether it shows, its state.
    void updateRowsOption();
    /// Modules, Clip where it's available, the editor's while one is open.
    void updateTabs();
    /// The audio editor's bar as its clip and range are now.
    void updateAudioBar();

    ui::PanelHeader header_;
    std::vector<Mode> tabModes_; // what each tab shows
    engine::Session& session_;
    const ActionRegistry& actions_;
    /// Notes and Quantise, in the clip view's header while it shows notes (menu-bar.md §15,
    /// piano-roll.md §4).
    ui::MenuButton notes_{"Notes"};
    ui::Toggle quantise_{"Quantise"};
    /// Transform and Generate (piano-roll.md §25): each opens the Tools pane on its tab.
    ui::Toggle transform_{"Transform"}, generate_{"Generate"};
    /// The piano roll's options (piano-roll.md §4): Draw, In key (with a song key: §21), Others;
    /// while drawing chords, the kind side by side and the voicing (§20.2).
    ui::ViewChoice draw_, drawVoicing_;
    ui::Segments drawKind_;
    ui::ViewOption inKey_{"In key"}, others_{"Others"}; // In key reads Pads on a Drum Kit's clip
    /// The piano roll's Snap and grid step (ADR 0047), on the right while it shows a clip.
    ui::ViewOption snap_{"Snap"};
    ui::ViewChoice grid_;
    /// The audio editor's bar (audio-editor.md §4, §20): a range's commands while one is selected;
    /// the Audio menu; its tools; the clip's gain and Reverse.
    ui::Toggle split_{"Split"}, crop_{"Crop"}, silence_{"Silence"}, fadeRange_{"Fade"}, loopRange_{"Loop"};
    ui::MenuButton audioButton_{"Audio"};
    ui::Toggle quantiseAudio_{"Quantise"}, tempoTool_{"Tempo"}, slice_{"Slice"};
    ui::Toggle normalise_{"Normalise"};
    ui::BarField gain_{"Gain"}, transpose_{"Transpose"};
    ui::Toggle reverse_{"Reverse"};
    ui::Toggle warp_{"Warp"};
    ui::MenuButton warpMode_{"Tones"};
    ui::BarField warpTempo_{""};
    ui::Toggle tempoDouble_{juce::String::fromUTF8("\xC3\x97"
                                                   "2")},
        tempoHalf_{juce::String::fromUTF8("\xC3\xB7"
                                          "2")};
    // A drag of a field: one undo step.
    std::optional<engine::Session::EditGroup> gainGesture_, transposeGesture_, tempoGesture_;
    Mode mode_ = Mode::Devices;
    bool clipViewAvailable_ = true;
    std::optional<model::TrackInfo> track_;
    DeviceChain chain_;
    PianoRoll roll_;
    ToolsPane tools_; // docked at the clip view's right edge, or floating over it when narrow
    AudioClipView audio_;
    std::unique_ptr<ui::ModuleEditor> editor_;
    std::uint64_t editorModule_ = 0;
};
} // namespace fd::app
