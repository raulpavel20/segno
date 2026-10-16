#include "ui/BrowserDrops.h"

#include "model/DrumKit.h"
#include "model/Modulators.h"
#include "model/Racks.h"

#include <fd/device/Registry.h>

namespace fd::app::browser
{
namespace
{
using Kind = model::BrowserKind;

/// The device a key names, as the chain's + menu lists it.
std::optional<engine::AvailableDevice> deviceOf(engine::Session& session, const std::string& key)
{
    for (auto& d : session.availableDevices())
        if (d.key == key)
            return d;
    return std::nullopt;
}

const Preset* presetOf(engine::Session& session, const Ref& ref)
{
    const auto* info = engine::Session::nativeInfo(ref.key); // a module's or a modulator's
    if (info == nullptr)
        return nullptr;
    auto& library = session.presetLibrary();
    library.rescan();
    const auto* p = library.find(*info, ref.preset);
    return p != nullptr && !presetTooNew(*info, *p) ? p : nullptr;
}
} // namespace

bool loadPreset(engine::Session& session, std::uint64_t module, const Ref& ref)
{
    const auto* info = engine::Session::nativeInfo(ref.key);
    const auto* preset = presetOf(session, ref);
    auto host = session.presetHost(module);
    if (info == nullptr || preset == nullptr || host == nullptr)
        return false;
    PresetController(*info, *host).load(*preset);
    return true;
}

namespace
{

/// The step's name, for what's put in: "Add Compressor", "Insert Kick 808".
bool isModule(Kind kind)
{
    return kind == Kind::Module || kind == Kind::Plugin || kind == Kind::Preset;
}

bool isClip(Kind kind)
{
    return kind == Kind::Sample || kind == Kind::Midi;
}

/// A wavetable file's name (a factory table's file is named for it).
std::string tableName(const Ref& ref)
{
    return ref.file.getFileNameWithoutExtension().toStdString();
}

/// A wavetable on a track: a Wavetable with it on its first oscillator, in place of the track's
/// instrument (or the track's Wavetable, if it has one, takes it).
Result tableOnTrack(engine::Session& session, const Ref& ref, std::uint64_t channel)
{
    const auto old = session.instrumentOf(channel);
    const auto name = tableName(ref);
    if (old && old->deviceId == "wavetable")
        return {session.loadWavetable(old->id, 0, ref.file), {}, 0, old->id};
    const engine::Session::EditGroup group(session, old ? "Replace " + old->name + " with Wavetable"
                                                        : "Add Wavetable with " + name);
    const auto synth = session.replaceInstrument(channel, "fd:wavetable");
    if (synth == 0 || !session.loadWavetable(synth, 0, ref.file))
        return {};
    Result r{true, {}, 0, synth};
    if (old)
        r.toast = "Replaced " + juce::String(old->name) + " with Wavetable";
    return r;
}

/// A sample or a multisample in a Sampler of its own, in place of the track's instrument.
Result samplerWith(engine::Session& session, const Ref& ref, std::uint64_t channel)
{
    const auto old = session.instrumentOf(channel);
    const engine::Session::EditGroup group(
        session, old ? "Replace " + old->name + " with Sampler"
                     : "Add Sampler with " + ref.file.getFileNameWithoutExtension().toStdString());
    const auto sampler = session.replaceInstrument(channel, "fd:sampler");
    if (sampler == 0 || !session.loadSample(sampler, ref.file))
        return {};
    Result r{true, {}, 0, sampler};
    if (old && old->deviceId != "sampler")
        r.toast = "Replaced " + juce::String(old->name) + " with Sampler";
    return r;
}

std::string stepName(engine::Session& session, const Ref& ref)
{
    if (isModule(ref.kind))
    {
        if (const auto d = deviceOf(session, ref.key))
            return "Add " + d->name;
        return "Add module";
    }
    if (isClip(ref.kind))
        return "Insert " + ref.file.getFileNameWithoutExtension().toStdString();
    if (ref.kind == Kind::Wavetable)
        return "Add Wavetable with " + tableName(ref);
    if (ref.kind == Kind::Multisample)
        return "Add Sampler with " + ref.file.getFileNameWithoutExtension().toStdString();
    if (ref.kind == Kind::Rack)
        return "Add rack " + ref.file.getFileNameWithoutExtension().toStdString();
    return {};
}

/// A module (with a preset) on a channel: an instrument in place of the channel's own, anything
/// else at `index` (-1: the end).
Result addModule(engine::Session& session, const Ref& ref, std::uint64_t channel, int index)
{
    const auto device = deviceOf(session, ref.key);
    if (!device)
        return {};
    Result r;
    const auto old = device->instrument ? session.instrumentOf(channel) : std::nullopt;
    // An effect plays after the instrument: dropped ahead of it, it goes right after it. A
    // modulator goes where it's dropped (modulators.md §6).
    if (!device->instrument && !device->modulator && index >= 0)
        if (const auto* c = session.snapshot()->channel(channel))
            for (size_t i = 0; i < c->devices.size(); ++i)
                if (c->devices[i].instrument && index <= static_cast<int>(i))
                    index = static_cast<int>(i) + 1;
    const engine::Session::EditGroup group(session, old ? "Replace " + old->name + " with " + device->name
                                                        : stepName(session, ref));
    r.module = device->instrument ? session.replaceInstrument(channel, ref.key)
                                  : session.addDevice(channel, ref.key, index);
    if (r.module == 0)
        return {};
    if (ref.kind == Kind::Preset)
        loadPreset(session, r.module, ref);
    r.done = true;
    if (old)
        r.toast = "Replaced " + juce::String(old->name) + " with " + juce::String(device->name);
    return r;
}

/// A rack file in a channel's chain at `index` (-1: the end): an instrument rack in place of the
/// channel's instrument, as any instrument (racks.md §7). What wasn't installed is said.
Result rackIn(engine::Session& session, const Ref& ref, std::uint64_t channel, int index)
{
    const auto info = engine::Session::readRackFile(ref.file);
    if (!info)
        return {};
    const auto old = info->instrument ? session.instrumentOf(channel) : std::nullopt;
    const engine::Session::EditGroup group(session, old ? "Replace " + old->name + " with " + info->name
                                                        : "Add rack " + info->name);
    if (old)
    {
        const auto place = session.placeOf(old->id);
        index = place ? place->index : 0;
        session.removeDevice(old->id);
    }
    else if (index >= 0)
        // An effect rack plays after the instrument: dropped ahead of it, it goes right after it.
        if (const auto* c = session.snapshot()->channel(channel))
            for (size_t i = 0; i < c->devices.size(); ++i)
                if (c->devices[i].instrument && index <= static_cast<int>(i))
                    index = static_cast<int>(i) + 1;
    const auto loaded = session.loadRack(ref.file, {.channel = channel, .index = index});
    if (loaded.rack == 0)
        return {};
    Result r{true, {}, 0, loaded.rack};
    if (old)
        r.toast =
            "Replaced " + juce::String(old->name) + " with " + juce::String::fromUTF8(info->name.c_str());
    if (!loaded.missing.empty())
    {
        juce::StringArray names;
        for (const auto& m : loaded.missing)
            names.add(juce::String::fromUTF8(m.c_str()));
        r.toast = "Not installed, left out: " + names.joinIntoString(", ");
    }
    return r;
}

std::optional<model::DeviceSlot> slotOf(engine::Session& session, std::uint64_t module)
{
    const auto snapshot = session.snapshot();
    const auto find = [module](const model::TrackInfo& t) -> std::optional<model::DeviceSlot>
    {
        if (const auto* d = model::findDevice(t.devices, module)) // in a rack too
            return *d;
        return std::nullopt;
    };
    for (const auto& t : snapshot->tracks)
        if (auto d = find(t))
            return d;
    return find(snapshot->master);
}
} // namespace

std::optional<std::string> itemOf(const juce::var& dragDescription)
{
    const auto text = dragDescription.toString().toStdString();
    if (!text.starts_with(dragPrefix))
        return std::nullopt;
    return text.substr(dragPrefix.size());
}

std::optional<Ref> parse(const std::string& id)
{
    Ref r;
    const auto after = [&id](std::string_view prefix) { return id.substr(prefix.size()); };
    if (id.starts_with("module:"))
    {
        r.kind = Kind::Module;
        r.key = after("module:");
    }
    else if (id.starts_with("plugin:"))
    {
        r.kind = Kind::Plugin;
        r.key = after("plugin:");
    }
    else if (id.starts_with("preset:"))
    {
        const auto rest = after("preset:");
        const auto bar = rest.find('|');
        if (bar == std::string::npos)
            return std::nullopt;
        r.kind = Kind::Preset;
        r.key = rest.substr(0, bar);
        r.preset = rest.substr(bar + 1);
    }
    else if (id.starts_with("file:"))
    {
        const auto path = after("file:");
        const auto kind = model::fileKind(path);
        if (!kind)
            return std::nullopt;
        r.kind = *kind;
        r.file = juce::File(juce::String::fromUTF8(path.c_str()));
    }
    else if (id.starts_with("folder:"))
    {
        r.kind = Kind::Folder;
        r.file = juce::File(juce::String::fromUTF8(after("folder:").c_str()));
    }
    else
        return std::nullopt;
    return r;
}

bool fitsChannel(const std::string& item, bool holdsClips)
{
    const auto ref = parse(item);
    if (!ref)
        return false;
    return isModule(ref->kind) || ref->kind == Kind::Rack ||
           ((isClip(ref->kind) || ref->kind == Kind::Wavetable || ref->kind == Kind::Multisample) &&
            holdsClips);
}

bool landsOn(const std::string& item, const model::DeviceSlot& module)
{
    const auto ref = parse(item);
    if (!ref)
        return false;
    if (ref->kind == Kind::Preset)
        return ref->key == "fd:" + module.deviceId;
    if (ref->kind == Kind::Wavetable)
        return module.deviceId == "wavetable";
    // A sample or a multisample: a Sampler's, or a Granular's source (its Source 1).
    return (ref->kind == Kind::Sample || ref->kind == Kind::Multisample) &&
           (module.deviceId == "sampler" || module.deviceId == "granular");
}

Result onChannel(engine::Session& session, const std::string& item, std::uint64_t channel, double seconds)
{
    const auto ref = parse(item);
    if (!ref)
        return {};
    if (isModule(ref->kind))
        return addModule(session, *ref, channel, -1);
    if (ref->kind == Kind::Rack)
        return rackIn(session, *ref, channel, -1);
    if (ref->kind == Kind::Wavetable)
        return tableOnTrack(session, *ref, channel);
    if (ref->kind == Kind::Multisample)
        return samplerWith(session, *ref, channel);
    if (!isClip(ref->kind))
        return {};
    const engine::Session::EditGroup group(session, stepName(session, *ref));
    const auto clip = ref->kind == Kind::Sample ? session.insertAudioFile(channel, ref->file, seconds)
                                                : session.insertMidiFile(channel, ref->file, seconds);
    return {clip != 0, {}, 0, 0};
}

Result inChain(engine::Session& session, const std::string& item, std::uint64_t channel, int index,
               std::optional<std::uint64_t> onModule)
{
    const auto ref = parse(item);
    if (!ref)
        return {};
    if (onModule)
        if (const auto slot = slotOf(session, *onModule); slot && landsOn(item, *slot))
        {
            if (ref->kind == Kind::Preset)
                return {loadPreset(session, *onModule, *ref), {}, 0, *onModule};
            if (ref->kind == Kind::Wavetable)
                return {session.loadWavetable(*onModule, 0, ref->file), {}, 0, *onModule};
            if (slot->deviceId == "granular")
                return {session.loadGranularSource(*onModule, 0, ref->file).has_value(), {}, 0, *onModule};
            return {session.loadSample(*onModule, ref->file).has_value(), {}, 0, *onModule};
        }
    if (isModule(ref->kind))
        return addModule(session, *ref, channel, index);
    if (ref->kind == Kind::Rack)
        return rackIn(session, *ref, channel, index);
    if (ref->kind == Kind::Wavetable)
        return tableOnTrack(session, *ref, channel);
    if (ref->kind == Kind::Sample || ref->kind == Kind::Multisample)
        return samplerWith(session, *ref, channel);
    return {};
}

Result inRack(engine::Session& session, const std::string& item, const engine::Session::ChainPlace& place)
{
    const auto ref = parse(item);
    if (!ref)
        return {};
    const bool sample = ref->kind == Kind::Sample || ref->kind == Kind::Multisample;
    if (!isModule(ref->kind) && !sample && ref->kind != Kind::Wavetable)
        return {};
    const engine::Session::EditGroup group(
        session, sample ? "Add Sampler with " + ref->file.getFileNameWithoutExtension().toStdString()
                        : stepName(session, *ref));
    const auto key = isModule(ref->kind) ? ref->key
                     : sample            ? std::string("fd:sampler")
                                         : std::string("fd:wavetable");
    const auto module = session.addDevice(place, key);
    if (module == 0)
        return {};
    if (ref->kind == Kind::Preset)
        loadPreset(session, module, *ref);
    else if (sample && !session.loadSample(module, ref->file))
        return {};
    else if (ref->kind == Kind::Wavetable && !session.loadWavetable(module, 0, ref->file))
        return {};
    return {true, {}, 0, module};
}

bool fitsPad(const std::string& item)
{
    const auto ref = parse(item);
    if (!ref)
        return false;
    if (ref->kind == Kind::Sample || ref->kind == Kind::Multisample || ref->kind == Kind::Wavetable)
        return true;
    if (!isModule(ref->kind))
        return false;
    // Not a kit (racks don't nest), not a modulator (it moves controls, it isn't a pad's sound).
    return ref->key != "fd:drum-kit" &&
           !(ref->key.starts_with("fd:") && model::modulatorKind(std::string_view(ref->key).substr(3)));
}

Result onPad(engine::Session& session, const std::string& item, std::uint64_t kit, int note)
{
    const auto ref = parse(item);
    if (!ref || !fitsPad(item))
        return {};
    if (ref->kind == Kind::Sample || ref->kind == Kind::Multisample)
    {
        const auto loaded = session.loadPads(kit, note, {ref->file});
        return {!loaded.empty(), {}, 0, 0};
    }
    const engine::Session::EditGroup group(session, ref->kind == Kind::Wavetable
                                                        ? "Add Wavetable with " + tableName(*ref)
                                                        : stepName(session, *ref));
    const auto key = ref->kind == Kind::Wavetable ? std::string("fd:wavetable") : ref->key;
    const auto module = session.addDevice(engine::Session::ChainPlace{.rack = kit, .pad = note}, key);
    if (module == 0)
        return {};
    if (ref->kind == Kind::Preset)
        loadPreset(session, module, *ref);
    else if (ref->kind == Kind::Wavetable && !session.loadWavetable(module, 0, ref->file))
        return {};
    return {true, {}, 0, module};
}

Result kitWith(engine::Session& session, const std::vector<juce::File>& samples, std::uint64_t channel)
{
    const auto old = session.instrumentOf(channel);
    const engine::Session::EditGroup group(session, old ? "Replace " + old->name + " with Drum Kit"
                                                        : "Add Drum Kit with " +
                                                              std::to_string(samples.size()) + " samples");
    const auto kit = session.replaceInstrument(channel, "fd:drum-kit");
    if (kit == 0 || session.loadPads(kit, model::kit::firstShown, samples).empty())
        return {};
    Result r{true, {}, 0, kit};
    if (old)
        r.toast = "Replaced " + juce::String(old->name) + " with Drum Kit";
    return r;
}

Result newTrack(engine::Session& session, const std::string& item, double seconds)
{
    const auto ref = parse(item);
    if (!ref || !fitsChannel(item, true))
        return {};
    const engine::Session::EditGroup group(session, stepName(session, *ref));
    const auto track = session.addTrack();
    if (track == 0)
        return {};
    auto r = onChannel(session, item, track, seconds);
    r.track = track;
    r.toast.clear(); // nothing was replaced on a new track
    return r;
}

Result onSelection(engine::Session& session, const std::string& item, std::optional<std::uint64_t> track,
                   std::optional<std::uint64_t> module, double seconds)
{
    if (module)
        if (const auto slot = slotOf(session, *module); slot && landsOn(item, *slot))
            return inChain(session, item, 0, -1, module);
    if (track)
    {
        const auto* channel = session.snapshot()->channel(*track);
        if (channel != nullptr && fitsChannel(item, channel->holdsClips()))
            return onChannel(session, item, *track, seconds);
    }
    return newTrack(session, item, seconds);
}
} // namespace fd::app::browser
