#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
// The controls of one mixer channel (M2 step C3, docs/design/mixer.md), bound to the Session.
// The track headers, the inspector and the mixer view use the same ones, so they behave the same
// everywhere: shared fd_ui controls, edits through Session commands (a drag is one undo step),
// values from the snapshot.

/// Volume as a bar, on the volume taper (0 dB at 80 %). Any channel.
class VolumeBar final : public ui::Knob
{
public:
    VolumeBar(engine::Session& session, std::uint64_t channel);
    /// Shows the snapshot's value (not while being dragged).
    void update(const model::TrackInfo& channel);

private:
    engine::Session& session_;
    std::uint64_t channel_;
    std::optional<engine::Session::EditGroup> gesture_;
};

/// Pan as a bipolar bar: a balance, "C", "25L", "50R". Any channel.
class PanBar final : public ui::Knob
{
public:
    PanBar(engine::Session& session, std::uint64_t channel);
    void update(const model::TrackInfo& channel);

private:
    engine::Session& session_;
    std::uint64_t channel_;
    std::optional<engine::Session::EditGroup> gesture_;
};

/// A track's send to one return track, as a bar on the send taper (0 dB at the top).
class SendBar final : public ui::Knob
{
public:
    SendBar(engine::Session& session, std::uint64_t track, std::uint64_t returnTrack);
    void update(const model::SendInfo& send, const juce::String& returnName);
    [[nodiscard]] std::uint64_t returnTrack() const noexcept { return return_; }

private:
    engine::Session& session_;
    std::uint64_t track_, return_;
    std::optional<engine::Session::EditGroup> gesture_;
};

/// Mute ("M"), solo ("S", exclusive; Ctrl+click adds) or a send's pre-fader switch ("Pre").
class ChannelToggle final : public ui::Toggle
{
public:
    enum class Kind { Mute, Solo, PreFader };
    ChannelToggle(engine::Session& session, Kind kind, std::uint64_t channel, std::uint64_t returnTrack = 0);
    void update(bool on);

private:
    void clicked() override;

    engine::Session& session_;
    Kind kind_;
    std::uint64_t channel_, return_;
};

/// A channel's stereo meter, through a level reader of its own.
class ChannelMeter final : public ui::Meter
{
public:
    ChannelMeter(engine::Session& session, std::uint64_t channel);

private:
    std::unique_ptr<engine::Session::LevelReader> reader_;
    std::array<float, 2> latest_{};
};

/// "-6.0 dB", "-inf dB".
[[nodiscard]] juce::String levelText(float db);
/// "C", "25L", "50R".
[[nodiscard]] juce::String panText(float pan);
} // namespace fd::app
