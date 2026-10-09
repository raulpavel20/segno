#include "ui/TrackInputs.h"

namespace fd::app
{
namespace
{
juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

bool hasInstrument(const model::TrackInfo& t)
{
    return std::any_of(t.devices.begin(), t.devices.end(), [](const auto& d) { return d.instrument; });
}

juce::String monitorName(model::Monitor m)
{
    return m == model::Monitor::In ? "In" : m == model::Monitor::Off ? "Off" : "Auto";
}
} // namespace

void addInputItems(engine::Session& session, const model::TrackInfo& track, juce::PopupMenu& menu)
{
    const auto id = track.id;
    const auto& in = track.input;
    juce::PopupMenu audio, midi, channel, monitor, records;
    audio.addItem("None", true, in.audio.empty(), [&session, id] { session.setAudioInput(id, {}); });
    for (const auto& i : session.audioInputs())
        audio.addItem(utf8(i.name), true, in.audio == i.id, [&session, id, input = i.id] { session.setAudioInput(id, input); });
    for (const auto& i : session.midiInputs())
        midi.addItem(utf8(i.name), true, in.midi == i.id, [&session, id, input = i.id] { session.setMidiInput(id, input); });
    midi.addItem("None", true, in.midi.empty(), [&session, id] { session.setMidiInput(id, {}); });
    channel.addItem("All channels", true, in.channel == 0, [&session, id] { session.setMidiChannel(id, 0); });
    for (int c = 1; c <= 16; ++c)
        channel.addItem("Channel " + juce::String(c), true, in.channel == c, [&session, id, c] { session.setMidiChannel(id, c); });
    for (const auto m : {model::Monitor::Auto, model::Monitor::In, model::Monitor::Off})
        monitor.addItem(m == model::Monitor::Auto ? "Auto: while armed" : m == model::Monitor::In ? "In: always" : "Off",
                        true, in.monitor == m, [&session, id, m] { session.setMonitor(id, m); });
    const auto kind = model::recordKind(in, hasInstrument(track));
    for (const auto k : {model::RecordKind::Midi, model::RecordKind::Audio})
        records.addItem(k == model::RecordKind::Midi ? "MIDI" : "Audio", true, kind == k, [&session, id, k] { session.setRecordKind(id, k); });
    menu.addSubMenu("Audio input", audio);
    menu.addSubMenu("MIDI input", midi);
    menu.addSubMenu("MIDI channel", channel, !in.midi.empty());
    menu.addSubMenu("Monitor", monitor);
    if (!in.audio.empty() && !in.midi.empty())
        menu.addSubMenu("Records", records);
}

// ---- InputButton --------------------------------------------------------------------------

InputButton::InputButton(engine::Session& session) : session_(session)
{
    setTooltip("What this track listens to and records: click to choose");
}

void InputButton::update(const model::TrackInfo& track)
{
    info_ = track;
    const auto text = utf8(model::inputSummary(track.input, hasInstrument(track)));
    if (text != text_)
    {
        text_ = text;
        repaint();
    }
}

void InputButton::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto r = getLocalBounds().toFloat();
    g.setColour(t.lift(t.surface2, isMouseOver(), false));
    g.fillRoundedRectangle(r, t.radiusControl);
    g.setColour(t.textSecondary);
    g.setFont(ui::font(ui::TextStyle::Caption));
    g.drawText(text_, getLocalBounds().reduced(6, 0), juce::Justification::centredLeft, true);
}

void InputButton::mouseDown(const juce::MouseEvent&)
{
    if (!info_)
        return;
    juce::PopupMenu menu;
    addInputItems(session_, *info_, menu);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

// ---- TrackInputSection --------------------------------------------------------------------

TrackInputSection::TrackInputSection(engine::Session& session) : session_(session)
{
    for (auto* box : {&audio_, &midi_, &channel_, &monitor_, &records_})
        addChildComponent(box);
    audio_.onChange = [this] {
        if (track_ && audio_.getSelectedItemIndex() >= 0)
            session_.setAudioInput(track_->id, audioIds_.at(static_cast<size_t>(audio_.getSelectedItemIndex())));
    };
    midi_.onChange = [this] {
        if (track_ && midi_.getSelectedItemIndex() >= 0)
            session_.setMidiInput(track_->id, midiIds_.at(static_cast<size_t>(midi_.getSelectedItemIndex())));
    };
    channel_.addItem("All channels", 1);
    for (int c = 1; c <= 16; ++c)
        channel_.addItem("Channel " + juce::String(c), c + 1);
    channel_.onChange = [this] {
        if (track_)
            session_.setMidiChannel(track_->id, channel_.getSelectedId() - 1);
    };
    for (const auto m : {model::Monitor::Auto, model::Monitor::In, model::Monitor::Off})
        monitor_.addItem(monitorName(m), static_cast<int>(m) + 1);
    monitor_.setTooltip("Auto: hear the input while armed. In: always. Off: never (the interface monitors)");
    monitor_.onChange = [this] {
        if (track_)
            session_.setMonitor(track_->id, static_cast<model::Monitor>(monitor_.getSelectedId() - 1));
    };
    records_.addItem("MIDI", 1);
    records_.addItem("Audio", 2);
    records_.onChange = [this] {
        if (track_)
            session_.setRecordKind(track_->id, records_.getSelectedId() == 2 ? model::RecordKind::Audio : model::RecordKind::Midi);
    };
}

void TrackInputSection::setTrack(const std::optional<model::TrackInfo>& track)
{
    const bool same = track && track_ && track->id == track_->id;
    track_ = track;
    if (!track_)
    {
        arm_.reset();
        for (auto* box : {&audio_, &midi_, &channel_, &monitor_, &records_})
            box->setVisible(false);
        return;
    }
    if (!same || arm_ == nullptr)
    {
        arm_ = std::make_unique<ChannelToggle>(session_, ChannelToggle::Kind::Arm, track_->id);
        addAndMakeVisible(*arm_);
    }
    arm_->updateArm(track_->input);
    const auto& in = track_->input;

    // The inputs there are now (devices come and go).
    audio_.clear(juce::dontSendNotification);
    audioIds_ = {""};
    audio_.addItem("None", 1);
    for (const auto& i : session_.audioInputs())
    {
        audioIds_.push_back(i.id);
        audio_.addItem(utf8(i.name), static_cast<int>(audioIds_.size()));
    }
    midi_.clear(juce::dontSendNotification);
    midiIds_.clear();
    for (const auto& i : session_.midiInputs())
    {
        midiIds_.push_back(i.id);
        midi_.addItem(utf8(i.name), static_cast<int>(midiIds_.size()));
    }
    midiIds_.push_back("");
    midi_.addItem("None", static_cast<int>(midiIds_.size()));
    auto select = [](juce::ComboBox& box, const std::vector<std::string>& ids, const std::string& id) {
        const auto it = std::find(ids.begin(), ids.end(), id);
        box.setSelectedItemIndex(it != ids.end() ? static_cast<int>(it - ids.begin()) : -1, juce::dontSendNotification);
    };
    select(audio_, audioIds_, in.audio);
    select(midi_, midiIds_, in.midi);
    channel_.setSelectedId(in.channel + 1, juce::dontSendNotification);
    monitor_.setSelectedId(static_cast<int>(in.monitor) + 1, juce::dontSendNotification);
    records_.setSelectedId(model::recordKind(in, hasInstrument(*track_)) == model::RecordKind::Audio ? 2 : 1,
                           juce::dontSendNotification);
    resized();
    repaint();
}

std::vector<TrackInputSection::Row> TrackInputSection::rows()
{
    if (!track_)
        return {};
    const auto& in = track_->input;
    std::vector<Row> out{{"Audio", &audio_}, {"MIDI", &midi_}};
    if (!in.midi.empty())
        out.push_back({"Channel", &channel_});
    out.push_back({"Monitor", &monitor_});
    if (!in.audio.empty() && !in.midi.empty())
        out.push_back({"Records", &records_});
    return out;
}

int TrackInputSection::rowCount() const
{
    if (!track_)
        return 0;
    const auto& in = track_->input;
    return 3 + (in.midi.empty() ? 0 : 1) + (!in.audio.empty() && !in.midi.empty() ? 1 : 0);
}

int TrackInputSection::preferredHeight() const
{
    if (!track_)
        return 0;
    return (rowCount() + 1) * ui::Theme::current().shell.rowHeight; // with the Record row
}

void TrackInputSection::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto r = getLocalBounds();
    for (auto* box : {&audio_, &midi_, &channel_, &monitor_, &records_})
        box->setVisible(false);
    if (!track_)
        return;
    auto top = r.removeFromTop(s.rowHeight);
    armLabel_ = top.withTrimmedLeft(s.propertyLabelWidth + s.rowHeight + s.itemGap);
    if (arm_ != nullptr)
        arm_->setBounds(top.withTrimmedLeft(s.propertyLabelWidth).removeFromLeft(s.rowHeight));
    for (const auto& row : rows())
    {
        row.box->setVisible(true);
        row.box->setBounds(r.removeFromTop(s.rowHeight).withTrimmedLeft(s.propertyLabelWidth).reduced(0, 1));
    }
}

void TrackInputSection::paint(juce::Graphics& g)
{
    if (!track_)
        return;
    const auto& in = track_->input;
    std::vector<ui::Property> labels{{"Record", {}}};
    for (const auto& row : rows())
        labels.push_back({row.label, {}});
    ui::paintProperties(g, getLocalBounds(), labels);
    const auto& t = ui::Theme::current();
    g.setColour(t.textSecondary);
    g.setFont(ui::font(ui::TextStyle::Body));
    g.drawText(in.armed ? "Armed" : in.softArmed ? "Armed by the selection" : "Not armed", armLabel_,
               juce::Justification::centredLeft, true);
}
} // namespace fd::app
