#pragma once

#include "engine/Session.h"
#include "model/Fades.h"
#include "model/Selection.h"
#include "ui/ChannelInspector.h"
#include "ui/ActionRegistry.h"
#include "ui/DeviceChain.h"
#include "ui/LaneAssignments.h"
#include "ui/TrackInputs.h"
#include "ui/AudioClipView.h"
#include "ui/PianoRoll.h"

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
    /// Puts the keyboard in the lane's name field (Rename).
    void focusLaneName();
    /// Puts the keyboard in the take's name field (Rename).
    void focusTakeName();
    /// The shell's actions, for the TAKE section's commands (they act on the selected take).
    void setActions(const ActionRegistry& actions);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    /// The selected section's fields (arranger.md §10), made once.
    void wireSection();
    /// A section's property rows (none for MIX and SENDS, which are components).
    [[nodiscard]] std::vector<ui::Property> properties(model::InspectorSection section) const;
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
    std::vector<model::InspectorSection> sections_;
    std::optional<model::TrackInfo> channel_;
    std::optional<model::ClipInfo> clip_;
    int clipCount_ = 0;
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
    /// Its loop (ADR 0048, clip-loops.md §7): on or off, and while on its start in the content and
    /// its length, dragged in sixteenths or typed ("2.1.1", "1 bar 2 beats", for audio "1.5 s").
    ui::Toggle clipLoop_{"On"};
    ui::NumberField loopStart_, loopLength_;
    std::optional<engine::Session::EditGroup> loopGesture_;
    /// Names typed and colours chosen (tracks-and-clips.md §4, §8): a track's or return's, a
    /// clip's (with its parts), a lane's colour. Each one undo step.
    juce::TextEditor channelName_, clipName_;
    ui::ColourSwatch channelColour_, clipColour_, laneColour_;
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
    TrackInputSection input_{session_};
    model::TempoMap tempo_;
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
    [[nodiscard]] ui::ViewChoice& drawChoice() noexcept { return draw_; }
    [[nodiscard]] ui::ViewChoice& foldChoice() noexcept { return fold_; }
    [[nodiscard]] ui::ViewOption& keyOption() noexcept { return keySnap_; }
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
    /// Modules, Clip where it's available, the editor's while one is open.
    void updateTabs();

    ui::PanelHeader header_;
    std::vector<Mode> tabModes_; // what each tab shows
    const ActionRegistry& actions_;
    /// Notes and Quantise, in the clip view's header while it shows notes (menu-bar.md §15,
    /// piano-roll.md §4).
    ui::MenuButton notes_{"Notes"};
    ui::Toggle quantise_{"Quantise"};
    /// The piano roll's options (piano-roll.md §4): Draw, Key (with a song key), Fold, Others.
    ui::ViewChoice draw_, fold_;
    ui::ViewOption keySnap_{"Key"}, others_{"Others"};
    /// The piano roll's Snap and grid step (ADR 0047), on the right while it shows a clip.
    ui::ViewOption snap_{"Snap"};
    ui::ViewChoice grid_;
    Mode mode_ = Mode::Devices;
    bool clipViewAvailable_ = true;
    std::optional<model::TrackInfo> track_;
    DeviceChain chain_;
    PianoRoll roll_;
    AudioClipView audio_;
    std::unique_ptr<ui::ModuleEditor> editor_;
    std::uint64_t editorModule_ = 0;
};
} // namespace fd::app
