#include "ui/ChannelInspector.h"

namespace fd::app
{
namespace
{
constexpr int kMeterWidth = 12; // a stereo meter beside the volume and pan rows
} // namespace

// ---- ChannelMix ---------------------------------------------------------------------------

ChannelMix::ChannelMix(engine::Session& session) : session_(session) {}

void ChannelMix::setChannel(const std::optional<model::TrackInfo>& channel)
{
    const auto id = channel ? std::optional(channel->id) : std::nullopt;
    if (id != channel_)
    {
        channel_ = id;
        volume_.reset();
        pan_.reset();
        mute_.reset();
        solo_.reset();
        meter_.reset();
        if (!channel)
            return;
        role_ = channel->role;
        volume_ = std::make_unique<VolumeBar>(session_, channel->id);
        pan_ = std::make_unique<PanBar>(session_, channel->id);
        meter_ = std::make_unique<ChannelMeter>(session_, channel->id);
        for (juce::Component* c : {static_cast<juce::Component*>(volume_.get()), static_cast<juce::Component*>(pan_.get()),
                                   static_cast<juce::Component*>(meter_.get())})
            addAndMakeVisible(c);
        if (role_ != model::TrackInfo::Role::Master)
        {
            mute_ = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::Mute, channel->id);
            solo_ = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::Solo, channel->id);
            addAndMakeVisible(*mute_);
            addAndMakeVisible(*solo_);
        }
        resized();
    }
    if (!channel)
        return;
    volume_->update(*channel);
    pan_->update(*channel);
    if (mute_ != nullptr)
        mute_->update(channel->mix.mute);
    if (solo_ != nullptr)
        solo_->update(channel->mix.solo);
}

int ChannelMix::preferredHeight() const
{
    if (!channel_)
        return 0;
    const auto& s = ui::Theme::current().shell;
    int h = 2 * s.rowHeight + s.itemGap; // volume, pan
    if (role_ != model::TrackInfo::Role::Master)
        h += s.itemGap + s.rowHeight; // mute and solo
    return h;
}

void ChannelMix::resized()
{
    if (!channel_)
        return;
    const auto& s = ui::Theme::current().shell;
    auto r = getLocalBounds();
    auto top = r.removeFromTop(2 * s.rowHeight + s.itemGap);
    meter_->setBounds(top.removeFromRight(kMeterWidth));
    top.removeFromRight(s.itemGap);
    volume_->setBounds(top.removeFromTop(s.rowHeight));
    top.removeFromTop(s.itemGap);
    pan_->setBounds(top.removeFromTop(s.rowHeight));
    if (mute_ == nullptr)
        return;
    r.removeFromTop(s.itemGap);
    auto row = r.removeFromTop(s.rowHeight);
    mute_->setBounds(row.removeFromLeft(s.rowHeight));
    row.removeFromLeft(s.itemGap);
    solo_->setBounds(row.removeFromLeft(s.rowHeight));
}

// ---- ChannelSends -------------------------------------------------------------------------

ChannelSends::ChannelSends(engine::Session& session) : session_(session)
{
    addReturn_.setClickingTogglesState(false);
    addReturn_.setTooltip("Add a return track, for shared effects such as a reverb (Ctrl+Alt+T)");
    addReturn_.onClick = [this] { session_.addReturnTrack(); };
    addChildComponent(addReturn_);
}

void ChannelSends::setChannel(const std::optional<model::TrackInfo>& track, const std::vector<model::TrackInfo>& returns)
{
    std::vector<std::uint64_t> ids;
    for (const auto& r : returns)
        ids.push_back(r.id);
    const auto id = track ? std::optional(track->id) : std::nullopt;
    if (id != track_ || ids != returnIds_)
    {
        track_ = id;
        returnIds_ = ids;
        sends_.clear();
        if (track)
            for (const auto& r : returns)
            {
                Send s;
                s.level = std::make_unique<SendBar>(session_, track->id, r.id);
                s.pre = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::PreFader, track->id, r.id);
                addAndMakeVisible(*s.level);
                addAndMakeVisible(*s.pre);
                sends_.push_back(std::move(s));
            }
        addReturn_.setVisible(track.has_value() && returns.empty());
        resized();
    }
    if (!track)
        return;
    for (size_t i = 0; i < sends_.size() && i < track->mix.sends.size(); ++i)
    {
        const auto& send = track->mix.sends[i];
        sends_[i].level->update(send, juce::String::fromUTF8(returns[i].name.c_str()));
        sends_[i].pre->update(send.preFader);
    }
}

int ChannelSends::preferredHeight() const
{
    if (!track_)
        return 0;
    const auto& s = ui::Theme::current().shell;
    return sends_.empty() ? s.rowHeight : static_cast<int>(sends_.size()) * (s.rowHeight + s.itemGap) - s.itemGap;
}

void ChannelSends::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto r = getLocalBounds();
    if (sends_.empty())
    {
        addReturn_.setBounds(r.removeFromTop(s.rowHeight));
        return;
    }
    const int preWidth = 2 * s.rowHeight;
    for (auto& send : sends_)
    {
        auto row = r.removeFromTop(s.rowHeight);
        send.pre->setBounds(row.removeFromRight(preWidth));
        row.removeFromRight(s.itemGap);
        send.level->setBounds(row);
        r.removeFromTop(s.itemGap);
    }
}
} // namespace fd::app
