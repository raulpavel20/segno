#pragma once

#include "engine/Session.h"
#include "ui/ActionRegistry.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
/// Thin resize handle between docked panels.
class Splitter final : public juce::Component
{
public:
    std::function<void(float delta)> onDrag;

    Splitter() { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }
    void mouseDown(const juce::MouseEvent& e) override { last_ = e.getScreenPosition().x; }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int x = e.getScreenPosition().x;
        if (onDrag)
            onDrag(static_cast<float>(x - last_));
        last_ = x;
    }

private:
    int last_ = 0;
};

/// Top bar (ui-principles §2): transport, tempo and position, CPU, panel toggles.
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

private:
    ui::IconButton& add(ui::IconButton& b, std::string id);

    engine::Session& session_;
    const ActionRegistry& actions_;
    ui::IconButton play_{ui::Icon::Play}, stop_{ui::Icon::Stop};
    ui::IconButton browser_{ui::Icon::PanelLeft}, detail_{ui::Icon::PanelBottom}, inspector_{ui::Icon::PanelRight};
    ui::IconButton settings_{ui::Icon::Settings};
    std::vector<std::pair<ui::IconButton*, std::string>> bound_;
    model::TempoMap tempo_;
    double position_ = -1.0;
    int cpu_ = -1;
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
    std::vector<std::pair<juce::String, std::vector<juce::String>>> sections_;
    bool rail_ = false;
};

/// Right panel: properties of the selection (filled in as editing lands).
class InspectorPanel final : public juce::Component
{
public:
    void setSelection(std::optional<model::TrackInfo> track);
    void paint(juce::Graphics& g) override;

private:
    std::optional<model::TrackInfo> track_;
};

/// Bottom panel: the selected track's device chain (C6) or the clip view; Tab switches.
class DetailPanel final : public juce::Component
{
public:
    static constexpr int headerHeight = 24;

    enum class Mode { Devices, Clip };
    void setMode(Mode m);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    void setSelection(std::optional<model::TrackInfo> track);
    void paint(juce::Graphics& g) override;

private:
    Mode mode_ = Mode::Devices;
    std::optional<model::TrackInfo> track_;
};
} // namespace fd::app
