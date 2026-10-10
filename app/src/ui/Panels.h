#pragma once

#include "engine/Session.h"
#include "model/Selection.h"
#include "ui/ChannelInspector.h"
#include "ui/ActionRegistry.h"
#include "ui/DeviceChain.h"
#include "ui/LaneAssignments.h"
#include "ui/TrackInputs.h"
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

/// Left panel: one search across devices, presets, samples, plugins and files (M3). In M1 it
/// lists the built-in devices.
class BrowserPanel final : public juce::Component
{
public:
    explicit BrowserPanel(const ActionRegistry& actions);
    void paint(juce::Graphics& g) override;
    void resized() override;
    /// Narrow rail when collapsed (ui-principles §2, compact width).
    void setRail(bool rail);

private:
    const ActionRegistry& actions_;
    juce::TextEditor search_;
    ui::IconButton expand_{ui::Icon::PanelLeft}; // on the rail
    struct Section
    {
        juce::String title;
        std::vector<juce::String> items;
    };
    std::vector<Section> sections_;
    bool rail_ = false;
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
    /// Puts the keyboard in the lane's name field (Rename).
    void focusLaneName();
    /// Puts the keyboard in the take's name field (Rename).
    void focusTakeName();
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    /// A section's property rows (none for MIX and SENDS, which are components).
    [[nodiscard]] std::vector<ui::Property> properties(model::InspectorSection section) const;

    engine::Session& session_;
    ui::PanelHeader header_;
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
    /// The selected clip's mute (bounce.md §6), as on a track's mute button.
    ui::Toggle clipMute_{"M"};
    /// An audio clip's fade lengths, dragged or typed in ms (fades.md §3): one undo step each.
    ui::NumberField fadeIn_, fadeOut_;
    std::optional<engine::Session::EditGroup> fadeGesture_;
    TrackInputSection input_{session_};
    model::TempoMap tempo_;
};

/// Bottom panel: the selected track's device chain (C6) or the clip view, which shows the
/// selected MIDI clip in the piano roll (M2 C2); Tab switches. In the mixer view it shows only
/// the chain, under a plain "Modules" title (docs/design/selection.md §4).
class DetailPanel final : public juce::Component
{
public:
    DetailPanel(engine::Session& session, const ActionRegistry& actions);

    enum class Mode { Devices, Clip };
    /// Clip is ignored while the clip view isn't available.
    void setMode(Mode m);
    /// The arrangement: Modules and Clip tabs. The mixer view: modules only, no tabs.
    void setClipViewAvailable(bool available);
    [[nodiscard]] bool clipViewAvailable() const noexcept { return clipViewAvailable_; }
    /// A tab was clicked. The shell owns the mode, so it decides.
    std::function<void(Mode)> onModeChosen;
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    void setSelection(std::optional<model::TrackInfo> track);
    /// The MIDI clip the clip view edits (nullopt: none).
    void setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo);
    /// True while the clip view is showing a clip in the piano roll.
    [[nodiscard]] bool showsPianoRoll() const noexcept { return mode_ == Mode::Clip && roll_.clip().has_value(); }
    [[nodiscard]] DeviceChain& chain() noexcept { return chain_; }
    /// Shows the module view with `id` selected.
    void selectModule(std::uint64_t id);
    [[nodiscard]] PianoRoll& pianoRoll() noexcept { return roll_; }
    /// Once per display frame.
    void frame(double playheadSeconds);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void updateVisibility();
    void updateHeader();

    ui::PanelHeader header_;
    Mode mode_ = Mode::Devices;
    bool clipViewAvailable_ = true;
    std::optional<model::TrackInfo> track_;
    DeviceChain chain_;
    PianoRoll roll_;
};
} // namespace fd::app
