#pragma once

#include "engine/Session.h"
#include "ui/ChannelInspector.h"
#include "ui/ActionRegistry.h"
#include "ui/DeviceChain.h"
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

/// Top bar (ui-principles §2): the product's mark and name, transport, tempo and position, CPU,
/// panel toggles.
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
    /// The assistants button (shown when the app has an MCP server), and whether an assistant
    /// made a request in the last few seconds (its light).
    [[nodiscard]] juce::Rectangle<int> assistantsButtonBounds() const { return assistants_.getBounds(); }
    std::function<bool()> assistantsActive;

private:
    ui::IconButton& add(ui::IconButton& b, std::string id);

    engine::Session& session_;
    const ActionRegistry& actions_;
    ui::IconButton play_{ui::Icon::Play}, stop_{ui::Icon::Stop};
    ui::IconButton loop_{ui::Icon::Loop}, metronome_{ui::Icon::Metronome}, keyboard_{ui::Icon::Keyboard};
    ui::NumberField tempoField_, numeratorField_, denominatorField_;
    ui::IconButton browser_{ui::Icon::PanelLeft}, detail_{ui::Icon::PanelBottom}, inspector_{ui::Icon::PanelRight};
    ui::IconButton settings_{ui::Icon::Settings};
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
    // Laid out by resized(): the painted parts between the controls.
    juce::Rectangle<int> brandArea_, bpmArea_, slashArea_, readoutArea_, midiLight_, assistantLight_;
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

/// Right panel: properties of the selection (filled in as editing lands).
class InspectorPanel final : public juce::Component
{
public:
    explicit InspectorPanel(engine::Session& session);
    void resized() override;
    /// What is selected: a channel (a track, a return track or the master) and the clips.
    void setSelection(std::optional<model::TrackInfo> track, std::optional<model::ClipInfo> clip = std::nullopt,
                      int clipCount = 0, model::TempoMap tempo = {});
    void paint(juce::Graphics& g) override;

private:
    /// The property rows painted at the top: the clip's, then the channel's.
    [[nodiscard]] std::vector<ui::Property> clipProperties() const;
    [[nodiscard]] std::vector<ui::Property> channelProperties() const;

    engine::Session& session_;
    ui::PanelHeader header_;
    ChannelInspector channel_{session_};
    std::optional<model::TrackInfo> track_;
    std::optional<model::ClipInfo> clip_;
    int clipCount_ = 0;
    model::TempoMap tempo_;
};

/// Bottom panel: the selected track's device chain (C6) or the clip view, which shows the
/// selected MIDI clip in the piano roll (M2 C2); Tab switches.
class DetailPanel final : public juce::Component
{
public:
    DetailPanel(engine::Session& session, const ActionRegistry& actions);

    enum class Mode { Devices, Clip };
    void setMode(Mode m);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    void setSelection(std::optional<model::TrackInfo> track);
    /// The MIDI clip the clip view edits (nullopt: none).
    void setClip(std::optional<model::ClipInfo> clip, const model::TempoMap& tempo);
    /// True while the clip view is showing a clip in the piano roll.
    [[nodiscard]] bool showsPianoRoll() const noexcept { return mode_ == Mode::Clip && roll_.clip().has_value(); }
    [[nodiscard]] DeviceChain& chain() noexcept { return chain_; }
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
    std::optional<model::TrackInfo> track_;
    DeviceChain chain_;
    PianoRoll roll_;
};
} // namespace fd::app
