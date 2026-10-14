// Automation (M2 step C4, ADRs 0028 and 0029, docs/design/automation.md): automation lanes of
// their own, driving the parameters assigned to them through tracktion's automation curves.

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/Racks.h"
#include "model/TrackEdits.h"

#include <cmath>
#include <cstdlib>
#include <map>

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

/// A lane value of a lane without one assignment to read it by: a percentage.
std::string percentText(float value)
{
    return std::to_string(juce::roundToInt(std::clamp(value, 0.0f, 1.0f) * 100.0f)) + " %";
}

std::optional<float> parsePercent(const std::string& text)
{
    const auto t = juce::String(text).trim();
    if (!t.containsAnyOf("0123456789"))
        return std::nullopt;
    return std::clamp(t.getFloatValue() / 100.0f, 0.0f, 1.0f);
}

/// Send levels top out at 0 dB: the lower part of the fader taper (0 dB at 80 %).
const float kSendTop = te::decibelsToVolumeFaderPosition(0.0f);

std::uint64_t idProperty(const juce::var& v)
{
    return static_cast<std::uint64_t>(static_cast<juce::int64>(v));
}

juce::var idVar(std::uint64_t id)
{
    return static_cast<juce::int64>(id);
}

/// Commands that compose others: the inner ones join the outer one's undo step.
struct Nested
{
    explicit Nested(int& depth) : depth_(depth) { ++depth_; }
    ~Nested() { --depth_; }
    Nested(const Nested&) = delete;
    Nested& operator=(const Nested&) = delete;
    int& depth_;
};
} // namespace

// ---- parameters ----------------------------------------------------------------------------

std::vector<Session::Impl::Param> Session::Impl::paramsOf(std::uint64_t channelId) const
{
    std::vector<Param> out;
    auto* list = plugins(channelId);
    if (list == nullptr)
        return out;
    if (auto* v = findIn<te::VolumeAndPanPlugin>(*list))
    {
        out.push_back({.kind = Param::Kind::Volume, .param = v->volParam.get(), .key = "volume", .name = "Volume", .module = "Mixer"});
        out.push_back({.kind = Param::Kind::Pan, .param = v->panParam.get(), .key = "pan", .name = "Pan", .module = "Mixer",
                       .offset = -1.0f, .scale = 2.0f});
    }
    if (auto* t = track(channelId))
        for (auto* r : returns())
        {
            auto* send = sendIn(t->pluginList, busOf(*r));
            out.push_back({.kind = Param::Kind::Send,
                           .param = send != nullptr ? send->gain.get() : nullptr,
                           .key = "send/" + std::to_string(r->itemID.getRawID()),
                           .name = "Send " + r->getName().toStdString(),
                           .module = "Mixer",
                           .returnId = r->itemID.getRawID(),
                           .scale = kSendTop});
        }
    const auto addModule = [&](te::Plugin* p)
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
                out.push_back({.kind = Param::Kind::Module,
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
            return;
        }
        for (auto* param : p->getAutomatableParameters())
        {
            // A hosted plugin's own parameters; not tracktion's dry and wet levels around it.
            if (param->paramID == "dry level" || param->paramID == "wet level")
                continue;
            const auto range = param->getValueRange();
            out.push_back({.kind = Param::Kind::External,
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
    };
    for (auto* p : chain(*list))
    {
        auto* rack = rackTypeOf(p);
        if (rack == nullptr)
        {
            addModule(p);
            continue;
        }
        // A rack (racks.md §4): its macros, as its parameters, then the modules in its chains.
        const auto macros = rack->getMacroParameters();
        for (int i = 0; i < macros.size() && i < static_cast<int>(Session::rackMacros().params.size()); ++i)
        {
            const auto& spec = Session::rackMacros().params[static_cast<size_t>(i)];
            const juce::String id(std::string(spec.id));
            auto name = macros[i]->macroName.get().toStdString();
            out.push_back({.kind = Param::Kind::Macro,
                           .param = macros[i].get(),
                           .key = moduleKey(*p, id),
                           .name = name.empty() ? std::string(spec.name) : name,
                           .module = rack->rackName.get().toStdString(),
                           .paramId = std::string(spec.id),
                           .device = p->itemID.getRawID()});
        }
        for (auto* m : rackModules(*rack))
            addModule(m);
    }
    return out;
}

std::optional<Session::Impl::Param> Session::Impl::param(const ParamTarget& target) const
{
    for (auto& p : paramsOf(target.channel))
        if (p.key == target.key)
            return p;
    return std::nullopt;
}

std::vector<std::uint64_t> Session::Impl::channels() const
{
    std::vector<std::uint64_t> out;
    for (auto* t : te::getAudioTracks(*edit))
        out.push_back(t->itemID.getRawID());
    out.push_back(masterId());
    return out;
}

std::optional<Session::ParamTarget> Session::Impl::targetOf(const te::AutomatableParameter& p) const
{
    for (auto id : channels())
        for (const auto& candidate : paramsOf(id))
            if (candidate.param == &p)
                return ParamTarget{id, candidate.key};
    return std::nullopt;
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

// ---- lanes ---------------------------------------------------------------------------------

juce::ValueTree Session::Impl::laneTree(std::uint64_t id) const
{
    for (auto lane : lanesTree())
        if (idOf(lane) == id)
            return lane;
    return {};
}

juce::ValueTree Session::Impl::assignTree(const ParamTarget& target) const
{
    for (auto lane : lanesTree())
        for (auto a : lane)
            if (a.hasType(assignId) && targetIn(a) == target)
                return a;
    return {};
}

std::uint64_t Session::Impl::idOf(const juce::ValueTree& lane)
{
    return te::EditItemID::fromID(lane).getRawID();
}

model::AutomationPoints Session::Impl::pointsOf(const juce::ValueTree& lane)
{
    model::AutomationPoints out;
    for (auto p : lane.getChildWithName(pointsId))
        out.push_back({.time = static_cast<double>(p[te::IDs::t]),
                       .value = static_cast<float>(p[te::IDs::v]),
                       .curve = static_cast<float>(p[te::IDs::c])});
    return out;
}

Session::ParamTarget Session::Impl::targetIn(const juce::ValueTree& assign)
{
    return {idProperty(assign[assignChannelId]), assign[keyId].toString().toStdString()};
}

model::AssignmentRange Session::Impl::rangeOf(const juce::ValueTree& assign)
{
    return {static_cast<float>(assign.getProperty(fromId, 0.0f)), static_cast<float>(assign.getProperty(toId, 1.0f))};
}

void Session::Impl::writePoints(juce::ValueTree points, const model::AutomationPoints& with, juce::UndoManager* um)
{
    const int n = static_cast<int>(with.size());
    for (int i = 0; i < n; ++i)
    {
        const auto& p = with[static_cast<size_t>(i)];
        const bool added = i >= points.getNumChildren();
        auto child = added ? juce::ValueTree(te::IDs::POINT) : points.getChild(i);
        child.setProperty(te::IDs::t, p.time, added ? nullptr : um);
        child.setProperty(te::IDs::v, p.value, added ? nullptr : um);
        child.setProperty(te::IDs::c, p.curve, added ? nullptr : um);
        if (added)
            points.appendChild(child, um);
    }
    while (points.getNumChildren() > n)
        points.removeChild(points.getNumChildren() - 1, um);
}

void Session::Impl::writeCurve(te::AutomationCurve& curve, const model::AutomationPoints& with, juce::UndoManager* um)
{
    const int n = static_cast<int>(with.size());
    const int kept = std::min(n, curve.getNumPoints());
    for (int i = 0; i < kept; ++i)
    {
        const auto& p = with[static_cast<size_t>(i)];
        if (te::toUnderlying(curve.getPointTime(i)) != p.time)
            curve.setPointPosition(i, te::TimePosition::fromSeconds(p.time), um);
        if (curve.getPointValue(i) != p.value)
            curve.setPointValue(i, p.value, um);
        if (curve.getPointCurve(i) != p.curve)
            curve.setCurveValue(i, p.curve, um);
    }
    for (int i = kept; i < n; ++i) // after the others: in order
    {
        const auto& p = with[static_cast<size_t>(i)];
        curve.addPoint(te::TimePosition::fromSeconds(p.time), p.value, p.curve, um);
    }
    while (curve.getNumPoints() > n)
        curve.removePoint(curve.getNumPoints() - 1, um);
}

std::vector<std::string> Session::Impl::laneNames() const
{
    std::vector<std::string> names;
    for (const auto& lane : lanesTree())
        names.push_back(lane[nameId].toString().toStdString());
    return names;
}

juce::ValueTree Session::Impl::makeLane(std::uint64_t after, int colour, int index, juce::UndoManager* um)
{
    juce::ValueTree lane(laneId);
    edit->createNewItemID().writeID(lane, nullptr);
    lane.setProperty(nameId, juce::String(model::nextLaneName(laneNames())), nullptr); // tracks-and-clips.md §18
    lane.setProperty(afterId, idVar(after), nullptr);
    lane.setProperty(colourId, colour, nullptr);
    lane.appendChild(juce::ValueTree(pointsId), nullptr);
    lanesTree().addChild(lane, index, um);
    return lane;
}

std::pair<std::uint64_t, int> Session::Impl::placeFor(std::uint64_t after) const
{
    // Right after a lane, with the same track; at the end of a track's lanes; returns and the
    // master have none (lanes sit above them): after the last track.
    if (after == 0)
        return {0, -1};
    if (const auto lane = laneTree(after); lane.isValid())
        return {idProperty(lane[afterId]), lanesTree().indexOf(lane) + 1};
    if (track(after) != nullptr)
        return {after, -1};
    const auto all = tracks();
    return {all.empty() ? 0 : all.back()->itemID.getRawID(), -1};
}

bool Session::Impl::assignTo(juce::ValueTree lane, const ParamTarget& target, model::AssignmentRange range,
                             juce::UndoManager* um)
{
    auto p = param(target);
    if (!p)
        return false;
    if (p->kind == Param::Kind::Send && p->param == nullptr)
    {
        // A send is made when first used (ADR 0026); automating it uses it.
        Nested nested(groupDepth);
        if (!owner.setSend(target.channel, p->returnId, std::nullopt, std::nullopt) || !(p = param(target)) ||
            p->param == nullptr)
            return false;
    }
    auto assign = assignTree(target);
    if (assign.isValid() && assign.getParent() != lane)
    {
        assign.getParent().removeChild(assign, um); // one lane per parameter: it moves
        assign = {};
    }
    if (!assign.isValid())
    {
        assign = juce::ValueTree(assignId);
        assign.setProperty(assignChannelId, idVar(target.channel), nullptr);
        assign.setProperty(keyId, juce::String(target.key), nullptr);
        lane.appendChild(assign, um);
    }
    assign.setProperty(fromId, range.from, um);
    assign.setProperty(toId, range.to, um);
    if (p->param->getCurve().bypass.get()) // assigning is a choice to hear it
        p->param->getCurve().bypass = false;
    derive(pointsOf(lane), assign, um);
    return true;
}

void Session::Impl::derive(const juce::ValueTree& lane, juce::UndoManager* um)
{
    const auto points = pointsOf(lane);
    for (auto a : lane)
        if (a.hasType(assignId))
            derive(points, a, um);
}

void Session::Impl::derive(const model::AutomationPoints& lanePoints, const juce::ValueTree& assign, juce::UndoManager* um)
{
    const auto p = param(targetIn(assign));
    if (!p || p->param == nullptr || p->param->isCurrentlyRecording()) // a pass is imported when it ends
        return;
    const auto range = rangeOf(assign);
    writeCurve(p->param->getCurve(),
               model::mapValues(lanePoints, [&](float v) { return p->toEngine(range.toTravel(v)); }), um);
}

int Session::Impl::laneSteps(const juce::ValueTree& lane) const
{
    // Steps only for one full-range stepped assignment (automation.md §5).
    juce::ValueTree only;
    for (auto a : lane)
        if (a.hasType(assignId))
        {
            if (only.isValid())
                return 0;
            only = a;
        }
    if (!only.isValid())
        return 0;
    const auto p = param(targetIn(only));
    return p && p->steps >= 2 && rangeOf(only).full() ? p->steps : 0;
}

std::vector<model::AutomationLaneInfo> Session::Impl::laneInfos() const
{
    std::map<std::uint64_t, std::vector<Param>> params; // by channel, for this snapshot
    std::vector<model::AutomationLaneInfo> out;
    for (auto lane : lanesTree())
    {
        model::AutomationLaneInfo info{.id = idOf(lane),
                                       .name = lane[nameId].toString().toStdString(),
                                       .colour = static_cast<int>(lane[colourId]),
                                       .after = idProperty(lane[afterId]),
                                       .points = pointsOf(lane)};
        for (auto a : lane)
        {
            if (!a.hasType(assignId))
                continue;
            const auto target = targetIn(a);
            auto [it, made] = params.try_emplace(target.channel);
            if (made)
                it->second = paramsOf(target.channel);
            const auto found = std::find_if(it->second.begin(), it->second.end(),
                                            [&](const auto& p) { return p.key == target.key; });
            if (found == it->second.end())
                continue; // its parameter is gone (undo can bring it back)
            const auto* t = channelTrack(target.channel);
            info.assignments.push_back({.channel = target.channel,
                                        .key = target.key,
                                        .range = rangeOf(a),
                                        .on = found->param == nullptr || !found->param->getCurve().bypass.get(),
                                        .name = found->name,
                                        .module = found->module,
                                        .channelName = t != nullptr ? t->getName().toStdString() : std::string("Master"),
                                        .device = found->device,
                                        .paramId = found->paramId,
                                        .steps = found->steps});
        }
        out.push_back(std::move(info));
    }
    // Row order: those at the top, then each track's in track order, then those whose track is gone.
    const auto all = tracks();
    auto rank = [&all](std::uint64_t after) {
        if (after == 0)
            return -1;
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i]->itemID.getRawID() == after)
                return static_cast<int>(i);
        return static_cast<int>(all.size());
    };
    std::stable_sort(out.begin(), out.end(), [&](const auto& a, const auto& b) { return rank(a.after) < rank(b.after); });
    return out;
}

void Session::Impl::loadLanes(bool convert)
{
    auto root = lanesTree();
    if (!root.isValid())
        edit->state.appendChild(juce::ValueTree(automationId), nullptr);
    root = lanesTree();
    if (convert)
    {
        // Format 2 (ADR 0028): a parameter's lane sat under its channel, shown or with points. Each
        // becomes a lane of its own after its track (returns and the master: the last track's).
        static const juce::Identifier shownId{"fdLanes"}, foldedId{"fdLanesFolded"};
        const auto all = tracks();
        for (auto id : channels())
        {
            te::Track* state = channelTrack(id);
            if (state == nullptr)
                state = edit->getMasterTrack();
            if (state == nullptr)
                continue;
            const auto params = paramsOf(id);
            std::vector<const Param*> order;
            for (const auto& key : juce::StringArray::fromLines(state->state[shownId].toString()))
                for (const auto& p : params)
                    if (p.key == key.toStdString() && std::find(order.begin(), order.end(), &p) == order.end())
                        order.push_back(&p);
            for (const auto& p : params)
                if (p.param != nullptr && p.param->getCurve().getNumPoints() > 0 &&
                    std::find(order.begin(), order.end(), &p) == order.end())
                    order.push_back(&p);
            const auto after = track(id) != nullptr ? id : all.empty() ? 0 : all.back()->itemID.getRawID();
            for (const auto* p : order)
            {
                const int colour = colourOf(id); // they sat under their channel
                auto lane = makeLane(after, colour >= 0 ? colour : nextRowColour(), -1, nullptr);
                model::AutomationPoints points;
                if (p->param != nullptr)
                {
                    auto& curve = p->param->getCurve();
                    for (int i = 0; i < curve.getNumPoints(); ++i)
                        points.push_back({.time = te::toUnderlying(curve.getPointTime(i)),
                                          .value = p->toTravel(curve.getPointValue(i)),
                                          .curve = curve.getPointCurve(i)});
                }
                writePoints(lane.getChildWithName(pointsId), points, nullptr);
                juce::ValueTree assign(assignId);
                assign.setProperty(assignChannelId, idVar(id), nullptr);
                assign.setProperty(keyId, juce::String(p->key), nullptr);
                assign.setProperty(fromId, 0.0f, nullptr);
                assign.setProperty(toId, 1.0f, nullptr);
                lane.appendChild(assign, nullptr);
            }
            state->state.removeProperty(shownId, nullptr);
            state->state.removeProperty(foldedId, nullptr);
        }
    }
    // Assignments whose channel or module is gone; a third-party plugin that failed to load keeps
    // its assignments (its parameters come back with it).
    for (auto lane : root)
        for (int i = lane.getNumChildren(); --i >= 0;)
            if (auto a = lane.getChild(i); a.hasType(assignId))
            {
                const auto target = targetIn(a);
                bool keep = param(target).has_value();
                if (!keep && target.key.rfind('m', 0) == 0)
                {
                    const auto device = std::strtoull(target.key.c_str() + 1, nullptr, 10);
                    auto* list = plugins(target.channel);
                    keep = list != nullptr && plugin(device) != nullptr && plugin(device)->getOwnerList() == list &&
                           dynamic_cast<DevicePlugin*>(plugin(device)) == nullptr;
                }
                if (!keep)
                    lane.removeChild(i, nullptr);
            }
    for (auto lane : root)
        derive(lane, nullptr);
    // Lanes from before lanes were numbered (named after what they drove): numbered now, top to
    // bottom, with the project still saved (tracks-and-clips.md §18).
    for (const auto& info : laneInfos())
        if (info.name.empty())
            laneTree(info.id).setProperty(nameId, juce::String(model::nextLaneName(laneNames())), nullptr);
}

// ---- playback, override and recording ------------------------------------------------------

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
    // Each recorded parameter's pass goes into its lane at its punch-out, inside the pass's undo
    // step: punched out here, before the step closes, whichever of tracktion's listeners runs first.
    if (edit != nullptr && !recorded.empty())
        edit->getAutomationRecordManager().punchOut(false);
    for (auto& r : recorded)
        r.param->removeListener(this);
    recorded.clear();
    // tracktion closes the step once edits pause; the pass's name stays on it.
    recordingPass.reset();
    recordFrom.reset();
    liveStarts.clear();
    liveFiles.clear();
    if (auto clips = std::exchange(recordedClips, {}); !clips.empty() && owner.onRecorded)
        owner.onRecorded(clips);
}

void Session::Impl::startRecording(Recorded& r)
{
    const auto lane = assignTree(r.target).getParent();
    r.lane = lane.isValid() ? idOf(lane) : 0;
    r.laneAtStart = lane.isValid() ? pointsOf(lane) : model::AutomationPoints{};
}

void Session::Impl::recordingStatusChanged(te::AutomatableParameter& p)
{
    const auto it = std::find_if(recorded.begin(), recorded.end(), [&p](const auto& r) { return r.param.get() == &p; });
    if (it == recorded.end())
        return;
    if (p.isCurrentlyRecording())
        startRecording(*it); // touched again in the same pass
    else
        importRecording(*it); // punched out: tracktion has written and thinned it
}

void Session::Impl::importRecording(const Recorded& r)
{
    auto* um = &edit->getUndoManager();
    const auto p = param(r.target);
    if (!p || p->param == nullptr)
        return;
    auto& curve = p->param->getCurve();
    model::AutomationPoints written; // engine values
    for (int i = 0; i < curve.getNumPoints(); ++i)
        written.push_back({te::toUnderlying(curve.getPointTime(i)), curve.getPointValue(i), curve.getPointCurve(i)});

    auto assign = assignTree(r.target);
    if (!assign.isValid())
    {
        // A control without a lane gets one, at the end of the tracks (§8).
        if (written.empty())
            return;
        auto lane = makeLane(lastTrack(), nextRowColour(), -1, um);
        assign = juce::ValueTree(assignId);
        assign.setProperty(assignChannelId, idVar(r.target.channel), nullptr);
        assign.setProperty(keyId, juce::String(r.target.key), nullptr);
        assign.setProperty(fromId, 0.0f, nullptr);
        assign.setProperty(toId, 1.0f, nullptr);
        lane.appendChild(assign, um);
        auto points = model::mapValues(written, [&](float v) { return p->toTravel(v); });
        if (const int steps = laneSteps(lane); steps >= 2)
            for (auto& q : points)
                q = {q.time, model::snapToSteps(q.value, steps), model::kStepCurve};
        writePoints(lane.getChildWithName(pointsId), points, um);
        derive(lane, um);
        return;
    }
    auto lane = assign.getParent();
    const auto range = rangeOf(assign);
    if (!range.toLane(0.5f)) // an empty range can't be written back: the lane stays
    {
        derive(lane, um);
        return;
    }
    // Back through the range: what the lane wrote stays the lane's own; moves beyond the range
    // are clipped to it.
    auto toEngine = [&](float v) { return p->toEngine(range.toTravel(v)); };
    auto toLane = [&](float v) { return *range.toLane(p->toTravel(v)); };
    const auto now = pointsOf(lane);
    const auto& start = r.lane == idOf(lane) ? r.laneAtStart : now;
    auto points = model::spliceRecorded(start, model::mapValues(start, toEngine), written, toLane);
    if (now != start)
    {
        // Another control on this lane was recorded in this pass: this one wins where it wrote.
        model::AutomationPoints fresh;
        for (const auto& q : points)
            if (std::find(start.begin(), start.end(), q) == start.end())
                fresh.push_back(q);
        points = fresh.empty() ? now : model::replaceRange(now, fresh.front().time, fresh.back().time, fresh);
    }
    if (const int steps = laneSteps(lane); steps >= 2)
        for (auto& q : points)
            q = {q.time, model::snapToSteps(q.value, steps), model::kStepCurve};
    writePoints(lane.getChildWithName(pointsId), points, um);
    derive(lane, um); // the lane's other assignments follow
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

void Session::Impl::pluginParameterChanged(te::AutomatableParameter& p, bool isFollowingAutomation)
{
    if (isFollowingAutomation)
        return; // played values (also on the audio thread)
    lastTouched = {p.automatableEditElement.itemID.getRawID(), p.paramID};
    if (p.isCurrentlyRecording())
    {
        beginPass("Record automation"); // joins a Record pass if one is open
        if (std::none_of(recorded.begin(), recorded.end(), [&p](const auto& r) { return r.param.get() == &p; }))
            if (const auto target = targetOf(p))
            {
                Recorded r{.param = &p, .target = *target};
                startRecording(r);
                p.addListener(this);
                recorded.push_back(std::move(r));
            }
        dirty = true;
        return;
    }
    // Moving an assigned control turns its assignment off; the control keeps the value (§7).
    if (auto& curve = p.getCurve(); curve.getNumPoints() > 0 && !curve.bypass.get())
        curve.bypass = true;
    dirty = true; // the value itself (its write was taken for a played one)
}

// ---- Session: lanes ------------------------------------------------------------------------

std::vector<Session::ParamChoice> Session::automatableParameters(std::uint64_t channelId) const
{
    std::vector<ParamChoice> out;
    for (const auto& p : impl_->paramsOf(channelId))
    {
        ParamChoice c{.key = p.key, .name = p.name, .module = p.module};
        if (p.param != nullptr)
            c.valueText = paramText({channelId, p.key}, p.toTravel(p.param->getCurrentValue()));
        c.lane = laneOf({channelId, p.key}).value_or(0);
        out.push_back(std::move(c));
    }
    return out;
}

std::uint64_t Session::addAutomationLane(const std::optional<ParamTarget>& first)
{
    // Like a new track: at the end of the tracks.
    return addAutomationLaneAfter(impl_->lastTrack(), first);
}

std::uint64_t Session::addAutomationLaneAfter(std::uint64_t after, const std::optional<ParamTarget>& first)
{
    if (first && !impl_->param(*first))
        return 0;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit("Add automation lane");
    const auto [anchor, index] = impl_->placeFor(after);
    auto lane = impl_->makeLane(anchor, impl_->nextRowColour(), index, um);
    if (first)
    {
        // A parameter on another lane brings its curve, so it sounds the same (full range here).
        model::AutomationPoints points;
        if (const auto old = impl_->assignTree(*first); old.isValid())
        {
            const auto range = Impl::rangeOf(old);
            points = model::mapValues(Impl::pointsOf(old.getParent()), [&](float v) { return range.toTravel(v); });
        }
        Impl::writePoints(lane.getChildWithName(Impl::pointsId), points, um);
        if (!impl_->assignTo(lane, *first, {}, um))
            return 0;
    }
    return Impl::idOf(lane);
}

void Session::removeAutomationLane(std::uint64_t id)
{
    auto lane = impl_->laneTree(id);
    if (!lane.isValid())
        return;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit("Delete automation lane");
    for (auto a : lane)
        if (a.hasType(Impl::assignId))
            if (const auto p = impl_->param(Impl::targetIn(a)); p && p->param != nullptr)
                Impl::writeCurve(p->param->getCurve(), {}, um); // it keeps its value now
    impl_->lanesTree().removeChild(lane, um);
}

bool Session::renameAutomationLane(std::uint64_t id, const std::string& name)
{
    auto lane = impl_->laneTree(id);
    const auto trimmed = juce::String(name).trim();
    if (!lane.isValid() || trimmed.isEmpty() || lane[Impl::nameId].toString() == trimmed)
        return false;
    impl_->beginEdit("Rename automation lane");
    lane.setProperty(Impl::nameId, trimmed, &impl_->edit->getUndoManager());
    return true;
}

bool Session::setLanePoints(std::uint64_t id, const model::AutomationPoints& points,
                            std::optional<std::pair<double, double>> range, const std::string& undoName)
{
    auto lane = impl_->laneTree(id);
    if (!lane.isValid())
        return false;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(juce::String(undoName));
    auto all = range ? model::replaceRange(Impl::pointsOf(lane), std::max(0.0, range->first), range->second, points) : points;
    const int steps = impl_->laneSteps(lane);
    for (auto& p : all)
    {
        p.time = std::max(0.0, p.time);
        p.value = steps >= 2 ? model::snapToSteps(p.value, steps) : std::clamp(p.value, 0.0f, 1.0f);
        p.curve = steps >= 2 ? model::kStepCurve : std::clamp(p.curve, -0.5f, 0.5f);
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    Impl::writePoints(lane.getChildWithName(Impl::pointsId), all, um);
    impl_->derive(lane, um);
    return true;
}

bool Session::assign(std::uint64_t id, const ParamTarget& target, model::AssignmentRange range)
{
    auto lane = impl_->laneTree(id);
    if (!lane.isValid() || !impl_->param(target))
        return false;
    impl_->beginEdit("Assign parameter");
    return impl_->assignTo(lane, target, range, &impl_->edit->getUndoManager());
}

void Session::unassign(const ParamTarget& target)
{
    auto a = impl_->assignTree(target);
    if (!a.isValid())
        return;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit("Remove from automation lane");
    if (const auto p = impl_->param(target); p && p->param != nullptr)
        Impl::writeCurve(p->param->getCurve(), {}, um); // it keeps its value now
    a.getParent().removeChild(a, um);
}

void Session::setAssignmentRange(const ParamTarget& target, model::AssignmentRange range)
{
    auto a = impl_->assignTree(target);
    if (!a.isValid() || Impl::rangeOf(a) == range)
        return;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit("Assignment range");
    a.setProperty(Impl::fromId, std::clamp(range.from, 0.0f, 1.0f), um);
    a.setProperty(Impl::toId, std::clamp(range.to, 0.0f, 1.0f), um);
    impl_->derive(Impl::pointsOf(a.getParent()), a, um);
}

std::optional<std::uint64_t> Session::laneOf(const ParamTarget& target) const
{
    const auto a = impl_->assignTree(target);
    return a.isValid() ? std::optional(Impl::idOf(a.getParent())) : std::nullopt;
}

std::uint64_t Session::automateInNewLane(const ParamTarget& target)
{
    return addAutomationLane(target);
}

void Session::setAssignmentOn(const ParamTarget& target, bool on)
{
    if (auto p = impl_->param(target); p && p->param != nullptr && p->param->getCurve().bypass.get() == on)
        p->param->getCurve().bypass = !on;
}

void Session::setLaneOn(std::uint64_t id, bool on)
{
    for (auto a : impl_->laneTree(id))
        if (a.hasType(Impl::assignId))
            setAssignmentOn(Impl::targetIn(a), on);
}

void Session::setAllLanesOn()
{
    for (auto lane : impl_->lanesTree())
        setLaneOn(Impl::idOf(lane), true);
}

bool Session::anyLaneOff() const
{
    const auto snapshot = impl_->snapshot;
    return std::any_of(snapshot->automationLanes.begin(), snapshot->automationLanes.end(),
                       [](const auto& l) { return !l.points.empty() && l.anyOff(); });
}

std::optional<float> Session::laneValue(std::uint64_t id) const
{
    const auto* lane = impl_->snapshot->automationLane(id);
    if (lane == nullptr)
        return std::nullopt;
    if (!lane->points.empty())
        return model::automationValueAt(lane->points, position());
    if (lane->assignments.empty())
        return std::nullopt;
    const auto& a = lane->assignments.front();
    const auto travel = paramValue({a.channel, a.key});
    return travel ? a.range.toLane(*travel) : std::nullopt;
}

std::string Session::laneText(std::uint64_t id, float value) const
{
    const auto* lane = impl_->snapshot->automationLane(id);
    if (lane == nullptr || lane->assignments.size() != 1)
        return percentText(value);
    const auto& a = lane->assignments.front();
    return paramText({a.channel, a.key}, a.range.toTravel(value));
}

std::optional<float> Session::laneFromText(std::uint64_t id, const std::string& text) const
{
    const auto* lane = impl_->snapshot->automationLane(id);
    if (lane == nullptr || lane->assignments.size() != 1)
        return parsePercent(text);
    const auto& a = lane->assignments.front();
    const auto travel = paramFromText({a.channel, a.key}, text);
    return travel ? a.range.toLane(*travel) : std::nullopt;
}

// ---- Session: parameters -------------------------------------------------------------------

void Session::setParamValue(const ParamTarget& target, float travel)
{
    auto p = impl_->param(target);
    if (!p)
        return;
    if (p->param == nullptr) // a send not made yet: making it sets its level
    {
        if (p->kind == Impl::Param::Kind::Send)
            setSend(target.channel, p->returnId, te::volumeFaderPositionToDB(p->toEngine(travel)), std::nullopt);
        return;
    }
    const float v = p->steps >= 2 ? model::snapToSteps(travel, p->steps) : std::clamp(travel, 0.0f, 1.0f);
    if (std::abs(p->toTravel(p->param->getCurrentValue()) - v) < 1e-6f)
        return;
    impl_->beginEdit(juce::String(p->name));
    p->param->setParameter(p->toEngine(v), juce::sendNotification);
}

std::optional<float> Session::paramValue(const ParamTarget& target) const
{
    auto p = impl_->param(target);
    if (!p)
        return std::nullopt;
    if (p->param == nullptr)
        return 0.0f; // a send not made yet: off
    return p->toTravel(p->param->getCurrentValue());
}

std::string Session::paramText(const ParamTarget& target, float travel) const
{
    auto p = impl_->param(target);
    if (!p)
        return {};
    switch (p->kind)
    {
    case Impl::Param::Kind::Volume:
    case Impl::Param::Kind::Send: return levelText(te::volumeFaderPositionToDB(p->toEngine(travel)));
    case Impl::Param::Kind::Pan: return panText(p->toEngine(travel));
    case Impl::Param::Kind::Module:
    {
        const auto& spec = p->native->device().info().params[p->index];
        return spec.toText(spec.fromNormalized(travel));
    }
    case Impl::Param::Kind::External: return p->param->valueToString(p->toEngine(travel)).toStdString();
    case Impl::Param::Kind::Macro:
        return percentText(travel); // a rack's macro, 0 .. 100 %
    }
    return {};
}

std::optional<double> Session::paramPlain(const ParamTarget& target, float travel) const
{
    auto p = impl_->param(target);
    if (!p)
        return std::nullopt;
    switch (p->kind)
    {
    case Impl::Param::Kind::Volume:
    case Impl::Param::Kind::Send: return te::volumeFaderPositionToDB(p->toEngine(travel));
    case Impl::Param::Kind::Pan: return p->toEngine(travel);
    case Impl::Param::Kind::Module:
    {
        const auto& spec = p->native->device().info().params[p->index];
        return spec.fromNormalized(travel);
    }
    case Impl::Param::Kind::External: return std::clamp(travel, 0.0f, 1.0f); // normalised (mcp.md)
    case Impl::Param::Kind::Macro:
        return 100.0 * std::clamp(travel, 0.0f, 1.0f);
    }
    return std::nullopt;
}

std::optional<float> Session::paramFromPlain(const ParamTarget& target, double plain) const
{
    auto p = impl_->param(target);
    if (!p)
        return std::nullopt;
    const auto v = static_cast<float>(plain);
    switch (p->kind)
    {
    case Impl::Param::Kind::Volume: return p->toTravel(te::decibelsToVolumeFaderPosition(std::min(v, 6.0f)));
    case Impl::Param::Kind::Send: return p->toTravel(te::decibelsToVolumeFaderPosition(std::min(v, 0.0f)));
    case Impl::Param::Kind::Pan: return p->toTravel(std::clamp(v, -1.0f, 1.0f));
    case Impl::Param::Kind::Module:
    {
        const auto& spec = p->native->device().info().params[p->index];
        return spec.toNormalized(v);
    }
    case Impl::Param::Kind::External: return std::clamp(v, 0.0f, 1.0f);
    case Impl::Param::Kind::Macro:
        return std::clamp(v / 100.0f, 0.0f, 1.0f);
    }
    return std::nullopt;
}

std::optional<float> Session::paramFromText(const ParamTarget& target, const std::string& text) const
{
    auto p = impl_->param(target);
    if (!p)
        return std::nullopt;
    switch (p->kind)
    {
    case Impl::Param::Kind::Volume:
    case Impl::Param::Kind::Send:
        if (const auto db = parseLevel(text))
            return paramFromPlain(target, *db);
        return std::nullopt;
    case Impl::Param::Kind::Pan:
        if (const auto pan = parsePan(text))
            return paramFromPlain(target, *pan);
        return std::nullopt;
    case Impl::Param::Kind::Module:
    {
        const auto& spec = p->native->device().info().params[p->index];
        if (const auto v = spec.fromText(text))
            return spec.toNormalized(*v);
        return std::nullopt;
    }
    case Impl::Param::Kind::External: return p->toTravel(p->param->stringToValue(juce::String(text)));
    case Impl::Param::Kind::Macro:
        return parsePercent(text);
    }
    return std::nullopt;
}

std::pair<std::string, std::string> Session::paramName(const ParamTarget& target) const
{
    const auto p = impl_->param(target);
    return p ? std::pair{p->name, p->module} : std::pair<std::string, std::string>{};
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

std::optional<Session::ParamTarget> Session::lastTouchedParameter() const
{
    if (!impl_->lastTouched)
        return std::nullopt;
    const auto [pluginId, paramId] = *impl_->lastTouched;
    for (auto id : impl_->channels())
        for (const auto& p : impl_->paramsOf(id))
            if (p.param != nullptr && p.param->automatableEditElement.itemID.getRawID() == pluginId &&
                p.param->paramID == paramId)
                return ParamTarget{id, p.key};
    return std::nullopt;
}

std::optional<Session::ParamTarget> Session::moduleParameter(std::uint64_t deviceId, const std::string& paramId) const
{
    const auto snapshot = impl_->snapshot;
    auto find = [&](const model::TrackInfo& t) -> std::optional<ParamTarget>
    {
        if (model::findDevice(t.devices, deviceId) != nullptr) // modules in racks too
            return ParamTarget{t.id, "m" + std::to_string(deviceId) + "/" + paramId};
        return std::nullopt;
    };
    for (const auto* list : {&snapshot->tracks, &snapshot->returns})
        for (const auto& t : *list)
            if (auto target = find(t))
                return target;
    return find(snapshot->master);
}

void Session::revealLane(std::uint64_t lane)
{
    if (onLaneRevealed && impl_->laneTree(lane).isValid())
        onLaneRevealed(lane);
}

Session::ParamGesture::ParamGesture(Session& session, const ParamTarget& target, const std::string& name)
    : session_(session), target_(target)
{
    auto& impl = session_.impl();
    if (impl.groupDepth++ == 0)
        impl.edit->getUndoManager().beginNewTransaction(juce::String(name));
    if (auto p = impl.param(target_); p && p->param != nullptr)
    {
        p->param->parameterChangeGestureBegin(); // a touch, while recording
        touching_ = true;
    }
}

Session::ParamGesture::~ParamGesture()
{
    auto& impl = session_.impl();
    if (touching_)
        if (auto p = impl.param(target_); p && p->param != nullptr)
            p->param->parameterChangeGestureEnd();
    --impl.groupDepth;
}
} // namespace fd::engine
