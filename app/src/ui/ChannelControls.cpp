#include "ui/ChannelControls.h"

#include "model/Taper.h"

#include <cmath>

namespace fd::app
{
juce::String levelText(float db)
{
    return db <= model::minusInfinityDb ? juce::String("-inf dB") : juce::String(db, 1) + " dB";
}

juce::String panText(float pan)
{
    const int amount = juce::roundToInt(std::abs(pan) * 50.0f);
    return amount == 0 ? juce::String("C") : juce::String(amount) + (pan < 0.0f ? "L" : "R");
}

namespace
{
/// A level typed by hand: "-6", "-6 dB", "-inf".
std::optional<float> parseLevel(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t.contains("inf"))
        return model::minusInfinityDb;
    if (!t.containsAnyOf("0123456789"))
        return std::nullopt;
    return t.getFloatValue();
}
} // namespace

// ---- VolumeBar ----------------------------------------------------------------------------

VolumeBar::VolumeBar(engine::Session& session, std::uint64_t channel, Style style) : session_(session), channel_(channel)
{
    setStyle(style);
    setName("Vol");
    setTooltip("Volume (double-click: 0 dB)");
    setDefaultValue(static_cast<float>(model::volumePosition(0.0)));
    textFromValue = [](float v) { return levelText(static_cast<float>(model::volumeDbAt(v))); };
    valueFromText = [](const juce::String& text) -> std::optional<float> {
        if (const auto db = parseLevel(text))
            return static_cast<float>(model::volumePosition(*db));
        return std::nullopt;
    };
    onGestureStart = [this] { gesture_.emplace(session_, "Volume"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) { session_.setVolume(channel_, static_cast<float>(model::volumeDbAt(v))); };
}

void VolumeBar::update(const model::TrackInfo& channel)
{
    if (!gesture_)
        setValue(static_cast<float>(model::volumePosition(channel.mix.volumeDb)), juce::dontSendNotification);
}

// ---- PanBar -------------------------------------------------------------------------------

PanBar::PanBar(engine::Session& session, std::uint64_t channel) : session_(session), channel_(channel)
{
    setStyle(Style::Bar);
    setName("Pan");
    setTooltip("Pan, a balance (double-click: centre)");
    setArcOrigin(0.5f);
    setDefaultValue(0.5f);
    textFromValue = [](float v) { return panText(v * 2.0f - 1.0f); };
    valueFromText = [](const juce::String& text) -> std::optional<float> {
        const auto t = text.trim().toUpperCase();
        if (t == "C")
            return 0.5f;
        if (t.endsWithChar('L') || t.endsWithChar('R'))
        {
            const float amount = juce::jlimit(0.0f, 50.0f, t.dropLastCharacters(1).getFloatValue()) / 50.0f;
            return 0.5f + 0.5f * (t.endsWithChar('L') ? -amount : amount);
        }
        return std::nullopt;
    };
    onGestureStart = [this] { gesture_.emplace(session_, "Pan"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) { session_.setPan(channel_, v * 2.0f - 1.0f); };
}

void PanBar::update(const model::TrackInfo& channel)
{
    if (!gesture_)
        setValue(channel.mix.pan * 0.5f + 0.5f, juce::dontSendNotification);
}

// ---- SendBar ------------------------------------------------------------------------------

SendBar::SendBar(engine::Session& session, std::uint64_t track, std::uint64_t returnTrack)
    : session_(session), track_(track), return_(returnTrack)
{
    setStyle(Style::Bar);
    setDefaultValue(0.0f);
    textFromValue = [](float v) { return levelText(static_cast<float>(model::sendDbAt(v))); };
    valueFromText = [](const juce::String& text) -> std::optional<float> {
        if (const auto db = parseLevel(text))
            return static_cast<float>(model::sendPosition(*db));
        return std::nullopt;
    };
    onGestureStart = [this] { gesture_.emplace(session_, "Send"); }; // one undo step per drag
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) {
        session_.setSend(track_, return_, static_cast<float>(model::sendDbAt(v)), std::nullopt);
    };
}

void SendBar::update(const model::SendInfo& send, const juce::String& returnName, bool showName)
{
    const auto name = showName ? returnName : juce::String();
    if (getName() != name)
        setName(name);
    setTooltip("Send to " + returnName + " (double-click: off)");
    if (!gesture_)
        setValue(static_cast<float>(model::sendPosition(send.levelDb)), juce::dontSendNotification);
}

// ---- ChannelToggle ------------------------------------------------------------------------

ChannelToggle::ChannelToggle(engine::Session& session, Kind kind, std::uint64_t channel, std::uint64_t returnTrack)
    : ui::Toggle(kind == Kind::Mute ? "M" : kind == Kind::Solo ? "S" : "Pre"), session_(session), kind_(kind),
      channel_(channel), return_(returnTrack)
{
    setClickingTogglesState(false); // the snapshot says what it is
    setWantsKeyboardFocus(false);
    setTooltip(kind == Kind::Mute   ? "Mute (Alt+M)"
               : kind == Kind::Solo ? "Solo (Alt+S); Ctrl+click adds to the soloed tracks"
                                    : "Send before the volume (pre-fader)");
}

void ChannelToggle::update(bool on)
{
    setToggleState(on, juce::dontSendNotification);
}

void ChannelToggle::clicked()
{
    const bool on = !getToggleState();
    switch (kind_)
    {
    case Kind::Mute:
        session_.setMute(channel_, on);
        break;
    case Kind::Solo:
        session_.setSolo(channel_, on, !juce::ModifierKeys::currentModifiers.isCommandDown());
        break;
    case Kind::PreFader:
        session_.setSend(channel_, return_, std::nullopt, on);
        break;
    }
    setToggleState(on, juce::dontSendNotification); // at once; the snapshot confirms
}

// ---- VolumeField --------------------------------------------------------------------------

VolumeField::VolumeField(engine::Session& session, std::uint64_t channel) : session_(session), channel_(channel)
{
    setRange(model::minusInfinityDb, 6.0, 0.1);
    setDragSensitivity(0.2);
    setTooltip("Volume: drag or type (\"-6\", \"-inf\")");
    textFromValue = [](double v) { return levelText(static_cast<float>(v)); };
    valueFromText = [](const juce::String& text) -> std::optional<double> {
        if (const auto db = parseLevel(text))
            return *db;
        return std::nullopt;
    };
    onGestureStart = [this] { gesture_.emplace(session_, "Volume"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](double v) { session_.setVolume(channel_, static_cast<float>(v)); };
}

void VolumeField::setCompact(bool compact)
{
    textFromValue = compact ? std::function<juce::String(double)>([](double v) {
        return v <= model::minusInfinityDb ? juce::String("-inf") : juce::String(v, 1);
    })
                            : std::function<juce::String(double)>([](double v) { return levelText(static_cast<float>(v)); });
    repaint();
}

void VolumeField::update(const model::TrackInfo& channel)
{
    if (!gesture_)
        setValue(channel.mix.volumeDb, juce::dontSendNotification);
}

// ---- ChannelMeter -------------------------------------------------------------------------

ChannelMeter::ChannelMeter(engine::Session& session, std::uint64_t channel)
    : ui::Meter(2), reader_(session.levelReader(channel))
{
    setLevelSource([this](int c) {
        if (reader_ == nullptr)
            return 0.0f;
        if (c == 0)
        {
            latest_ = reader_->takePeaks(); // both channels at once
            held_ = std::max({held_, latest_[0], latest_[1]});
        }
        return latest_[static_cast<size_t>(std::clamp(c, 0, 1))];
    });
}
} // namespace fd::app
