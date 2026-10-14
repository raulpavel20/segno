// Racks (M3 P2, docs/design/racks.md, ADR 0056): modules of modules on tracktion's racks. A
// rack's chains are Segno's, kept on its plugins' places in the rack (SessionImpl.h, "racks");
// the connections between them follow from those (wireRack), in the same undo step as the edit.

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/Racks.h"

#include <fd/device/Registry.h>
#include <set>
#include <tuple>

namespace fd::engine
{
namespace
{
constexpr std::string_view kMacroDescription =
    "A macro: moves the parameters mapped to it, each over its own range. Drag its name onto a knob in the "
    "rack to map it.";

// The rack's parameters: its eight macros, then Morph (racks.md §18.3).
const std::array<ParamSpec, model::RackInfo::macroCount + 1> kMacroSpecs = []
{
    static constexpr std::array<std::string_view, model::RackInfo::macroCount> ids{
        "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8"};
    static constexpr std::array<std::string_view, model::RackInfo::macroCount> names{
        "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8"};
    std::array<ParamSpec, model::RackInfo::macroCount + 1> specs;
    for (size_t i = 0; i < ids.size(); ++i)
        specs[i] = {.id = ids[i],
                    .name = names[i],
                    .shortName = names[i],
                    .min = 0.0f,
                    .max = 100.0f,
                    .unit = Unit::Percent,
                    .decimals = 0,
                    .description = kMacroDescription};
    specs[model::RackInfo::morphParam] = {
        .id = "morph",
        .name = "Morph",
        .shortName = "Morph",
        .min = 0.0f,
        .max = 100.0f,
        .unit = Unit::Percent,
        .decimals = 0,
        .description =
            "Morph: while it's on, takes the macros from where snapshot A has them to where B has them."};
    return specs;
}();

juce::String defaultMacroName(int macro)
{
    return "Macro " + juce::String(macro + 1);
}

float volumeDb(const te::VolumeAndPanPlugin* v)
{
    return v != nullptr ? std::max(model::minusInfinityDb, te::volumeFaderPositionToDB(v->volume.get()))
                        : 0.0f;
}

/// The macro a modifier assignment comes from (its index in the rack), or -1.
int macroOf(const juce::ValueTree& assignment, const juce::ReferenceCountedArray<te::MacroParameter>& macros)
{
    if (!assignment.hasType(te::IDs::MACRO))
        return -1;
    const auto source = assignment[te::IDs::source].toString();
    for (int i = 0; i < macros.size(); ++i)
        if (macros[i]->paramID == source)
            return i;
    return -1;
}

/// A parameter's place in its range (0 .. 1), and back.
float normalised(const te::AutomatableParameter& p, float value)
{
    const auto range = p.getValueRange();
    return range.getLength() > 0.0f ? std::clamp((value - range.getStart()) / range.getLength(), 0.0f, 1.0f)
                                    : 0.0f;
}

float engineValue(const te::AutomatableParameter& p, float normalisedValue)
{
    const auto range = p.getValueRange();
    return range.getStart() + std::clamp(normalisedValue, 0.0f, 1.0f) * range.getLength();
}

/// Whether a module's parameter can be set by a macro: one an automation lane can drive.
bool mappable(te::Plugin& module, const te::AutomatableParameter& param)
{
    if (auto* native = dynamic_cast<DevicePlugin*>(&module))
    {
        const auto index = native->device().params().indexOf(param.paramID.toStdString());
        if (!index)
            return false;
        const auto& spec = native->device().info().params[*index];
        return spec.hasFlag(ParamFlags::Automatable) && !spec.hasFlag(ParamFlags::Hidden) &&
               !spec.hasFlag(ParamFlags::ChangesLatency);
    }
    return param.paramID != "dry level" && param.paramID != "wet level";
}
} // namespace

// ---- the rack's shape ---------------------------------------------------------------------

te::RackType* Session::Impl::rackTypeOf(const te::Plugin* plugin)
{
    auto* instance = dynamic_cast<const te::RackInstance*>(plugin);
    return instance != nullptr ? instance->type.get() : nullptr;
}

std::vector<Session::Impl::RackChain> Session::Impl::rackChains(const te::RackType& rack)
{
    std::map<int, RackChain> byNumber;
    for (auto* p : rack.getPlugins())
    {
        const auto place = p->state.getParent();
        auto& chain = byNumber[static_cast<int>(place.getProperty(chainId, 0))];
        if (static_cast<bool>(place[chainMixerId]))
        {
            chain.mixer = place;
            chain.volume = dynamic_cast<te::VolumeAndPanPlugin*>(p);
        }
        else
            chain.modules.push_back(p);
    }
    std::vector<RackChain> chains;
    for (auto& [number, chain] : byNumber)
        chains.push_back(std::move(chain));
    return chains;
}

std::vector<te::Plugin*> Session::Impl::rackModules(const te::RackType& rack)
{
    std::vector<te::Plugin*> out;
    for (const auto& chain : rackChains(rack))
        out.insert(out.end(), chain.modules.begin(), chain.modules.end());
    return out;
}

void Session::Impl::wireRack(te::RackType& rack, juce::UndoManager* um)
{
    juce::ignoreUnused(um); // the rack's edits go through the edit's undo manager
    using Connection = std::tuple<te::EditItemID, int, te::EditItemID, int>;
    std::set<Connection> wanted;
    const auto chains = rackChains(rack);
    const bool soloed =
        std::any_of(chains.begin(), chains.end(), [](const RackChain& c) { return c.solo(); });
    for (const auto& c : chains)
    {
        std::vector<te::Plugin*> all = c.modules;
        if (c.volume != nullptr)
            all.push_back(c.volume);
        if (all.empty())
            continue;
        // MIDI (pin 0) and audio (1, 2) from the rack's input, module to module, to its output.
        for (int pin = 0; pin <= 2; ++pin)
            wanted.insert({te::EditItemID(), pin, all.front()->itemID, pin});
        for (size_t i = 0; i + 1 < all.size(); ++i)
            for (int pin = 0; pin <= 2; ++pin)
                wanted.insert({all[i]->itemID, pin, all[i + 1]->itemID, pin});
        if (c.on() && (!soloed || c.solo()))
            for (int pin = 1; pin <= 2; ++pin)
                wanted.insert({all.back()->itemID, pin, te::EditItemID(), pin});
    }
    // Only what changed, so an unchanged chain keeps playing as it was.
    std::vector<Connection> unwanted;
    for (const auto* rc : rack.getConnections())
    {
        const Connection c{rc->sourceID.get(), rc->sourcePin.get(), rc->destID.get(), rc->destPin.get()};
        if (wanted.erase(c) == 0)
            unwanted.push_back(c);
    }
    for (const auto& [src, srcPin, dst, dstPin] : unwanted)
        rack.removeConnection(src, srcPin, dst, dstPin);
    for (const auto& [src, srcPin, dst, dstPin] : wanted)
        rack.addConnection(src, srcPin, dst, dstPin);
}

te::RackInstance* Session::Impl::rackInstance(std::uint64_t id) const
{
    return dynamic_cast<te::RackInstance*>(plugin(id));
}

te::RackInstance* Session::Impl::rackHolding(const te::Plugin& module) const
{
    if (!module.isInRack())
        return nullptr;
    const auto look = [&](te::PluginList& list) -> te::RackInstance*
    {
        for (auto* p : list)
            if (auto* instance = dynamic_cast<te::RackInstance*>(p))
                if (instance->type != nullptr &&
                    instance->type->getPlugins().contains(const_cast<te::Plugin*>(&module)))
                    return instance;
        return nullptr;
    };
    for (auto* t : te::getAudioTracks(*edit))
        if (auto* found = look(t->pluginList))
            return found;
    return look(edit->getMasterPluginList());
}

te::AudioTrack* Session::Impl::trackOf(const te::Plugin& module) const
{
    if (auto* rack = rackHolding(module))
        return dynamic_cast<te::AudioTrack*>(rack->getOwnerTrack());
    return dynamic_cast<te::AudioTrack*>(module.getOwnerTrack());
}

bool Session::Impl::playsNotes(te::Plugin& plugin)
{
    if (auto* rack = rackTypeOf(&plugin))
    {
        for (const auto& chain : rackChains(*rack))
            if (!chain.modules.empty() && chain.modules.front()->isSynth())
                return true;
        return false;
    }
    return plugin.isSynth();
}

te::RackType::Ptr Session::Impl::makeRack(const juce::String& name)
{
    auto rack = edit->getRackList().addNewRack();
    if (rack == nullptr)
        return {};
    rack->rackName = name;
    auto& macros = rack->getMacroParameterListForWriting();
    // At 0 %, so a knob mapped From where it is stays there (tracktion starts them half way).
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
        if (auto* macro = macros.createMacroParameter())
        {
            macro->macroName = defaultMacroName(i);
            macro->setParameter(0.0f, juce::sendNotificationSync);
        }
    addChain(*rack);
    return rack;
}

void Session::Impl::addChain(te::RackType& rack)
{
    auto* um = &edit->getUndoManager();
    const auto chains = rackChains(rack);
    auto mixer = edit->getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {});
    if (mixer == nullptr || !rack.addPlugin(mixer, {0.9f, 0.5f}, false))
        return;
    if (auto* v = dynamic_cast<te::VolumeAndPanPlugin*>(mixer.get()))
        v->state.setProperty(te::IDs::panLaw, static_cast<int>(te::PanLawBalance), um); // as every channel's
    auto place = mixer->state.getParent();
    place.setProperty(chainId, static_cast<int>(chains.size()), um);
    place.setProperty(chainMixerId, true, um);
}

bool Session::Impl::putInRack(te::RackType& rack, const te::Plugin::Ptr& module, int chain, int index)
{
    if (module == nullptr || !te::RackType::isPluginAllowed(module))
        return false; // a rack in a rack
    const auto chains = rackChains(rack);
    if (chain < 0 || chain >= static_cast<int>(chains.size()))
        return false;
    auto* um = &edit->getUndoManager();
    const te::Plugin::Ptr keep = module;
    if (keep->state.getParent().isValid())
        keep->removeFromParent(); // from a chain, or from a rack's (even this one's)
    // Where: before the module now at `index`, or before the chain's mixer.
    const auto now = rackChains(rack);
    const auto& c = now[static_cast<size_t>(chain)];
    const auto before = index >= 0 && index < static_cast<int>(c.modules.size())
                            ? c.modules[static_cast<size_t>(index)]->state.getParent()
                            : c.mixer;
    if (!rack.addPlugin(keep, {0.5f, 0.5f}, false))
        return false;
    auto place = keep->state.getParent();
    place.setProperty(chainId, chain, um);
    if (before.isValid())
        rack.state.moveChild(rack.state.indexOf(place), rack.state.indexOf(before), um);
    return true;
}

void Session::Impl::dropLanesOf(std::uint64_t module)
{
    const auto prefix = juce::String("m" + std::to_string(module) + "/");
    for (auto lane : lanesTree())
        for (int i = lane.getNumChildren(); --i >= 0;)
            if (lane.getChild(i).hasType(assignId) && lane.getChild(i)[keyId].toString().startsWith(prefix))
                lane.removeChild(i, &edit->getUndoManager());
}

void Session::Impl::unmapFrom(te::Plugin& module, const te::RackType& rack)
{
    const auto macros = rack.getMacroParameters();
    for (auto* param : module.getAutomatableParameters())
        for (auto assignment : param->getAssignments())
            if (macroOf(assignment->state, macros) >= 0)
            {
                const float now = param->getCurrentValue();
                param->removeModifier(*assignment);
                param->setParameter(now, juce::sendNotificationSync);
            }
}

void Session::Impl::copyRacksIn(juce::ValueTree trackCopy, te::EditItemID::IDMap& ids)
{
    std::function<void(juce::ValueTree)> visit = [&](juce::ValueTree v)
    {
        if (v.hasType(te::IDs::PLUGIN) && v[te::IDs::type].toString() == te::RackInstance::xmlTypeName)
            if (auto rack =
                    edit->getRackList().getRackTypeForID(te::EditItemID::fromProperty(v, te::IDs::rackType)))
            {
                rack->flushStateToValueTree();
                auto copy = rack->state.createCopy();
                te::EditItemID::remapIDs(copy, nullptr, *edit, &ids); // its macros' mappings follow them
                if (auto added = edit->getRackList().addRackTypeFrom(copy))
                    v.setProperty(te::IDs::rackType, added->itemID.toVar(), nullptr);
            }
        for (auto child : v)
            visit(child);
    };
    visit(trackCopy);
}

void Session::Impl::removeRack(te::RackInstance& instance)
{
    const te::RackType::Ptr type = instance.type;
    dropLanesOf(instance.itemID.getRawID());
    if (type != nullptr)
        for (auto* m : rackModules(*type))
            dropLanesOf(m->itemID.getRawID());
    instance.removeFromParent();
    if (type != nullptr)
        edit->getRackList().removeRackType(type);
}

std::shared_ptr<const model::RackInfo>
Session::Impl::rackInfo(te::RackInstance& instance,
                        const std::function<model::DeviceSlot(te::Plugin&)>& slotOf) const
{
    auto info = std::make_shared<model::RackInfo>();
    auto* type = instance.type.get();
    if (type == nullptr)
        return info;
    const auto chains = rackChains(*type);
    for (size_t i = 0; i < chains.size(); ++i)
    {
        const auto& c = chains[i];
        model::RackChain chain;
        chain.name = c.mixer[chainNameId].toString().toStdString();
        if (chain.name.empty())
            chain.name = "Chain " + std::to_string(i + 1);
        chain.on = c.on();
        chain.solo = c.solo();
        chain.volumeDb = volumeDb(c.volume);
        chain.pan = c.volume != nullptr ? c.volume->pan.get() : 0.0f;
        for (auto* m : c.modules)
            chain.devices.push_back(slotOf(*m));
        info->chains.push_back(std::move(chain));
    }
    info->shownChain = std::clamp(static_cast<int>(instance.state.getProperty(shownChainId, 0)), 0,
                                  std::max(0, static_cast<int>(info->chains.size()) - 1));
    const auto macros = type->getMacroParameters();
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
    {
        auto& macro = info->macros[static_cast<size_t>(i)];
        macro.name = macros[i]->macroName.get().toStdString();
        if (macro.name.empty())
            macro.name = defaultMacroName(i).toStdString();
        macro.value = std::clamp(macros[i]->value.get(), 0.0f, 1.0f);
    }
    // Each module's parameters set by a macro: From is the parameter's own value, To From plus
    // the assignment's value, Curve twice the engine's (racks.md §12).
    for (auto* m : rackModules(*type))
        for (auto* param : m->getAutomatableParameters())
            for (auto assignment : param->getAssignments())
                if (const int macro = macroOf(assignment->state, macros);
                    macro >= 0 && macro < model::RackInfo::macroCount)
                {
                    model::MacroMappingInfo mapping;
                    mapping.module = m->itemID.getRawID();
                    mapping.param = param->paramID.toStdString();
                    mapping.paramName = param->getParameterName().toStdString();
                    if (auto* native = dynamic_cast<DevicePlugin*>(m))
                        if (const auto index = native->device().params().indexOf(mapping.param))
                            mapping.paramName = std::string(native->device().info().params[*index].name);
                    const float base = param->getCurrentBaseValue();
                    mapping.from = normalised(*param, base);
                    mapping.to = normalised(*param, base + assignment->value.get());
                    mapping.curve = std::clamp(assignment->curve.get() * 2.0f, -1.0f, 1.0f);
                    info->macros[static_cast<size_t>(macro)].mappings.push_back(std::move(mapping));
                }
    return info;
}

te::MacroParameter* Session::Impl::macroFor(const juce::ValueTree& macroState) const
{
    for (int i = 0; i < edit->getRackList().size(); ++i)
        if (auto rack = edit->getRackList().getRackType(i))
            for (auto* m : rack->getMacroParameters())
                if (m->state == macroState)
                    return m;
    return nullptr;
}

// ---- the rack's macros, as a module's parameters -------------------------------------------

namespace
{
/// A rack's macros as the panel kit sees parameters: "macro1" .. "macro8", 0 .. 100 %.
class RackMacroBinding final : public ui::ParamBinding
{
public:
    RackMacroBinding(Session& session, te::RackInstance& rack) : session_(session), rack_(&rack) {}

    float getPlain(size_t i) const override
    {
        auto* m = macro(i);
        return m != nullptr ? 100.0f * m->getCurrentValue() : 0.0f;
    }
    void setPlain(size_t i, float plain) override
    {
        if (auto* m = macro(i))
            m->setParameter(std::clamp(plain / 100.0f, 0.0f, 1.0f), juce::sendNotification);
    }
    void beginGesture(size_t i) override
    {
        if (auto* m = macro(i))
            m->parameterChangeGestureBegin();
    }
    void endGesture(size_t i) override
    {
        if (auto* m = macro(i))
            m->parameterChangeGestureEnd();
    }
    void applyExtraState(const nlohmann::json&) override {}
    void beginEdit(const juce::String& name) override { session_.impl().beginEdit(name); }

    [[nodiscard]] ui::AutomationState automationState(size_t i) const override
    {
        auto* m = macro(i);
        if (m == nullptr || m->getCurve().getNumPoints() == 0)
            return ui::AutomationState::None;
        return m->getCurve().bypass.get() ? ui::AutomationState::Off : ui::AutomationState::On;
    }
    [[nodiscard]] bool canAutomate(size_t) const override { return true; }
    void addAutomationItems(size_t i, juce::PopupMenu& menu) override
    {
        if (const auto t = session_.moduleParameter(rack_->itemID.getRawID(), std::string(kMacroSpecs[i].id));
            t && session_.automationMenu)
            session_.automationMenu(*t, menu);
    }

private:
    te::MacroParameter* macro(size_t i) const
    {
        auto* rack = Session::Impl::rackTypeOf(rack_.get());
        if (rack == nullptr)
            return nullptr;
        const auto macros = rack->getMacroParameters();
        return static_cast<int>(i) < macros.size() ? macros[static_cast<int>(i)].get() : nullptr;
    }

    Session& session_;
    te::Plugin::Ptr rack_; // keeps the rack alive while its panel exists
};

class RackHandle final : public DeviceHandle
{
public:
    RackHandle(Session& session, te::RackInstance& rack)
        : binding_(session, rack), context_{Session::rackMacros(), binding_}
    {
    }
    ui::DeviceUiContext& context() override { return context_; }
    std::string_view deviceId() const override { return "rack"; }

private:
    RackMacroBinding binding_;
    ui::DeviceUiContext context_;
};

std::uint64_t idOf(const te::Plugin& p)
{
    return p.itemID.getRawID();
}
} // namespace

const DeviceInfo& Session::rackMacros()
{
    static const DeviceInfo info{.id = "rack", .name = "Rack", .category = "Racks", .params = kMacroSpecs};
    return info;
}

std::unique_ptr<DeviceHandle> Session::openRack(std::uint64_t rack)
{
    if (auto* instance = impl_->rackInstance(rack))
        return std::make_unique<RackHandle>(*this, *instance);
    return nullptr;
}

void Session::setMacroPointer(const MacroPointer& pointer)
{
    impl_->macroPointer = pointer;
}

const Session::MacroPointer& Session::macroPointer() const
{
    return impl_->macroPointer;
}

// ---- making and unmaking racks ------------------------------------------------------------

namespace
{
/// "Rack", then "Rack 2" ... : a name no other rack in the project has.
juce::String newRackName(te::Edit& edit)
{
    std::set<juce::String> taken;
    for (int i = 0; i < edit.getRackList().size(); ++i)
        if (auto r = edit.getRackList().getRackType(i))
            taken.insert(r->rackName.get());
    juce::String name = "Rack";
    for (int n = 2; taken.contains(name); ++n)
        name = "Rack " + juce::String(n);
    return name;
}
} // namespace

std::uint64_t Session::groupIntoRack(const std::vector<std::uint64_t>& modules)
{
    if (modules.empty())
        return 0;
    // Every one in one channel's own chain (not in a rack), and none a rack.
    te::PluginList* list = nullptr;
    std::vector<te::Plugin::Ptr> picked;
    for (auto id : modules)
    {
        auto* p = impl_->plugin(id);
        if (p == nullptr || p->isInRack() || Impl::rackTypeOf(p) != nullptr || p->getOwnerList() == nullptr)
            return 0;
        if (list != nullptr && p->getOwnerList() != list)
            return 0;
        list = p->getOwnerList();
        picked.push_back(p);
    }
    // In their order in the chain; the rack goes where the first was.
    const auto order = Impl::chain(*list);
    std::sort(picked.begin(), picked.end(),
              [&](const auto& a, const auto& b)
              {
                  return std::find(order.begin(), order.end(), a.get()) <
                         std::find(order.begin(), order.end(), b.get());
              });
    const int at = list->indexOf(picked.front().get());

    impl_->beginEdit("Group into rack");
    auto rack = impl_->makeRack(newRackName(*impl_->edit));
    if (rack == nullptr)
        return 0;
    for (const auto& p : picked)
        impl_->putInRack(*rack, p, 0, -1);
    Impl::wireRack(*rack, &impl_->edit->getUndoManager());
    auto instance = impl_->edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(*rack));
    if (instance == nullptr)
        return 0;
    list->insertPlugin(instance, std::min(at, list->size()), nullptr);
    return idOf(*instance);
}

std::vector<std::uint64_t> Session::ungroupRack(std::uint64_t rackId)
{
    auto* instance = impl_->rackInstance(rackId);
    auto* list = instance != nullptr ? instance->getOwnerList() : nullptr;
    if (instance == nullptr || instance->type == nullptr || list == nullptr)
        return {};
    const te::Plugin::Ptr keepInstance = instance;
    const te::RackType::Ptr type = instance->type;
    const auto chains = Impl::rackChains(*type);
    const int shown = std::clamp(static_cast<int>(instance->state.getProperty(Impl::shownChainId, 0)), 0,
                                 static_cast<int>(chains.size()) - 1);
    impl_->beginEdit("Ungroup rack");
    // The shown chain's modules, out of the rack and into the chain where it was, in order; their
    // macros' mappings go (each parameter keeps its value now).
    std::vector<te::Plugin::Ptr> out(chains[static_cast<size_t>(shown)].modules.begin(),
                                     chains[static_cast<size_t>(shown)].modules.end());
    int at = list->indexOf(instance);
    std::vector<std::uint64_t> placed;
    for (const auto& p : out)
    {
        impl_->unmapFrom(*p, *type);
        p->removeFromParent();
        list->insertPlugin(p, at++, nullptr);
        placed.push_back(idOf(*p));
    }
    impl_->removeRack(*instance);
    return placed;
}

std::uint64_t Session::addRack(std::uint64_t channel, int index)
{
    auto* list = impl_->plugins(channel);
    if (list == nullptr)
        return 0;
    impl_->beginEdit("Add rack");
    auto rack = impl_->makeRack(newRackName(*impl_->edit));
    if (rack == nullptr)
        return 0;
    Impl::wireRack(*rack, &impl_->edit->getUndoManager());
    auto instance = impl_->edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(*rack));
    if (instance == nullptr)
        return 0;
    // Before the module at `index`, or before the mixer's own plugins at the end.
    const auto devices = Impl::chain(*list);
    int at = list->size();
    if (index >= 0 && index < static_cast<int>(devices.size()))
        at = list->indexOf(devices[static_cast<size_t>(index)]);
    else
        for (int i = 0; i < list->size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*>((*list)[i]) != nullptr ||
                dynamic_cast<te::LevelMeterPlugin*>((*list)[i]) != nullptr ||
                dynamic_cast<te::AuxSendPlugin*>((*list)[i]) != nullptr)
            {
                at = i;
                break;
            }
    list->insertPlugin(instance, at, nullptr);
    return idOf(*instance);
}

// ---- modules in racks ---------------------------------------------------------------------

std::optional<Session::ChainPlace> Session::placeOf(std::uint64_t deviceId) const
{
    auto* p = impl_->plugin(deviceId);
    if (p == nullptr)
        return std::nullopt;
    const auto channelOf = [this](te::Plugin& plugin) -> std::uint64_t
    {
        if (auto* t = dynamic_cast<te::AudioTrack*>(plugin.getOwnerTrack()))
            return t->itemID.getRawID();
        return impl_->masterId();
    };
    if (auto* rack = impl_->rackHolding(*p))
    {
        const auto chains = Impl::rackChains(*rack->type);
        for (size_t c = 0; c < chains.size(); ++c)
            for (size_t i = 0; i < chains[c].modules.size(); ++i)
                if (chains[c].modules[i] == p)
                    return ChainPlace{channelOf(*rack), idOf(*rack), static_cast<int>(c),
                                      static_cast<int>(i)};
        return std::nullopt;
    }
    auto* list = p->getOwnerList();
    if (list == nullptr)
        return std::nullopt;
    const auto order = Impl::chain(*list);
    const auto it = std::find(order.begin(), order.end(), p);
    return ChainPlace{channelOf(*p), 0, 0, static_cast<int>(it - order.begin())};
}

std::uint64_t Session::addDevice(const ChainPlace& place, const std::string& key)
{
    if (place.rack == 0)
        return addDevice(place.channel, key, place.index);
    auto* instance = impl_->rackInstance(place.rack);
    if (instance == nullptr || instance->type == nullptr)
        return 0;
    auto& rack = *instance->type;
    if (place.chain < 0 || place.chain >= static_cast<int>(Impl::rackChains(rack).size()))
        return 0;
    // Made in the track's chain, as any module (your default applies), then moved into the rack's,
    // in the same step.
    auto* list = instance->getOwnerList();
    if (list == nullptr)
        return 0;
    EditGroup group(*this, "Add module");
    const auto id = addDevice(dynamic_cast<te::AudioTrack*>(instance->getOwnerTrack()) != nullptr
                                  ? instance->getOwnerTrack()->itemID.getRawID()
                                  : impl_->masterId(),
                              key, -1);
    if (id == 0)
        return 0;
    if (!moveDevice(id, place))
    {
        removeDevice(id); // not allowed in a rack
        return 0;
    }
    return id;
}

bool Session::moveDevice(std::uint64_t deviceId, const ChainPlace& place)
{
    const te::Plugin::Ptr p = impl_->plugin(deviceId);
    const auto from = placeOf(deviceId);
    if (p == nullptr || !from)
        return false;
    te::RackInstance* source = impl_->rackHolding(*p);
    te::RackInstance* target = place.rack != 0 ? impl_->rackInstance(place.rack) : nullptr;
    if (place.rack != 0 &&
        (target == nullptr || target->type == nullptr || Impl::rackTypeOf(p.get()) != nullptr))
        return false; // no such rack, or a rack into a rack
    // Within its own channel only.
    auto* channel = impl_->trackOf(*p);
    if (target != nullptr && impl_->trackOf(*target) != channel)
        return false;
    if (place.rack == 0 && place.channel != 0 && place.channel != from->channel)
        return false;

    if (target == nullptr && source == nullptr)
    {
        // Along the channel's chain: where it ends up, once it has left its place.
        auto* list = p->getOwnerList();
        const int count = list != nullptr ? static_cast<int>(Impl::chain(*list).size()) : 0;
        int index = place.index < 0 ? count : place.index;
        if (index > from->index)
            --index;
        moveDevice(deviceId, index);
        return true;
    }
    impl_->beginEdit("Move module");
    auto* um = &impl_->edit->getUndoManager();
    if (source != nullptr && (target == nullptr || target->type != source->type))
        impl_->unmapFrom(*p, *source->type); // out of its rack: its macros no longer reach it
    if (target != nullptr)
    {
        const int chains = static_cast<int>(Impl::rackChains(*target->type).size());
        if (place.chain < 0 || place.chain >= chains)
            return false;
        // Along its own chain, the place counts as if it had left first.
        int index = place.index;
        if (source == target && from->chain == place.chain && index > from->index)
            --index;
        if (!impl_->putInRack(*target->type, p, place.chain, index))
            return false;
        Impl::wireRack(*target->type, um);
    }
    else
    {
        // Out of the rack, into its channel's chain at `index` (before the module there).
        auto* list = source->getOwnerList();
        if (list == nullptr)
            return false;
        const auto order = Impl::chain(*list); // the rack among them
        const int at = place.index >= 0 && place.index < static_cast<int>(order.size())
                           ? list->indexOf(order[static_cast<size_t>(place.index)])
                           : list->indexOf(order.back()) + 1;
        p->removeFromParent();
        list->insertPlugin(p, at, nullptr);
    }
    if (source != nullptr && source != target)
        Impl::wireRack(*source->type, um);
    return true;
}

// ---- chains -------------------------------------------------------------------------------

int Session::addRackChain(std::uint64_t rackId)
{
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr)
        return -1;
    const int count = static_cast<int>(Impl::rackChains(*instance->type).size());
    if (count >= maxRackChains)
        return -1;
    impl_->beginEdit("Add chain");
    impl_->addChain(*instance->type);
    Impl::wireRack(*instance->type, &impl_->edit->getUndoManager());
    instance->state.setProperty(Impl::shownChainId, count, nullptr); // the new one shows
    return count;
}

bool Session::removeRackChain(std::uint64_t rackId, int chain)
{
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr)
        return false;
    auto& rack = *instance->type;
    const auto chains = Impl::rackChains(rack);
    if (chains.size() <= 1 || chain < 0 || chain >= static_cast<int>(chains.size()))
        return false;
    impl_->beginEdit("Remove chain");
    auto* um = &impl_->edit->getUndoManager();
    const auto& c = chains[static_cast<size_t>(chain)];
    for (auto* m : c.modules)
    {
        impl_->dropLanesOf(idOf(*m));
        m->removeFromParent();
    }
    if (c.volume != nullptr)
        c.volume->removeFromParent();
    // The chains after it move up one.
    for (auto* p : rack.getPlugins())
    {
        auto place = p->state.getParent();
        if (const int n = place.getProperty(Impl::chainId, 0); n > chain)
            place.setProperty(Impl::chainId, n - 1, um);
    }
    Impl::wireRack(rack, um);
    const int shown = instance->state.getProperty(Impl::shownChainId, 0);
    if (shown >= chain && shown > 0)
        instance->state.setProperty(Impl::shownChainId, shown - 1, nullptr);
    return true;
}

bool Session::setRackChain(std::uint64_t rackId, int chain, const RackChainEdit& e)
{
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr)
        return false;
    auto& rack = *instance->type;
    const auto chains = Impl::rackChains(rack);
    if (chain < 0 || chain >= static_cast<int>(chains.size()))
        return false;
    const auto& c = chains[static_cast<size_t>(chain)];
    auto mixer = c.mixer;
    impl_->beginEdit(e.name       ? "Rename chain"
                     : e.on       ? "Chain on/off"
                     : e.solo     ? "Solo chain"
                     : e.volumeDb ? "Chain volume"
                                  : "Chain pan");
    auto* um = &impl_->edit->getUndoManager();
    // Solo rewires the rack, so it's an undo step too (the wiring's edits must all be in the
    // history); it's never kept in a rack's file.
    if (e.solo && *e.solo != c.solo())
    {
        mixer.setProperty(Impl::chainSoloId, *e.solo, um);
        Impl::wireRack(rack, um);
    }
    if (e.name)
        mixer.setProperty(Impl::chainNameId, juce::String::fromUTF8(e.name->c_str()), um);
    if (e.on && *e.on != c.on())
    {
        mixer.setProperty(Impl::chainOnId, *e.on, um);
        Impl::wireRack(rack, um);
    }
    if (e.volumeDb && c.volume != nullptr)
    {
        const float db = std::min(6.0f, *e.volumeDb);
        if (db <= model::minusInfinityDb)
            c.volume->setSliderPos(0.0f);
        else
            c.volume->setVolumeDb(db);
    }
    if (e.pan && c.volume != nullptr)
        c.volume->setPan(std::clamp(*e.pan, -1.0f, 1.0f));
    return true;
}

void Session::showRackChain(std::uint64_t rackId, int chain)
{
    if (auto* instance = impl_->rackInstance(rackId); instance != nullptr && instance->type != nullptr)
    {
        const int count = static_cast<int>(Impl::rackChains(*instance->type).size());
        instance->state.setProperty(Impl::shownChainId, std::clamp(chain, 0, std::max(0, count - 1)),
                                    nullptr);
    }
}

bool Session::renameRack(std::uint64_t rackId, const std::string& name)
{
    auto* instance = impl_->rackInstance(rackId);
    const auto trimmed = juce::String::fromUTF8(name.c_str()).trim();
    if (instance == nullptr || instance->type == nullptr || trimmed.isEmpty())
        return false;
    impl_->beginEdit("Rename rack");
    instance->type->state.setProperty(te::IDs::name, trimmed, &impl_->edit->getUndoManager());
    return true;
}

// ---- macros -------------------------------------------------------------------------------

bool Session::setMacro(std::uint64_t rackId, int macro, std::optional<float> value,
                       std::optional<std::string> name)
{
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr)
        return false;
    const auto macros = instance->type->getMacroParameters();
    if (macro < 0 || macro >= macros.size())
        return false;
    auto& m = *macros[macro];
    impl_->beginEdit(name ? "Rename macro" : "Macro");
    if (name)
    {
        const auto trimmed = juce::String::fromUTF8(name->c_str()).trim();
        m.macroName = trimmed.isNotEmpty() ? trimmed : defaultMacroName(macro);
    }
    if (value)
        m.setParameter(std::clamp(*value, 0.0f, 1.0f), juce::sendNotificationSync);
    return true;
}

bool Session::mapMacro(std::uint64_t rackId, int macro, std::uint64_t moduleId, const std::string& paramId,
                       float from, float to, float curve)
{
    auto* instance = impl_->rackInstance(rackId);
    auto* module = impl_->plugin(moduleId);
    if (instance == nullptr || instance->type == nullptr || module == nullptr ||
        impl_->rackHolding(*module) != instance)
        return false;
    const auto macros = instance->type->getMacroParameters();
    if (macro < 0 || macro >= macros.size())
        return false;
    auto param = module->getAutomatableParameterByID(juce::String(paramId));
    if (param == nullptr || !mappable(*module, *param))
        return false;
    from = std::clamp(from, 0.0f, 1.0f);
    to = std::clamp(to, 0.0f, 1.0f);
    const float bend = std::clamp(curve, -1.0f, 1.0f) * 0.5f; // the engine's curve: half ours
    const float span = engineValue(*param, to) - engineValue(*param, from);
    impl_->beginEdit("Map to macro");
    // Edited in place while it's this macro's, so playback goes on as it was; another macro's
    // gives it up (two macros never set one parameter).
    te::AutomatableParameter::ModifierAssignment::Ptr existing;
    for (auto assignment : param->getAssignments())
    {
        const int owner = macroOf(assignment->state, macros);
        if (owner == macro)
            existing = assignment;
        else if (assignment->state.hasType(te::IDs::MACRO))
            param->removeModifier(*assignment);
    }
    param->setParameter(engineValue(*param, from), juce::sendNotificationSync);
    if (existing == nullptr)
        existing = param->addModifier(*macros[macro], span, 0.0f, bend);
    if (existing == nullptr)
        return false;
    existing->value = span;
    existing->curve = bend; // tracktion leaves out a curve of 0.5 when it adds one: written here
    return true;
}

bool Session::unmapMacro(std::uint64_t moduleId, const std::string& paramId)
{
    auto* module = impl_->plugin(moduleId);
    auto* instance = module != nullptr ? impl_->rackHolding(*module) : nullptr;
    if (instance == nullptr || instance->type == nullptr)
        return false;
    auto param = module->getAutomatableParameterByID(juce::String(paramId));
    if (param == nullptr)
        return false;
    const auto macros = instance->type->getMacroParameters();
    for (auto assignment : param->getAssignments())
        if (macroOf(assignment->state, macros) >= 0)
        {
            impl_->beginEdit("Unmap");
            const float now = param->getCurrentValue();
            param->removeModifier(*assignment);
            param->setParameter(now, juce::sendNotificationSync);
            return true;
        }
    return false;
}
} // namespace fd::engine

// ---- rack files (racks.md §7) ----------------------------------------------------------------

namespace fd::engine
{
namespace
{
constexpr int kRackFormat = 1;
} // namespace

juce::File Session::racksFolder() const
{
    return segnoFolder().getChildFile("Racks");
}

bool Session::saveRack(std::uint64_t rackId, const juce::File& file, std::string* error)
{
    const auto fail = [error](std::string why)
    {
        if (error != nullptr)
            *error = std::move(why);
        return false;
    };
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr)
        return fail("It isn't a rack.");
    updateSnapshotNow();
    const model::DeviceSlot* slot = nullptr;
    for (const auto* list : {&impl_->snapshot->tracks, &impl_->snapshot->returns})
        for (const auto& t : *list)
            if (const auto* d = model::findDevice(t.devices, rackId))
                slot = d;
    if (slot == nullptr)
        slot = model::findDevice(impl_->snapshot->master.devices, rackId);
    if (slot == nullptr || slot->rack == nullptr)
        return fail("It isn't a rack.");
    const auto& info = *slot->rack;
    const auto chains = Impl::rackChains(*instance->type);

    nlohmann::json json{{"segnoRack", kRackFormat},
                        {"name", slot->name},
                        {"kind", slot->instrument ? "instrument" : "effect"},
                        {"tags", nlohmann::json::array()},
                        {"description", ""},
                        {"author", ""}};
    // Modules are referred to by their chain and place in it: ids are the project's.
    std::map<std::uint64_t, std::pair<int, int>> places;
    nlohmann::json chainsJson = nlohmann::json::array();
    for (size_t c = 0; c < chains.size() && c < info.chains.size(); ++c)
    {
        const auto& chain = info.chains[c];
        nlohmann::json modules = nlohmann::json::array();
        for (size_t i = 0; i < chains[c].modules.size(); ++i)
        {
            auto* p = chains[c].modules[i];
            places[p->itemID.getRawID()] = {static_cast<int>(c), static_cast<int>(i)};
            nlohmann::json m{{"name", p->getName().toStdString()}, {"on", p->isEnabled()}};
            if (auto* native = dynamic_cast<DevicePlugin*>(p))
            {
                const auto host = presetHost(p->itemID.getRawID());
                if (host == nullptr)
                    return fail("A module's state couldn't be read.");
                // As a preset holds it (ADR 0044): what's the sound, not the session.
                m["device"] = std::string(native->device().info().id);
                m["state"] = toJson(presetState(native->device().info(), host->state()));
            }
            else if (auto* external = dynamic_cast<te::ExternalPlugin*>(p))
            {
                external->flushPluginStateToValueTree();
                m["plugin"] = external->getIdentifierString().toStdString();
                m["state"] = p->state.createCopy().toXmlString().toStdString();
            }
            else
                continue;
            modules.push_back(std::move(m));
        }
        chainsJson.push_back({{"name", chain.name},
                              {"on", chain.on},
                              {"volumeDb", chain.volumeDb},
                              {"pan", chain.pan},
                              {"modules", std::move(modules)}});
    }
    json["chains"] = std::move(chainsJson);
    nlohmann::json macros = nlohmann::json::array();
    for (const auto& macro : info.macros)
    {
        nlohmann::json mappings = nlohmann::json::array();
        for (const auto& m : macro.mappings)
            if (const auto it = places.find(m.module); it != places.end())
                mappings.push_back({{"chain", it->second.first},
                                    {"module", it->second.second},
                                    {"param", m.param},
                                    {"from", m.from},
                                    {"to", m.to},
                                    {"curve", m.curve}});
        macros.push_back({{"name", macro.name}, {"value", macro.value}, {"mappings", std::move(mappings)}});
    }
    json["macros"] = std::move(macros);

    (void)file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(juce::String::fromUTF8(json.dump(2).c_str()) + "\n"))
        return fail("Couldn't write " + file.getFullPathName().toStdString() + ".");
    if (impl_->browserStarted)
        refreshBrowser(file.getParentDirectory());
    return true;
}

std::optional<Session::RackFileInfo> Session::readRackFile(const juce::File& file)
{
    const auto json = nlohmann::json::parse(file.loadFileAsString().toStdString(), nullptr, false);
    if (!json.is_object() || json.value("segnoRack", 0) < 1)
        return std::nullopt;
    RackFileInfo info;
    info.name = json.value("name", file.getFileNameWithoutExtension().toStdString());
    info.instrument = json.value("kind", "effect") == "instrument";
    if (const auto it = json.find("tags"); it != json.end() && it->is_array())
        for (const auto& t : *it)
            if (t.is_string())
                info.tags.push_back(t.get<std::string>());
    info.description = json.value("description", "");
    info.author = json.value("author", "");
    return info;
}

Session::LoadedRack Session::loadRack(const juce::File& file, const ChainPlace& place)
{
    LoadedRack loaded;
    const auto json = nlohmann::json::parse(file.loadFileAsString().toStdString(), nullptr, false);
    if (!json.is_object() || json.value("segnoRack", 0) < 1)
    {
        loaded.error = file.getFileName().toStdString() + " isn't a rack.";
        return loaded;
    }
    if (json.value("segnoRack", 0) > kRackFormat)
    {
        loaded.error = file.getFileName().toStdString() + " is from a newer Segno.";
        return loaded;
    }
    auto* list = impl_->plugins(place.channel);
    if (list == nullptr)
    {
        loaded.error = "There's no such track.";
        return loaded;
    }
    const auto chainsJson = json.value("chains", nlohmann::json::array());
    // Ours newer than this Segno: refused whole, by name (racks.md §7).
    for (const auto& c : chainsJson)
        for (const auto& m : c.value("modules", nlohmann::json::array()))
            if (m.contains("device"))
            {
                const auto* factory = fd::findDevice(m["device"].get<std::string>());
                const auto state = stateFromJson(m.value("state", nlohmann::json::object()));
                if (factory == nullptr || (state && state->stateVersion > factory->info().stateVersion))
                {
                    loaded.error = "It holds a " + m.value("name", m["device"].get<std::string>()) +
                                   " newer than this Segno: update Segno to load it.";
                    return loaded;
                }
            }

    const auto name =
        juce::String::fromUTF8(json.value("name", file.getFileNameWithoutExtension().toStdString()).c_str());
    EditGroup group(*this, "Add rack " + name.toStdString());
    const auto rackId = addRack(place.channel, place.index);
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr)
    {
        loaded.error = "The rack couldn't be made.";
        return loaded;
    }
    auto& rack = *instance->type;
    auto* um = &impl_->edit->getUndoManager();
    rack.state.setProperty(te::IDs::name, name, um);
    std::map<std::pair<int, int>, std::uint64_t> ids; // (chain, place) -> the module made
    for (size_t c = 0; c < chainsJson.size(); ++c)
    {
        const auto& chainJson = chainsJson[c];
        if (c > 0)
            impl_->addChain(rack);
        int placed = 0;
        const auto modules = chainJson.value("modules", nlohmann::json::array());
        for (size_t i = 0; i < modules.size(); ++i)
        {
            const auto& m = modules[i];
            te::Plugin::Ptr plugin;
            if (m.contains("device"))
            {
                plugin = impl_->edit->getPluginCache().createNewPlugin(
                    DevicePlugin::typeNameFor(m["device"].get<std::string>()), {});
                if (auto* native = dynamic_cast<DevicePlugin*>(plugin.get()))
                    if (auto state = stateFromJson(m.value("state", nlohmann::json::object())))
                    {
                        if (state->stateVersion < native->device().info().stateVersion)
                            native->device().migrateState(*state);
                        impl_->applyModuleState(*native, *state, {});
                    }
            }
            else if (m.contains("plugin"))
            {
                const auto known =
                    impl_->engine.getPluginManager().knownPluginList.getTypeForIdentifierString(
                        juce::String(m["plugin"].get<std::string>()));
                auto state = juce::ValueTree::fromXml(juce::String::fromUTF8(m.value("state", "").c_str()));
                if (known != nullptr && state.isValid())
                {
                    te::EditItemID::remapIDs(state, nullptr, *impl_->edit);
                    plugin = impl_->edit->getPluginCache().getOrCreatePluginFor(state);
                }
                if (plugin == nullptr)
                {
                    loaded.missing.push_back(m.value("name", m["plugin"].get<std::string>()));
                    continue;
                }
            }
            if (plugin == nullptr || !impl_->putInRack(rack, plugin, static_cast<int>(c), -1))
                continue;
            plugin->setEnabled(m.value("on", true));
            ids[{static_cast<int>(c), static_cast<int>(i)}] = plugin->itemID.getRawID();
            ++placed;
        }
        juce::ignoreUnused(placed);
        const auto chainsNow = Impl::rackChains(rack);
        if (c < chainsNow.size())
        {
            auto mixer = chainsNow[c].mixer;
            const auto chainName = chainJson.value("name", std::string());
            if (!chainName.empty() && chainName != "Chain " + std::to_string(c + 1))
                mixer.setProperty(Impl::chainNameId, juce::String::fromUTF8(chainName.c_str()), um);
            if (!chainJson.value("on", true))
                mixer.setProperty(Impl::chainOnId, false, um);
            if (auto* v = chainsNow[c].volume)
            {
                const float db = chainJson.value("volumeDb", 0.0f);
                if (db <= model::minusInfinityDb)
                    v->setSliderPos(0.0f);
                else
                    v->setVolumeDb(std::min(6.0f, db));
                v->setPan(std::clamp(chainJson.value("pan", 0.0f), -1.0f, 1.0f));
            }
        }
    }
    Impl::wireRack(rack, um);
    // The macros: names, positions, then their mappings onto the modules made.
    const auto macrosJson = json.value("macros", nlohmann::json::array());
    for (size_t i = 0; i < macrosJson.size() && i < static_cast<size_t>(model::RackInfo::macroCount); ++i)
    {
        const auto& macro = macrosJson[i];
        setMacro(rackId, static_cast<int>(i), std::clamp(macro.value("value", 0.0f), 0.0f, 1.0f),
                 macro.value("name", std::string()));
        for (const auto& m : macro.value("mappings", nlohmann::json::array()))
            if (const auto it = ids.find({m.value("chain", 0), m.value("module", 0)}); it != ids.end())
                mapMacro(rackId, static_cast<int>(i), it->second, m.value("param", std::string()),
                         m.value("from", 0.0f), m.value("to", 1.0f), m.value("curve", 0.0f));
    }
    loaded.rack = rackId;
    return loaded;
}
} // namespace fd::engine
