#pragma once

#include "engine/Session.h"

#include <fd_ui/fd_ui.h>

#include <optional>

namespace fd::app
{
// The controls of one mixer channel (M2 step C3, docs/design/mixer.md), bound to the Session.
// The track headers, the inspector and the mixer view use the same ones, so they behave the same
// everywhere: shared fd_ui controls, edits through Session commands (a drag is one undo step and,
// while recording, a touch), values from the snapshot. Assigned ones show the indicator, follow
// their lane while it plays, and offer its menu (C4, docs/design/automation.md §6).

/// What a mix control needs to know about its automation.
class LaneFollower
{
public:
    LaneFollower(engine::Session& session, engine::Session::ParamTarget target);
    /// Reads its assignment from the snapshot: whether it plays, and the indicator's state.
    [[nodiscard]] ui::AutomationState update();
    /// While its lane plays: the parameter's value now (the control's travel).
    [[nodiscard]] std::optional<float> played() const;
    /// The control's right-click menu: its automation, and `reset`.
    void showMenu(juce::Component& control, std::function<void()> reset) const;
    [[nodiscard]] const engine::Session::ParamTarget& target() const noexcept { return target_; }

private:
    engine::Session& session_;
    engine::Session::ParamTarget target_;
    bool plays_ = false;
};

/// Volume as a bar (headers, inspector) or a fader (the mixer view), on the volume taper (0 dB at
/// 80 %). Any channel.
class VolumeBar final : public ui::Knob
{
public:
    VolumeBar(engine::Session& session, std::uint64_t channel, Style style = Style::Bar);
    /// Shows the snapshot's value (not while being dragged).
    void update(const model::TrackInfo& channel);

    /// Each frame: follows the lane's played value while it plays.
    void follow();

private:

    engine::Session& session_;
    std::uint64_t channel_;
    LaneFollower lane_;
    std::optional<engine::Session::ParamGesture> gesture_;
    juce::VBlankAttachment vblank_{this, [this] { follow(); }};
};

/// Pan as a bipolar bar: a balance, "C", "25L", "50R". Any channel.
class PanBar final : public ui::Knob
{
public:
    PanBar(engine::Session& session, std::uint64_t channel);
    void update(const model::TrackInfo& channel);

    /// Each frame: follows the lane's played value while it plays.
    void follow();

private:

    engine::Session& session_;
    std::uint64_t channel_;
    LaneFollower lane_;
    std::optional<engine::Session::ParamGesture> gesture_;
    juce::VBlankAttachment vblank_{this, [this] { follow(); }};
};

/// A track's send to one return track, as a bar on the send taper (0 dB at the top).
class SendBar final : public ui::Knob
{
public:
    SendBar(engine::Session& session, std::uint64_t track, std::uint64_t returnTrack);
    /// `showName`: the return's name inside the bar (off where it's narrow; the tooltip names it).
    void update(const model::SendInfo& send, const juce::String& returnName, bool showName = true);
    [[nodiscard]] std::uint64_t returnTrack() const noexcept { return return_; }

    /// Each frame: follows the lane's played value while it plays.
    void follow();

private:

    engine::Session& session_;
    std::uint64_t track_, return_;
    LaneFollower lane_;
    std::optional<engine::Session::ParamGesture> gesture_;
    juce::VBlankAttachment vblank_{this, [this] { follow(); }};
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

/// The volume as a number you drag or type ("-6.0", "-inf"): the mixer view's value row.
class VolumeField final : public ui::NumberField
{
public:
    VolumeField(engine::Session& session, std::uint64_t channel);
    void update(const model::TrackInfo& channel);
    /// Without the unit ("-6.0"), for narrow strips.
    void setCompact(bool compact);

    /// Each frame: follows the lane's played value while it plays.
    void follow();

private:

    engine::Session& session_;
    std::uint64_t channel_;
    LaneFollower lane_;
    std::optional<engine::Session::ParamGesture> gesture_;
    juce::VBlankAttachment vblank_{this, [this] { follow(); }};
};

/// A channel's stereo meter, through a level reader of its own. It also keeps the loudest peak
/// since it was last reset (the mixer view's peak readout).
class ChannelMeter final : public ui::Meter
{
public:
    ChannelMeter(engine::Session& session, std::uint64_t channel);
    /// The loudest peak since resetHeld(), linear (0 for silence).
    [[nodiscard]] float heldPeak() const noexcept { return held_; }
    void resetHeld() noexcept { held_ = 0.0f; }

private:
    std::unique_ptr<engine::Session::LevelReader> reader_;
    std::array<float, 2> latest_{};
    float held_ = 0.0f;
};

/// "-6.0 dB", "-inf dB".
[[nodiscard]] juce::String levelText(float db);
/// "C", "25L", "50R".
[[nodiscard]] juce::String panText(float pan);
} // namespace fd::app
