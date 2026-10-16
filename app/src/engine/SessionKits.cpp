// The Drum Kit (M3 P7, docs/design/drum-kit.md, ADR 0064): one instrument whose pads hold any
// instrument and effects, the Sampler by default. Underneath it's a rack (SessionRacks.cpp) whose
// type says it's a kit and whose chains are its pads: each starts with a PadInlet (its note, Plays
// and choke group), then its modules, then its mixer. An empty pad has no chain.
#include "engine/DevicePlugin.h"
#include "engine/SessionImpl.h"
#include "model/DrumKit.h"

#include <fd/device/Registry.h>

namespace fd::engine
{
namespace
{
std::uint64_t idOf(const te::Plugin& p)
{
    return p.itemID.getRawID();
}

/// A pad's instrument: its first module, if it plays notes.
te::Plugin* instrumentOn(const Session::Impl::RackChain& chain)
{
    return !chain.modules.empty() && chain.modules.front()->isSynth() ? chain.modules.front() : nullptr;
}

/// A file's name, for a pad's name: "Kick 1".
std::string nameOf(const juce::File& file)
{
    return file.getFileNameWithoutExtension().toStdString();
}
} // namespace

// ---- the kit's shape ------------------------------------------------------------------------

bool Session::Impl::isKit(const te::RackType& rack)
{
    return static_cast<bool>(rack.state.getProperty(kitId, false));
}

bool Session::Impl::isKit(const te::Plugin* plugin)
{
    const auto* type = rackTypeOf(plugin);
    return type != nullptr && isKit(*type);
}

te::RackType::Ptr Session::Impl::makeKit()
{
    auto rack = edit->getRackList().addNewRack();
    if (rack == nullptr)
        return {};
    rack->rackName = "Drum Kit";
    rack->state.setProperty(kitId, true, &edit->getUndoManager());
    auto& macros = rack->getMacroParameterListForWriting();
    // At 0 %, so a knob mapped From where it is stays there (as a rack's, racks.md §17).
    for (int i = 0; i < model::RackInfo::macroCount; ++i)
        if (auto* macro = macros.createMacroParameter())
        {
            macro->macroName = "Macro " + juce::String(i + 1);
            macro->setParameter(0.0f, juce::sendNotificationSync);
        }
    return rack;
}

int Session::Impl::padChain(const te::RackType& kit, int note)
{
    const auto chains = rackChains(kit);
    for (size_t c = 0; c < chains.size(); ++c)
        if (chains[c].inlet != nullptr && chains[c].inlet->note() == note)
            return static_cast<int>(c);
    return -1;
}

int Session::Impl::ensurePad(te::RackType& kit, int note)
{
    if (note < 0 || note >= model::kit::padCount)
        return -1;
    if (const int c = padChain(kit, note); c >= 0)
        return c;
    // A chain of its own: its mixer (numbered as the next chain), then its inlet on it.
    auto* um = &edit->getUndoManager();
    const int chain = static_cast<int>(rackChains(kit).size());
    addChain(kit);
    auto inlet = edit->getPluginCache().createNewPlugin(PadInlet::xmlTypeName, {});
    if (inlet == nullptr || !kit.addPlugin(inlet, {0.1f, 0.5f}, false))
        return -1;
    inlet->state.getParent().setProperty(chainId, chain, um);
    auto& in = dynamic_cast<PadInlet&>(*inlet);
    in.setNote(note, um);
    in.setPlays(60, um);
    return chain;
}

void Session::Impl::prunePads(te::RackType& kit)
{
    auto* um = &edit->getUndoManager();
    for (bool removed = true; removed;)
    {
        removed = false;
        const auto chains = rackChains(kit);
        for (size_t c = 0; c < chains.size(); ++c)
            if (chains[c].modules.empty() && chains[c].modulators.empty())
            {
                removeChain(kit, static_cast<int>(c));
                removed = true;
                break;
            }
    }
    wireRack(kit, um);
}

std::uint64_t Session::addDrumKit(std::uint64_t channel, int index)
{
    auto* list = impl_->plugins(channel);
    if (list == nullptr)
        return 0;
    const EditGroup group(*this, "Add Drum Kit");
    auto kit = impl_->makeKit();
    if (kit == nullptr)
        return 0;
    Impl::wireRack(*kit, &impl_->edit->getUndoManager());
    auto instance = impl_->edit->getPluginCache().getOrCreatePluginFor(te::RackInstance::create(*kit));
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
    if (auto* track = impl_->chainTrackOf(*instance))
        if (const auto items = Impl::chainItems(*track);
            std::any_of(items.begin(), items.end(), [](const auto& item) { return item.modulator != nullptr; }))
            impl_->placeInChain(*track, Impl::ChainItem{instance.get(), nullptr}, index);
    // Your default kit, if you set one (drum-kit.md §9), in the same step.
    if (const auto start = PresetController::startingPoint(drumKit(), presetLibrary()))
        if (auto* placed = dynamic_cast<te::RackInstance*>(instance.get()))
            impl_->applyKitState(*this, *placed, start->first, start->second);
    return idOf(*instance);
}

// ---- pads -----------------------------------------------------------------------------------

namespace
{
/// The kit with this id: its instance and type (null if it isn't one).
te::RackType* kitTypeOf(Session::Impl& impl, std::uint64_t kit)
{
    auto* instance = impl.rackInstance(kit);
    return instance != nullptr && instance->type != nullptr && Session::Impl::isKit(*instance->type)
               ? instance->type.get()
               : nullptr;
}
} // namespace

std::uint64_t Session::addToPad(const ChainPlace& place, const std::string& key)
{
    auto* kit = kitTypeOf(*impl_, place.rack);
    auto* instance = impl_->rackInstance(place.rack);
    if (kit == nullptr || instance == nullptr || place.pad < 0 || place.pad >= model::kit::padCount)
        return 0;
    const EditGroup group(*this, "Add module");
    const bool fresh = Impl::padChain(*kit, place.pad) < 0;
    int chain = impl_->ensurePad(*kit, place.pad);
    if (chain < 0)
        return 0;
    // Made in the track's chain as any module (your default applies), then onto the pad.
    auto* owner = impl_->chainTrackOf(*instance);
    const auto id = addDevice(owner != nullptr ? owner->itemID.getRawID() : impl_->masterId(), key, -1);
    auto* p = impl_->plugin(id);
    if (id == 0 || Impl::rackTypeOf(p) != nullptr) // nothing made, or a rack: never on a pad
    {
        if (id != 0)
            removeDevice(id);
        impl_->prunePads(*kit);
        return 0;
    }
    // An instrument goes first, in place of the pad's own (drum-kit.md §6).
    const bool instrument = p != nullptr && p->isSynth();
    std::uint64_t replaced = 0;
    if (instrument)
        if (auto* old = instrumentOn(Impl::rackChains(*kit)[static_cast<size_t>(chain)]))
            replaced = idOf(*old);
    ChainPlace at = place;
    at.chain = chain;
    at.pad = -1;
    if (instrument)
        at.index = 0;
    if (!moveDevice(id, at))
    {
        removeDevice(id);
        impl_->prunePads(*kit);
        return 0;
    }
    if (replaced != 0)
        removeDevice(replaced);
    // A new pad is named for what's on it.
    if (fresh && p != nullptr)
        if (chain = Impl::padChain(*kit, place.pad); chain >= 0)
            setRackChain(place.rack, chain, {.name = p->getName().toStdString()});
    return id;
}

bool Session::moveToPad(std::uint64_t deviceId, const ChainPlace& place)
{
    auto* kit = kitTypeOf(*impl_, place.rack);
    auto* p = impl_->plugin(deviceId);
    if (kit == nullptr || place.pad < 0 || place.pad >= model::kit::padCount ||
        (p != nullptr && Impl::rackTypeOf(p) != nullptr))
        return false; // a rack or a kit never goes on a pad
    const EditGroup group(*this, "Move module");
    const int chain = impl_->ensurePad(*kit, place.pad);
    if (chain < 0)
        return false;
    // An instrument goes first, in place of the pad's own.
    const bool instrument = p != nullptr && p->isSynth();
    std::uint64_t replaced = 0;
    if (instrument)
        if (auto* old = instrumentOn(Impl::rackChains(*kit)[static_cast<size_t>(chain)]); old != nullptr && old != p)
            replaced = idOf(*old);
    ChainPlace at = place;
    at.chain = chain;
    at.pad = -1;
    if (instrument)
        at.index = 0;
    const bool moved = moveDevice(deviceId, at);
    if (moved && replaced != 0)
        removeDevice(replaced);
    impl_->prunePads(*kit);
    return moved;
}

bool Session::setPad(std::uint64_t kitId, int note, const PadEdit& e)
{
    auto* kit = kitTypeOf(*impl_, kitId);
    const int chain = kit != nullptr ? Impl::padChain(*kit, note) : -1;
    if (chain < 0)
        return false;
    const auto chains = Impl::rackChains(*kit);
    auto* inlet = chains[static_cast<size_t>(chain)].inlet;
    // The mixer's parts are a chain's (one step each, solo rewiring the kit).
    if (e.name || e.on || e.solo || e.volumeDb || e.pan)
    {
        const EditGroup group(*this, e.name       ? "Rename pad"
                                     : e.on       ? "Pad on/off"
                                     : e.solo     ? "Solo pad"
                                     : e.volumeDb ? "Pad volume"
                                                  : "Pad pan");
        setRackChain(kitId, chain, {.name = e.name, .on = e.on, .solo = e.solo, .volumeDb = e.volumeDb, .pan = e.pan});
    }
    if (e.plays || e.choke)
    {
        impl_->beginEdit(e.plays ? "Pad plays" : "Choke group");
        auto* um = &impl_->edit->getUndoManager();
        if (e.plays)
            inlet->setPlays(*e.plays, um);
        if (e.choke)
            inlet->setChoke(*e.choke, um);
        PadInlet::updateChokes(*kit);
    }
    return true;
}

std::vector<int> Session::loadPads(std::uint64_t kitId, int note, const std::vector<juce::File>& files)
{
    auto* kit = kitTypeOf(*impl_, kitId);
    std::vector<int> loaded;
    if (kit == nullptr || files.empty() || note < 0 || note >= model::kit::padCount)
        return loaded;
    const auto stepName = files.size() == 1 ? "Load " + nameOf(files.front()) + " on " + model::kit::noteName(note)
                                            : "Load " + std::to_string(files.size()) + " samples on pads";
    const EditGroup group(*this, stepName);
    for (size_t i = 0; i < files.size() && note + static_cast<int>(i) < model::kit::padCount; ++i)
    {
        const int pad = note + static_cast<int>(i);
        const auto& file = files[i];
        // The pad's Sampler takes it, its settings kept; else a Sampler of its own, in One-shot
        // mode, in place of the pad's instrument (drum-kit.md §6).
        std::uint64_t sampler = 0;
        if (const int chain = Impl::padChain(*kit, pad); chain >= 0)
            if (auto* instrument = instrumentOn(Impl::rackChains(*kit)[static_cast<size_t>(chain)]))
                if (auto* native = dynamic_cast<DevicePlugin*>(instrument);
                    native != nullptr && native->device().info().id == "sampler")
                    sampler = idOf(*native);
        const bool fresh = sampler == 0;
        if (fresh)
            sampler = addDevice(ChainPlace{.rack = kitId, .index = 0, .pad = pad}, "fd:sampler");
        auto* native = dynamic_cast<DevicePlugin*>(impl_->plugin(sampler));
        if (native == nullptr || !loadSample(sampler, file))
            continue;
        const auto& params = native->device().params();
        const auto& specs = native->device().info().params;
        // One-shot (a new pad's), and its Root Key at the pad's Plays: it plays at its own pitch,
        // whatever its file says.
        const int chain = Impl::padChain(*kit, pad);
        auto* inlet = Impl::rackChains(*kit)[static_cast<size_t>(chain)].inlet;
        if (const auto root = params.indexOf("root"))
            native->setStoredValue(*root, specs[*root].toNormalized(static_cast<float>(inlet->plays())));
        if (fresh)
        {
            if (const auto mode = params.indexOf("mode"))
                native->setStoredValue(*mode, specs[*mode].toNormalized(1.0f));
            setRackChain(kitId, chain, {.name = nameOf(file)});
        }
        loaded.push_back(pad);
    }
    return loaded;
}

bool Session::movePad(std::uint64_t kitId, int from, int to, bool copy)
{
    auto* kit = kitTypeOf(*impl_, kitId);
    if (kit == nullptr || from == to || to < 0 || to >= model::kit::padCount)
        return false;
    const int source = Impl::padChain(*kit, from);
    if (source < 0)
        return false;
    auto* um = &impl_->edit->getUndoManager();
    if (copy)
    {
        impl_->beginEdit("Copy pad");
        return impl_->placePad(*kit, to, impl_->padState(*kit, source));
    }
    impl_->beginEdit("Move pad");
    const int target = Impl::padChain(*kit, to);
    const auto chains = Impl::rackChains(*kit);
    chains[static_cast<size_t>(source)].inlet->setNote(to, um);
    if (target >= 0) // swapped with what was there
        chains[static_cast<size_t>(target)].inlet->setNote(from, um);
    PadInlet::updateChokes(*kit);
    if (auto* instance = impl_->rackInstance(kitId))
        instance->state.setProperty(Impl::selectedPadId, to, nullptr); // the pad moved is still the one selected
    return true;
}

bool Session::clearPad(std::uint64_t kitId, int note)
{
    auto* kit = kitTypeOf(*impl_, kitId);
    const int chain = kit != nullptr ? Impl::padChain(*kit, note) : -1;
    if (chain < 0)
        return false;
    impl_->beginEdit("Clear pad");
    impl_->removeChain(*kit, chain);
    Impl::wireRack(*kit, &impl_->edit->getUndoManager());
    return true;
}

juce::ValueTree Session::Impl::padState(te::RackType& kit, int chain)
{
    // The pad's chain as the kit holds it: its plugins' places (inlet, modules, mixer), copied.
    juce::ValueTree copied("FDPAD");
    for (auto* p : kit.getPlugins())
        if (static_cast<int>(p->state.getParent().getProperty(chainId, -1)) == chain)
        {
            p->flushPluginStateToValueTree();
            copied.appendChild(p->state.getParent().createCopy(), nullptr);
        }
    return copied;
}

bool Session::Impl::placePad(te::RackType& kit, int note, const juce::ValueTree& pad)
{
    auto* um = &edit->getUndoManager();
    if (const int there = padChain(kit, note); there >= 0)
        removeChain(kit, there); // over what was there
    const int chain = static_cast<int>(rackChains(kit).size());
    // Each plugin a copy of its own, with new ids, on the new chain.
    for (auto place : pad)
    {
        auto state = place.getChildWithName(te::IDs::PLUGIN).createCopy();
        if (!state.isValid())
            continue;
        te::EditItemID::remapIDs(state, nullptr, *edit);
        auto plugin = edit->getPluginCache().getOrCreatePluginFor(state);
        if (plugin == nullptr || !kit.addPlugin(plugin, {0.5f, 0.5f}, false))
            continue;
        auto newPlace = plugin->state.getParent();
        for (int i = 0; i < place.getNumProperties(); ++i)
            if (const auto id = place.getPropertyName(i); id.toString().startsWith("fd"))
                newPlace.setProperty(id, place[id], um);
        newPlace.setProperty(chainId, chain, um);
        if (auto* inlet = dynamic_cast<PadInlet*>(plugin.get()))
            inlet->setNote(note, um);
    }
    prunePads(kit); // wired, its chokes worked out; nothing placed leaves nothing
    return padChain(kit, note) >= 0;
}

bool Session::copyPad(std::uint64_t kitId, int note)
{
    auto* kit = kitTypeOf(*impl_, kitId);
    const int chain = kit != nullptr ? Impl::padChain(*kit, note) : -1;
    if (chain < 0)
        return false;
    impl_->padClipboard = impl_->padState(*kit, chain);
    return true;
}

bool Session::canPastePad() const
{
    return impl_->padClipboard.isValid() && impl_->padClipboard.getNumChildren() > 0;
}

bool Session::pastePad(std::uint64_t kitId, int note)
{
    auto* kit = kitTypeOf(*impl_, kitId);
    if (kit == nullptr || !canPastePad() || note < 0 || note >= model::kit::padCount)
        return false;
    impl_->beginEdit("Paste pad");
    return impl_->placePad(*kit, note, impl_->padClipboard);
}

void Session::selectPad(std::uint64_t kitId, int note)
{
    if (auto* instance = impl_->rackInstance(kitId); instance != nullptr && kitTypeOf(*impl_, kitId) != nullptr)
        instance->state.setProperty(Impl::selectedPadId, std::clamp(note, 0, model::kit::padCount - 1), nullptr);
}

void Session::showPads(std::uint64_t kitId, int from)
{
    if (auto* instance = impl_->rackInstance(kitId); instance != nullptr && kitTypeOf(*impl_, kitId) != nullptr)
        instance->state.setProperty(Impl::padsFromId, model::kit::shownFrom(from), nullptr);
}

std::array<std::uint32_t, 128> Session::padHits(std::uint64_t kitId) const
{
    std::array<std::uint32_t, 128> hits{};
    if (auto* kit = kitTypeOf(*impl_, kitId))
        for (const auto& c : Impl::rackChains(*kit))
            if (c.inlet != nullptr && c.inlet->note() >= 0 && c.inlet->note() < 128)
                hits[static_cast<size_t>(c.inlet->note())] = c.inlet->hits();
    return hits;
}
} // namespace fd::engine

// ---- kits as presets (drum-kit.md §9) ---------------------------------------------------------

namespace fd::engine
{
namespace
{
constexpr std::uint32_t kKitStateVersion = 1;
} // namespace

DeviceState Session::Impl::kitState(Session& session, te::RackInstance& instance)
{
    DeviceState s;
    s.deviceId = std::string(drumKit().id);
    s.stateVersion = kKitStateVersion;
    if (instance.type == nullptr)
        return s;
    auto& kit = *instance.type;
    const auto macros = kit.getMacroParameters();
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
        s.params["macro" + std::to_string(i + 1)] = macros[i]->getCurrentBaseValue() * 100.0f;

    // The pads, lowest first, each with its modules as a rack file holds them (racks.md §7).
    auto chains = rackChains(kit);
    std::sort(chains.begin(), chains.end(),
              [](const RackChain& a, const RackChain& b) { return a.inlet->note() < b.inlet->note(); });
    std::map<std::uint64_t, std::pair<int, int>> places; // a module's pad and place on it
    nlohmann::json pads = nlohmann::json::array();
    for (const auto& c : chains)
    {
        if (c.inlet == nullptr)
            continue;
        const int note = c.inlet->note();
        nlohmann::json modules = nlohmann::json::array();
        for (auto* p : c.modules)
        {
            nlohmann::json m{{"name", p->getName().toStdString()}, {"on", p->isEnabled()}};
            if (auto* native = dynamic_cast<DevicePlugin*>(p))
            {
                const auto host = session.presetHost(p->itemID.getRawID());
                if (host == nullptr)
                    continue;
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
            places[p->itemID.getRawID()] = {note, static_cast<int>(modules.size())};
            modules.push_back(std::move(m));
        }
        const auto* volume = c.volume;
        pads.push_back({{"note", note},
                        {"name", c.mixer[chainNameId].toString().toStdString()},
                        {"plays", c.inlet->plays()},
                        {"choke", c.inlet->choke()},
                        {"on", c.on()},
                        {"volumeDb", volume != nullptr ? volume->getVolumeDb() : 0.0f},
                        {"pan", volume != nullptr ? volume->pan.get() : 0.0f},
                        {"modules", std::move(modules)}});
    }
    // The macros: their names, and what they're mapped to by pad and place.
    nlohmann::json macroJson = nlohmann::json::array();
    const auto info = rackInfo(instance, [](te::Plugin& p) { return model::DeviceSlot{.id = p.itemID.getRawID()}; });
    for (const auto& macro : info->macros)
    {
        nlohmann::json mappings = nlohmann::json::array();
        for (const auto& m : macro.mappings)
            if (const auto it = places.find(m.module); it != places.end())
                mappings.push_back({{"pad", it->second.first},
                                    {"module", it->second.second},
                                    {"param", m.param},
                                    {"from", m.from},
                                    {"to", m.to},
                                    {"curve", m.curve}});
        macroJson.push_back({{"name", macro.name}, {"mappings", std::move(mappings)}});
    }
    s.extra = {{"pads", std::move(pads)}, {"macros", std::move(macroJson)}};
    return s;
}

void Session::Impl::applyKitState(Session& session, te::RackInstance& instance, const DeviceState& state,
                                  const PresetMemo& memo)
{
    if (instance.type == nullptr)
        return;
    auto& kit = *instance.type;
    auto* um = &edit->getUndoManager();
    // All of it in the step under way: what the session's own calls below would open joins it.
    ++groupDepth;
    const auto closeGroup = juce::ScopeGuard{[this] { --groupDepth; }};
    // Every pad goes, then the kit's are made again.
    while (!rackChains(kit).empty())
        removeChain(kit, 0);
    std::map<std::pair<int, int>, std::uint64_t> ids; // (pad, place) -> the module made
    const auto pads = state.extra.is_object() ? state.extra.value("pads", nlohmann::json::array()) : nlohmann::json::array();
    for (const auto& pad : pads)
    {
        const int note = pad.value("note", -1);
        const int chain = ensurePad(kit, note);
        if (chain < 0)
            continue;
        auto c = rackChains(kit)[static_cast<size_t>(chain)];
        c.inlet->setPlays(pad.value("plays", 60), um);
        c.inlet->setChoke(pad.value("choke", 0), um);
        if (const auto name = pad.value("name", std::string()); !name.empty())
            c.mixer.setProperty(chainNameId, juce::String::fromUTF8(name.c_str()), um);
        if (!pad.value("on", true))
            c.mixer.setProperty(chainOnId, false, um);
        if (c.volume != nullptr)
        {
            const float db = pad.value("volumeDb", 0.0f);
            if (db <= model::minusInfinityDb)
                c.volume->setSliderPos(0.0f);
            else
                c.volume->setVolumeDb(std::min(6.0f, db));
            c.volume->setPan(std::clamp(pad.value("pan", 0.0f), -1.0f, 1.0f));
        }
        const auto modules = pad.value("modules", nlohmann::json::array());
        for (size_t i = 0; i < modules.size(); ++i)
        {
            const auto& m = modules[i];
            te::Plugin::Ptr plugin;
            if (m.contains("device"))
            {
                plugin = edit->getPluginCache().createNewPlugin(DevicePlugin::typeNameFor(m["device"].get<std::string>()), {});
                if (auto* native = dynamic_cast<DevicePlugin*>(plugin.get()))
                    if (auto moduleState = stateFromJson(m.value("state", nlohmann::json::object())))
                    {
                        if (moduleState->stateVersion < native->device().info().stateVersion)
                            native->device().migrateState(*moduleState);
                        applyModuleState(*native, *moduleState, {});
                    }
            }
            else if (m.contains("plugin"))
            {
                const auto known = engine.getPluginManager().knownPluginList.getTypeForIdentifierString(
                    juce::String(m["plugin"].get<std::string>()));
                auto pluginState = juce::ValueTree::fromXml(juce::String::fromUTF8(m.value("state", "").c_str()));
                if (known != nullptr && pluginState.isValid())
                {
                    te::EditItemID::remapIDs(pluginState, nullptr, *edit);
                    plugin = edit->getPluginCache().getOrCreatePluginFor(pluginState);
                }
            }
            if (plugin == nullptr || !putInRack(kit, plugin, chain, -1))
                continue; // not installed: the rest of the pad loads
            plugin->setEnabled(m.value("on", true));
            ids[{note, static_cast<int>(i)}] = plugin->itemID.getRawID();
        }
    }
    prunePads(kit); // a pad whose modules weren't there goes; wired
    // The macros: their positions (the parameters) and names, then their mappings.
    const auto instanceId = instance.itemID.getRawID();
    const auto macros = kit.getMacroParameters();
    const auto macroJson =
        state.extra.is_object() ? state.extra.value("macros", nlohmann::json::array()) : nlohmann::json::array();
    for (int i = 0; i < model::RackInfo::macroCount && i < macros.size(); ++i)
    {
        const auto it = state.params.find("macro" + std::to_string(i + 1));
        macros[i]->setParameter(std::clamp((it != state.params.end() ? it->second : 0.0f) / 100.0f, 0.0f, 1.0f),
                                juce::sendNotificationSync);
        const auto name = i < static_cast<int>(macroJson.size()) ? macroJson[static_cast<size_t>(i)].value("name", std::string())
                                                                 : std::string();
        macros[i]->macroName = name.empty() ? "Macro " + juce::String(i + 1) : juce::String::fromUTF8(name.c_str());
        if (i < static_cast<int>(macroJson.size()))
            for (const auto& m : macroJson[static_cast<size_t>(i)].value("mappings", nlohmann::json::array()))
                if (const auto module = ids.find({m.value("pad", -1), m.value("module", -1)}); module != ids.end())
                    session.mapMacro(instanceId, i, module->second, m.value("param", std::string()), m.value("from", 0.0f),
                                     m.value("to", 1.0f), m.value("curve", 0.0f));
    }
    const auto json = memo.empty() ? juce::String() : juce::String(memo.toJson().dump());
    if (instance.state.getProperty(kitPresetId).toString() != json)
    {
        if (json.isEmpty())
            instance.state.removeProperty(kitPresetId, um);
        else
            instance.state.setProperty(kitPresetId, json, um);
    }
}
} // namespace fd::engine
