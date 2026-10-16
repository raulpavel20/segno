// Racks (M3 P2, docs/design/racks.md, ADR 0056): modules of modules on tracktion's racks. A
// rack's chains are Segno's, kept on its plugins' places in the rack (SessionImpl.h, "racks");
// the connections between them follow from those (wireRack), in the same undo step as the edit.

#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/Modulators.h"
#include "model/DrumKit.h"
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
        else if (auto* inlet = dynamic_cast<PadInlet*>(p))
            chain.inlet = inlet; // a Drum Kit's pad (drum-kit.md §15)
        else
            chain.modules.push_back(p);
    }
    // Its modulators (modulators.md §14): each in its chain, before the module it's placed before;
    // those placed before none (or a module no longer there) at the chain's end.
    for (auto* m : rack.getModifierList().getModifiers())
        if (auto* mod = dynamic_cast<Modulator*>(m))
            byNumber[static_cast<int>(mod->state.getProperty(Modulator::rackChainId, 0))]
                .modulators.push_back(mod);
    std::vector<RackChain> chains;
    for (auto& [number, chain] : byNumber)
    {
        if (!chain.mixer.isValid() && chain.volume == nullptr && chain.modules.empty() && chain.inlet == nullptr)
        {
            // Modulators of a chain that's gone: the last chain's, at its end.
            if (!chains.empty())
                chains.back().modulators.insert(chains.back().modulators.end(), chain.modulators.begin(),
                                                chain.modulators.end());
            continue;
        }
        chains.push_back(std::move(chain));
    }
    for (auto& c : chains)
    {
        std::vector<bool> placed(c.modulators.size(), false);
        for (auto* p : c.modules)
        {
            for (size_t i = 0; i < c.modulators.size(); ++i)
                if (!placed[i] && c.modulators[i]->place() == p->itemID)
                {
                    c.items.push_back({nullptr, c.modulators[i]});
                    placed[i] = true;
                }
            c.items.push_back({p, nullptr});
        }
        for (size_t i = 0; i < c.modulators.size(); ++i)
            if (!placed[i])
                c.items.push_back({nullptr, c.modulators[i]});
    }
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
    const bool kit = isKit(rack);
    if (kit) // the track's own audio, once, round the pads (drum-kit.md §15)
        for (int pin = 1; pin <= 2; ++pin)
            wanted.insert({te::EditItemID(), pin, te::EditItemID(), pin});
    for (const auto& c : chains)
    {
        if (c.modules.empty() && c.volume == nullptr)
            continue;
        // MIDI (pin 0) and audio (1, 2) from the rack's input, module to module, to its output. A
        // modulator takes what comes where it sits, and passes nothing on. A pad's chain starts
        // with its inlet, which takes the kit's notes alone: its sound starts from silence.
        te::EditItemID from; // the rack's input
        if (c.inlet != nullptr)
        {
            wanted.insert({from, 0, c.inlet->itemID, 0});
            from = c.inlet->itemID;
        }
        for (const auto& item : c.items)
        {
            const auto to = item.plugin != nullptr ? item.plugin->itemID : item.modulator->itemID;
            for (int pin = 0; pin <= 2; ++pin)
                wanted.insert({from, pin, to, pin});
            if (item.plugin != nullptr)
                from = to;
        }
        if (c.volume != nullptr)
        {
            for (int pin = 0; pin <= 2; ++pin)
                wanted.insert({from, pin, c.volume->itemID, pin});
            from = c.volume->itemID;
        }
        if (c.on() && (!soloed || c.solo()))
            for (int pin = 1; pin <= 2; ++pin)
                wanted.insert({from, pin, te::EditItemID(), pin});
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
    if (kit)
        PadInlet::updateChokes(rack); // which notes choke each pad, after any change to its pads
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
        if (isKit(*rack))
            return true;
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
    ensureMorph(*rack); // after them: the rack's parameters are its macros, then Morph
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
    // Along a chain with modulators, `index` counts them too (modulators.md §14).
    if (const auto after = rackChains(rack); !after[static_cast<size_t>(chain)].modulators.empty())
    {
        std::vector<ChainItem> order;
        for (const auto& item : after[static_cast<size_t>(chain)].items)
            if (item.plugin != keep.get())
                order.push_back(item);
        const auto at = index < 0 ? order.size() : std::min(order.size(), static_cast<size_t>(index));
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), ChainItem{keep.get(), nullptr});
        arrangeRackChain(rack, chain, order);
    }
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
    {
        for (auto* m : rackModules(*type))
            dropLanesOf(m->itemID.getRawID());
        for (auto* m : type->getModifierList().getModifiers()) // its modulators' (§14)
            dropLanesOf(m->itemID.getRawID());
    }
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
        for (const auto& item : c.items) // its modulators where they sit (modulators.md §14)
            chain.devices.push_back(item.plugin != nullptr ? slotOf(*item.plugin)
                                                           : modulatorSlot(*item.modulator));
        if (c.inlet != nullptr) // a Drum Kit's pad (drum-kit.md §14)
        {
            chain.note = c.inlet->note();
            chain.plays = c.inlet->plays();
            chain.choke = c.inlet->choke();
            if (c.mixer[chainNameId].toString().isEmpty())
                chain.name = model::kit::noteName(chain.note);
        }
        info->chains.push_back(std::move(chain));
    }
    if (isKit(*type))
    {
        info->kit = true;
        info->selectedPad = std::clamp(static_cast<int>(instance.state.getProperty(selectedPadId, model::kit::firstShown)),
                                       0, model::kit::padCount - 1);
        info->padsFrom = model::kit::shownFrom(instance.state.getProperty(padsFromId, model::kit::firstShown));
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
    }
    fillStep2(*type, *info);
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

// ---- snapshots, Morph and locks (racks.md §18) ---------------------------------------------

std::vector<model::RackInfo::Snapshot> Session::Impl::snapshotsOf(const te::RackType& rack)
{
    std::vector<model::RackInfo::Snapshot> out;
    const auto json = nlohmann::json::parse(rack.state[snapshotsId].toString().toStdString(), nullptr, false);
    if (!json.is_array())
        return out;
    for (const auto& s : json)
    {
        if (!s.is_object() || out.size() == static_cast<size_t>(model::RackInfo::maxSnapshots))
            continue;
        model::RackInfo::Snapshot snapshot{.name = s.value("name", std::string())};
        const auto positions = s.value("macros", nlohmann::json::array());
        for (size_t i = 0; i < snapshot.macros.size() && i < positions.size(); ++i)
            if (positions[i].is_number())
                snapshot.macros[i] = std::clamp(positions[i].get<float>(), 0.0f, 1.0f);
        out.push_back(std::move(snapshot));
    }
    return out;
}

void Session::Impl::setSnapshots(te::RackType& rack, const std::vector<model::RackInfo::Snapshot>& snapshots,
                                 juce::UndoManager* um)
{
    if (snapshots.empty())
    {
        rack.state.removeProperty(snapshotsId, um);
        return;
    }
    auto json = nlohmann::json::array();
    for (const auto& s : snapshots)
        json.push_back({{"name", s.name}, {"macros", s.macros}});
    rack.state.setProperty(snapshotsId, juce::String::fromUTF8(json.dump().c_str()), um);
}

te::MacroParameter* Session::Impl::morphOf(const te::RackType& rack)
{
    for (auto* m : rack.getMacroParameters())
        if (static_cast<bool>(m->state[morphId]))
            return m;
    return nullptr;
}

te::MacroParameter* Session::Impl::ensureMorph(te::RackType& rack)
{
    if (auto* morph = morphOf(rack))
        return morph;
    auto* morph = rack.getMacroParameterListForWriting().createMacroParameter();
    if (morph == nullptr)
        return nullptr;
    morph->macroName = "Morph";
    morph->state.setProperty(morphId, true, &edit->getUndoManager());
    morph->setParameter(0.0f, juce::sendNotificationSync);
    return morph;
}

float Session::Impl::macroPosition(const te::RackType& rack, int macro)
{
    const auto macros = rack.getMacroParameters();
    if (macro < 0 || macro >= macros.size())
        return 0.0f;
    auto& m = *macros[macro];
    if (auto* morph = morphOf(rack))
        for (auto assignment : m.getAssignments())
            if (assignment->isForModifierSource(*morph))
                return std::clamp(m.value.get() + assignment->value.get() * morph->getCurrentValue(), 0.0f,
                                  1.0f);
    return std::clamp(followsLane(m) ? m.getCurrentValue() : m.value.get(), 0.0f, 1.0f);
}

void Session::Impl::fillStep2(const te::RackType& rack, model::RackInfo& info)
{
    info.snapshots = snapshotsOf(rack);
    const int count = static_cast<int>(info.snapshots.size());
    const auto pick = [count](const juce::var& v)
    {
        const int i = v.isVoid() ? -1 : static_cast<int>(v);
        return i >= 0 && i < count ? i : -1;
    };
    info.currentSnapshot = pick(rack.state[snapshotId]);
    info.morph.on = static_cast<bool>(rack.state[morphOnId]);
    info.morph.a = pick(rack.state[morphAId]);
    info.morph.b = pick(rack.state[morphBId]);
    if (const auto* morph = morphOf(rack))
        info.morph.position = std::clamp(morph->getCurrentValue(), 0.0f, 1.0f);
    const auto macros = rack.getMacroParameters();
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
    {
        auto& macro = info.macros[static_cast<size_t>(i)];
        macro.value = macroPosition(rack, i);
        macro.locked = static_cast<bool>(macros[i]->state[lockedId]);
        macro.automated = followsLane(*macros[i]);
    }
}

void Session::Impl::syncMorph(te::RackType& rack)
{
    auto* morph = morphOf(rack);
    model::RackInfo info;
    fillStep2(rack, info);
    const auto macros = rack.getMacroParameters();
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
    {
        auto& m = *macros[i];
        te::AutomatableParameter::ModifierAssignment::Ptr existing;
        if (morph != nullptr)
            for (auto assignment : m.getAssignments())
                if (assignment->isForModifierSource(*morph))
                    existing = assignment;
        if (morph != nullptr && model::morphSets(info, i))
        {
            const auto at = static_cast<size_t>(i);
            const float from = info.snapshots[static_cast<size_t>(info.morph.a)].macros[at];
            const float to = info.snapshots[static_cast<size_t>(info.morph.b)].macros[at];
            if (std::abs(m.value.get() - from) > 1.0e-6f)
                m.setParameter(from, juce::sendNotificationSync);
            if (existing == nullptr)
            {
                existing = m.addModifier(*morph, to - from, 0.0f, 0.0f);
                if (existing != nullptr)
                    existing->curve = 0.0f; // straight: tracktion leaves the curve out when it adds one
            }
            else if (std::abs(existing->value.get() - (to - from)) > 1.0e-6f)
                existing->value = to - from;
        }
        else if (existing != nullptr)
        {
            // Let go where Morph has it, as a knob keeps its value when it's unmapped; unless its
            // lane has it now, which a value set by hand would turn off.
            const float now =
                std::clamp(m.value.get() + existing->value.get() * morph->getCurrentValue(), 0.0f, 1.0f);
            m.removeModifier(*existing);
            if (!followsLane(m))
                m.setParameter(now, juce::sendNotificationSync);
        }
    }
    refreshMacroTargets(rack);
}

void Session::Impl::syncMorphs()
{
    auto& racks = edit->getRackList();
    for (int i = 0; i < racks.size(); ++i)
        if (auto rack = racks.getRackType(i))
            syncMorph(*rack);
}

void Session::Impl::refreshMacroTargets(te::RackType& rack)
{
    const auto position = edit->getTransport().getPosition();
    auto* morph = morphOf(rack);
    for (auto* macro : rack.getMacroParameters())
    {
        if (macro == morph)
            continue;
        macro->updateFromAutomationSources(position); // Morph's part in it
        for (auto param : te::getAllParametersBeingModifiedBy(*edit, *macro))
            if (!param->automatableEditElement.isBeingActivelyPlayed())
                param->updateFromAutomationSources(position);
    }
}

// ---- the rack's macros, as a module's parameters -------------------------------------------

namespace
{
/// A rack's macros as the panel kit sees parameters: "macro1" .. "macro8", then "morph", 0 ..
/// 100 %.
class RackMacroBinding final : public ui::ParamBinding
{
public:
    RackMacroBinding(Session& session, te::RackInstance& rack) : session_(session), rack_(&rack) {}

    float getPlain(size_t i) const override
    {
        auto* rack = Session::Impl::rackTypeOf(rack_.get());
        if (rack == nullptr)
            return 0.0f;
        if (i < static_cast<size_t>(model::RackInfo::macroCount)) // where it is, Morph's place for it too
            return 100.0f * Session::Impl::macroPosition(*rack, static_cast<int>(i));
        auto* m = macro(i);
        return m != nullptr ? 100.0f * m->getCurrentValue() : 0.0f;
    }
    void setPlain(size_t i, float plain) override
    {
        if (macroMapping(i)) // Morph sets it: it isn't moved by hand
            return;
        if (auto* m = macro(i))
        {
            m->setParameter(std::clamp(plain / 100.0f, 0.0f, 1.0f), juce::sendNotificationSync);
            if (i == static_cast<size_t>(model::RackInfo::morphParam))
                if (auto* rack = Session::Impl::rackTypeOf(rack_.get())) // the macros' modules follow
                    session_.impl().refreshMacroTargets(*rack);
        }
    }
    // A macro Morph sets (racks.md §18.3): its range A to B a ring, and no dragging by hand.
    [[nodiscard]] std::optional<MacroMapping> macroMapping(size_t i) const override
    {
        auto* rack = Session::Impl::rackTypeOf(rack_.get());
        if (rack == nullptr || i >= static_cast<size_t>(model::RackInfo::macroCount))
            return std::nullopt;
        model::RackInfo info;
        Session::Impl::fillStep2(*rack, info);
        if (!model::morphSets(info, static_cast<int>(i)))
            return std::nullopt;
        const auto& a = info.snapshots[static_cast<size_t>(info.morph.a)];
        const auto& b = info.snapshots[static_cast<size_t>(info.morph.b)];
        return MacroMapping{juce::String::fromUTF8(("Morph: " + a.name + " \xe2\x86\x92 " + b.name).c_str()),
                            a.macros[i], b.macros[i]};
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
    [[nodiscard]] bool canAutomate(size_t i) const override { return macro(i) != nullptr; }
    void addAutomationItems(size_t i, juce::PopupMenu& menu) override
    {
        if (const auto t = target(i); t && session_.automationMenu)
            session_.automationMenu(*t, menu);
    }
    // Moved by the track's modulators (modulators.md §7): a macro and Morph are targets too.
    [[nodiscard]] std::optional<Modulated> modulated(size_t i) const override
    {
        auto* m = macro(i);
        return m != nullptr ? session_.impl().modulatedLook(*m) : std::nullopt;
    }
    [[nodiscard]] MacroDrag modulatorDrag(size_t i) const override
    {
        auto* m = macro(i);
        return m != nullptr ? session_.impl().modulatorDragLook(*m) : MacroDrag{};
    }
    bool beginModulatedDrag(size_t i) override { return ring_.begin(session_, target(i)); }
    void dragModulated(size_t, float delta) override { ring_.drag(session_, delta); }
    void endModulatedDrag(size_t) override { ring_.end(); }
    void addModulatorItems(size_t i, juce::PopupMenu& menu, juce::PopupMenu& with) override
    {
        if (const auto t = target(i); t && session_.modulatorMenu)
            session_.modulatorMenu(*t, menu, with);
    }

private:
    [[nodiscard]] std::optional<Session::ParamTarget> target(size_t i) const
    {
        return session_.moduleParameter(rack_->itemID.getRawID(), std::string(kMacroSpecs[i].id));
    }
    te::MacroParameter* macro(size_t i) const
    {
        auto* rack = Session::Impl::rackTypeOf(rack_.get());
        if (rack == nullptr)
            return nullptr;
        if (i == static_cast<size_t>(model::RackInfo::morphParam))
            return Session::Impl::morphOf(*rack);
        const auto macros = rack->getMacroParameters();
        return static_cast<int>(i) < macros.size() ? macros[static_cast<int>(i)].get() : nullptr;
    }

    Session& session_;
    te::Plugin::Ptr rack_; // keeps the rack alive while its panel exists
    RouteDrag ring_;
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

/// A Drum Kit's panel context (drum-kit.md §8): the module "Drum Kit", its macros a rack's, its
/// presets its kits (§9).
class KitHandle final : public DeviceHandle
{
public:
    KitHandle(Session& session, te::RackInstance& kit)
        : binding_(session, kit), presets_(session.presetHost(kit.itemID.getRawID())),
          context_{Session::drumKit(), binding_, nullptr, nullptr, nullptr, presets_.get()}
    {
    }
    ui::DeviceUiContext& context() override { return context_; }
    std::string_view deviceId() const override { return "drum-kit"; }

private:
    RackMacroBinding binding_;
    std::unique_ptr<PresetHost> presets_;
    ui::DeviceUiContext context_;
};

std::uint64_t idOf(const te::Plugin& p)
{
    return p.itemID.getRawID();
}

/// Whether a track has modulators: its chain's places count them then (modulators.md §6), and
/// where modules go, they stay where they sit.
bool modulated(te::Track& track)
{
    const auto items = Session::Impl::chainItems(track);
    return std::any_of(items.begin(), items.end(),
                       [](const auto& item) { return item.modulator != nullptr; });
}
} // namespace

const DeviceInfo& Session::rackMacros()
{
    static const DeviceInfo info{.id = "rack", .name = "Rack", .category = "Racks", .params = kMacroSpecs};
    return info;
}

const DeviceInfo& Session::drumKit()
{
    static const DeviceInfo info{.id = "drum-kit",
                                 .name = "Drum Kit",
                                 .category = "Instruments",
                                 .kind = DeviceKind::Instrument,
                                 .buses = {.inputChannels = 0, .acceptsMidi = true},
                                 .params = std::span(kMacroSpecs.data(), model::RackInfo::macroCount)};
    return info;
}

std::unique_ptr<DeviceHandle> Session::openRack(std::uint64_t rack)
{
    if (auto* instance = impl_->rackInstance(rack); instance != nullptr && Impl::isKit(instance))
        return std::make_unique<KitHandle>(*this, *instance);
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
    impl_->droppedRoutes = 0;
    if (modules.empty())
        return 0;
    // Every one in one channel's own chain (not in a rack), and none a rack; its modulators come
    // along (modulators.md §14).
    te::PluginList* list = nullptr;
    te::Track* track = nullptr;
    std::vector<te::Plugin::Ptr> picked;
    std::set<std::uint64_t> pickedModulators;
    for (auto id : modules)
    {
        if (auto* m = impl_->modulator(id))
        {
            auto* t = impl_->trackOf(*m);
            if (t == nullptr || impl_->rackOf(*m) != nullptr || (track != nullptr && t != track))
                return 0;
            track = t;
            pickedModulators.insert(id);
            continue;
        }
        auto* p = impl_->plugin(id);
        if (p == nullptr || p->isInRack() || Impl::rackTypeOf(p) != nullptr || p->getOwnerList() == nullptr)
            return 0;
        if (list != nullptr && p->getOwnerList() != list)
            return 0;
        auto* t = impl_->chainTrackOf(*p);
        if (track != nullptr && t != nullptr && t != track)
            return 0;
        track = t != nullptr ? t : track;
        list = p->getOwnerList();
        picked.push_back(p);
    }
    if (picked.empty())
        return 0; // a rack holds modules; modulators alone move into one
    // In their order in the chain; the rack goes where the first was.
    const auto order = Impl::chain(*list);
    std::sort(picked.begin(), picked.end(),
              [&](const auto& a, const auto& b)
              {
                  return std::find(order.begin(), order.end(), a.get()) <
                         std::find(order.begin(), order.end(), b.get());
              });
    const int at = list->indexOf(picked.front().get());
    // A track's modulators stay where they sit, but for those grouped; the rack in the first
    // grouped item's place.
    auto items = track != nullptr && (modulated(*track) || !pickedModulators.empty())
                     ? Impl::chainItems(*track)
                     : std::vector<Impl::ChainItem>{};
    const auto isPicked = [&](const Impl::ChainItem& item)
    {
        if (item.modulator != nullptr)
            return pickedModulators.contains(item.modulator->itemID.getRawID());
        return std::any_of(picked.begin(), picked.end(),
                           [&](const auto& p) { return p.get() == item.plugin; });
    };

    impl_->beginEdit("Group into rack");
    auto rack = impl_->makeRack(newRackName(*impl_->edit));
    if (rack == nullptr)
        return 0;
    for (const auto& p : picked)
        impl_->putInRack(*rack, p, 0, -1);
    // The modulators, in the chain's order among the modules grouped with them.
    std::vector<Impl::ChainItem> rackOrder;
    for (const auto& item : items)
        if (isPicked(item))
            rackOrder.push_back(item);
    std::vector<std::uint64_t> moving;
    for (const auto& item : rackOrder)
        if (item.modulator != nullptr)
            moving.push_back(item.modulator->itemID.getRawID());
    for (const auto id : moving)
        if (auto* m = impl_->modulator(id))
            impl_->droppedRoutes += impl_->putModulatorInRack(*rack, *m, 0, -1);
    if (!moving.empty())
    {
        std::vector<Impl::ChainItem> wanted;
        for (const auto& item : rackOrder)
            wanted.push_back(item.plugin != nullptr ? item
                                                    : Impl::ChainItem{nullptr, impl_->modulator(item.id())});
        if (std::all_of(wanted.begin(), wanted.end(),
                        [](const auto& w) { return w.plugin != nullptr || w.modulator != nullptr; }))
            impl_->arrangeRackChain(*rack, 0, wanted);
    }
    Impl::wireRack(*rack, &impl_->edit->getUndoManager());
    auto instance = impl_->edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(*rack));
    if (instance == nullptr)
        return 0;
    list->insertPlugin(instance, std::min(at, list->size()), nullptr);
    if (!items.empty())
    {
        const auto first = std::find_if(items.begin(), items.end(), isPicked);
        *first = Impl::ChainItem{instance.get(), nullptr};
        items.erase(std::remove_if(items.begin(), items.end(), [&](const auto& item)
                                   { return item.plugin != instance.get() && isPicked(item); }),
                    items.end());
        impl_->arrangeChain(*track, items);
    }
    return idOf(*instance);
}

std::vector<std::uint64_t> Session::ungroupRack(std::uint64_t rackId)
{
    auto* instance = impl_->rackInstance(rackId);
    auto* list = instance != nullptr ? instance->getOwnerList() : nullptr;
    if (instance == nullptr || instance->type == nullptr || list == nullptr || Impl::isKit(*instance->type))
        return {}; // a kit isn't a group of modules
    const te::Plugin::Ptr keepInstance = instance;
    const te::RackType::Ptr type = instance->type;
    const auto chains = Impl::rackChains(*type);
    const int shown = std::clamp(static_cast<int>(instance->state.getProperty(Impl::shownChainId, 0)), 0,
                                 static_cast<int>(chains.size()) - 1);
    impl_->beginEdit("Ungroup rack");
    // The shown chain's modules and modulators, out of the rack and into the chain where it was,
    // in order; their macros' mappings go (each parameter keeps its value now), and the
    // modulators' routes to the rack's macros. A track's modulators stay where they sit.
    const auto& shownChain = chains[static_cast<size_t>(shown)];
    std::vector<te::Plugin::Ptr> out(shownChain.modules.begin(), shownChain.modules.end());
    std::vector<std::uint64_t> shownItems; // in order: the modules' and modulators' ids
    for (const auto& item : shownChain.items)
        shownItems.push_back(item.id());
    auto* track = impl_->chainTrackOf(*instance);
    auto items = track != nullptr && (modulated(*track) || !shownChain.modulators.empty())
                     ? Impl::chainItems(*track)
                     : std::vector<Impl::ChainItem>{};
    for (auto* m : shownChain.modulators)
        for (const auto& r : impl_->routesOf(*m))
            if (dynamic_cast<te::MacroParameter*>(r.param.param) != nullptr)
                r.param.param->removeModifier(*r.assignment);
    // The other chains' modulators go with the rack: their lanes first.
    for (size_t c = 0; c < chains.size(); ++c)
        if (static_cast<int>(c) != shown)
            for (auto* m : chains[c].modulators)
                impl_->dropLanesOf(m->itemID.getRawID());
    int at = list->indexOf(instance);
    std::vector<std::uint64_t> placed;
    for (const auto& p : out)
    {
        impl_->unmapFrom(*p, *type);
        p->removeFromParent();
        list->insertPlugin(p, at++, nullptr);
    }
    if (track != nullptr)
        for (auto* m : shownChain.modulators)
            impl_->takeModulatorOutOfRack(*m, *track, -1);
    impl_->removeRack(*instance);
    for (const auto id : shownItems)
        placed.push_back(id);
    if (!items.empty())
    {
        std::vector<Impl::ChainItem> order;
        for (const auto& item : items)
            if (item.plugin != keepInstance.get())
                order.push_back(item);
            else
                for (const auto id : shownItems)
                    if (auto* m = impl_->modulator(id))
                        order.push_back({nullptr, m});
                    else if (auto* p = impl_->plugin(id))
                        order.push_back({p, nullptr});
        impl_->arrangeChain(*track, order);
    }
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
    // Along a track's chain, its modulators count among the places (modulators.md §6).
    if (auto* track = impl_->chainTrackOf(*instance); track != nullptr && modulated(*track))
        impl_->placeInChain(*track, Impl::ChainItem{instance.get(), nullptr}, index);
    return idOf(*instance);
}

// ---- modules in racks ---------------------------------------------------------------------

std::optional<Session::ChainPlace> Session::placeOf(std::uint64_t deviceId) const
{
    // Along a track's chain, its modulators count among the places (modulators.md §6).
    if (auto* m = impl_->modulator(deviceId))
    {
        auto* track = impl_->trackOf(*m);
        if (track == nullptr)
            return std::nullopt;
        if (auto* rack = impl_->rackOf(*m)) // in a rack's chain (§14)
        {
            const auto chains = Impl::rackChains(*rack);
            auto* instance = impl_->instanceOf(*rack);
            for (size_t c = 0; c < chains.size(); ++c)
                for (size_t i = 0; i < chains[c].items.size(); ++i)
                    if (chains[c].items[i].modulator == m && instance != nullptr)
                        return ChainPlace{track->itemID.getRawID(), idOf(*instance), static_cast<int>(c),
                                          static_cast<int>(i)};
            return std::nullopt;
        }
        return ChainPlace{track->itemID.getRawID(), 0, 0, Impl::chainIndexOf(*track, deviceId)};
    }
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
            for (size_t i = 0; i < chains[c].items.size(); ++i) // its modulators counted (§14)
                if (chains[c].items[i].plugin == p)
                    return ChainPlace{channelOf(*rack), idOf(*rack), static_cast<int>(c), static_cast<int>(i),
                                      chains[c].inlet != nullptr ? chains[c].inlet->note() : -1};
        return std::nullopt;
    }
    if (auto* track = impl_->chainTrackOf(*p))
        return ChainPlace{channelOf(*p), 0, 0, Impl::chainIndexOf(*track, deviceId)};
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
    if (place.pad >= 0 && Impl::isKit(rack))
        return addToPad(place, key);
    if (place.chain < 0 || place.chain >= static_cast<int>(Impl::rackChains(rack).size()))
        return 0;
    // Made in the track's chain, as any module (your default applies), then moved into the rack's,
    // in the same step.
    auto* list = instance->getOwnerList();
    if (list == nullptr)
        return 0;
    EditGroup group(*this, "Add module");
    auto* owner = impl_->chainTrackOf(*instance);
    const auto id = addDevice(owner != nullptr ? owner->itemID.getRawID() : impl_->masterId(), key, -1);
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
    if (place.pad >= 0)
        return moveToPad(deviceId, place);
    const auto from = placeOf(deviceId);
    if (auto* m = impl_->modulator(deviceId))
    {
        // Within its own channel: along its chain, into a rack's chain, or out of it (§14).
        auto* track = impl_->trackOf(*m);
        if (track == nullptr || !from || (place.channel != 0 && place.channel != from->channel))
            return false;
        auto* target = place.rack != 0 ? impl_->rackInstance(place.rack) : nullptr;
        if (place.rack != 0 &&
            (target == nullptr || target->type == nullptr || impl_->chainTrackOf(*target) != track))
            return false;
        if (target != nullptr || from->rack != 0)
            impl_->beginEdit("Move " + m->getName());
        if (target != nullptr)
        {
            // Along its own chain in the rack, the place counts as if it had left first.
            int index = place.index;
            if (from->rack == place.rack && from->chain == place.chain && index > from->index)
                --index;
            impl_->droppedRoutes = impl_->putModulatorInRack(*target->type, *m, place.chain, index);
            return true;
        }
        if (from->rack != 0)
        {
            impl_->takeModulatorOutOfRack(*m, *track, place.index);
            return true;
        }
        int index = place.index < 0 ? static_cast<int>(Impl::chainItems(*track).size()) : place.index;
        if (index > from->index)
            --index;
        moveDevice(deviceId, index);
        return true;
    }
    const te::Plugin::Ptr p = impl_->plugin(deviceId);
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
        auto* track = impl_->chainTrackOf(*p);
        const int count = track != nullptr  ? static_cast<int>(Impl::chainItems(*track).size())
                          : list != nullptr ? static_cast<int>(Impl::chain(*list).size())
                                            : 0;
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
        // Out of the rack, into its channel's chain at `index` (before the module there; along a
        // track's chain its modulators count, modulators.md §6).
        auto* list = source->getOwnerList();
        if (list == nullptr)
            return false;
        const auto order = Impl::chain(*list); // the rack among them
        const int at = place.index >= 0 && place.index < static_cast<int>(order.size())
                           ? list->indexOf(order[static_cast<size_t>(place.index)])
                           : list->indexOf(order.back()) + 1;
        p->removeFromParent();
        list->insertPlugin(p, at, nullptr);
        if (auto* track = impl_->chainTrackOf(*source); track != nullptr && modulated(*track))
            impl_->placeInChain(*track, Impl::ChainItem{p.get(), nullptr}, place.index);
    }
    if (source != nullptr && source != target)
        Impl::wireRack(*source->type, um);
    if (source != nullptr && Impl::isKit(*source->type))
        impl_->prunePads(*source->type); // a pad with nothing left on it is empty (drum-kit.md §4)
    return true;
}

// ---- chains -------------------------------------------------------------------------------

int Session::addRackChain(std::uint64_t rackId)
{
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr || Impl::isKit(*instance->type))
        return -1; // a kit's chains are its pads (drum-kit.md)
    const int count = static_cast<int>(Impl::rackChains(*instance->type).size());
    if (count >= maxRackChains)
        return -1;
    impl_->beginEdit("Add chain");
    impl_->addChain(*instance->type);
    Impl::wireRack(*instance->type, &impl_->edit->getUndoManager());
    instance->state.setProperty(Impl::shownChainId, count, nullptr); // the new one shows
    return count;
}

void Session::Impl::removeChain(te::RackType& rack, int chain)
{
    const auto chains = rackChains(rack);
    if (chain < 0 || chain >= static_cast<int>(chains.size()))
        return;
    auto* um = &edit->getUndoManager();
    const auto& c = chains[static_cast<size_t>(chain)];
    // Its modulators and their routes go with it (modulators.md §14).
    for (auto* m : c.modulators)
    {
        dropLanesOf(m->itemID.getRawID());
        for (const auto& r : routesOf(*m))
            r.param.param->removeModifier(*r.assignment);
        auto state = m->state;
        state.getParent().removeChild(state, um);
    }
    for (auto* m : c.modules)
    {
        dropLanesOf(m->itemID.getRawID());
        m->removeFromParent();
    }
    if (c.inlet != nullptr)
        c.inlet->removeFromParent();
    if (c.volume != nullptr)
    {
        dropLanesOf(c.volume->itemID.getRawID()); // a pad's volume and pan, automated
        c.volume->removeFromParent();
    }
    // The chains after it move up one, their modulators with them.
    for (auto* p : rack.getPlugins())
    {
        auto place = p->state.getParent();
        if (const int n = place.getProperty(chainId, 0); n > chain)
            place.setProperty(chainId, n - 1, um);
    }
    for (auto* m : rack.getModifierList().getModifiers())
        if (const int n = m->state.getProperty(Modulator::rackChainId, 0); n > chain)
            m->state.setProperty(Modulator::rackChainId, n - 1, um);
}

bool Session::removeRackChain(std::uint64_t rackId, int chain)
{
    auto* instance = impl_->rackInstance(rackId);
    if (instance == nullptr || instance->type == nullptr || Impl::isKit(*instance->type))
        return false;
    auto& rack = *instance->type;
    const auto chains = Impl::rackChains(rack);
    if (chains.size() <= 1 || chain < 0 || chain >= static_cast<int>(chains.size()))
        return false;
    impl_->beginEdit("Remove chain");
    auto* um = &impl_->edit->getUndoManager();
    impl_->removeChain(rack, chain);
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
    if (instance == nullptr || instance->type == nullptr || trimmed.isEmpty() || Impl::isKit(*instance->type))
        return false; // a kit is called Drum Kit; its kits are presets
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
    if (value)
        if (auto* morph = Impl::morphOf(*instance->type))
            for (auto assignment : m.getAssignments())
                if (assignment->isForModifierSource(*morph))
                    return false; // Morph sets it (racks.md §18.3)
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

// ---- snapshots, Morph, locks and Vary (racks.md §18) -----------------------------------------

namespace
{
/// A rack's, never a Drum Kit's: a kit's macros have no snapshots, Morph, locks or Vary yet
/// (drum-kit.md §10).
te::RackType* rackTypeFor(Session::Impl& impl, std::uint64_t rackId)
{
    auto* instance = impl.rackInstance(rackId);
    return instance != nullptr && !Session::Impl::isKit(instance) ? instance->type.get() : nullptr;
}

int intOf(const juce::var& v)
{
    return v.isVoid() ? -1 : static_cast<int>(v);
}
} // namespace

int Session::storeSnapshot(std::uint64_t rackId, std::optional<int> over)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return -1;
    auto snapshots = Impl::snapshotsOf(*rack);
    const int count = static_cast<int>(snapshots.size());
    if (over ? (*over < 0 || *over >= count) : count >= model::RackInfo::maxSnapshots)
        return -1;
    impl_->beginEdit(over ? "Update snapshot" : "Store snapshot");
    auto* um = &impl_->edit->getUndoManager();
    impl_->ensureMorph(*rack); // a rack from before step 2 gets its Morph now
    model::RackInfo::Snapshot snapshot;
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
        snapshot.macros[static_cast<size_t>(i)] = Impl::macroPosition(*rack, i);
    int index = 0;
    if (over)
    {
        snapshot.name = snapshots[static_cast<size_t>(*over)].name;
        snapshots[static_cast<size_t>(*over)] = snapshot;
        index = *over;
    }
    else
    {
        model::RackInfo named;
        named.snapshots = snapshots;
        snapshot.name = model::newSnapshotName(named);
        snapshots.push_back(snapshot);
        index = count;
    }
    Impl::setSnapshots(*rack, snapshots, um);
    rack->state.setProperty(Impl::snapshotId, index, um);
    // The first two are what Morph goes between, until chosen otherwise.
    if (snapshots.size() == 2 && intOf(rack->state[Impl::morphAId]) < 0 &&
        intOf(rack->state[Impl::morphBId]) < 0)
    {
        rack->state.setProperty(Impl::morphAId, 0, um);
        rack->state.setProperty(Impl::morphBId, 1, um);
    }
    impl_->syncMorph(*rack);
    return index;
}

bool Session::recallSnapshot(std::uint64_t rackId, int snapshot)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return false;
    const auto snapshots = Impl::snapshotsOf(*rack);
    if (snapshot < 0 || snapshot >= static_cast<int>(snapshots.size()))
        return false;
    impl_->beginEdit("Recall snapshot");
    auto* um = &impl_->edit->getUndoManager();
    // Morph lets go first: a click on a snapshot always puts the macros there (racks.md §18.3).
    rack->state.setProperty(Impl::morphOnId, false, um);
    impl_->syncMorph(*rack);
    const auto macros = rack->getMacroParameters();
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
        if (!static_cast<bool>(macros[i]->state[Impl::lockedId]) && !Impl::followsLane(*macros[i]))
            macros[i]->setParameter(snapshots[static_cast<size_t>(snapshot)].macros[static_cast<size_t>(i)],
                                    juce::sendNotificationSync);
    rack->state.setProperty(Impl::snapshotId, snapshot, um);
    impl_->refreshMacroTargets(*rack);
    return true;
}

bool Session::removeSnapshot(std::uint64_t rackId, int snapshot)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return false;
    auto snapshots = Impl::snapshotsOf(*rack);
    if (snapshot < 0 || snapshot >= static_cast<int>(snapshots.size()))
        return false;
    impl_->beginEdit("Remove snapshot");
    auto* um = &impl_->edit->getUndoManager();
    snapshots.erase(snapshots.begin() + snapshot);
    Impl::setSnapshots(*rack, snapshots, um);
    // What pointed at it points nowhere; what pointed past it, one back.
    const auto shift = [&](const juce::Identifier& id)
    {
        const int i = intOf(rack->state[id]);
        if (i == snapshot)
            rack->state.setProperty(id, -1, um);
        else if (i > snapshot)
            rack->state.setProperty(id, i - 1, um);
        return i == snapshot;
    };
    shift(Impl::snapshotId);
    const bool lostA = shift(Impl::morphAId), lostB = shift(Impl::morphBId);
    if (lostA || lostB)
        rack->state.setProperty(Impl::morphOnId, false, um);
    impl_->syncMorph(*rack);
    return true;
}

bool Session::setSnapshot(std::uint64_t rackId, int snapshot, std::optional<std::string> name,
                          std::optional<std::array<float, 8>> macros)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return false;
    auto snapshots = Impl::snapshotsOf(*rack);
    if (snapshot < 0 || snapshot >= static_cast<int>(snapshots.size()))
        return false;
    impl_->beginEdit(name ? "Rename snapshot" : "Set snapshot");
    auto& s = snapshots[static_cast<size_t>(snapshot)];
    if (name)
        if (const auto trimmed = juce::String::fromUTF8(name->c_str()).trim(); trimmed.isNotEmpty())
            s.name = trimmed.toStdString();
    if (macros)
        for (size_t i = 0; i < s.macros.size(); ++i)
            s.macros[i] = std::clamp((*macros)[i], 0.0f, 1.0f);
    Impl::setSnapshots(*rack, snapshots, &impl_->edit->getUndoManager());
    impl_->syncMorph(*rack); // A or B may have moved
    return true;
}

bool Session::setMorph(std::uint64_t rackId, const MorphEdit& e)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return false;
    const int count = static_cast<int>(Impl::snapshotsOf(*rack).size());
    const auto valid = [count](int i) { return i >= -1 && i < count; };
    if ((e.a && !valid(*e.a)) || (e.b && !valid(*e.b)))
        return false;
    const int a = e.a.value_or(intOf(rack->state[Impl::morphAId]));
    const int b = e.b.value_or(intOf(rack->state[Impl::morphBId]));
    if (e.on && *e.on && (a < 0 || a >= count || b < 0 || b >= count))
        return false; // on needs two snapshots to go between
    impl_->beginEdit(e.on ? (*e.on ? "Morph on" : "Morph off") : (e.a || e.b) ? "Morph between" : "Morph");
    auto* um = &impl_->edit->getUndoManager();
    auto* morph = impl_->ensureMorph(*rack);
    if (e.a)
        rack->state.setProperty(Impl::morphAId, *e.a, um);
    if (e.b)
        rack->state.setProperty(Impl::morphBId, *e.b, um);
    if ((a < 0 || b < 0) && static_cast<bool>(rack->state[Impl::morphOnId]))
        rack->state.setProperty(Impl::morphOnId, false, um); // A or B cleared
    if (e.on)
        rack->state.setProperty(Impl::morphOnId, *e.on, um);
    if (e.position && morph != nullptr)
        morph->setParameter(std::clamp(*e.position, 0.0f, 1.0f), juce::sendNotificationSync);
    impl_->syncMorph(*rack);
    return true;
}

bool Session::lockMacro(std::uint64_t rackId, int macro, bool locked)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return false;
    const auto macros = rack->getMacroParameters();
    if (macro < 0 || macro >= model::RackInfo::macroCount || macro >= macros.size())
        return false;
    impl_->beginEdit(locked ? "Lock macro" : "Unlock macro");
    macros[macro]->state.setProperty(Impl::lockedId, locked, &impl_->edit->getUndoManager());
    impl_->syncMorph(*rack); // Morph lets go of it, or takes it
    return true;
}

bool Session::varyRack(std::uint64_t rackId, float amount)
{
    auto* rack = rackTypeFor(*impl_, rackId);
    if (rack == nullptr)
        return false;
    model::RackInfo info;
    Impl::fillStep2(*rack, info);
    if (model::morphing(info))
        return false;
    const auto macros = rack->getMacroParameters();
    std::array<float, model::RackInfo::macroCount> values{};
    std::array<bool, model::RackInfo::macroCount> moves{};
    bool any = false;
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
    {
        const auto at = static_cast<size_t>(i);
        values[at] = info.macros[at].value;
        moves[at] = !model::macroHeld(info, i) &&
                    !te::getAllParametersBeingModifiedBy(*impl_->edit, *macros[i]).isEmpty();
        any = any || moves[at];
    }
    if (!any)
        return false;
    impl_->beginEdit("Vary");
    auto& random = juce::Random::getSystemRandom();
    const auto next =
        model::varied(values, moves, std::clamp(amount, 0.0f, 1.0f), [&] { return random.nextFloat(); });
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
        if (moves[static_cast<size_t>(i)])
            macros[i]->setParameter(next[static_cast<size_t>(i)], juce::sendNotificationSync);
    impl_->refreshMacroTargets(*rack);
    return true;
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
    if (instance == nullptr || instance->type == nullptr || Impl::isKit(*instance->type))
        return fail("It isn't a rack."); // a kit is kept as a preset (drum-kit.md §9)
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
    // Its modulators (modulators.md §14), each in its chain before the module it sits before (by
    // its place: -1, the end), with its settings and its routes inside the rack: to a module (by
    // its chain and place), a macro or Morph.
    for (size_t c = 0; c < chains.size() && c < chainsJson.size(); ++c)
    {
        nlohmann::json modulators = nlohmann::json::array();
        for (size_t i = 0; i < chains[c].items.size(); ++i)
        {
            auto* m = chains[c].items[i].modulator;
            if (m == nullptr)
                continue;
            int before = -1;
            for (size_t j = i + 1; j < chains[c].items.size() && before < 0; ++j)
                if (auto* p = chains[c].items[j].plugin)
                    if (const auto at = places.find(p->itemID.getRawID()); at != places.end())
                        before = at->second.second;
            nlohmann::json routes = nlohmann::json::array();
            for (const auto& r : impl_->routesOf(*m))
            {
                nlohmann::json route{{"amount", r.assignment->value.get()}};
                if (r.param.device == rackId)
                    route["macro"] = r.param.paramId; // "macro1" ... "morph"
                else if (const auto at = places.find(r.param.device); at != places.end())
                {
                    route["chain"] = at->second.first;
                    route["module"] = at->second.second;
                    route["param"] = r.param.paramId;
                }
                else
                    continue;
                routes.push_back(std::move(route));
            }
            modulators.push_back(
                {{"type", std::string(m->info().id)},
                 {"name", m->getName().toStdString()},
                 {"on", m->enabledParam == nullptr || m->enabledParam->getCurrentValue() >= 0.5f},
                 {"before", before},
                 {"state", toJson(Impl::modulatorState(*m))},
                 {"routes", std::move(routes)}});
        }
        if (!modulators.empty())
            chainsJson[c]["modulators"] = std::move(modulators);
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
        nlohmann::json m{{"name", macro.name}, {"value", macro.value}, {"mappings", std::move(mappings)}};
        if (macro.locked)
            m["locked"] = true;
        macros.push_back(std::move(m));
    }
    json["macros"] = std::move(macros);
    // Its snapshots and Morph (racks.md §18.9), when it has some.
    if (!info.snapshots.empty())
    {
        auto snapshots = nlohmann::json::array();
        for (const auto& s : info.snapshots)
            snapshots.push_back({{"name", s.name}, {"macros", s.macros}});
        json["snapshots"] = std::move(snapshots);
        json["morph"] = {{"on", info.morph.on},
                         {"a", info.morph.a},
                         {"b", info.morph.b},
                         {"position", info.morph.position}};
    }

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
        if (macro.value("locked", false))
            rack.getMacroParameters()[static_cast<int>(i)]->state.setProperty(Impl::lockedId, true, um);
    }
    // Its modulators (modulators.md §14): made on the track, moved into their chains where they
    // sat, set up, and routed inside the rack.
    for (size_t c = 0; c < chainsJson.size(); ++c)
        for (const auto& mj : chainsJson[c].value("modulators", nlohmann::json::array()))
        {
            const auto kind = model::modulatorKind(mj.value("type", std::string()));
            if (!kind)
                continue;
            const auto id = addModulator(place.channel, *kind, -1);
            auto* m = impl_->modulator(id);
            if (m == nullptr)
                continue;
            if (auto state = stateFromJson(mj.value("state", nlohmann::json::object())))
                impl_->applyModulatorState(*m, *state, {});
            if (!mj.value("on", true) && m->enabledParam != nullptr)
                m->enabledParam->setParameter(0.0f, juce::sendNotificationSync);
            // Before the module it sat before: its place among the chain's items now.
            int index = -1;
            if (const auto before = ids.find({static_cast<int>(c), mj.value("before", -1)});
                before != ids.end())
                if (const auto at = placeOf(before->second); at && at->rack == rackId)
                    index = at->index;
            impl_->putModulatorInRack(rack, *m, static_cast<int>(c), index);
            for (const auto& r : mj.value("routes", nlohmann::json::array()))
            {
                std::string key;
                if (r.contains("macro"))
                    key = "m" + std::to_string(rackId) + "/" + r["macro"].get<std::string>();
                else if (const auto module = ids.find({r.value("chain", 0), r.value("module", 0)});
                         module != ids.end())
                    key = "m" + std::to_string(module->second) + "/" + r.value("param", std::string());
                if (!key.empty())
                    routeModulator(id, {place.channel, key}, r.value("amount", 0.5f));
            }
        }
    // Its snapshots and Morph (racks.md §18.9); a file from before step 2 has none.
    if (const auto snapshotsJson = json.value("snapshots", nlohmann::json::array()); !snapshotsJson.empty())
    {
        std::vector<model::RackInfo::Snapshot> snapshots;
        for (const auto& s : snapshotsJson)
        {
            if (!s.is_object() || snapshots.size() == static_cast<size_t>(model::RackInfo::maxSnapshots))
                continue;
            model::RackInfo::Snapshot snapshot{.name = s.value("name", std::string())};
            const auto positions = s.value("macros", nlohmann::json::array());
            for (size_t m = 0; m < snapshot.macros.size() && m < positions.size(); ++m)
                if (positions[m].is_number())
                    snapshot.macros[m] = std::clamp(positions[m].get<float>(), 0.0f, 1.0f);
            snapshots.push_back(std::move(snapshot));
        }
        Impl::setSnapshots(rack, snapshots, um);
        const auto morph = json.value("morph", nlohmann::json::object());
        const int count = static_cast<int>(snapshots.size());
        const auto pick = [count](int v) { return v >= 0 && v < count ? v : -1; };
        const int a = pick(morph.value("a", -1)), b = pick(morph.value("b", -1));
        rack.state.setProperty(Impl::morphAId, a, um);
        rack.state.setProperty(Impl::morphBId, b, um);
        rack.state.setProperty(Impl::morphOnId, morph.value("on", false) && a >= 0 && b >= 0, um);
        if (auto* m = Impl::morphOf(rack))
            m->setParameter(std::clamp(morph.value("position", 0.0f), 0.0f, 1.0f),
                            juce::sendNotificationSync);
        impl_->syncMorph(rack);
    }
    loaded.rack = rackId;
    return loaded;
}
} // namespace fd::engine
