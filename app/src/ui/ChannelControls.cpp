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
using Value = std::function<float(const model::TrackInfo&)>;
using Apply = std::function<void(std::uint64_t channel, float value)>;

/// A gesture on one of several channels chosen together (groups.md §8): where each stood, and a
/// touch on each of the others' `key`. None for a channel alone.
std::optional<GangMove> startGang(engine::Session& session, std::uint64_t channel, const std::string& key,
                                  const std::string& name, const Value& value)
{
    const auto others = session.gangedWith(channel);
    const auto snapshot = session.snapshot();
    const auto* own = snapshot->channel(channel);
    if (others.empty() || own == nullptr)
        return std::nullopt;
    GangMove gang{.own = value(*own)};
    for (const auto id : others)
        if (const auto* t = snapshot->channel(id))
        {
            gang.others.emplace_back(id, value(*t));
            gang.touches.push_back(std::make_unique<engine::Session::ParamGesture>(
                session, engine::Session::ParamTarget{id, key}, name));
        }
    return gang;
}

/// Sets `channel` to `now` and moves the others chosen with it by as much (the Session clamps),
/// in the gesture's step, or in one of its own when there's no gesture (a typed value, a reset).
void moveGang(engine::Session& session, std::uint64_t channel, const std::optional<GangMove>& during,
              float now, const std::string& key, const std::string& name, const Value& value,
              const Apply& apply)
{
    if (during)
    {
        apply(channel, now);
        for (const auto& [id, from] : during->others)
            apply(id, from + (now - during->own));
        return;
    }
    if (session.gangedWith(channel).empty())
    {
        apply(channel, now);
        return;
    }
    const engine::Session::EditGroup step(session, name);
    auto once = startGang(session, channel, key, name, value);
    once->touches.clear(); // no gesture: nothing to touch
    moveGang(session, channel, once, now, key, name, value, apply);
}

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
    // With others chosen together, they move by as many dB (groups.md §8).
    static const Value db = [](const model::TrackInfo& t) { return t.mix.volumeDb; };
    const auto apply = [this](std::uint64_t c, float v) { session_.setVolume(c, v); };
    onGestureStart = [this]
    {
        gesture_.emplace(session_, lane_.target(), "Volume");
        gang_ = startGang(session_, channel_, "volume", "Volume", db);
    };
    onGestureEnd = [this]
    {
        gang_.reset();
        gesture_.reset();
    };
    onValueChange = [this, apply](float v)
    {
        moveGang(session_, channel_, gang_, static_cast<float>(model::volumeDbAt(v)), "volume", "Volume", db,
                 apply);
    };
    onPopupMenu = [this, apply]
    {
        lane_.showMenu(*this, [this, apply]
                       { moveGang(session_, channel_, gang_, 0.0f, "volume", "Volume", db, apply); });
    };
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
    // With others chosen together, they move by as much (groups.md §8).
    static const Value pan = [](const model::TrackInfo& t) { return t.mix.pan; };
    const auto apply = [this](std::uint64_t c, float v) { session_.setPan(c, v); };
    onGestureStart = [this]
    {
        gesture_.emplace(session_, lane_.target(), "Pan");
        gang_ = startGang(session_, channel_, "pan", "Pan", pan);
    };
    onGestureEnd = [this]
    {
        gang_.reset();
        gesture_.reset();
    };
    onValueChange = [this, apply](float v)
    { moveGang(session_, channel_, gang_, v * 2.0f - 1.0f, "pan", "Pan", pan, apply); };
    onPopupMenu = [this, apply]
    {
        lane_.showMenu(*this, [this, apply]
                       { moveGang(session_, channel_, gang_, 0.0f, "pan", "Pan", pan, apply); });
    };
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
    // With others chosen together, their sends to the same return move by as many dB (groups.md §8).
    const auto key = "send/" + std::to_string(returnTrack);
    const Value level = [returnTrack](const model::TrackInfo& t)
    {
        for (const auto& s : t.mix.sends)
            if (s.returnId == returnTrack)
                return s.levelDb;
        return model::minusInfinityDb;
    };
    const auto apply = [this](std::uint64_t c, float v) { session_.setSend(c, return_, v, std::nullopt); };
    onGestureStart = [this, key, level] // one undo step per drag
    {
        gesture_.emplace(session_, lane_.target(), "Send");
        gang_ = startGang(session_, track_, key, "Send", level);
    };
    onGestureEnd = [this]
    {
        gang_.reset();
        gesture_.reset();
    };
    onValueChange = [this, key, level, apply](float v)
    { moveGang(session_, track_, gang_, static_cast<float>(model::sendDbAt(v)), key, "Send", level, apply); };
    onPopupMenu = [this, key, level, apply]
    {
        lane_.showMenu(
            *this, [this, key, level, apply]
            { moveGang(session_, track_, gang_, model::minusInfinityDb, key, "Send", level, apply); });
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
    // With others chosen together, they take the same state, in one step (groups.md §8).
    const auto others = session_.gangedWith(channel_);
    std::optional<engine::Session::EditGroup> step;
    if (!others.empty())
        step.emplace(session_, kind_ == Kind::Mute ? "Mute" : kind_ == Kind::Arm ? "Arm" : "Send");
    const bool alone = !juce::ModifierKeys::currentModifiers.isCommandDown();
    switch (kind_)
    {
    case Kind::Mute:
        session_.setMute(channel_, on);
        for (const auto id : others)
            session_.setMute(id, on);
        break;
    case Kind::Solo:
        session_.setSolo(channel_, on, alone);
        for (const auto id : others)
            session_.setSolo(id, on, false);
        break;
    case Kind::PreFader:
        session_.setSend(channel_, return_, std::nullopt, on);
        for (const auto id : others)
            session_.setSend(id, return_, std::nullopt, on);
        break;
    case Kind::Arm: // a click arms this one alone; Ctrl+click one more (recording.md §4)
        session_.setArmed(channel_, on, alone);
        for (const auto id : others)
            session_.setArmed(id, on, false);
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
