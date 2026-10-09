#include "ui/ChannelInspector.h"

namespace fd::app
{
namespace
{
constexpr int kMeterWidth = 12; // a stereo meter beside the volume and pan rows
} // namespace

ChannelInspector::ChannelInspector(engine::Session& session) : session_(session)
{
    addReturn_.setClickingTogglesState(false);
    addReturn_.setTooltip("Add a return track, for shared effects such as a reverb (Ctrl+Alt+T)");
    addReturn_.onClick = [this] { session_.addReturnTrack(); };
    addChildComponent(addReturn_);
}

void ChannelInspector::setChannel(const std::optional<model::TrackInfo>& channel,
                                  const std::vector<model::TrackInfo>& returns)
{
    std::vector<std::uint64_t> ids;
    for (const auto& r : returns)
        ids.push_back(r.id);
    const auto id = channel ? std::optional(channel->id) : std::nullopt;
    if (!channel)
    {
        channel_.reset();
        volume_.reset();
        pan_.reset();
        mute_.reset();
        solo_.reset();
        meter_.reset();
        sends_.clear();
        addReturn_.setVisible(false);
        returnIds_.clear();
        return;
    }
    if (id != channel_ || ids != returnIds_)
        rebuild(*channel, returns);

    volume_->update(*channel);
    pan_->update(*channel);
    if (mute_ != nullptr)
        mute_->update(channel->mix.mute);
    if (solo_ != nullptr)
        solo_->update(channel->mix.solo);
    for (size_t i = 0; i < sends_.size() && i < channel->mix.sends.size(); ++i)
    {
        const auto& send = channel->mix.sends[i];
        sends_[i].level->update(send, juce::String::fromUTF8(returns[i].name.c_str()));
        sends_[i].pre->update(send.preFader);
    }
}

void ChannelInspector::rebuild(const model::TrackInfo& channel, const std::vector<model::TrackInfo>& returns)
{
    channel_ = channel.id;
    role_ = channel.role;
    returnIds_.clear();
    for (const auto& r : returns)
        returnIds_.push_back(r.id);

    volume_ = std::make_unique<VolumeBar>(session_, channel.id);
    pan_ = std::make_unique<PanBar>(session_, channel.id);
    meter_ = std::make_unique<ChannelMeter>(session_, channel.id);
    for (juce::Component* c : {static_cast<juce::Component*>(volume_.get()), static_cast<juce::Component*>(pan_.get()),
                               static_cast<juce::Component*>(meter_.get())})
        addAndMakeVisible(c);
    mute_.reset();
    solo_.reset();
    if (role_ != model::TrackInfo::Role::Master)
    {
        mute_ = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::Mute, channel.id);
        solo_ = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::Solo, channel.id);
        addAndMakeVisible(*mute_);
        addAndMakeVisible(*solo_);
    }
    sends_.clear();
    if (role_ == model::TrackInfo::Role::Track)
        for (const auto& r : returns)
        {
            Send s;
            s.level = std::make_unique<SendBar>(session_, channel.id, r.id);
            s.pre = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::PreFader, channel.id, r.id);
            addAndMakeVisible(*s.level);
            addAndMakeVisible(*s.pre);
            sends_.push_back(std::move(s));
        }
    addReturn_.setVisible(role_ == model::TrackInfo::Role::Track && returns.empty());
    resized();
}

int ChannelInspector::preferredHeight() const
{
    if (!channel_)
        return 0;
    const auto& s = ui::Theme::current().shell;
    int h = 2 * s.rowHeight + s.itemGap; // volume, pan
    if (role_ != model::TrackInfo::Role::Master)
        h += s.itemGap + s.rowHeight; // mute and solo
    if (role_ == model::TrackInfo::Role::Track)
    {
        h += s.groupGap + s.rowHeight; // the "Sends" label
        h += sends_.empty() ? s.rowHeight : static_cast<int>(sends_.size()) * (s.rowHeight + s.itemGap);
    }
    return h;
}

void ChannelInspector::resized()
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
    if (mute_ != nullptr)
    {
        r.removeFromTop(s.itemGap);
        auto row = r.removeFromTop(s.rowHeight);
        mute_->setBounds(row.removeFromLeft(s.rowHeight));
        row.removeFromLeft(s.itemGap);
        solo_->setBounds(row.removeFromLeft(s.rowHeight));
    }
    if (role_ != model::TrackInfo::Role::Track)
        return;
    r.removeFromTop(s.groupGap);
    sendsLabel_ = r.removeFromTop(s.rowHeight);
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

void ChannelInspector::paint(juce::Graphics& g)
{
    if (channel_ && role_ == model::TrackInfo::Role::Track && !sendsLabel_.isEmpty())
        ui::paintCapsLabel(g, "Sends", sendsLabel_.toFloat());
}
} // namespace fd::app
