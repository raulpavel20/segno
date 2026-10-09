#pragma once

#include "ui/ChannelControls.h"

namespace fd::app
{
// The selected channel in the inspector (C3, docs/design/mixer.md §7), as two of its sections
// (docs/design/selection.md §5): MIX and SENDS. Each is rebuilt only when what it shows changes
// (so its meter keeps running); values follow the snapshot.

/// The MIX section: volume and pan with the meter beside them, then mute and solo (none on the
/// master).
class ChannelMix final : public juce::Component
{
public:
    explicit ChannelMix(engine::Session& session);

    /// The channel to show (nullopt: none, which also drops its meter).
    void setChannel(const std::optional<model::TrackInfo>& channel);
    /// The height its contents need.
    [[nodiscard]] int preferredHeight() const;

    void resized() override;

private:
    engine::Session& session_;
    std::optional<std::uint64_t> channel_;
    model::TrackInfo::Role role_ = model::TrackInfo::Role::Track;
    std::unique_ptr<VolumeBar> volume_;
    std::unique_ptr<PanBar> pan_;
    std::unique_ptr<ChannelToggle> mute_, solo_;
    std::unique_ptr<ChannelMeter> meter_;
};

/// The SENDS section of a track: one bar and Pre switch per return track, or a button to add
/// the first one.
class ChannelSends final : public juce::Component
{
public:
    explicit ChannelSends(engine::Session& session);

    /// The track to show (nullopt: none) and the project's return tracks.
    void setChannel(const std::optional<model::TrackInfo>& track, const std::vector<model::TrackInfo>& returns);
    [[nodiscard]] int preferredHeight() const;

    void resized() override;

private:
    struct Send
    {
        std::unique_ptr<SendBar> level;
        std::unique_ptr<ChannelToggle> pre;
    };

    engine::Session& session_;
    std::optional<std::uint64_t> track_;
    std::vector<std::uint64_t> returnIds_;
    std::vector<Send> sends_;
    ui::Toggle addReturn_{"Add return track"};
};
} // namespace fd::app
