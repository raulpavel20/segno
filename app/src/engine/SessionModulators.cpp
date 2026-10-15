// Modulators in the session (docs/design/modulators.md, ADR 0061, ADR 0062): where they sit in a
// track's chain, their routes, their rolls, their panels.
#include "engine/Modulator.h"
#include "engine/SessionImpl.h"
#include "engine/SpecParameter.h"

#include <set>

namespace fd::engine
{
using model::ModulatorKind;

namespace
{
/// The mixer's own plugins (the volume and pan, the meter, sends, a return's input): not modules.
bool isMixer(const te::Plugin* p)
{
    return dynamic_cast<const te::VolumeAndPanPlugin*>(p) != nullptr ||
           dynamic_cast<const te::LevelMeterPlugin*>(p) != nullptr ||
           dynamic_cast<const te::AuxSendPlugin*>(p) != nullptr ||
           dynamic_cast<const te::AuxReturnPlugin*>(p) != nullptr;
}

/// Moves `items` (children of one tree) into the order given, over the places they hold now; the
/// tree's other children stay where they are.
void reorder(const std::vector<juce::ValueTree>& items, juce::UndoManager* um)
{
    if (items.empty())
        return;
    auto parent = items.front().getParent();
    std::vector<int> places;
    for (const auto& v : items)
        places.push_back(parent.indexOf(v));
    std::sort(places.begin(), places.end());
    for (size_t i = 0; i < items.size(); ++i)
        if (const int from = parent.indexOf(items[i]); from != places[i])
            parent.moveChild(from, places[i], um);
}
} // namespace

// ---- the chain ----------------------------------------------------------------------------------

std::uint64_t Session::Impl::ChainItem::id() const
{
    return plugin != nullptr ? plugin->itemID.getRawID() : modulator->itemID.getRawID();
}

std::vector<Session::Impl::ChainItem> Session::Impl::chainItems(te::Track& track)
{
    std::vector<Modulator*> modulators;
    if (auto* list = track.getModifierList())
        for (auto* m : list->getModifiers())
            if (auto* mod = dynamic_cast<Modulator*>(m))
                modulators.push_back(mod);
    std::vector<bool> placed(modulators.size(), false);
    std::vector<ChainItem> out;
    for (auto* p : track.pluginList)
    {
        // Each modulator just before the module it's placed before; those before the mixer's
        // plugins (the fader) come after the last module, below.
        if (!isMixer(p))
            for (size_t i = 0; i < modulators.size(); ++i)
                if (!placed[i] && modulators[i]->place() == p->itemID)
                {
                    out.push_back({nullptr, modulators[i]});
                    placed[i] = true;
                }
        if (!isMixer(p))
            out.push_back({p, nullptr});
    }
    for (size_t i = 0; i < modulators.size(); ++i)
        if (!placed[i])
            out.push_back({nullptr, modulators[i]});
    return out;
}

void Session::Impl::arrangeChain(te::Track& track, const std::vector<ChainItem>& order)
{
    auto* um = &edit->getUndoManager();
    // The modules, in the order given, over the places modules hold in the track's plugins.
    std::vector<juce::ValueTree> modules, modulators;
    for (const auto& item : order)
        (item.plugin != nullptr ? modules : modulators)
            .push_back(item.plugin != nullptr ? item.plugin->state : item.modulator->state);
    reorder(modules, um);
    // Each modulator placed before the module after it; after the last, before the fader (so a
    // Follower there hears the chain, not the fader's level), or after everything without one.
    te::EditItemID end;
    for (auto* p : track.pluginList)
        if (dynamic_cast<te::VolumeAndPanPlugin*>(p) != nullptr)
        {
            end = p->itemID;
            break;
        }
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (order[i].modulator == nullptr)
            continue;
        te::EditItemID before = end;
        for (size_t j = i + 1; j < order.size(); ++j)
            if (order[j].plugin != nullptr)
            {
                before = order[j].plugin->itemID;
                break;
            }
        if (order[i].modulator->place() != before)
            order[i].modulator->setPlace(before, um);
    }
    // And the modulators in that order among themselves (the order they're processed in).
    reorder(modulators, um);
}

void Session::Impl::placeInChain(te::Track& track, const ChainItem& item, int index)
{
    auto order = chainItems(track);
    order.erase(std::remove_if(order.begin(), order.end(),
                               [&](const ChainItem& other) { return other.id() == item.id(); }),
                order.end());
    const auto at = index < 0 ? order.size() : std::min(order.size(), static_cast<size_t>(index));
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), item);
    arrangeChain(track, order);
}

int Session::Impl::chainIndexOf(te::Track& track, std::uint64_t id)
{
    const auto order = chainItems(track);
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i].id() == id)
            return static_cast<int>(i);
    return -1;
}

Modulator* Session::Impl::modulator(std::uint64_t id) const
{
    for (auto* m : modulators())
        if (m->itemID.getRawID() == id)
            return m;
    return nullptr;
}

std::vector<Modulator*> Session::Impl::modulators() const
{
    std::vector<Modulator*> out;
    for (auto* t : te::getAudioTracks(*edit))
        if (auto* list = t->getModifierList())
            for (auto* m : list->getModifiers())
                if (auto* mod = dynamic_cast<Modulator*>(m))
                    out.push_back(mod);
    return out;
}

te::AudioTrack* Session::Impl::trackOf(const Modulator& modulator) const
{
    for (auto* t : te::getAudioTracks(*edit))
        if (auto* list = t->getModifierList())
            for (auto* m : list->getModifiers())
                if (m == &modulator)
                    return t;
    return nullptr;
}

// ---- routes -------------------------------------------------------------------------------------

std::vector<Session::Impl::Route> Session::Impl::routesOf(const Modulator& modulator) const
{
    std::vector<Route> out;
    auto* track = trackOf(modulator);
    if (track == nullptr)
        return out;
    for (auto& p : paramsOf(track->itemID.getRawID()))
        if (p.param != nullptr)
            for (auto a : p.param->getAssignments())
                if (a->isForModifierSource(modulator))
                    out.push_back({p, a});
    return out;
}

bool Session::Impl::wouldLoop(const Modulator& from, const te::AutomatableParameter& param) const
{
    // A parameter of a modulator that moves `from`'s, however far round: `from` would move itself.
    std::vector<const Modulator*> moving{&from};
    std::set<const Modulator*> seen;
    while (!moving.empty())
    {
        const auto* m = moving.back();
        moving.pop_back();
        if (!seen.insert(m).second)
            continue;
        if (&param.automatableEditElement == m)
            return true;
        // Whatever moves this modulator's parameters is upstream of it.
        for (auto* other : modulators())
            for (auto* p : const_cast<Modulator*>(m)->getAutomatableParameters())
                for (auto a : p->getAssignments())
                    if (a->isForModifierSource(*other))
                        moving.push_back(other);
    }
    return false;
}

void Session::Impl::refreshModulatorRoutes()
{
    for (auto* m : modulators())
    {
        const auto routes = routesOf(*m);
        int copies = 1;
        for (const auto& r : routes)
            copies =
                std::max(copies, static_cast<int>(r.assignment->state.getProperty(Modulator::copyId, 0)) + 1);
        m->setRouteCount(std::max(copies, static_cast<int>(routes.size())));
        for (const auto& r : routes)
            if (auto* a = dynamic_cast<Modulator::Assignment*>(r.assignment.get()))
                a->copy.store(static_cast<int>(r.assignment->state.getProperty(Modulator::copyId, 0)));
    }
}

std::optional<Session::Impl::Moved> Session::Impl::movedBy(te::AutomatableParameter& param) const
{
    Moved out;
    float down = 0.0f, up = 0.0f;
    for (auto a : param.getAssignments())
    {
        const auto* mine = dynamic_cast<const Modulator::Assignment*>(a);
        if (mine == nullptr)
            continue;
        auto* m = modulator(mine->modulator.getRawID());
        if (m == nullptr)
            continue;
        // The reach of its route: around the value both ways, else one way (the amount's sign).
        const float depth = m->plain(model::depthParam(m->kind())) * 0.01f;
        const float reach = a->value.get() * depth;
        const auto both = model::bothWaysParam(m->kind());
        if (both && m->plain(*both) >= 0.5f)
        {
            down -= std::abs(reach) * 0.5f;
            up += std::abs(reach) * 0.5f;
        }
        else if (reach >= 0.0f)
            up += reach;
        else
            down += reach;
        if (std::find(out.by.begin(), out.by.end(), m) == out.by.end())
            out.by.push_back(m);
    }
    if (out.by.empty())
        return std::nullopt;
    const float base = param.getCurrentBaseValue();
    const auto range = param.getValueRange();
    const float length = range.getLength() > 0.0f ? range.getLength() : 1.0f;
    const float v = (base - range.getStart()) / length;
    out.from = std::clamp(v + down, 0.0f, 1.0f);
    out.to = std::clamp(v + up, 0.0f, 1.0f);
    return out;
}

void Session::Impl::startModulatorPasses()
{
    for (auto* m : modulators())
        if (!m->kept())
            m->startPass(static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64()));
}

std::shared_ptr<const model::ModulatorInfo> Session::Impl::modulatorInfo(const Modulator& modulator) const
{
    auto info = std::make_shared<model::ModulatorInfo>();
    info->kind = modulator.kind();
    info->kept = modulator.kept();
    auto* track = trackOf(modulator);
    for (const auto& r : routesOf(modulator))
        info->routes.push_back(
            {.channel = track != nullptr ? track->itemID.getRawID() : 0,
             .key = r.param.key,
             .module = r.param.module,
             .param = r.param.name,
             .amount = r.assignment->value.get(),
             .copy = static_cast<int>(r.assignment->state.getProperty(Modulator::copyId, 0))});
    return info;
}

std::optional<ui::ParamBinding::Modulated> Session::Impl::modulatedLook(te::AutomatableParameter& param) const
{
    const auto moved = movedBy(param);
    if (!moved)
        return std::nullopt;
    juce::StringArray by;
    bool shown = modulatorPointer.modulator == 0;
    for (auto* m : moved->by)
    {
        by.add(m->getName());
        shown = shown || m->itemID.getRawID() == modulatorPointer.modulator;
    }
    const auto range = param.getValueRange();
    const float length = range.getLength() > 0.0f ? range.getLength() : 1.0f;
    return ui::ParamBinding::Modulated{moved->from, moved->to,
                                       (param.getCurrentValue() - range.getStart()) / length,
                                       by.joinIntoString(", "), !shown};
}

ui::ParamBinding::MacroDrag Session::Impl::modulatorDragLook(te::AutomatableParameter& param) const
{
    if (!modulatorPointer.dragging)
        return {};
    if (modulatorPointer.modulator == 0) // a new one from the browser: anything on its track
    {
        const auto target = targetOf(param);
        if (!target || target->channel != modulatorPointer.channel)
            return {};
        return {true, modulatorPointer.over == target};
    }
    auto* m = modulator(modulatorPointer.modulator);
    auto* track = m != nullptr ? trackOf(*m) : nullptr;
    if (track == nullptr)
        return {};
    const auto target = targetOf(param);
    if (!target || target->channel != track->itemID.getRawID() || wouldLoop(*m, param))
        return {};
    return {true, modulatorPointer.over == target};
}

// ---- the public calls -----------------------------------------------------------------------------

void Session::setModulatorPointer(const ModulatorPointer& pointer)
{
    impl_->modulatorPointer = pointer;
    if (pointer.modulator != 0)
        impl_->lastModulator = pointer.modulator;
}

const Session::ModulatorPointer& Session::modulatorPointer() const
{
    return impl_->modulatorPointer;
}

bool Session::canRoute(std::uint64_t modulatorId, const ParamTarget& target) const
{
    auto* m = impl_->modulator(modulatorId);
    auto* track = m != nullptr ? impl_->trackOf(*m) : nullptr;
    if (track == nullptr || target.channel != track->itemID.getRawID())
        return false;
    const auto p = impl_->param(target);
    return p && p->param != nullptr && !impl_->wouldLoop(*m, *p->param);
}

std::optional<std::pair<std::uint64_t, float>> Session::routeToDrag(const ParamTarget& target) const
{
    const auto p = impl_->param(target);
    if (!p || p->param == nullptr)
        return std::nullopt;
    std::optional<std::pair<std::uint64_t, float>> first;
    for (auto a : p->param->getAssignments())
        for (auto* m : impl_->modulators())
            if (a->isForModifierSource(*m))
            {
                const std::pair route{m->itemID.getRawID(), a->value.get()};
                if (route.first == impl_->lastModulator)
                    return route;
                if (!first)
                    first = route;
            }
    return first;
}

bool RouteDrag::begin(Session& session, const std::optional<Session::ParamTarget>& target)
{
    end();
    const auto route = target ? session.routeToDrag(*target) : std::nullopt;
    if (!route)
        return false;
    modulator_ = route->first;
    amount_ = route->second;
    target_ = *target;
    group_.emplace(session, "Route amount");
    return true;
}

void RouteDrag::drag(Session& session, float delta)
{
    if (modulator_ == 0)
        return;
    amount_ = std::clamp(amount_ + delta, -2.0f, 2.0f);
    session.setRouteAmount(modulator_, target_, amount_);
}

void RouteDrag::end()
{
    group_.reset();
    modulator_ = 0;
}

std::uint64_t Session::addModulator(std::uint64_t channel, ModulatorKind kind, int index)
{
    auto* track = impl_->channelTrack(channel); // tracks and returns; not the master, for now
    if (track == nullptr)
        return 0;
    impl_->beginEdit("Add " + juce::String(std::string(model::modulatorInfo(kind).name)));
    auto added = track->getModifierList()->insertModifier(Modulator::createState(kind), -1, nullptr);
    auto* m = dynamic_cast<Modulator*>(added.get());
    if (m == nullptr)
        return 0;
    // Where it was asked for, everything else staying where it was.
    impl_->placeInChain(*track, Impl::ChainItem{nullptr, m}, index);
    // From your default, if you set one (ADR 0044), in the same step.
    if (const auto start = PresetController::startingPoint(m->info(), presetLibrary()))
        impl_->applyModulatorState(*m, start->first, start->second);
    impl_->refreshModulatorRoutes();
    return m->itemID.getRawID();
}

bool Session::routeModulator(std::uint64_t modulatorId, const ParamTarget& target, float amount)
{
    auto* m = impl_->modulator(modulatorId);
    auto* track = m != nullptr ? impl_->trackOf(*m) : nullptr;
    if (track == nullptr || target.channel != track->itemID.getRawID())
        return false; // only what's on its own track (§5)
    const auto p = impl_->param(target);
    if (!p || p->param == nullptr || impl_->wouldLoop(*m, *p->param))
        return false;
    amount = std::clamp(amount, -2.0f, 2.0f);
    for (auto a : p->param->getAssignments())
        if (a->isForModifierSource(*m))
            return setRouteAmount(modulatorId, target, amount);
    impl_->beginEdit("Route " + m->getName());
    // Its copy (Per Target): the first not taken by another of its routes.
    std::set<int> taken;
    for (const auto& r : impl_->routesOf(*m))
        taken.insert(static_cast<int>(r.assignment->state.getProperty(Modulator::copyId, 0)));
    int copy = 0;
    while (taken.contains(copy))
        ++copy;
    auto assignment = p->param->addModifier(*m, amount, 0.0f, 0.0f);
    if (assignment == nullptr)
        return false;
    assignment->state.setProperty(Modulator::copyId, copy, &impl_->edit->getUndoManager());
    impl_->refreshModulatorRoutes();
    return true;
}

bool Session::setRouteAmount(std::uint64_t modulatorId, const ParamTarget& target, float amount)
{
    auto* m = impl_->modulator(modulatorId);
    const auto p = impl_->param(target);
    if (m == nullptr || !p || p->param == nullptr)
        return false;
    for (auto a : p->param->getAssignments())
        if (a->isForModifierSource(*m))
        {
            impl_->beginEdit("Route amount");
            a->value = std::clamp(amount, -2.0f, 2.0f);
            return true;
        }
    return false;
}

bool Session::removeRoute(std::uint64_t modulatorId, const ParamTarget& target)
{
    auto* m = impl_->modulator(modulatorId);
    const auto p = impl_->param(target);
    if (m == nullptr || !p || p->param == nullptr)
        return false;
    for (auto a : p->param->getAssignments())
        if (a->isForModifierSource(*m))
        {
            impl_->beginEdit("Remove route");
            p->param->removeModifier(*a);
            impl_->refreshModulatorRoutes();
            return true;
        }
    return false;
}

void Session::removeRoutes(std::uint64_t modulatorId)
{
    auto* m = impl_->modulator(modulatorId);
    if (m == nullptr)
        return;
    const auto routes = impl_->routesOf(*m);
    if (routes.empty())
        return;
    impl_->beginEdit("Remove routes");
    for (const auto& r : routes)
        r.param.param->removeModifier(*r.assignment);
    impl_->refreshModulatorRoutes();
}

bool Session::keepRoll(std::uint64_t modulatorId, bool keep)
{
    auto* m = impl_->modulator(modulatorId);
    if (m == nullptr || !model::hasKeep(m->kind()) || m->kept() == keep)
        return false;
    impl_->beginEdit(keep ? "Keep as played" : "Let it roll");
    m->setKept(keep, &impl_->edit->getUndoManager());
    return true;
}

void Session::keepAllRolls(bool keep)
{
    std::vector<Modulator*> changing;
    for (auto* m : impl_->modulators())
        if (model::hasKeep(m->kind()) && m->kept() != keep)
            changing.push_back(m);
    if (changing.empty())
        return;
    impl_->beginEdit(keep ? "Keep all as played" : "Let all roll");
    for (auto* m : changing)
        m->setKept(keep, &impl_->edit->getUndoManager());
}

// ---- the panel ----------------------------------------------------------------------------------

namespace
{
/// A modulator's parameters for its panel: edits through its parameters (automation recording
/// and undo work), as a module's (DevicePluginBinding). A knob shows its own value; what other
/// modulators do to it shows as its ring.
class ModulatorBinding final : public ui::ParamBinding
{
public:
    ModulatorBinding(Session& session, Modulator& m) : session_(session), m_(m) {}

    float getPlain(size_t i) const override { return m_.plain(i); }
    void setPlain(size_t i, float plain) override
    {
        m_.parameter(i).setParameter(spec(i).toNormalized(plain), juce::sendNotification);
    }
    void beginGesture(size_t i) override { m_.parameter(i).parameterChangeGestureBegin(); }
    void endGesture(size_t i) override { m_.parameter(i).parameterChangeGestureEnd(); }
    void applyExtraState(const nlohmann::json&) override {}
    void beginEdit(const juce::String& name) override { session_.impl().beginEdit(name); }
    [[nodiscard]] ui::AutomationState automationState(size_t i) const override
    {
        auto& curve = m_.parameter(i).getCurve();
        if (curve.getNumPoints() == 0)
            return ui::AutomationState::None;
        return curve.bypass.get() ? ui::AutomationState::Off : ui::AutomationState::On;
    }
    [[nodiscard]] bool canAutomate(size_t) const override { return true; }
    [[nodiscard]] std::optional<Modulated> modulated(size_t i) const override
    {
        return session_.impl().modulatedLook(m_.parameter(i));
    }
    [[nodiscard]] MacroDrag modulatorDrag(size_t i) const override
    {
        return session_.impl().modulatorDragLook(m_.parameter(i));
    }
    bool beginModulatedDrag(size_t i) override { return ring_.begin(session_, target(i)); }
    void dragModulated(size_t, float delta) override { ring_.drag(session_, delta); }
    void endModulatedDrag(size_t) override { ring_.end(); }
    void addAutomationItems(size_t i, juce::PopupMenu& menu) override
    {
        if (const auto t = target(i); t && session_.automationMenu)
            session_.automationMenu(*t, menu);
    }
    void addModulatorItems(size_t i, juce::PopupMenu& menu, juce::PopupMenu& with) override
    {
        if (const auto t = target(i); t && session_.modulatorMenu)
            session_.modulatorMenu(*t, menu, with);
    }

private:
    [[nodiscard]] const ParamSpec& spec(size_t i) const { return m_.info().params[i]; }
    [[nodiscard]] std::optional<Session::ParamTarget> target(size_t i) const
    {
        return session_.moduleParameter(m_.itemID.getRawID(), std::string(spec(i).id));
    }
    Session& session_;
    Modulator& m_;
    RouteDrag ring_;
};

/// What the panel shows that isn't a parameter, from the engine's modulator.
class ModulatorHostImpl final : public model::ModulatorHost
{
public:
    ModulatorHostImpl(Session& session, Modulator& m) : session_(session), m_(m) {}
    int routeCount() const override { return static_cast<int>(session_.impl().routesOf(m_).size()); }
    double position() const override { return m_.position(); }
    std::vector<double> playing() const override { return m_.playing(); }
    std::uint64_t roll() const override { return m_.roll(); }
    bool kept() const override { return m_.kept(); }
    void setKept(bool keep) override { session_.keepRoll(m_.itemID.getRawID(), keep); }
    std::vector<float> heard() const override { return m_.heard(3.0); }
    std::vector<float> stepValues() const override { return m_.stepValues(); }
    std::vector<float> stepChances() const override { return m_.stepChances(); }
    void setStep(int step, float value, float chance) override
    {
        session_.impl().beginEdit("Edit step");
        m_.setStep(step, value, chance, &session_.impl().edit->getUndoManager());
    }

private:
    Session& session_;
    Modulator& m_;
};

/// The Follower's Listen (§12.4): Here, for now; other tracks come with its sidechain.
class ListenHere final : public ui::SidechainRouting
{
public:
    std::vector<std::string> sources() const override { return {"Here"}; }
    int selected() const override { return 0; }
    void select(int) override {}
};

class ModulatorHandleImpl final : public ModulatorHandle
{
public:
    ModulatorHandleImpl(Session& session, Modulator& m)
        : keepAlive_(&m), modulator_(m), binding_(session, m), host_(session, m),
          presets_(session.presetHost(m.itemID.getRawID())),
          context_{m.info(),
                   binding_,
                   nullptr,
                   nullptr,
                   m.kind() == ModulatorKind::Follower ? &listen_ : nullptr,
                   presets_.get()}
    {
    }
    ui::DeviceUiContext& context() override { return context_; }
    model::ModulatorHost& host() override { return host_; }
    model::ModulatorKind kind() const override { return modulator_.kind(); }

private:
    te::Modifier::Ptr keepAlive_; // keeps it alive while its panel exists
    Modulator& modulator_;
    ModulatorBinding binding_;
    ModulatorHostImpl host_;
    ListenHere listen_;
    std::unique_ptr<PresetHost> presets_; // its header's presets (modulators.md §14)
    ui::DeviceUiContext context_;
};
} // namespace

std::unique_ptr<ModulatorHandle> Session::openModulator(std::uint64_t modulatorId)
{
    if (auto* m = impl_->modulator(modulatorId))
        return std::make_unique<ModulatorHandleImpl>(*this, *m);
    return nullptr;
}
} // namespace fd::engine
