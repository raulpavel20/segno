#include "ui/BrowserDrops.h"

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
    const auto* factory = findDevice(ref.key.substr(3));
    if (factory == nullptr)
        return nullptr;
    auto& library = session.presetLibrary();
    library.rescan();
    const auto* p = library.find(factory->info(), ref.preset);
    return p != nullptr && !presetTooNew(factory->info(), *p) ? p : nullptr;
}

/// Loads `preset` into one of our modules, in the step under way (or its own, "Load preset …").
bool loadPreset(engine::Session& session, std::uint64_t module, const Ref& ref)
{
    const auto* factory = findDevice(ref.key.substr(3));
    const auto* preset = presetOf(session, ref);
    auto host = session.presetHost(module);
    if (factory == nullptr || preset == nullptr || host == nullptr)
        return false;
    PresetController(factory->info(), *host).load(*preset);
    return true;
}

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
    // An effect plays after the instrument: dropped ahead of it, it goes right after it.
    if (!device->instrument && index >= 0)
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

std::optional<model::DeviceSlot> slotOf(engine::Session& session, std::uint64_t module)
{
    const auto snapshot = session.snapshot();
    const auto find = [module](const model::TrackInfo& t) -> std::optional<model::DeviceSlot>
    {
        for (const auto& d : t.devices)
            if (d.id == module)
                return d;
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
    return isModule(ref->kind) || ((isClip(ref->kind) || ref->kind == Kind::Wavetable) && holdsClips);
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
    return ref->kind == Kind::Sample && module.deviceId == "sampler";
}

Result onChannel(engine::Session& session, const std::string& item, std::uint64_t channel, double seconds)
{
    const auto ref = parse(item);
    if (!ref)
        return {};
    if (isModule(ref->kind))
        return addModule(session, *ref, channel, -1);
    if (ref->kind == Kind::Wavetable)
        return tableOnTrack(session, *ref, channel);
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
            return {session.loadSample(*onModule, ref->file).has_value(), {}, 0, *onModule};
        }
    if (isModule(ref->kind))
        return addModule(session, *ref, channel, index);
    if (ref->kind == Kind::Wavetable)
        return tableOnTrack(session, *ref, channel);
    if (ref->kind == Kind::Sample)
    {
        // A Sampler with it, in place of the track's instrument.
        const auto old = session.instrumentOf(channel);
        const engine::Session::EditGroup group(
            session, old ? "Replace " + old->name + " with Sampler"
                         : "Add Sampler with " + ref->file.getFileNameWithoutExtension().toStdString());
        const auto sampler = session.replaceInstrument(channel, "fd:sampler");
        if (sampler == 0 || !session.loadSample(sampler, ref->file))
            return {};
        Result r{true, {}, 0, sampler};
        if (old && old->deviceId != "sampler")
            r.toast = "Replaced " + juce::String(old->name) + " with Sampler";
        return r;
    }
    return {};
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
