// Automation (M2 step C4, ADR 0028, docs/design/automation.md): lanes over tracktion's
// automation curves, in the control's units.

#include "engine/SessionImpl.h"

#include "engine/DevicePlugin.h"

#include <cmath>

namespace fd::engine
{
namespace
{
std::string moduleKey(const te::Plugin& plugin, const juce::String& paramId)
{
    return "m" + std::to_string(plugin.itemID.getRawID()) + "/" + paramId.toStdString();
}

std::string levelText(float db)
{
    if (db <= model::minusInfinityDb)
        return "-inf dB";
    return juce::String(db, 1).toStdString() + " dB";
}

std::string panText(float pan)
{
    const int amount = juce::roundToInt(std::abs(pan) * 50.0f);
    return amount == 0 ? std::string("C") : std::to_string(amount) + (pan < 0.0f ? "L" : "R");
}

std::optional<float> parseLevel(const std::string& text)
{
    const auto t = juce::String(text).trim().toLowerCase();
    if (t.contains("inf"))
        return model::minusInfinityDb;
    if (!t.containsAnyOf("0123456789"))
        return std::nullopt;
    return t.getFloatValue();
}

std::optional<float> parsePan(const std::string& text)
{
    const auto t = juce::String(text).trim().toUpperCase();
    if (t == "C")
        return 0.0f;
    if (t.endsWithChar('L') || t.endsWithChar('R'))
    {
        const float amount = juce::jlimit(0.0f, 50.0f, t.dropLastCharacters(1).getFloatValue()) / 50.0f;
        return t.endsWithChar('L') ? -amount : amount;
    }
    if (t.containsAnyOf("0123456789"))
        return juce::jlimit(-1.0f, 1.0f, t.getFloatValue());
    return std::nullopt;
}

/// Send levels top out at 0 dB: the lower part of the fader taper (0 dB at 80 %).
const float kSendTop = te::decibelsToVolumeFaderPosition(0.0f);
} // namespace

// ---- lanes ---------------------------------------------------------------------------------

std::vector<Session::Impl::Lane> Session::Impl::lanesOf(std::uint64_t channelId) const
{
    std::vector<Lane> out;
    auto* list = plugins(channelId);
    if (list == nullptr)
        return out;
    if (auto* v = findIn<te::VolumeAndPanPlugin>(*list))
    {
        out.push_back({.kind = Lane::Kind::Volume, .param = v->volParam.get(), .key = "volume", .name = "Volume", .module = "Mixer"});
        out.push_back({.kind = Lane::Kind::Pan, .param = v->panParam.get(), .key = "pan", .name = "Pan", .module = "Mixer",
                       .offset = -1.0f, .scale = 2.0f});
    }
    if (auto* t = track(channelId))
        for (auto* r : returns())
        {
            auto* send = sendIn(t->pluginList, busOf(*r));
            out.push_back({.kind = Lane::Kind::Send,
                           .param = send != nullptr ? send->gain.get() : nullptr,
                           .key = "send/" + std::to_string(r->itemID.getRawID()),
                           .name = "Send " + r->getName().toStdString(),
                           .module = "Mixer",
                           .returnId = r->itemID.getRawID(),
                           .scale = kSendTop});
        }
    for (auto* p : chain(*list))
    {
        const auto device = p->itemID.getRawID();
        if (auto* native = dynamic_cast<DevicePlugin*>(p))
        {
            const auto specs = native->device().info().params;
            for (size_t i = 0; i < specs.size(); ++i)
            {
                const auto& spec = specs[i];
                if (!spec.hasFlag(ParamFlags::Automatable) || spec.hasFlag(ParamFlags::Hidden) ||
                    spec.hasFlag(ParamFlags::ChangesLatency))
                    continue;
                const juce::String id(std::string(spec.id));
                out.push_back({.kind = Lane::Kind::Module,
                               .param = &native->parameter(i),
                               .key = moduleKey(*p, id),
                               .name = std::string(spec.name),
                               .module = p->getName().toStdString(),
                               .paramId = std::string(spec.id),
                               .device = device,
                               .steps = spec.numSteps(),
                               .native = native,
                               .index = i});
            }
            continue;
        }
        for (auto* param : p->getAutomatableParameters())
        {
            // A hosted plugin's own parameters; not tracktion's dry and wet levels around it.
            if (param->paramID == "dry level" || param->paramID == "wet level")
                continue;
            const auto range = param->getValueRange();
            out.push_back({.kind = Lane::Kind::External,
                           .param = param,
                           .key = moduleKey(*p, param->paramID),
                           .name = param->getParameterName().toStdString(),
                           .module = p->getName().toStdString(),
                           .paramId = param->paramID.toStdString(),
                           .device = device,
                           .steps = param->isDiscrete() ? param->getNumberOfStates() : 0,
                           .offset = range.getStart(),
                           .scale = range.getLength() > 0.0f ? range.getLength() : 1.0f});
        }
    }
    return out;
}

std::optional<Session::Impl::Lane> Session::Impl::lane(const LaneTarget& target) const
{
    for (auto& l : lanesOf(target.channel))
        if (l.key == target.lane)
            return l;
    return std::nullopt;
}

te::Track* Session::Impl::stateTrack(std::uint64_t channelId) const
{
    if (auto* t = channelTrack(channelId))
        return t;
    return channelId != 0 && channelId == masterId() ? edit->getMasterTrack() : nullptr;
}

std::vector<std::string> Session::Impl::shownLanes(const te::Track& track)
{
    std::vector<std::string> keys;
    for (const auto& k : juce::StringArray::fromLines(track.state[lanesId].toString()))
        if (k.isNotEmpty())
            keys.push_back(k.toStdString());
    return keys;
}

void Session::Impl::setShownLanes(te::Track& track, const std::vector<std::string>& keys)
{
    juce::StringArray lines;
    for (const auto& k : keys)
        lines.add(juce::String(k));
    track.state.setProperty(lanesId, lines.joinIntoString("\n"), nullptr); // view state
}

std::vector<model::LaneInfo> Session::Impl::laneInfos(std::uint64_t channelId) const
{
    std::vector<model::LaneInfo> out;
    auto* state = stateTrack(channelId);
    if (state == nullptr)
        return out;
    const auto all = lanesOf(channelId);
    auto info = [](const Lane& l) {
        model::LaneInfo i{.key = l.key, .device = l.device, .paramId = l.paramId, .name = l.name, .module = l.module,
                          .steps = l.steps};
        if (l.param == nullptr)
            return i;
        auto& curve = l.param->getCurve();
        i.on = !curve.bypass.get();
        for (int p = 0; p < curve.getNumPoints(); ++p)
            i.points.push_back({.time = te::toUnderlying(curve.getPointPosition(p)),
                                .value = l.toLane(curve.getPointValue(p)),
                                .curve = curve.getPointCurve(p)});
        return i;
    };
    std::set<std::string> listed;
    for (const auto& key : shownLanes(*state))
        for (const auto& l : all)
            if (l.key == key && listed.insert(key).second)
                out.push_back(info(l));
    for (const auto& l : all)
        if (l.param != nullptr && l.param->getCurve().getNumPoints() > 0 && listed.insert(l.key).second)
            out.push_back(info(l));
    return out;
}

bool Session::Impl::followsLane(const te::AutomatableParameter& param)
{
    auto& curve = param.getCurve();
    return curve.getNumPoints() > 0 && !curve.bypass.get() && !param.isCurrentlyRecording();
}

te::AutomatableParameter* Session::Impl::parameterFor(const juce::ValueTree& pluginState, const juce::Identifier& id) const
{
    auto* p = plugin(te::EditItemID::fromID(pluginState).getRawID());
    if (p == nullptr)
        return nullptr;
    if (auto* v = dynamic_cast<te::VolumeAndPanPlugin*>(p))
        return id == te::IDs::volume ? v->volParam.get() : id == te::IDs::pan ? v->panParam.get() : nullptr;
    if (auto* s = dynamic_cast<te::AuxSendPlugin*>(p))
        return id == te::IDs::auxSendSliderPos ? s->gain.get() : nullptr;
    if (dynamic_cast<DevicePlugin*>(p) == nullptr)
        return nullptr;
    return p->getAutomatableParameterByID(id.toString()).get();
}

void Session::Impl::applyAutomationMode()
{
    const auto mode = armed ? te::AutomationMode::touch : te::AutomationMode::read;
    for (auto* t : te::getAudioTracks(*edit))
        if (t->automationMode.get() != mode)
            t->automationMode = mode;
    if (auto* master = edit->getMasterTrack(); master != nullptr && master->automationMode.get() != mode)
        master->automationMode = mode;
}

void Session::Impl::endRecordingPass()
{
    // tracktion closes the step once edits pause; the pass's name stays on it.
    recordingPass.reset();
}

void Session::Impl::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (edit != nullptr && source == &edit->getTransport())
    {
        if (!edit->getTransport().isPlaying())
            endRecordingPass();
        return;
    }
    midiDevicesChanged();
}

void Session::Impl::pluginParameterChanged(te::AutomatableParameter& param, bool isFollowingAutomation)
{
    if (isFollowingAutomation)
        return; // played values (also on the audio thread)
    lastTouched = {param.automatableEditElement.itemID.getRawID(), param.paramID};
    if (param.isCurrentlyRecording())
    {
        // A recording pass is one undo step, however many controls move (§8).
        if (recordingPass == nullptr)
        {
            edit->getUndoManager().beginNewTransaction("Record automation");
            recordingPass = std::make_unique<te::Edit::UndoTransactionInhibitor>(*edit);
        }
        dirty = true;
        return;
    }
    // Moving an automated control turns its lane off; the control keeps the value (§7).
    if (auto& curve = param.getCurve(); curve.getNumPoints() > 0 && !curve.bypass.get())
        curve.bypass = true;
    dirty = true; // the value itself (its write was taken for a played one)
}

// ---- Session -------------------------------------------------------------------------------

std::vector<Session::LaneChoice> Session::automatableParameters(std::uint64_t channelId) const
{
    std::vector<LaneChoice> out;
    const auto* channel = impl_->snapshot->channel(channelId);
    for (const auto& l : impl_->lanesOf(channelId))
    {
        LaneChoice c{.lane = l.key, .name = l.name, .module = l.module};
        if (l.param != nullptr)
            c.valueText = laneText({channelId, l.key}, l.toLane(l.param->getCurrentValue()));
        c.shown = channel != nullptr && channel->lane(l.key) != nullptr;
        out.push_back(std::move(c));
    }
    return out;
}

bool Session::setAutomation(const LaneTarget& target, const model::AutomationPoints& points,
                            std::optional<std::pair<double, double>> range, const std::string& undoName)
{
    auto l = impl_->lane(target);
    if (!l)
        return false;
    impl_->beginEdit(juce::String(undoName));
    if (l->kind == Impl::Lane::Kind::Send && l->param == nullptr)
    {
        // A send is made when first used (ADR 0026); automating it uses it.
        EditGroup group(*this, undoName);
        if (!setSend(target.channel, l->returnId, std::nullopt, std::nullopt) || !(l = impl_->lane(target)) ||
            l->param == nullptr)
            return false;
    }
    auto& curve = l->param->getCurve();
    auto* um = &impl_->edit->getUndoManager();
    if (range)
        curve.removePoints(te::EditTimeRange(te::TimePosition::fromSeconds(std::max(0.0, range->first)),
                                             te::TimePosition::fromSeconds(range->second + 1e-9)),
                           um);
    else
        while (curve.getNumPoints() > 0)
            curve.removePoint(curve.getNumPoints() - 1, um);
    const bool stepped = l->steps >= 2;
    for (const auto& p : points)
    {
        const float value = stepped ? model::snapToSteps(p.value, l->steps) : std::clamp(p.value, 0.0f, 1.0f);
        curve.addPoint(te::TimePosition::fromSeconds(std::max(0.0, p.time)), l->toEngine(value),
                       stepped ? model::kStepCurve : std::clamp(p.curve, -0.5f, 0.5f), um);
    }
    showLane(target);
    return true;
}

bool Session::showLane(const LaneTarget& target)
{
    auto* state = impl_->stateTrack(target.channel);
    if (state == nullptr || !impl_->lane(target))
        return false;
    auto keys = Impl::shownLanes(*state);
    if (std::find(keys.begin(), keys.end(), target.lane) == keys.end())
    {
        keys.push_back(target.lane);
        Impl::setShownLanes(*state, keys);
    }
    setLanesFolded(target.channel, false);
    return true;
}

void Session::removeLane(const LaneTarget& target)
{
    auto* state = impl_->stateTrack(target.channel);
    if (state == nullptr)
        return;
    if (auto l = impl_->lane(target); l && l->param != nullptr && l->param->getCurve().getNumPoints() > 0)
    {
        impl_->beginEdit("Remove automation");
        auto& curve = l->param->getCurve();
        while (curve.getNumPoints() > 0)
            curve.removePoint(curve.getNumPoints() - 1, &impl_->edit->getUndoManager());
    }
    auto keys = Impl::shownLanes(*state);
    keys.erase(std::remove(keys.begin(), keys.end(), target.lane), keys.end());
    Impl::setShownLanes(*state, keys);
}

void Session::setLanesFolded(std::uint64_t channelId, bool folded)
{
    if (auto* state = impl_->stateTrack(channelId); state != nullptr && static_cast<bool>(state->state[Impl::lanesFoldedId]) != folded)
        state->state.setProperty(Impl::lanesFoldedId, folded, nullptr); // view state
}

void Session::setLaneOn(const LaneTarget& target, bool on)
{
    if (auto l = impl_->lane(target); l && l->param != nullptr && l->param->getCurve().bypass.get() == on)
        l->param->getCurve().bypass = !on;
}

void Session::setAllLanesOn()
{
    const auto snapshot = impl_->snapshot;
    std::vector<std::uint64_t> channels;
    for (const auto* list : {&snapshot->tracks, &snapshot->returns})
        for (const auto& t : *list)
            channels.push_back(t.id);
    channels.push_back(snapshot->master.id);
    for (auto id : channels)
        for (auto& l : impl_->lanesOf(id))
            if (l.param != nullptr && l.param->getCurve().bypass.get())
                l.param->getCurve().bypass = false;
}

bool Session::anyLaneOff() const
{
    const auto snapshot = impl_->snapshot;
    auto off = [](const model::TrackInfo& t) {
        return std::any_of(t.lanes.begin(), t.lanes.end(), [](const auto& l) { return !l.on && !l.points.empty(); });
    };
    return std::any_of(snapshot->tracks.begin(), snapshot->tracks.end(), off) ||
           std::any_of(snapshot->returns.begin(), snapshot->returns.end(), off) || off(snapshot->master);
}

std::optional<float> Session::laneValue(const LaneTarget& target) const
{
    auto l = impl_->lane(target);
    if (!l)
        return std::nullopt;
    if (l->param == nullptr)
        return 0.0f; // a send not made yet: off
    return l->toLane(l->param->getCurrentValue());
}

std::string Session::laneText(const LaneTarget& target, float value) const
{
    auto l = impl_->lane(target);
    if (!l)
        return {};
    switch (l->kind)
    {
    case Impl::Lane::Kind::Volume:
    case Impl::Lane::Kind::Send: return levelText(te::volumeFaderPositionToDB(l->toEngine(value)));
    case Impl::Lane::Kind::Pan: return panText(l->toEngine(value));
    case Impl::Lane::Kind::Module:
    {
        const auto& spec = l->native->device().info().params[l->index];
        return spec.toText(spec.fromNormalized(value));
    }
    case Impl::Lane::Kind::External: return l->param->valueToString(l->toEngine(value)).toStdString();
    }
    return {};
}

std::optional<double> Session::lanePlain(const LaneTarget& target, float value) const
{
    auto l = impl_->lane(target);
    if (!l)
        return std::nullopt;
    switch (l->kind)
    {
    case Impl::Lane::Kind::Volume:
    case Impl::Lane::Kind::Send: return te::volumeFaderPositionToDB(l->toEngine(value));
    case Impl::Lane::Kind::Pan: return l->toEngine(value);
    case Impl::Lane::Kind::Module:
    {
        const auto& spec = l->native->device().info().params[l->index];
        return spec.fromNormalized(value);
    }
    case Impl::Lane::Kind::External: return std::clamp(value, 0.0f, 1.0f); // normalised (mcp.md)
    }
    return std::nullopt;
}

std::optional<float> Session::laneFromPlain(const LaneTarget& target, double plain) const
{
    auto l = impl_->lane(target);
    if (!l)
        return std::nullopt;
    const auto v = static_cast<float>(plain);
    switch (l->kind)
    {
    case Impl::Lane::Kind::Volume: return l->toLane(te::decibelsToVolumeFaderPosition(std::min(v, 6.0f)));
    case Impl::Lane::Kind::Send: return l->toLane(te::decibelsToVolumeFaderPosition(std::min(v, 0.0f)));
    case Impl::Lane::Kind::Pan: return l->toLane(std::clamp(v, -1.0f, 1.0f));
    case Impl::Lane::Kind::Module:
    {
        const auto& spec = l->native->device().info().params[l->index];
        return spec.toNormalized(v);
    }
    case Impl::Lane::Kind::External: return std::clamp(v, 0.0f, 1.0f);
    }
    return std::nullopt;
}

std::optional<float> Session::laneFromText(const LaneTarget& target, const std::string& text) const
{
    auto l = impl_->lane(target);
    if (!l)
        return std::nullopt;
    switch (l->kind)
    {
    case Impl::Lane::Kind::Volume:
    case Impl::Lane::Kind::Send:
        if (const auto db = parseLevel(text))
            return laneFromPlain(target, *db);
        return std::nullopt;
    case Impl::Lane::Kind::Pan:
        if (const auto pan = parsePan(text))
            return laneFromPlain(target, *pan);
        return std::nullopt;
    case Impl::Lane::Kind::Module:
    {
        const auto& spec = l->native->device().info().params[l->index];
        if (const auto v = spec.fromText(text))
            return spec.toNormalized(*v);
        return std::nullopt;
    }
    case Impl::Lane::Kind::External: return l->toLane(l->param->stringToValue(juce::String(text)));
    }
    return std::nullopt;
}

void Session::setAutomationArmed(bool armed)
{
    impl_->armed = armed;
    impl_->applyAutomationMode();
}

bool Session::isAutomationArmed() const
{
    return impl_->armed;
}

std::optional<Session::LaneTarget> Session::lastTouchedParameter() const
{
    if (!impl_->lastTouched)
        return std::nullopt;
    const auto [pluginId, paramId] = *impl_->lastTouched;
    const auto snapshot = impl_->snapshot;
    std::vector<std::uint64_t> channels;
    for (const auto* list : {&snapshot->tracks, &snapshot->returns})
        for (const auto& t : *list)
            channels.push_back(t.id);
    channels.push_back(snapshot->master.id);
    for (auto id : channels)
        for (const auto& l : impl_->lanesOf(id))
            if (l.param != nullptr && l.param->automatableEditElement.itemID.getRawID() == pluginId &&
                l.param->paramID == paramId)
                return LaneTarget{id, l.key};
    return std::nullopt;
}

Session::LaneGesture::LaneGesture(Session& session, const LaneTarget& target, const std::string& name)
    : session_(session), target_(target)
{
    auto& impl = session_.impl();
    if (impl.groupDepth++ == 0)
        impl.edit->getUndoManager().beginNewTransaction(juce::String(name));
    if (auto l = impl.lane(target_); l && l->param != nullptr)
    {
        l->param->parameterChangeGestureBegin(); // a touch, while recording
        touching_ = true;
    }
}

Session::LaneGesture::~LaneGesture()
{
    auto& impl = session_.impl();
    if (touching_)
        if (auto l = impl.lane(target_); l && l->param != nullptr)
            l->param->parameterChangeGestureEnd();
    --impl.groupDepth;
}
} // namespace fd::engine
