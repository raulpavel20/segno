// MIDI learn (M3 C8, docs/design/midi-learn.md): a controller's knob, fader, button or pad moves a
// control. The project's mappings live on its root (FDMIDIMAP), each change one undo step; the
// transport's are the app's. The inputs' listeners (MidiMapper) take mapped messages on the MIDI
// thread; here, each frame, they move their controls the mouse's way.

#include "engine/SessionImpl.h"

namespace fd::engine
{
namespace
{
const juce::Identifier kMidiMapId{"FDMIDIMAP"}, kMappingId{"MAPPING"};
const juce::Identifier kDeviceId{"device"}, kKindId{"kind"}, kChannelId{"channel"}, kNumberId{"number"},
    kTargetId{"target"}, kKeyId{"key"}, kMinId{"min"}, kMaxId{"max"}, kModeId{"mode"};

constexpr double kRest = 0.5; // a mapped move ends after this long at rest (ML9)

double now()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

std::string sourceKey(const model::MidiSource& s)
{
    return s.device + "|" + model::kindKey(s.kind) + "|" + std::to_string(s.channel) + "|" +
           std::to_string(s.number);
}

std::string targetKey(const model::MidiTarget& t)
{
    return std::to_string(t.channel) + "|" + t.key;
}

std::optional<model::MidiMapping> mappingOf(const juce::ValueTree& v)
{
    nlohmann::json j{{"device", v[kDeviceId].toString().toStdString()},
                     {"kind", v[kKindId].toString().toStdString()},
                     {"channel", static_cast<int>(v[kChannelId])},
                     {"number", static_cast<int>(v[kNumberId])},
                     {"target",
                      {{"channel", static_cast<std::uint64_t>(static_cast<juce::int64>(v[kTargetId]))},
                       {"key", v[kKeyId].toString().toStdString()}}},
                     {"min", static_cast<float>(static_cast<double>(v.getProperty(kMinId, 0.0)))},
                     {"max", static_cast<float>(static_cast<double>(v.getProperty(kMaxId, 1.0)))},
                     {"mode", v[kModeId].toString().toStdString()}};
    return model::midiMappingFromJson(j);
}

juce::ValueTree treeOf(const model::MidiMapping& m)
{
    juce::ValueTree v(kMappingId);
    v.setProperty(kDeviceId, juce::String(m.source.device), nullptr);
    v.setProperty(kKindId, juce::String(model::kindKey(m.source.kind)), nullptr);
    v.setProperty(kChannelId, m.source.channel, nullptr);
    v.setProperty(kNumberId, m.source.number, nullptr);
    v.setProperty(kTargetId, static_cast<juce::int64>(m.target.channel), nullptr);
    v.setProperty(kKeyId, juce::String(m.target.key), nullptr);
    v.setProperty(kMinId, m.min, nullptr);
    v.setProperty(kMaxId, m.max, nullptr);
    v.setProperty(kModeId, juce::String(model::modeKey(m.mode)), nullptr);
    return v;
}

constexpr std::pair<const char*, const char*> kTransport[] = {
    {"transport.play", "Play"}, {"transport.stop", "Stop"},           {"transport.record", "Record"},
    {"transport.loop", "Loop"}, {"transport.metronome", "Metronome"}, {"transport.capture", "Capture MIDI"}};

te::MidiOutputDevice* outputNamed(te::Engine& engine, const std::string& name)
{
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
        if (auto* d = dm.getMidiOutDevice(i);
            d != nullptr && d->isEnabled() && d->getName().toStdString() == name)
            return d;
    return nullptr;
}
} // namespace

// ---- the project's mappings ------------------------------------------------------------------

std::vector<model::MidiMapping> Session::Impl::projectMidiMappings() const
{
    std::vector<model::MidiMapping> out;
    const auto map = edit->state.getChildWithName(kMidiMapId);
    for (const auto& v : map)
        if (auto m = mappingOf(v))
            out.push_back(std::move(*m));
    return out;
}

void Session::Impl::publishMidiMapped()
{
    std::vector<model::MidiSource> sources;
    if (snapshot != nullptr)
        for (const auto& m : snapshot->midiMappings)
            sources.push_back(m.source);
    midiMapper.setMapped(sources);
}

std::vector<model::MidiMapping> Session::midiMappings() const
{
    return impl_->snapshot != nullptr ? impl_->snapshot->midiMappings : std::vector<model::MidiMapping>{};
}

std::optional<model::MidiMapping> Session::midiMappingOf(const model::MidiTarget& target) const
{
    for (const auto& m : midiMappings())
        if (m.target == target)
            return m;
    return std::nullopt;
}

bool Session::canMapMidi(const model::MidiTarget& target) const
{
    if (target.isTransport())
        return std::any_of(std::begin(kTransport), std::end(kTransport),
                           [&](const auto& t) { return target.key == t.first; });
    if (target.key == "mute" || target.key == "solo" || target.key == "arm")
    {
        const auto* channel = impl_->snapshot != nullptr ? impl_->snapshot->channel(target.channel) : nullptr;
        if (channel == nullptr || channel->role == model::TrackInfo::Role::Master)
            return false;
        return target.key != "arm" || channel->holdsClips();
    }
    return impl_->param({target.channel, target.key}).has_value();
}

bool Session::mapMidi(const model::MidiMapping& mapping)
{
    const auto modes = model::modesFor(mapping.source.kind);
    if (!canMapMidi(mapping.target) || std::find(modes.begin(), modes.end(), mapping.mode) == modes.end())
        return false;
    auto& app = impl_->midiMap.app;
    // One source, one target (ML6): the source leaves whatever it moved, the target whatever moved it.
    const auto before = app.size();
    std::erase_if(app,
                  [&](const auto& m) { return m.source == mapping.source || m.target == mapping.target; });
    bool appChanged = app.size() != before;
    auto& um = impl_->edit->getUndoManager();
    auto map = impl_->edit->state.getChildWithName(kMidiMapId);
    std::vector<juce::ValueTree> replaced;
    for (const auto& v : map)
        if (const auto m = mappingOf(v); m && (m->source == mapping.source || m->target == mapping.target))
            replaced.push_back(v);
    if (mapping.target.isTransport())
    {
        if (!replaced.empty())
        {
            impl_->beginEdit("Map MIDI");
            for (auto& v : replaced)
                map.removeChild(v, &um);
        }
        app.push_back(mapping);
        appChanged = true;
    }
    else
    {
        impl_->beginEdit("Map MIDI");
        if (!map.isValid())
        {
            impl_->edit->state.appendChild(juce::ValueTree(kMidiMapId), &um);
            map = impl_->edit->state.getChildWithName(kMidiMapId);
        }
        for (auto& v : replaced)
            map.removeChild(v, &um);
        map.appendChild(treeOf(mapping), &um);
    }
    impl_->midiMap.follow.erase(sourceKey(mapping.source)); // picked up afresh
    impl_->midiMap.firstValues.erase(sourceKey(mapping.source));
    if (appChanged)
    {
        impl_->triggerAsyncUpdate();
        if (onAppMidiMappingsChanged)
            onAppMidiMappingsChanged(app);
    }
    return true;
}

bool Session::removeMidiMapping(const model::MidiTarget& target)
{
    auto& app = impl_->midiMap.app;
    if (std::erase_if(app, [&](const auto& m) { return m.target == target; }) > 0)
    {
        impl_->triggerAsyncUpdate();
        if (onAppMidiMappingsChanged)
            onAppMidiMappingsChanged(app);
        return true;
    }
    auto map = impl_->edit->state.getChildWithName(kMidiMapId);
    for (const auto& v : map)
        if (const auto m = mappingOf(v); m && m->target == target)
        {
            impl_->beginEdit("Remove MIDI mapping");
            map.removeChild(v, &impl_->edit->getUndoManager());
            return true;
        }
    return false;
}

bool Session::setMidiMapping(const model::MidiTarget& target, float min, float max, model::MidiMode mode)
{
    min = std::clamp(min, 0.0f, 1.0f);
    max = std::clamp(max, 0.0f, 1.0f);
    for (auto& m : impl_->midiMap.app)
        if (m.target == target)
        {
            const auto modes = model::modesFor(m.source.kind);
            if (std::find(modes.begin(), modes.end(), mode) == modes.end())
                return false;
            m.min = min;
            m.max = max;
            m.mode = mode;
            impl_->triggerAsyncUpdate();
            if (onAppMidiMappingsChanged)
                onAppMidiMappingsChanged(impl_->midiMap.app);
            return true;
        }
    auto map = impl_->edit->state.getChildWithName(kMidiMapId);
    for (auto v : map)
        if (const auto m = mappingOf(v); m && m->target == target)
        {
            const auto modes = model::modesFor(m->source.kind);
            if (std::find(modes.begin(), modes.end(), mode) == modes.end())
                return false;
            impl_->beginEdit("MIDI mapping");
            auto* um = &impl_->edit->getUndoManager();
            v.setProperty(kMinId, min, um);
            v.setProperty(kMaxId, max, um);
            v.setProperty(kModeId, juce::String(model::modeKey(mode)), um);
            impl_->midiMap.follow.erase(sourceKey(m->source));
            return true;
        }
    return false;
}

// ---- learning --------------------------------------------------------------------------------

void Session::learnMidi(const model::MidiTarget& target)
{
    impl_->midiMap.learning = target;
    impl_->midiMap.pending.reset();
    impl_->midiMapper.setLearning(true);
}

void Session::cancelMidiLearn()
{
    impl_->midiMap.learning.reset();
    impl_->midiMap.pending.reset();
    impl_->midiMapper.setLearning(false);
}

std::optional<model::MidiTarget> Session::midiLearnTarget() const
{
    return impl_->midiMap.learning;
}

void Session::Impl::learnMidiFrom(const model::MidiSource& source, int value)
{
    if (!midiMap.learning)
        return;
    const auto target = *midiMap.learning;
    midiMap.learning.reset();
    midiMap.pending.reset();
    midiMapper.setLearning(false);
    model::MidiMapping mapping{.source = source, .target = target, .mode = model::learnedMode(source, value)};
    if (owner.mapMidi(mapping) && owner.onMidiLearned)
        owner.onMidiLearned(mapping);
}

void Session::receiveMidi(const std::string& device, const juce::MidiMessage& message)
{
    impl_->midiMapper.inject(device, message);
}

void Session::drainMidiNow()
{
    impl_->drainMidi();
}

// ---- playing a mapping -----------------------------------------------------------------------

void Session::Impl::drainMidi()
{
    if (edit == nullptr)
        return;
    for (const auto& m : midiMapper.drain())
        handleMidi(m);
    // A coarse controller learned with no fine half after it: a 7-bit one.
    if (midiMap.pending && now() - midiMap.pending->at > 0.03)
    {
        const auto p = *midiMap.pending;
        midiMap.pending.reset();
        learnMidiFrom(p.source, p.value);
    }
    endMidiGestures(false);
    sendMidiBack();
}

void Session::Impl::handleMidi(const MidiMapper::Message& message)
{
    using K = model::MidiSource::Kind;
    if (message.size < 3)
        return;
    const int status = message.bytes[0] & 0xf0;
    const int channel = (message.bytes[0] & 0x0f) + 1;
    model::MidiSource source{.channel = channel, .device = message.device};
    int value = 0;
    bool release = false;
    if (status == 0xb0)
    {
        source.kind = K::Cc;
        source.number = message.bytes[1];
        value = message.bytes[2];
    }
    else if (status == 0x90 || status == 0x80)
    {
        source.kind = K::Note;
        source.number = message.bytes[1];
        value = status == 0x80 ? 0 : message.bytes[2];
        release = value == 0;
    }
    else if (status == 0xe0)
    {
        source.kind = K::PitchBend;
        value = message.bytes[1] | (message.bytes[2] << 7);
    }
    else
        return;

    // Learning (§3): the first message maps the control waiting; a coarse controller waits a
    // moment for a fine one 32 above it (a 14-bit pair).
    if (midiMap.learning)
    {
        if (release)
            return;
        if (auto& p = midiMap.pending; source.kind == K::Cc && p && p->source.device == source.device &&
                                       p->source.channel == source.channel &&
                                       source.number == p->source.number + 32)
        {
            auto pair = p->source;
            pair.kind = K::Cc14;
            const int coarse = p->value;
            p.reset();
            learnMidiFrom(pair, coarse * 128 + value);
            return;
        }
        if (source.kind == K::Cc && source.number < 32)
        {
            midiMap.pending = MidiMapState::Pending{source, value, now()};
            return;
        }
        learnMidiFrom(source, value);
        return;
    }

    for (const auto& m : owner.midiMappings())
    {
        const auto& s = m.source;
        if (s.device != source.device || s.channel != source.channel)
            continue;
        int scaled = value; // in the mapping's own resolution
        if (s.kind == K::Cc14 && source.kind == K::Cc &&
            (source.number == s.number || source.number == s.number + 32))
        {
            auto& halves = midiMap.wide[sourceKey(s)];
            halves[source.number == s.number ? 0 : 1] = value;
            scaled = halves[0] * 128 + halves[1];
        }
        else if (s.kind != source.kind || (s.kind != K::PitchBend && s.number != source.number))
            continue;

        const auto key = sourceKey(s);
        midiMap.received[key] = scaled;
        // A button's press or release: a note, or a controller crossing its middle.
        const int top = model::fullScale(s.kind);
        std::optional<bool> edge;
        if (s.kind == K::Note)
            edge = !release;
        else if (const bool held = scaled * 2 > top; !midiMap.down.contains(key) || midiMap.down[key] != held)
        {
            midiMap.down[key] = held;
            edge = held;
        }

        if (m.target.isTransport())
        {
            if (edge.value_or(false) && owner.onMidiTransport)
                owner.onMidiTransport(m.target.key);
            continue;
        }
        if (m.target.isSwitchOnly()) // mute, solo, arm
        {
            const auto* c = snapshot != nullptr ? snapshot->channel(m.target.channel) : nullptr;
            if (c == nullptr || !edge)
                continue;
            const bool on = m.target.key == "mute"   ? c->mix.mute
                            : m.target.key == "solo" ? c->mix.solo
                                                     : c->input.armed;
            std::optional<bool> next;
            if (m.mode == model::MidiMode::Toggle)
                next = *edge ? std::optional(!on) : std::nullopt;
            else
                next = *edge; // momentary, or a knob past its middle
            if (!next || *next == on)
                continue;
            if (m.target.key == "mute")
                owner.setMute(m.target.channel, *next);
            else if (m.target.key == "solo")
                owner.setSolo(m.target.channel, *next, false);
            else
                owner.setArmed(m.target.channel, *next, false);
            continue;
        }

        const Session::ParamTarget target{m.target.channel, m.target.key};
        const auto p = param(target);
        if (!p)
            continue;
        const float current = owner.paramValue(target).value_or(0.0f);
        std::optional<float> travel;
        if (model::isButton(m.mode))
        {
            if (edge)
                travel = model::buttonMove(m, *edge, p->steps, current);
        }
        else if (model::isRelative(m.mode))
            travel =
                model::relativeMove(m, model::relativeSteps(m.mode, value),
                                    p->steps > 1 ? 1.0f / static_cast<float>(p->steps - 1) : 0.01f, current);
        else
        {
            // A controller's first values may say it's a relative encoder (§3): the guess shows in
            // its row, where it's corrected.
            if (s.kind == K::Cc)
                if (auto& first = midiMap.firstValues[key]; first.size() < 8)
                {
                    first.push_back(value);
                    if (const auto guess = model::guessRelative(first))
                    {
                        auto mapping = m;
                        mapping.mode = *guess;
                        auto map = edit->state.getChildWithName(kMidiMapId);
                        for (auto v : map)
                            if (mappingOf(v) == m)
                                v.setProperty(kModeId, juce::String(model::modeKey(*guess)), nullptr);
                        continue;
                    }
                }
            travel = model::absoluteMove(m, static_cast<float>(scaled) / static_cast<float>(top), current,
                                         midiMap.takeover, midiMap.follow[key]);
        }
        if (!travel)
            continue;
        // One gesture until it rests (ML9): one undo step, a touch while recording automation.
        // Two knobs turned together share a step: a controller's move doesn't end another's.
        midiMap.moving = true;
        auto& g = midiMap.gestures[targetKey(m.target)];
        if (g.gesture == nullptr)
        {
            g.inhibitor = std::make_unique<te::Edit::UndoTransactionInhibitor>(*edit);
            g.gesture = std::make_unique<Session::ParamGesture>(owner, target, p->name);
        }
        g.last = now();
        owner.setParamValue(target, *travel);
        midiMap.moving = false;
    }
}

void Session::Impl::endMidiGestures(bool all)
{
    const double t = now();
    for (auto it = midiMap.gestures.begin(); it != midiMap.gestures.end();)
        if (all || t - it->second.last > kRest)
        {
            it->second.gesture.reset(); // its touch ends, then the step closes
            it = midiMap.gestures.erase(it);
        }
        else
            ++it;
}

void Session::Impl::sendMidiBack()
{
    if (midiMap.sendBack.empty() || snapshot == nullptr)
        return;
    using K = model::MidiSource::Kind;
    for (const auto& m : snapshot->midiMappings)
    {
        if (m.target.isTransport() || std::find(midiMap.sendBack.begin(), midiMap.sendBack.end(),
                                                m.source.device) == midiMap.sendBack.end())
            continue;
        auto* out = outputNamed(engine, m.source.device);
        if (out == nullptr)
            continue;
        float travel = 0.0f;
        if (m.target.isSwitchOnly())
        {
            const auto* c = snapshot->channel(m.target.channel);
            const bool on = c != nullptr && (m.target.key == "mute"   ? c->mix.mute
                                             : m.target.key == "solo" ? c->mix.solo
                                                                      : c->input.armed);
            travel = on ? m.max : m.min;
        }
        else if (const auto v = owner.paramValue({m.target.channel, m.target.key}))
            travel = *v;
        else
            continue;
        const int value = model::feedbackValue(m, travel);
        const auto key = sourceKey(m.source);
        if (midiMap.sent.contains(key) && midiMap.sent[key] == value)
            continue;
        midiMap.sent[key] = value;
        if (midiMap.received.contains(key) && midiMap.received[key] == value)
            continue; // the hardware is there already: it moved it
        const int channel = m.source.channel;
        switch (m.source.kind)
        {
        case K::Cc:
            out->fireMessage(juce::MidiMessage::controllerEvent(channel, m.source.number, value));
            break;
        case K::Cc14:
            out->fireMessage(juce::MidiMessage::controllerEvent(channel, m.source.number, value / 128));
            out->fireMessage(juce::MidiMessage::controllerEvent(channel, m.source.number + 32, value % 128));
            break;
        case K::Note:
            out->fireMessage(
                juce::MidiMessage::noteOn(channel, m.source.number, static_cast<juce::uint8>(value)));
            break;
        case K::PitchBend:
            out->fireMessage(juce::MidiMessage::pitchWheel(channel, value));
            break;
        }
    }
}

// ---- names and values ------------------------------------------------------------------------

std::string Session::midiTargetName(const model::MidiTarget& target) const
{
    for (const auto& [key, name] : kTransport)
        if (target.key == key)
            return name;
    const auto* channel = impl_->snapshot != nullptr ? impl_->snapshot->channel(target.channel) : nullptr;
    const std::string channelName = channel != nullptr ? channel->name : std::string();
    if (target.key == "mute")
        return channelName + " \xc2\xb7 Mute";
    if (target.key == "solo")
        return channelName + " \xc2\xb7 Solo";
    if (target.key == "arm")
        return channelName + " \xc2\xb7 Arm";
    const auto [name, module] = paramName({target.channel, target.key});
    if (name.empty())
        return {};
    return (module.empty() || module == "Mixer" ? channelName : module) + " \xc2\xb7 " + name;
}

std::string Session::midiValueText(const model::MidiTarget& target, float travel) const
{
    if (target.isSwitchOnly())
        return travel >= 0.5f ? "On" : "Off";
    return paramText({target.channel, target.key}, travel);
}

std::optional<float> Session::midiValueFromText(const model::MidiTarget& target,
                                                const std::string& text) const
{
    if (target.isSwitchOnly())
    {
        const auto t = juce::String(text).trim().toLowerCase();
        if (t == "on")
            return 1.0f;
        if (t == "off")
            return 0.0f;
        return std::nullopt;
    }
    return paramFromText({target.channel, target.key}, text);
}

// ---- the app's settings ----------------------------------------------------------------------

void Session::setMidiTakeover(model::MidiTakeover takeover)
{
    impl_->midiMap.takeover = takeover;
    impl_->midiMap.follow.clear();
}

model::MidiTakeover Session::midiTakeover() const
{
    return impl_->midiMap.takeover;
}

void Session::setMidiSendBack(std::vector<std::string> devices)
{
    impl_->midiMap.sendBack = std::move(devices);
    impl_->midiMap.sent.clear(); // each control's value goes once
}

bool Session::hasMidiOutput(const std::string& name) const
{
    auto& dm = impl_->engine.getDeviceManager();
    for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
        if (auto* d = dm.getMidiOutDevice(i); d != nullptr && d->getName().toStdString() == name)
            return true;
    return false;
}

void Session::setAppMidiMappings(std::vector<model::MidiMapping> mappings)
{
    std::erase_if(mappings, [](const auto& m) { return !m.target.isTransport(); });
    impl_->midiMap.app = std::move(mappings);
    impl_->triggerAsyncUpdate();
}

std::vector<model::MidiMapping> Session::appMidiMappings() const
{
    return impl_->midiMap.app;
}
} // namespace fd::engine
