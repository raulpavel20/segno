#pragma once

#include "ui/ChannelControls.h"

namespace fd::app
{
/// The selected channel in the inspector (C3, docs/design/mixer.md §7): volume and pan with the
/// meter beside them, mute and solo, and a track's sends (one bar and Pre switch per return
/// track, or a button to add the first one). Return tracks show no sends; the master shows only
/// volume, pan and its meter. Rebuilt when the channel or the set of returns changes; values
/// follow the snapshot.
class ChannelInspector final : public juce::Component
{
public:
    explicit ChannelInspector(engine::Session& session);

    /// The channel to show (nullopt: none) and the project's return tracks.
    void setChannel(const std::optional<model::TrackInfo>& channel, const std::vector<model::TrackInfo>& returns);
    /// The height its contents need at the current width.
    [[nodiscard]] int preferredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Send
    {
        std::unique_ptr<SendBar> level;
        std::unique_ptr<ChannelToggle> pre;
    };
    void rebuild(const model::TrackInfo& channel, const std::vector<model::TrackInfo>& returns);

    engine::Session& session_;
    std::optional<std::uint64_t> channel_;
    model::TrackInfo::Role role_ = model::TrackInfo::Role::Track;
    std::vector<std::uint64_t> returnIds_;
    std::unique_ptr<VolumeBar> volume_;
    std::unique_ptr<PanBar> pan_;
    std::unique_ptr<ChannelToggle> mute_, solo_;
    std::unique_ptr<ChannelMeter> meter_;
    std::vector<Send> sends_;
    ui::Toggle addReturn_{"Add return track"};
    juce::Rectangle<int> sendsLabel_;
};
} // namespace fd::app
