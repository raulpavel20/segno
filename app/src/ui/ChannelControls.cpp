#include "ui/ChannelControls.h"

#include "model/Taper.h"
#include "ui/AutomationMenu.h"
#include "ui/MidiMapView.h"

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

// ---- LaneFollower -------------------------------------------------------------------------

LaneFollower::LaneFollower(engine::Session& session, engine::Session::ParamTarget target)
    : session_(session), target_(std::move(target))
{
}

ui::AutomationState LaneFollower::update()
{
    const auto snapshot = session_.snapshot();
    const auto [lane, assignment] = snapshot->assignmentOf(target_.channel, target_.key);
    plays_ = lane != nullptr && !lane->points.empty() && assignment->on;
    if (lane == nullptr || lane->points.empty())
        return ui::AutomationState::None;
    return assignment->on ? ui::AutomationState::On : ui::AutomationState::Off;
}

std::optional<float> LaneFollower::played() const
{
    return plays_ ? session_.paramValue(target_) : std::nullopt;
}

void LaneFollower::showMenu(juce::Component& control, std::function<void()> reset) const
{
    juce::Component::SafePointer<juce::Component> alive(&control);
    juce::PopupMenu menu;
    addAutomationItems(session_, target_, menu);
    menu.addSeparator();
    menu.addItem("Reset to default", [alive, run = std::move(reset)] {
        if (alive != nullptr)
            run();
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&control));
}

// ---- VolumeBar ----------------------------------------------------------------------------

VolumeBar::VolumeBar(engine::Session& session, std::uint64_t channel, Style style)
    : session_(session), channel_(channel), lane_(session, {channel, "volume"})
{
    setStyle(style);
    setName("Vol");
    setTooltip("Volume (double-click: 0 dB)");
    setMidiTarget(*this, {channel, "volume"}); // MIDI learn (midi-learn.md §7)
    setDefaultValue(static_cast<float>(model::volumePosition(0.0)));
    textFromValue = [](float v) { return levelText(static_cast<float>(model::volumeDbAt(v))); };
    valueFromText = [](const juce::String& text) -> std::optional<float> {
        if (const auto db = parseLevel(text))
            return static_cast<float>(model::volumePosition(*db));
        return std::nullopt;
    };
    onGestureStart = [this] { gesture_.emplace(session_, lane_.target(), "Volume"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) { session_.setVolume(channel_, static_cast<float>(model::volumeDbAt(v))); };
    onPopupMenu = [this] { lane_.showMenu(*this, [this] { session_.setVolume(channel_, 0.0f); }); };
}

void VolumeBar::update(const model::TrackInfo& channel)
{
    setAutomationState(lane_.update());
    if (!gesture_)
        setValue(static_cast<float>(model::volumePosition(channel.mix.volumeDb)), juce::dontSendNotification);
}

void VolumeBar::follow()
{
    // The volume lane's value is the fader's own position (the taper, ADR 0028).
    if (const auto v = lane_.played(); v && !gesture_ && std::abs(*v - getValue()) > 1e-5f)
        setValue(*v, juce::dontSendNotification);
}

// ---- PanBar -------------------------------------------------------------------------------

PanBar::PanBar(engine::Session& session, std::uint64_t channel)
    : session_(session), channel_(channel), lane_(session, {channel, "pan"})
{
    setStyle(Style::Bar);
    setName("Pan");
    setTooltip("Pan, a balance (double-click: centre)");
    setMidiTarget(*this, {channel, "pan"});
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
    onGestureStart = [this] { gesture_.emplace(session_, lane_.target(), "Pan"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) { session_.setPan(channel_, v * 2.0f - 1.0f); };
    onPopupMenu = [this] { lane_.showMenu(*this, [this] { session_.setPan(channel_, 0.0f); }); };
}

void PanBar::update(const model::TrackInfo& channel)
{
    setAutomationState(lane_.update());
    if (!gesture_)
        setValue(channel.mix.pan * 0.5f + 0.5f, juce::dontSendNotification);
}

void PanBar::follow()
{
    if (const auto v = lane_.played(); v && !gesture_ && std::abs(*v - getValue()) > 1e-5f)
        setValue(*v, juce::dontSendNotification);
}

// ---- SendBar ------------------------------------------------------------------------------

SendBar::SendBar(engine::Session& session, std::uint64_t track, std::uint64_t returnTrack)
    : session_(session), track_(track), return_(returnTrack), lane_(session, {track, "send/" + std::to_string(returnTrack)})
{
    setStyle(Style::Bar);
    setMidiTarget(*this, {track, "send/" + std::to_string(returnTrack)});
    setDefaultValue(0.0f);
    textFromValue = [](float v) { return levelText(static_cast<float>(model::sendDbAt(v))); };
    valueFromText = [](const juce::String& text) -> std::optional<float> {
        if (const auto db = parseLevel(text))
            return static_cast<float>(model::sendPosition(*db));
        return std::nullopt;
    };
    onGestureStart = [this] { gesture_.emplace(session_, lane_.target(), "Send"); }; // one undo step per drag
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](float v) {
        session_.setSend(track_, return_, static_cast<float>(model::sendDbAt(v)), std::nullopt);
    };
    onPopupMenu = [this] {
        lane_.showMenu(*this, [this] { session_.setSend(track_, return_, model::minusInfinityDb, std::nullopt); });
    };
}

void SendBar::follow()
{
    // A send lane's value is the send's own position (0 dB at the top).
    if (const auto v = lane_.played(); v && !gesture_ && std::abs(*v - getValue()) > 1e-5f)
        setValue(*v, juce::dontSendNotification);
}

void SendBar::update(const model::SendInfo& send, const juce::String& returnName, bool showName)
{
    const auto name = showName ? returnName : juce::String();
    if (getName() != name)
        setName(name);
    setTooltip("Send to " + returnName + " (double-click: off)");
    setAutomationState(lane_.update());
    if (!gesture_)
        setValue(static_cast<float>(model::sendPosition(send.levelDb)), juce::dontSendNotification);
}

// ---- ChannelToggle ------------------------------------------------------------------------

ChannelToggle::ChannelToggle(engine::Session& session, Kind kind, std::uint64_t channel, std::uint64_t returnTrack)
    : ui::Toggle(kind == Kind::Mute ? "M" : kind == Kind::Solo ? "S" : kind == Kind::Arm ? "" : "Pre"), session_(session),
      kind_(kind), channel_(channel), return_(returnTrack)
{
    setClickingTogglesState(false); // the snapshot says what it is
    setWantsKeyboardFocus(false);
    setArmStyle(kind == Kind::Arm);
    if (kind != Kind::PreFader) // mute, solo and arm are what controllers' buttons are for (§7)
    {
        const model::MidiTarget target{channel, kind == Kind::Mute   ? "mute"
                                                : kind == Kind::Solo ? "solo"
                                                                     : "arm"};
        setMidiTarget(*this, target);
        onPopupMenu = [this, target]
        {
            juce::PopupMenu menu;
            addMidiItems(session_, target, menu);
            if (menu.getNumItems() > 0)
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
        };
    }
    setTooltip(kind == Kind::Mute   ? "Mute (Alt+M)"
               : kind == Kind::Solo ? "Solo (Alt+S); Ctrl+click adds to the soloed tracks"
               : kind == Kind::Arm  ? "Arm for recording (Alt+R); Ctrl+click arms one more"
                                    : "Send before the volume (pre-fader)");
}

void ChannelToggle::updateArm(const model::InputInfo& input)
{
    setToggleState(input.armed, juce::dontSendNotification);
    setPartlyOn(input.softArmed);
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
    case Kind::Arm: // a click arms this one alone; Ctrl+click one more (recording.md §4)
        session_.setArmed(channel_, on, !juce::ModifierKeys::currentModifiers.isCommandDown());
        break;
    }
    setToggleState(on, juce::dontSendNotification); // at once; the snapshot confirms
}

// ---- VolumeField --------------------------------------------------------------------------

VolumeField::VolumeField(engine::Session& session, std::uint64_t channel)
    : session_(session), channel_(channel), lane_(session, {channel, "volume"})
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
    onGestureStart = [this] { gesture_.emplace(session_, lane_.target(), "Volume"); };
    onGestureEnd = [this] { gesture_.reset(); };
    onValueChange = [this](double v) { session_.setVolume(channel_, static_cast<float>(v)); };
}

void VolumeField::follow()
{
    if (const auto v = lane_.played(); v && !gesture_)
        if (const auto db = session_.paramPlain(lane_.target(), *v); db && std::abs(*db - getValue()) > 0.01)
            setValue(std::max<double>(*db, model::minusInfinityDb), juce::dontSendNotification);
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
    (void)lane_.update();
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
