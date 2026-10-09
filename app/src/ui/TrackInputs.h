#pragma once

#include "engine/Session.h"
#include "ui/ChannelControls.h"

#include <fd_ui/fd_ui.h>

#include <memory>
#include <optional>

namespace fd::app
{
/// A track's inputs as menu items (docs/design/recording.md §3): Audio input ▸, MIDI input ▸,
/// MIDI channel ▸, Monitor ▸ and, with both inputs, Records ▸. Each choice is one undo step.
void addInputItems(engine::Session& session, const model::TrackInfo& track, juce::PopupMenu& menu);

/// The mixer strip's input row: what the track listens to, in a few words; a click opens the
/// input menu.
class InputButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InputButton(engine::Session& session);
    void update(const model::TrackInfo& track);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    engine::Session& session_;
    std::optional<model::TrackInfo> info_;
    juce::String text_;
};

/// The inspector's INPUT section of a track (recording.md §10): the arm button, then audio input,
/// MIDI input and channel, monitoring and, with both inputs, what it records.
class TrackInputSection final : public juce::Component
{
public:
    explicit TrackInputSection(engine::Session& session);
    void setTrack(const std::optional<model::TrackInfo>& track);
    [[nodiscard]] int preferredHeight() const;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Row
    {
        juce::String label;
        juce::ComboBox* box = nullptr;
    };
    [[nodiscard]] std::vector<Row> rows();
    [[nodiscard]] int rowCount() const;

    engine::Session& session_;
    std::optional<model::TrackInfo> track_;
    std::unique_ptr<ChannelToggle> arm_;
    juce::ComboBox audio_, midi_, channel_, monitor_, records_;
    std::vector<std::string> audioIds_, midiIds_;
    juce::Rectangle<int> armLabel_;
};
} // namespace fd::app
