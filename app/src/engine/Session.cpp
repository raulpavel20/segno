#include "engine/SessionImpl.h"

#include "engine/DevicePlugin.h"
#include "model/NoteEditing.h"

#include <fd/device/Registry.h>

#include <atomic>

namespace fd::engine
{
namespace
{
const juce::Identifier kColourId{"fdColour"}; // index into the clip palette
const juce::Identifier kRoleId{"fdRole"};     // "return" on a return track (ADR 0026)
// The project format: 2 since mixer levels sit on Segno's taper (ADR 0028), 3 since automation
// lanes are rows of their own (ADR 0029). Older projects are converted when opened.
const juce::Identifier kFormatId{"fdFormat"};
constexpr int kFormat = 3;

/// Number of colours in ui::Theme::clipColours; snapshots carry indices modulo this.
constexpr int kPaletteSize = 16;

/// One of our devices with a sidechain bus (ADR 0025).
bool takesSidechain(te::Plugin& p)
{
    auto* native = dynamic_cast<DevicePlugin*>(&p);
    return native != nullptr && native->device().info().buses.sidechainChannels > 0 &&
           native->device().info().buses.inputChannels > 0;
}

/// tracktion's plugins that make up the mixer (volume and pan, the meter, sends and the return
/// input): part of the channel, not the module chain.
bool isMixerPlugin(const te::Plugin* p)
{
    return dynamic_cast<const te::VolumeAndPanPlugin*>(p) != nullptr ||
           dynamic_cast<const te::LevelMeterPlugin*>(p) != nullptr ||
           dynamic_cast<const te::AuxSendPlugin*>(p) != nullptr || dynamic_cast<const te::AuxReturnPlugin*>(p) != nullptr;
}

/// The stored volume (the parameter's own value catches up asynchronously, e.g. after undo).
float volumeDbOf(te::VolumeAndPanPlugin* v)
{
    return v != nullptr ? std::max(model::minusInfinityDb, te::volumeFaderPositionToDB(v->volume.get())) : 0.0f;
}

/// UI access to one of our devices (the panel's binding through the engine).
/// The header's sidechain picker in the DAW (ADR 0025): "None", then the tracks that can feed
/// the module, by name.
class SessionSidechain final : public ui::SidechainRouting
{
public:
    SessionSidechain(Session& session, std::uint64_t deviceId) : session_(session), deviceId_(deviceId) {}

    std::vector<std::string> sources() const override
    {
        choices_ = session_.sidechainSources(deviceId_);
        const auto current = source();
        if (current && std::find(choices_.begin(), choices_.end(), *current) == choices_.end())
            choices_.push_back(*current);
        std::vector<std::string> names{"None"};
        const auto snapshot = session_.snapshot();
        for (auto id : choices_)
        {
            const auto it = std::find_if(snapshot->tracks.begin(), snapshot->tracks.end(),
                                         [id](const auto& t) { return t.id == id; });
            names.push_back(it != snapshot->tracks.end() ? it->name : std::string("Track"));
        }
        return names;
    }

    int selected() const override
    {
        const auto current = source();
        const auto it = current ? std::find(choices_.begin(), choices_.end(), *current) : choices_.end();
        return it != choices_.end() ? static_cast<int>(it - choices_.begin()) + 1 : 0;
    }

    void select(int index) override
    {
        session_.setSidechainSource(deviceId_, index >= 1 && index <= static_cast<int>(choices_.size())
                                                   ? std::optional(choices_[static_cast<size_t>(index - 1)])
                                                   : std::nullopt);
    }

private:
    std::optional<std::uint64_t> source() const
    {
        for (const auto& t : session_.snapshot()->tracks)
            for (const auto& d : t.devices)
                if (d.id == deviceId_)
                    return d.sidechainSource;
        return std::nullopt;
    }

    Session& session_;
    std::uint64_t deviceId_;
    mutable std::vector<std::uint64_t> choices_;
};

/// A module's parameters in the DAW, with its automation (a control's menu, ADR 0028).
class SessionDeviceBinding final : public DevicePluginBinding
{
public:
    SessionDeviceBinding(Session& session, DevicePlugin& plugin) : DevicePluginBinding(plugin), session_(session) {}

    void addAutomationItems(size_t i, juce::PopupMenu& menu) override
    {
        if (const auto t = target(i); t && session_.automationMenu)
            session_.automationMenu(*t, menu);
    }

private:
    std::optional<Session::ParamTarget> target(size_t i) const
    {
        return session_.moduleParameter(plugin_.itemID.getRawID(), std::string(spec(i).id));
    }

    Session& session_;
};

class NativeDeviceHandle final : public DeviceHandle
{
public:
    NativeDeviceHandle(Session& session, DevicePlugin& plugin)
        : plugin_(&plugin), native_(plugin), binding_(session, plugin), sidechain_(session, plugin.itemID.getRawID()),
          context_{plugin.device().info(), binding_, &plugin.device().analysis(), &plugin.device(), &sidechain_}
    {
    }
    ui::DeviceUiContext& context() override { return context_; }
    std::string_view deviceId() const override { return native_.device().info().id; }

private:
    te::Plugin::Ptr plugin_; // keeps the device alive while its panel exists
    DevicePlugin& native_;
    SessionDeviceBinding binding_;
    SessionSidechain sidechain_;
    ui::DeviceUiContext context_;
};

model::Note noteOf(const te::MidiNote& n)
{
    return {n.getNoteNumber(), n.getStartBeat().inBeats(), n.getLengthBeats().inBeats(),
            static_cast<float>(n.getVelocity()) / 127.0f};
}

int midiVelocity(float velocity)
{
    return std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127);
}

model::ClipInfo clipInfo(te::Clip& c, int trackColour)
{
    model::ClipInfo clip;
    clip.id = c.itemID.getRawID();
    clip.kind = dynamic_cast<te::MidiClip*>(&c) != nullptr ? model::ClipInfo::Kind::Midi : model::ClipInfo::Kind::Audio;
    clip.name = c.getName().toStdString();
    const auto pos = c.getPosition();
    clip.start = pos.getStart().inSeconds();
    clip.length = pos.getLength().inSeconds();
    clip.offset = pos.getOffset().inSeconds();
    clip.colour = static_cast<int>(c.state.getProperty(kColourId, trackColour));
    if (auto* midi = dynamic_cast<te::MidiClip*>(&c))
    {
        // Notes count beats from the content start (the clip's start minus its offset).
        clip.contentStartBeat = c.edit.tempoSequence.toBeats(pos.getStart() - pos.getOffset()).inBeats();
        for (auto* n : midi->getSequence().getNotes())
            clip.notes.push_back(noteOf(*n));
        std::sort(clip.notes.begin(), clip.notes.end(), model::noteOrder);
    }
    if (auto* audio = dynamic_cast<te::AudioClipBase*>(&c))
    {
        clip.sourcePath = audio->getOriginalFile().getFullPathName().toStdString();
        clip.sourceLength = audio->getSourceLength().inSeconds();
    }
    return clip;
}

model::TrackInfo::Kind kindOf(te::AudioTrack& t)
{
    for (auto* p : t.pluginList)
        if (p->isSynth())
            return model::TrackInfo::Kind::Instrument;
    return model::TrackInfo::Kind::Audio;
}
} // namespace

/// Scans plugin folders one file per tick on the message thread (some formats must load there;
/// JUCE's own list does the same). A crash mid-scan is remembered by the dead man's pedal file
/// and that plugin is skipped next time.
class PluginScanner final : private juce::Timer
{
public:
    PluginScanner(te::Engine& engine, std::function<void(const juce::String&)> progress, std::function<void(int)> done)
        : engine_(engine), progress_(std::move(progress)), done_(std::move(done))
    {
        auto& pm = engine_.getPluginManager();
        const auto pedal = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile(ui::brand::name)
                               .getChildFile("plugin-scan-in-progress.txt");
        pedal.getParentDirectory().createDirectory();
        for (auto* format : pm.pluginFormatManager.getFormats())
            if (format->canScanForPlugins() && !format->getName().containsIgnoreCase("internal"))
                scanners_.push_back(std::make_unique<juce::PluginDirectoryScanner>(
                    pm.knownPluginList, *format, format->getDefaultLocationsToSearch(), true, pedal, false));
        startTimer(5);
    }

    [[nodiscard]] bool isDone() const noexcept { return scanners_.empty(); }

private:
    void timerCallback() override
    {
        if (scanners_.empty())
            return;
        auto& scanner = *scanners_.front();
        if (progress_)
            progress_(scanner.getNextPluginFileThatWillBeScanned());
        juce::String name;
        if (!scanner.scanNextFile(true, name))
            scanners_.erase(scanners_.begin());
        if (scanners_.empty())
        {
            stopTimer();
            if (done_)
                done_(engine_.getPluginManager().knownPluginList.getNumTypes());
        }
    }

    te::Engine& engine_;
    std::function<void(const juce::String&)> progress_;
    std::function<void(int)> done_;
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners_;
};

// ---- Impl ---------------------------------------------------------------------------------

Session::Impl::Impl(Session& o, te::Engine& e) : owner(o), engine(e)
{
    // Live MIDI (C7): the computer keyboard is one more input. tracktion fills its device
    // list asynchronously (and again when devices come and go): midiDevicesChanged() sets
    // every input up and re-applies the routing.
    auto& devices = engine.getDeviceManager();
    if (!std::any_of(devices.getMidiInDevices().begin(), devices.getMidiInDevices().end(),
                     [](const auto& d) { return d->getName() == computerKeyboardName; }))
        (void)devices.createVirtualMidiDevice(computerKeyboardName);
    devices.addChangeListener(this);
    attach(newEdit());
}

std::unique_ptr<te::Edit> Session::Impl::newEdit()
{
    auto e = te::Edit::createSingleTrackEdit(engine);
    e->state.setProperty(kFormatId, kFormat, nullptr);
    e->ensureNumberOfAudioTracks(4);
    int i = 0;
    for (auto* t : te::getAudioTracks(*e))
    {
        t->setName(juce::String(i + 1) + " Track");
        t->state.setProperty(kColourId, i * 3 % kPaletteSize, nullptr);
        ++i;
    }
    return e;
}

void Session::Impl::attach(std::unique_ptr<te::Edit> e)
{
    edit = std::move(e);
    const int format = edit->state.getProperty(kFormatId, 0);
    if (format < 2)
    {
        // Before ADR 0028 the mixer's levels sat on tracktion's own fader curve: the same levels
        // in dB move onto Segno's taper (modules convert their own parameters).
        auto oldDb = [](float position) { return position > 0.0f ? 20.0f * std::log(position) + 6.0f : -100.0f; };
        auto convert = [&](te::PluginList& list) {
            for (auto* p : list)
                if (auto* v = dynamic_cast<te::VolumeAndPanPlugin*>(p))
                    v->setVolumeDb(oldDb(v->volParam->getCurrentValue()));
                else if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p))
                    send->setGainDb(oldDb(send->gain->getCurrentValue()));
        };
        for (auto* t : te::getAudioTracks(*edit))
            convert(t->pluginList);
        convert(edit->getMasterPluginList());
    }
    ensureChannels(nullptr);
    loadLanes(format < 3); // ADR 0029: lanes of their own, every curve derived from them
    edit->state.setProperty(kFormatId, kFormat, nullptr);
    // tracktion sorts the edit's children by type asynchronously, through the undo manager: done
    // now, so a fresh project's history really is empty (no invisible first undo step).
    te::TrackList::sortTracksByType(edit->state, nullptr);
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    dirty = false;
    midiTarget.reset();
    midiDevicesChanged(); // playback context and input routing for this edit
    applyAutomationMode();
    rebuildSnapshot();
    edit->state.addListener(this);
    edit->getParameterChangeHandler().addListener(*this); // moved controls (automation, §7)
    edit->getTransport().addChangeListener(this);          // a recording pass ends with playback
}

void Session::Impl::detach()
{
    if (edit == nullptr)
        return;
    cancelPendingUpdate();
    endRecordingPass();
    lastTouched.reset();
    edit->getTransport().removeChangeListener(this);
    edit->getParameterChangeHandler().removeListener(*this);
    edit->state.removeListener(this);
    edit->getTransport().stop(false, true);
    edit.reset();
}

Session::Impl::~Impl()
{
    renders.clear(); // cancels them (their Edit copies go before the engine)
    engine.getDeviceManager().removeChangeListener(this);
    detach();
}

void Session::Impl::midiDevicesChanged()
{
    if (edit == nullptr)
        return;
    for (auto& in : engine.getDeviceManager().getMidiInDevices())
    {
        if (!in->isEnabled())
            in->setEnabled(true);
        if (in->getMonitorMode() != te::InputDevice::MonitorMode::on)
            in->setMonitorMode(te::InputDevice::MonitorMode::on);
    }
    edit->getTransport().ensureContextAllocated(true); // instances for new inputs
    applyMidiTarget();
}

void Session::Impl::applyMidiTarget()
{
    // Only the virtual inputs: "All MIDI Ins" (every physical device, merged) and the computer
    // keyboard. Routing the physical devices too would play every note twice.
    auto* target = midiTarget ? track(*midiTarget) : nullptr;
    for (auto* instance : edit->getAllInputDevices())
    {
        if (instance->getInputDevice().getDeviceType() != te::InputDevice::virtualMidiDevice)
            continue;
        for (auto id : instance->getTargets())
            if (target == nullptr || id != target->itemID)
                (void)instance->removeTarget(id, nullptr);
        if (target != nullptr && !instance->getTargets().contains(target->itemID))
            (void)instance->setTarget(target->itemID, true, nullptr, 0);
    }
}

void Session::Impl::changed(juce::ValueTree& tree)
{
    // Transport state (position, play state) changes constantly and isn't part of a snapshot.
    for (auto t = tree; t.isValid(); t = t.getParent())
        if (t.hasType(te::IDs::TRANSPORT))
            return;
    triggerAsyncUpdate();
}

void Session::Impl::rebuildSnapshot()
{
    auto s = std::make_shared<model::Snapshot>();
    s->version = ++version;
    if (auto* tempo = edit->tempoSequence.getTempo(0))
        s->tempo.bpm = tempo->getBpm();
    if (auto* sig = edit->tempoSequence.getTimeSig(0))
    {
        s->tempo.numerator = sig->numerator;
        s->tempo.denominator = sig->denominator;
    }

    // The module chain of a channel, as the views show it.
    auto devicesOf = [](te::PluginList& list, model::TrackInfo& info) {
        for (auto* p : chain(list))
        {
            model::DeviceSlot slot;
            slot.id = p->itemID.getRawID();
            slot.name = p->getName().toStdString();
            slot.enabled = p->isEnabled();
            slot.instrument = p->isSynth();
            if (slot.instrument && info.instrument.empty())
                info.instrument = slot.name;
            if (auto* native = dynamic_cast<DevicePlugin*>(p))
            {
                slot.kind = model::DeviceSlot::Kind::Native;
                slot.deviceId = std::string(native->device().info().id);
                slot.takesSidechain = takesSidechain(*p);
                if (slot.takesSidechain && p->getSidechainSourceID().isValid())
                    slot.sidechainSource = p->getSidechainSourceID().getRawID();
            }
            else
                slot.kind = model::DeviceSlot::Kind::External;
            info.devices.push_back(std::move(slot));
        }
    };
    auto mixOf = [](te::PluginList& list) {
        model::MixInfo mix;
        if (auto* v = findIn<te::VolumeAndPanPlugin>(list))
        {
            mix.volumeDb = volumeDbOf(v);
            mix.pan = v->pan.get();
        }
        return mix;
    };

    const auto returnTracks = returns();
    int index = 0;
    for (auto* t : te::getAudioTracks(*edit))
    {
        model::TrackInfo info;
        info.id = t->itemID.getRawID();
        info.kind = kindOf(*t);
        info.role = isReturn(*t) ? model::TrackInfo::Role::Return : model::TrackInfo::Role::Track;
        info.name = t->getName().toStdString();
        info.colour = static_cast<int>(t->state.getProperty(kColourId, index * 3 % kPaletteSize));
        for (auto* c : t->getClips())
        {
            auto clip = clipInfo(*c, info.colour);
            info.clips.push_back(std::move(clip));
        }
        std::sort(info.clips.begin(), info.clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        devicesOf(t->pluginList, info);
        info.mix = mixOf(t->pluginList);
        info.mix.mute = t->isMuted(false);
        info.mix.solo = t->isSolo(false);
        if (info.role == model::TrackInfo::Role::Track)
        {
            auto& volume = *t->getVolumePlugin();
            const int volumeAt = t->pluginList.indexOf(&volume);
            for (auto* r : returnTracks)
            {
                model::SendInfo send{.returnId = r->itemID.getRawID()};
                if (auto* plugin = sendIn(t->pluginList, busOf(*r)))
                {
                    send.levelDb = std::max(model::minusInfinityDb, plugin->getGainDb());
                    send.preFader = t->pluginList.indexOf(plugin) < volumeAt;
                }
                info.mix.sends.push_back(send);
            }
            s->tracks.push_back(std::move(info));
        }
        else
            s->returns.push_back(std::move(info));
        ++index;
    }

    auto& master = s->master;
    master.id = masterId();
    master.role = model::TrackInfo::Role::Master;
    master.name = "Master";
    devicesOf(edit->getMasterPluginList(), master);
    master.mix = mixOf(edit->getMasterPluginList());
    s->automationLanes = laneInfos();
    snapshot = std::move(s);
    if (owner.onSnapshotChanged)
        owner.onSnapshotChanged();
    listeners.call([](Session::Listener& l) { l.snapshotChanged(); });
}

int Session::Impl::colourOf(std::uint64_t channel) const
{
    const auto all = te::getAudioTracks(*edit);
    for (int i = 0; i < all.size(); ++i)
        if (all[i]->itemID.getRawID() == channel)
            return static_cast<int>(all[i]->state.getProperty(kColourId, i * 3 % kPaletteSize));
    return -1;
}

int Session::Impl::nextRowColour() const
{
    const auto lanes = lanesTree();
    return (static_cast<int>(tracks().size()) + (lanes.isValid() ? lanes.getNumChildren() : 0)) * 3 % kPaletteSize;
}

std::uint64_t Session::Impl::lastTrack() const
{
    const auto all = tracks();
    return all.empty() ? 0 : all.back()->itemID.getRawID();
}

te::AudioTrack* Session::Impl::channelTrack(std::uint64_t id) const
{
    for (auto* t : te::getAudioTracks(*edit))
        if (t->itemID.getRawID() == id)
            return t;
    return nullptr;
}

te::AudioTrack* Session::Impl::track(std::uint64_t id) const
{
    auto* t = channelTrack(id);
    return t != nullptr && !isReturn(*t) ? t : nullptr;
}

te::AudioTrack* Session::Impl::returnTrack(std::uint64_t id) const
{
    auto* t = channelTrack(id);
    return t != nullptr && isReturn(*t) ? t : nullptr;
}

bool Session::Impl::isReturn(const te::Track& track)
{
    return track.state[kRoleId].toString() == "return";
}

std::uint64_t Session::Impl::masterId() const
{
    auto* master = edit->getMasterTrack();
    return master != nullptr ? master->itemID.getRawID() : 0;
}

te::PluginList* Session::Impl::plugins(std::uint64_t channelId) const
{
    if (auto* t = channelTrack(channelId))
        return &t->pluginList;
    return channelId != 0 && channelId == masterId() ? &edit->getMasterPluginList() : nullptr;
}

std::vector<te::AudioTrack*> Session::Impl::tracks() const
{
    std::vector<te::AudioTrack*> out;
    for (auto* t : te::getAudioTracks(*edit))
        if (!isReturn(*t))
            out.push_back(t);
    return out;
}

std::vector<te::AudioTrack*> Session::Impl::returns() const
{
    std::vector<te::AudioTrack*> out;
    for (auto* t : te::getAudioTracks(*edit))
        if (isReturn(*t))
            out.push_back(t);
    return out;
}

void Session::Impl::ensureChannels(juce::UndoManager* undo)
{
    for (auto* t : te::getAudioTracks(*edit))
        if (auto* v = t->getVolumePlugin(); v != nullptr && v->getPanLaw() != te::PanLawBalance)
            v->state.setProperty(te::IDs::panLaw, static_cast<int>(te::PanLawBalance), undo);
    // The master: tracktion's master volume stays at unity (it starts at -3 dB); ours sits at
    // the end of the master chain with a meter after it, as on every track.
    const float unity = te::decibelsToVolumeFaderPosition(0.0f);
    if (auto masterVolume = edit->getMasterVolumePlugin(); masterVolume != nullptr && masterVolume->volume.get() != unity)
        masterVolume->state.setProperty(te::IDs::volume, unity, undo);
    auto& list = edit->getMasterPluginList();
    if (findIn<te::VolumeAndPanPlugin>(list) == nullptr)
    {
        list.insertPlugin(edit->getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::xmlTypeName, {}), -1, nullptr);
        list.insertPlugin(edit->getPluginCache().createNewPlugin(te::LevelMeterPlugin::xmlTypeName, {}), -1, nullptr);
    }
    if (auto* v = findIn<te::VolumeAndPanPlugin>(list); v != nullptr && v->getPanLaw() != te::PanLawBalance)
        v->state.setProperty(te::IDs::panLaw, static_cast<int>(te::PanLawBalance), undo);
}

te::AudioTrack* Session::Impl::insertTrack(const juce::String& name)
{
    // After the last track and before the return tracks (ADR 0026).
    const auto existing = tracks();
    const int count = static_cast<int>(existing.size());
    const int colour = nextRowColour(); // tracks and lanes take turns through the palette
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint(nullptr, existing.empty() ? nullptr : existing.back()),
                                           nullptr);
    if (track == nullptr)
        return nullptr;
    track->state.setProperty(kColourId, colour, &edit->getUndoManager());
    track->setName(juce::String(count + 1) + " " + name);
    if (auto* v = track->getVolumePlugin())
        v->setPanLaw(te::PanLawBalance);
    return track.get();
}

te::Plugin* Session::Impl::insertDevice(te::PluginList& list, const std::string& key, int index)
{
    te::Plugin::Ptr plugin;
    if (key.starts_with("fd:"))
        plugin = edit->getPluginCache().createNewPlugin(DevicePlugin::typeNameFor(key.substr(3)), {});
    else if (key.starts_with("ext:"))
        if (auto desc = engine.getPluginManager().knownPluginList.getTypeForIdentifierString(key.substr(4)))
            plugin = edit->getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *desc);
    if (plugin == nullptr)
        return nullptr;

    // Chain index -> plugin list index: before the device at `index`, or before the mixer
    // plugins at the end.
    const auto devices = chain(list);
    int at = list.size();
    if (index >= 0 && index < static_cast<int>(devices.size()))
        at = list.indexOf(devices[static_cast<size_t>(index)]);
    else
        for (int i = 0; i < list.size(); ++i)
            if (isMixerPlugin(list[i]) && dynamic_cast<te::AuxReturnPlugin*>(list[i]) == nullptr)
            {
                at = i;
                break;
            }
    list.insertPlugin(plugin, at, nullptr);
    return plugin.get();
}

void Session::Impl::clearUnder(te::AudioTrack& track, double start, double end, const std::set<std::uint64_t>& except)
{
    std::vector<model::ClipInfo> clips;
    for (auto* c : track.getClips())
        clips.push_back(clipInfo(*c, 0));
    for (const auto& cut : model::clearRange(clips, start, end, except))
    {
        auto* c = clip(cut.clip);
        if (c == nullptr)
            continue;
        const auto at = [](double s) { return te::TimePosition::fromSeconds(s); };
        switch (cut.kind)
        {
        case model::ClipCut::Kind::Remove:
            c->removeFromParent();
            break;
        case model::ClipCut::Kind::Trim:
            c->setPosition({{at(cut.start), at(cut.start + cut.length)}, te::TimeDuration::fromSeconds(cut.offset)});
            break;
        case model::ClipCut::Kind::SplitAround:
        {
            const auto pos = c->getPosition();
            track.splitClip(*c, at(cut.to)); // the tail after the range becomes its own clip
            c->setPosition({{pos.getStart(), at(cut.from)}, pos.getOffset()});
            break;
        }
        }
    }
}

bool Session::Impl::ensureInstrument(te::AudioTrack& track)
{
    const auto devices = chain(track.pluginList);
    if (std::any_of(devices.begin(), devices.end(), [](auto* p) { return p->isSynth(); }))
        return false;
    return insertDevice(track.pluginList, "fd:subtractive-synth", 0) != nullptr;
}

std::vector<te::Plugin*> Session::Impl::chain(te::PluginList& list)
{
    std::vector<te::Plugin*> out;
    for (auto* p : list)
        if (!isMixerPlugin(p))
            out.push_back(p);
    return out;
}

te::Plugin* Session::Impl::plugin(std::uint64_t id) const
{
    for (auto* t : te::getAudioTracks(*edit))
        for (auto* p : t->pluginList)
            if (p->itemID.getRawID() == id)
                return p;
    for (auto* p : edit->getMasterPluginList())
        if (p->itemID.getRawID() == id)
            return p;
    return nullptr;
}

te::Clip* Session::Impl::clip(std::uint64_t id) const
{
    return te::findClipForID(*edit, te::EditItemID::fromRawID(id));
}

// ---- Session ------------------------------------------------------------------------------

Session::Session(te::Engine& engine) : impl_(std::make_unique<Impl>(*this, engine)) {}
Session::~Session() = default;

std::shared_ptr<const model::Snapshot> Session::snapshot() const
{
    return impl_->snapshot;
}

void Session::updateSnapshotNow()
{
    impl_->rebuildSnapshot();
}

void Session::flushSnapshot()
{
    if (impl_->isUpdatePending())
    {
        impl_->cancelPendingUpdate();
        impl_->rebuildSnapshot();
    }
}

void Session::addListener(Listener* listener)
{
    impl_->listeners.add(listener);
}

void Session::removeListener(Listener* listener)
{
    impl_->listeners.remove(listener);
}

void Session::play()
{
    impl_->applyAutomationMode(); // tracks added since arming write too
    impl_->edit->getTransport().play(false);
}

void Session::stop()
{
    impl_->edit->getTransport().stop(false, false);
}

bool Session::isPlaying() const
{
    return impl_->edit->getTransport().isPlaying();
}

double Session::position() const
{
    return impl_->edit->getTransport().getPosition().inSeconds();
}

void Session::setPosition(double seconds)
{
    impl_->edit->getTransport().setPosition(te::TimePosition::fromSeconds(std::max(0.0, seconds)));
}

bool Session::isLooping() const
{
    return impl_->edit->getTransport().looping;
}

void Session::setLooping(bool on)
{
    impl_->edit->getTransport().looping = on;
}

std::pair<double, double> Session::loopRange() const
{
    const auto r = impl_->edit->getTransport().getLoopRange();
    return {r.getStart().inSeconds(), r.getEnd().inSeconds()};
}

void Session::setLoopRange(double start, double end)
{
    if (end < start)
        std::swap(start, end);
    impl_->edit->getTransport().setLoopRange(
        {te::TimePosition::fromSeconds(std::max(0.0, start)), te::TimePosition::fromSeconds(std::max(0.0, end))});
}

bool Session::isMetronomeOn() const
{
    return impl_->edit->clickTrackEnabled;
}

void Session::setMetronome(bool on)
{
    impl_->edit->clickTrackEnabled = on;
}

void Session::setTempo(double bpm)
{
    if (auto* tempo = impl_->edit->tempoSequence.getTempo(0))
    {
        impl_->beginEdit("Tempo");
        tempo->setBpm(std::clamp(bpm, 20.0, 999.0));
    }
}

void Session::setTimeSignature(int numerator, int denominator)
{
    if (auto* sig = impl_->edit->tempoSequence.getTimeSig(0))
    {
        impl_->beginEdit("Time signature");
        sig->setStringTimeSig(juce::String(std::clamp(numerator, 1, 32)) + "/" + juce::String(denominator));
    }
}

std::uint64_t Session::addTrack(const std::string& name)
{
    impl_->beginEdit("Add track");
    auto* track = impl_->insertTrack("Track");
    if (track == nullptr)
        return 0;
    if (!name.empty())
        track->setName(juce::String(name));
    return track->itemID.getRawID();
}

std::uint64_t Session::insertMidiFile(std::uint64_t trackId, const juce::File& file, double seconds, bool* addedInstrument)
{
    auto* track = impl_->track(trackId);
    juce::FileInputStream in(file);
    juce::MidiFile midi;
    if (track == nullptr || !in.openedOk() || !midi.readFrom(in))
        return 0;
    midi.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence all;
    for (int i = 0; i < midi.getNumTracks(); ++i)
        all.addSequence(*midi.getTrack(i), 0.0);
    all.updateMatchedPairs();
    const double length = std::max(0.1, all.getEndTime());

    impl_->beginEdit("Insert MIDI file");
    const auto start = te::TimePosition::fromSeconds(std::max(0.0, seconds));
    auto clip = track->insertMIDIClip(file.getFileNameWithoutExtension(), {start, start + te::TimeDuration::fromSeconds(length)},
                                      nullptr);
    if (clip == nullptr)
        return 0;
    // The import takes edit times: shift the file's times to the clip's start.
    all.addTimeToMessages(start.inSeconds());
    clip->getSequence().importMidiSequence(all, impl_->edit.get(), start, &impl_->edit->getUndoManager());
    impl_->clearUnder(*track, start.inSeconds(), start.inSeconds() + length, {clip->itemID.getRawID()});
    const bool added = impl_->ensureInstrument(*track);
    if (addedInstrument != nullptr)
        *addedInstrument = added;
    return clip->itemID.getRawID();
}

std::uint64_t Session::createMidiClip(std::uint64_t trackId, double seconds, double lengthSeconds, bool* addedInstrument)
{
    auto* track = impl_->track(trackId);
    if (track == nullptr || !(lengthSeconds > 0.0))
        return 0;
    impl_->beginEdit("Create MIDI clip");
    const auto start = te::TimePosition::fromSeconds(std::max(0.0, seconds));
    auto clip = track->insertMIDIClip(track->getName(), {start, start + te::TimeDuration::fromSeconds(lengthSeconds)}, nullptr);
    if (clip == nullptr)
        return 0;
    impl_->clearUnder(*track, start.inSeconds(), start.inSeconds() + lengthSeconds, {clip->itemID.getRawID()});
    const bool added = impl_->ensureInstrument(*track);
    if (addedInstrument != nullptr)
        *addedInstrument = added;
    return clip->itemID.getRawID();
}

void Session::setNotes(std::uint64_t clipId, const std::vector<model::Note>& notes)
{
    auto* midi = dynamic_cast<te::MidiClip*>(impl_->clip(clipId));
    if (midi == nullptr)
        return;
    auto& list = midi->getSequence();
    auto* undo = &impl_->edit->getUndoManager();

    // Match by value: notes that stay are left alone.
    std::vector<model::Note> added = notes;
    std::vector<te::MidiNote*> changed;
    for (auto* n : list.getNotes())
        if (auto it = std::find(added.begin(), added.end(), noteOf(*n)); it != added.end())
            added.erase(it);
        else
            changed.push_back(n);
    if (added.empty() && changed.empty())
        return;
    impl_->beginEdit("Edit notes");

    // The rest pair up in time order: a moved note keeps its other properties (colour, mute).
    std::sort(added.begin(), added.end(), model::noteOrder);
    std::sort(changed.begin(), changed.end(), [](auto* a, auto* b) { return model::noteOrder(noteOf(*a), noteOf(*b)); });
    const auto paired = std::min(added.size(), changed.size());
    for (size_t i = 0; i < paired; ++i)
    {
        const auto& to = added[i];
        changed[i]->setStartAndLength(te::BeatPosition::fromBeats(to.start), te::BeatDuration::fromBeats(to.length), undo);
        changed[i]->setNoteNumber(to.pitch, undo);
        changed[i]->setVelocity(midiVelocity(to.velocity), undo);
    }
    for (size_t i = paired; i < changed.size(); ++i)
        list.removeNote(*changed[i], undo);
    for (size_t i = paired; i < added.size(); ++i)
        list.addNote(added[i].pitch, te::BeatPosition::fromBeats(added[i].start), te::BeatDuration::fromBeats(added[i].length),
                     midiVelocity(added[i].velocity), 0, undo);

    // Notes past the clip's end lengthen it to the end of that bar when nothing is in the way
    // (B15), in the same undo step.
    if (auto* track = midi->getClipTrack())
    {
        std::vector<model::ClipInfo> clips;
        for (auto* c : track->getClips())
            clips.push_back(clipInfo(*c, 0));
        model::TempoMap tempo;
        if (auto* t = impl_->edit->tempoSequence.getTempo(0))
            tempo.bpm = t->getBpm();
        if (auto* sig = impl_->edit->tempoSequence.getTimeSig(0))
        {
            tempo.numerator = sig->numerator;
            tempo.denominator = sig->denominator;
        }
        if (const auto end = model::lengthenedClipEnd(clipInfo(*midi, 0), clips, notes, tempo))
        {
            const auto pos = midi->getPosition();
            midi->setPosition({{pos.getStart(), te::TimePosition::fromSeconds(*end)}, pos.getOffset()});
        }
    }
}

void Session::setMidiInputTarget(std::optional<std::uint64_t> trackId)
{
    impl_->midiTarget = trackId;
    impl_->applyMidiTarget();
}

std::optional<std::uint64_t> Session::midiInputTarget() const
{
    return impl_->midiTarget;
}

bool Session::midiNotesHeld() const
{
    for (auto& in : impl_->engine.getDeviceManager().getMidiInDevices())
        for (int n = 0; n < 128; ++n)
            if (in->keyboardState.isNoteOnForChannels(0xffff, n))
                return true;
    return false;
}

void Session::computerKeyboardNote(int note, float velocity)
{
    for (auto& in : impl_->engine.getDeviceManager().getMidiInDevices())
        if (in->getName() == computerKeyboardName)
            if (auto* keyboard = dynamic_cast<te::VirtualMidiInputDevice*>(in.get()))
            {
                auto message = velocity > 0.0f
                                   ? juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, note), velocity)
                                   : juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, note));
                message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
                keyboard->handleIncomingMidiMessage(message, te::MPESourceID{1});
            }
}

std::uint64_t Session::insertAudioFile(std::uint64_t trackId, const juce::File& file, double seconds)
{
    auto* track = impl_->track(trackId);
    const te::AudioFile audio(impl_->engine, file);
    if (track == nullptr || !audio.isValid())
        return 0;
    impl_->beginEdit("Insert audio file");
    const auto start = te::TimePosition::fromSeconds(std::max(0.0, seconds));
    auto clip = track->insertWaveClip(file.getFileNameWithoutExtension(), file,
                                      {{start, te::TimeDuration::fromSeconds(audio.getLength())}, {}}, false);
    if (clip == nullptr)
        return 0;
    impl_->clearUnder(*track, start.inSeconds(), start.inSeconds() + audio.getLength(), {clip->itemID.getRawID()});
    return clip->itemID.getRawID();
}

void Session::applyClipEdits(const std::vector<model::ClipEdit>& edits)
{
    if (edits.empty())
        return;
    impl_->beginEdit("Move clips");
    for (const auto& e : edits)
    {
        auto* c = impl_->clip(e.clip);
        auto* target = impl_->track(e.track);
        if (c == nullptr || target == nullptr)
            continue;
        if (c->getClipTrack() != target)
            c->moveTo(*target);
        c->setPosition({{te::TimePosition::fromSeconds(e.start), te::TimePosition::fromSeconds(e.start + e.length)},
                        te::TimeDuration::fromSeconds(e.offset)});
    }
    // No overlaps (ADR 0019): the placed clips cut what they cover, in the same undo step.
    std::set<std::uint64_t> placed;
    for (const auto& e : edits)
        placed.insert(e.clip);
    for (const auto& e : edits)
        if (auto* target = impl_->track(e.track))
            impl_->clearUnder(*target, e.start, e.start + e.length, placed);
}

std::vector<std::uint64_t> Session::duplicateClips(const std::vector<model::ClipEdit>& placements)
{
    std::vector<std::uint64_t> created;
    if (placements.empty())
        return created;
    impl_->beginEdit("Duplicate clips");
    for (const auto& p : placements)
    {
        auto* source = impl_->clip(p.clip);
        auto* target = impl_->track(p.track);
        if (source == nullptr || target == nullptr)
            continue;
        if (auto copy = te::duplicateClip(*source))
        {
            if (copy->getClipTrack() != target)
                copy->moveTo(*target);
            copy->setPosition({{te::TimePosition::fromSeconds(p.start), te::TimePosition::fromSeconds(p.start + p.length)},
                               te::TimeDuration::fromSeconds(p.offset)});
            created.push_back(copy->itemID.getRawID());
        }
    }
    const std::set<std::uint64_t> placed(created.begin(), created.end());
    for (const auto& p : placements)
        if (auto* target = impl_->track(p.track))
            impl_->clearUnder(*target, p.start, p.start + p.length, placed);
    return created;
}

void Session::deleteClips(const std::vector<std::uint64_t>& ids)
{
    if (ids.empty())
        return;
    impl_->beginEdit("Delete clips");
    for (auto id : ids)
        if (auto* c = impl_->clip(id))
            c->removeFromParent();
}

std::vector<std::uint64_t> Session::splitClips(const std::vector<std::uint64_t>& ids, double seconds)
{
    std::vector<std::uint64_t> created;
    const auto at = te::TimePosition::fromSeconds(seconds);
    bool begun = false;
    for (auto id : ids)
    {
        auto* c = impl_->clip(id);
        if (c == nullptr || !c->getEditTimeRange().containsInclusive(at) || c->getPosition().getStart() == at ||
            c->getPosition().getEnd() == at)
            continue;
        if (!begun)
        {
            impl_->beginEdit("Split clips");
            begun = true;
        }
        if (auto* track = c->getClipTrack())
            if (auto* right = track->splitClip(*c, at))
                created.push_back(right->itemID.getRawID());
    }
    return created;
}

std::vector<AvailableDevice> Session::availableDevices() const
{
    std::vector<AvailableDevice> out;
    for (const auto* f : fd::allDevices())
    {
        const bool instrument = f->info().kind == DeviceKind::Instrument;
        out.push_back({"fd:" + std::string(f->info().id), std::string(f->info().name),
                       instrument ? "Instruments" : "Audio effects", true, instrument});
    }
    for (const auto& d : impl_->engine.getPluginManager().knownPluginList.getTypes())
        out.push_back({"ext:" + d.createIdentifierString().toStdString(), d.name.toStdString(),
                       d.manufacturerName.isNotEmpty() ? d.manufacturerName.toStdString() : "Plugins", false, d.isInstrument});
    return out;
}

std::uint64_t Session::addDevice(std::uint64_t trackId, const std::string& key, int index)
{
    auto* list = impl_->plugins(trackId); // any channel: tracks, returns, the master
    if (list == nullptr)
        return 0;
    impl_->beginEdit("Add module");
    auto* plugin = impl_->insertDevice(*list, key, index);
    return plugin != nullptr ? plugin->itemID.getRawID() : 0;
}

void Session::removeDevice(std::uint64_t deviceId)
{
    if (auto* p = impl_->plugin(deviceId))
    {
        impl_->beginEdit("Remove module");
        // Its parameters leave their lanes in the same step (ADR 0029).
        const auto prefix = "m" + std::to_string(deviceId) + "/";
        for (auto lane : impl_->lanesTree())
            for (int i = lane.getNumChildren(); --i >= 0;)
                if (lane.getChild(i).hasType(Impl::assignId) &&
                    lane.getChild(i)[Impl::keyId].toString().startsWith(juce::String(prefix)))
                    lane.removeChild(i, &impl_->edit->getUndoManager());
        p->removeFromParent();
    }
}

void Session::moveDevice(std::uint64_t deviceId, int index)
{
    auto* p = impl_->plugin(deviceId);
    auto* list = p != nullptr ? p->getOwnerList() : nullptr;
    if (list == nullptr)
        return;
    const auto devices = Impl::chain(*list);
    index = std::clamp(index, 0, static_cast<int>(devices.size()) - 1);
    auto* target = devices[static_cast<size_t>(index)];
    if (target == p)
        return;
    auto parent = p->state.getParent();
    impl_->beginEdit("Move module");
    parent.moveChild(parent.indexOf(p->state), parent.indexOf(target->state), &impl_->edit->getUndoManager());
}

std::vector<std::uint64_t> Session::sidechainSources(std::uint64_t deviceId) const
{
    auto* p = impl_->plugin(deviceId);
    auto* owner = p != nullptr ? p->getOwnerTrack() : nullptr;
    if (owner == nullptr || !takesSidechain(*p))
        return {};
    // A track can't feed this module if its own sound already depends on the module's track:
    // it, or a track it takes a sidechain from (and so on), is that track.
    // Sources are tracks; returns wait for routing work with racks (ADR 0026).
    const auto tracks = impl_->tracks();
    std::function<bool(te::Track&, int)> dependsOnOwner = [&](te::Track& t, int depth) {
        if (&t == owner)
            return true;
        if (depth > static_cast<int>(tracks.size()) + 64)
            return false;
        for (auto* q : t.getAllPlugins())
            if (const auto id = q->getSidechainSourceID(); id.isValid())
                if (auto* source = te::findTrackForID(*impl_->edit, id))
                    if (dependsOnOwner(*source, depth + 1))
                        return true;
        return false;
    };
    std::vector<std::uint64_t> out;
    for (auto* t : tracks)
        if (!dependsOnOwner(*t, 0))
            out.push_back(t->itemID.getRawID());
    return out;
}

bool Session::setSidechainSource(std::uint64_t deviceId, std::optional<std::uint64_t> trackId)
{
    auto* p = impl_->plugin(deviceId);
    if (p == nullptr || !takesSidechain(*p))
        return false;
    te::EditItemID source;
    if (trackId)
    {
        const auto allowed = sidechainSources(deviceId);
        if (std::find(allowed.begin(), allowed.end(), *trackId) == allowed.end())
            return false;
        source = impl_->track(*trackId)->itemID;
    }
    if (p->getSidechainSourceID() == source)
        return true;
    impl_->beginEdit("Set sidechain");
    // The wires first (main channels to main, the source's to the sidechain), then the source,
    // whose change rebuilds the playback graph.
    if (source.isValid() && p->getNumWires() == 0)
        p->guessSidechainRouting();
    p->setSidechainSourceID(source);
    return true;
}

// ---- mixer (C3) -----------------------------------------------------------------------------

namespace
{
constexpr int kMaxReturns = 32; // tracktion's aux buses

juce::String returnName(int index)
{
    juce::String letters;
    for (int i = index; i >= 0; i = i / 26 - 1)
        letters = juce::String::charToString(static_cast<juce::juce_wchar>('A' + i % 26)) + letters;
    return letters + " Return";
}
} // namespace

std::uint64_t Session::addReturnTrack(const std::string& name)
{
    auto& edit = *impl_->edit;
    const auto existing = impl_->returns();
    // The lowest aux bus no return listens to yet.
    std::set<int> used;
    for (auto* r : existing)
        used.insert(busOf(*r));
    int bus = 0;
    while (used.contains(bus))
        ++bus;
    if (bus >= kMaxReturns)
        return 0;

    impl_->beginEdit("Add return track");
    auto* um = &edit.getUndoManager();
    auto track = edit.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(edit), nullptr);
    if (track == nullptr)
        return 0;
    const auto title = name.empty() ? returnName(static_cast<int>(existing.size())) : juce::String(name);
    track->setName(title);
    track->state.setProperty(kRoleId, "return", um);
    track->state.setProperty(kColourId, (static_cast<int>(existing.size()) * 5 + 2) % kPaletteSize, um);
    track->setSoloIsolate(true); // soloing a track never silences the returns (solo in place)
    if (auto* v = track->getVolumePlugin())
        v->setPanLaw(te::PanLawBalance);
    auto input = edit.getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {});
    if (auto* in = dynamic_cast<te::AuxReturnPlugin*>(input.get()))
        in->busNumber = bus;
    track->pluginList.insertPlugin(input, 0, nullptr);
    edit.setAuxBusName(bus, title);
    return track->itemID.getRawID();
}

void Session::setVolume(std::uint64_t channelId, float db)
{
    auto* list = impl_->plugins(channelId);
    auto* volume = list != nullptr ? findIn<te::VolumeAndPanPlugin>(*list) : nullptr;
    if (volume == nullptr)
        return;
    const float clamped = std::min(6.0f, db);
    if (std::abs(volumeDbOf(volume) - std::max(model::minusInfinityDb, clamped)) < 1e-4f)
        return;
    impl_->beginEdit("Volume");
    if (clamped <= model::minusInfinityDb)
        volume->setSliderPos(0.0f);
    else
        volume->setVolumeDb(clamped);
}

void Session::setPan(std::uint64_t channelId, float pan)
{
    auto* list = impl_->plugins(channelId);
    auto* volume = list != nullptr ? findIn<te::VolumeAndPanPlugin>(*list) : nullptr;
    if (volume == nullptr)
        return;
    const float clamped = std::clamp(pan, -1.0f, 1.0f);
    if (std::abs(volume->pan.get() - clamped) < 1e-6f)
        return;
    impl_->beginEdit("Pan");
    volume->setPan(clamped);
}

void Session::setMute(std::uint64_t channelId, bool mute)
{
    auto* t = impl_->channelTrack(channelId);
    if (t == nullptr || t->isMuted(false) == mute)
        return;
    impl_->beginEdit("Mute");
    t->state.setProperty(te::IDs::mute, mute, &impl_->edit->getUndoManager()); // tracktion's own setter isn't undoable
}

void Session::setSolo(std::uint64_t channelId, bool solo, bool exclusive)
{
    auto* target = impl_->channelTrack(channelId);
    if (target == nullptr)
        return;
    // A listening aid: never an undo step, and never an unsaved change on its own (tracktion
    // marks the edit changed; the mark is taken back when there was none before).
    auto& edit = *impl_->edit;
    const bool wasChanged = edit.hasChangedSinceSaved();
    const bool wasDirty = impl_->dirty;
    if (exclusive)
        for (auto* t : te::getAudioTracks(edit))
            if (t != target && t->isSolo(false))
                t->state.setProperty(te::IDs::solo, false, nullptr);
    target->state.setProperty(te::IDs::solo, solo, nullptr);
    if (!wasChanged)
        edit.resetChangedStatus();
    impl_->dirty = wasDirty;
}

bool Session::setSend(std::uint64_t trackId, std::uint64_t returnId, std::optional<float> levelDb,
                      std::optional<bool> preFader)
{
    auto* t = impl_->track(trackId);
    auto* r = impl_->returnTrack(returnId);
    if (t == nullptr || r == nullptr)
        return false;
    const int bus = busOf(*r);
    auto* send = sendIn(t->pluginList, bus);
    auto* volume = t->getVolumePlugin();
    const bool wantPre = preFader.value_or(send != nullptr && volume != nullptr &&
                                           t->pluginList.indexOf(send) < t->pluginList.indexOf(volume));
    const float level = std::clamp(levelDb.value_or(send != nullptr ? send->getGainDb() : model::minusInfinityDb),
                                   model::minusInfinityDb, 0.0f);
    impl_->beginEdit("Send to " + r->getName());
    auto* um = &impl_->edit->getUndoManager();
    const bool isNew = send == nullptr;
    if (send == nullptr)
    {
        auto created = impl_->edit->getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {});
        send = dynamic_cast<te::AuxSendPlugin*>(created.get());
        if (send == nullptr)
            return false;
        send->busNumber = bus;
        t->pluginList.insertPlugin(created, t->pluginList.indexOf(volume) + 1, nullptr);
    }
    // Only a new level (or a new send) touches the gain: switching pre/post isn't a move of the
    // send's control (it would turn its automation off).
    if (levelDb || isNew)
    {
        if (level <= model::minusInfinityDb)
            send->gain->setParameter(0.0f, juce::sendNotification);
        else
            send->setGainDb(level);
    }
    // Pre-fader sends sit just before the volume, post-fader ones just after it.
    if (volume != nullptr)
    {
        auto parent = send->state.getParent();
        const int sendAt = parent.indexOf(send->state);
        const int volumeAt = parent.indexOf(volume->state);
        if (wantPre && sendAt > volumeAt)
            parent.moveChild(sendAt, volumeAt, um);
        else if (!wantPre && sendAt < volumeAt)
            parent.moveChild(sendAt, volumeAt, um);
    }
    return true;
}

namespace
{
/// Reads one channel's meter plugin through a client of its own (tracktion keeps each
/// client's peaks apart), reporting silence while the channel isn't heard.
class MeterReader final : public Session::LevelReader
{
public:
    MeterReader(te::LevelMeterPlugin& meter, te::Track* track) : meter_(&meter), track_(track)
    {
        meter_->measurer.addClient(client_);
    }
    ~MeterReader() override { meter_->measurer.removeClient(client_); }

    std::array<float, 2> takePeaks() override
    {
        std::array<float, 2> peaks{};
        for (int c = 0; c < 2; ++c)
            peaks[static_cast<size_t>(c)] = juce::Decibels::decibelsToGain(client_.getAndClearAudioLevel(c).dB, -100.0f);
        if (track_ != nullptr && !heard())
            return {};
        return peaks;
    }

private:
    bool heard() const
    {
        if (track_->isMuted(true))
            return false;
        return !track_->edit.areAnyTracksSolo() || track_->isSolo(true) || track_->isSoloIsolate(true);
    }

    juce::ReferenceCountedObjectPtr<te::LevelMeterPlugin> meter_;
    te::Track::Ptr track_;
    te::LevelMeasurer::Client client_;
};
} // namespace

std::unique_ptr<Session::LevelReader> Session::levelReader(std::uint64_t channelId)
{
    auto* list = impl_->plugins(channelId);
    auto* meter = list != nullptr ? findIn<te::LevelMeterPlugin>(*list) : nullptr;
    if (meter == nullptr)
        return nullptr;
    return std::make_unique<MeterReader>(*meter, impl_->channelTrack(channelId));
}

void Session::setDeviceEnabled(std::uint64_t deviceId, bool enabled)
{
    if (auto* p = impl_->plugin(deviceId); p != nullptr && p->isEnabled() != enabled)
    {
        impl_->beginEdit("Module on/off");
        p->setEnabled(enabled);
    }
}

std::string Session::deviceViewState(std::uint64_t deviceId) const
{
    if (auto* p = impl_->plugin(deviceId))
        return p->state.getProperty(Impl::panelViewId).toString().toStdString();
    return {};
}

void Session::setDeviceViewState(std::uint64_t deviceId, const std::string& json)
{
    auto* p = impl_->plugin(deviceId);
    if (p == nullptr)
        return;
    if (json.empty())
        p->state.removeProperty(Impl::panelViewId, nullptr);
    else
        p->state.setProperty(Impl::panelViewId, juce::String(json), nullptr); // no undo manager
    impl_->listeners.call([deviceId](Listener& l) { l.deviceViewChanged(deviceId); });
}

std::unique_ptr<DeviceHandle> Session::openDevice(std::uint64_t deviceId)
{
    if (auto* native = dynamic_cast<DevicePlugin*>(impl_->plugin(deviceId)))
        return std::make_unique<NativeDeviceHandle>(*this, *native);
    return nullptr;
}

std::vector<Session::PluginParameter> Session::pluginParameters(std::uint64_t deviceId) const
{
    std::vector<PluginParameter> out;
    if (auto* p = impl_->plugin(deviceId))
        for (auto* param : p->getAutomatableParameters())
            out.push_back({param->paramID.toStdString(), param->getParameterName().toStdString(),
                           param->getCurrentNormalisedValue(), param->getCurrentValueAsString().toStdString()});
    return out;
}

bool Session::setPluginParameter(std::uint64_t deviceId, const std::string& parameterId, float value)
{
    auto* p = impl_->plugin(deviceId);
    if (p == nullptr)
        return false;
    for (auto* param : p->getAutomatableParameters())
        if (param->paramID.toStdString() == parameterId)
        {
            param->setNormalisedParameter(std::clamp(value, 0.0f, 1.0f), juce::sendNotification);
            return true;
        }
    return false;
}

void Session::showPluginEditor(std::uint64_t deviceId)
{
    if (auto* p = impl_->plugin(deviceId))
        p->showWindowExplicitly();
}

void Session::scanPlugins(std::function<void(const juce::String&)> onProgress, std::function<void(int)> onDone)
{
    if (isScanning())
        return;
    impl_->scanner = std::make_unique<PluginScanner>(impl_->engine, std::move(onProgress), std::move(onDone));
}

bool Session::isScanning() const
{
    return impl_->scanner != nullptr && !static_cast<PluginScanner*>(impl_->scanner.get())->isDone();
}

// ---- projects ------------------------------------------------------------------------------

namespace
{
const juce::Identifier kViewStateId{"fdView"}; // the UI's view state, JSON (not undoable)
} // namespace

void Session::replaceEdit(std::unique_ptr<te::Edit> edit, std::optional<juce::File> file)
{
    if (onProjectWillChange)
        onProjectWillChange(); // the UI drops its device handles and panels first
    impl_->detach();
    impl_->file = std::move(file);
    impl_->attach(std::move(edit));
    if (onProjectChanged)
        onProjectChanged();
    impl_->listeners.call([](Listener& l) { l.projectReplaced(); });
}

void Session::newProject()
{
    replaceEdit(impl_->newEdit(), std::nullopt);
}

bool Session::open(const juce::File& file, std::string* error)
{
    if (!file.existsAsFile())
    {
        if (error != nullptr)
            *error = "File not found: " + file.getFullPathName().toStdString();
        return false;
    }
    auto edit = te::loadEditFromFile(impl_->engine, file);
    if (edit == nullptr)
    {
        if (error != nullptr)
            *error = "Not a project: " + file.getFullPathName().toStdString();
        return false;
    }
    replaceEdit(std::move(edit), file);
    return true;
}

bool Session::saveAs(const juce::File& file)
{
    auto& edit = *impl_->edit;
    if (viewStateProvider)
        edit.state.setProperty(kViewStateId, juce::String(viewStateProvider()), nullptr);
    edit.flushState(); // device states (and our devices' extra state) into the tree
    if (!te::EditFileOperations(edit).writeToFile(file, false))
        return false;
    impl_->file = file;
    edit.resetChangedStatus();
    impl_->dirty = false;
    return true;
}

bool Session::save()
{
    return impl_->file && saveAs(*impl_->file);
}

std::optional<juce::File> Session::file() const
{
    return impl_->file;
}

bool Session::hasUnsavedChanges() const
{
    return impl_->dirty || impl_->edit->hasChangedSinceSaved();
}

std::string Session::savedViewState() const
{
    return impl_->edit->state.getProperty(kViewStateId).toString().toStdString();
}

void Session::undo()
{
    impl_->edit->getUndoManager().undo();
}

void Session::redo()
{
    impl_->edit->getUndoManager().redo();
}

bool Session::canUndo() const
{
    return impl_->edit->getUndoManager().canUndo();
}

bool Session::canRedo() const
{
    return impl_->edit->getUndoManager().canRedo();
}

std::string Session::undoDescription() const
{
    return impl_->edit->getUndoManager().getUndoDescription().toStdString();
}

std::string Session::redoDescription() const
{
    return impl_->edit->getUndoManager().getRedoDescription().toStdString();
}

Session::EditGroup::EditGroup(Session& session, const std::string& name) : session_(session)
{
    auto& impl = session_.impl();
    if (impl.groupDepth++ == 0)
        impl.edit->getUndoManager().beginNewTransaction(juce::String(name));
}

Session::EditGroup::~EditGroup()
{
    // The step stays open, as after any command: tracktion's own follow-ups (it re-sorts clips
    // and tracks asynchronously, through the undo manager) join it, and tracktion closes it
    // once edits pause. The next command or group starts a new one.
    --session_.impl().groupDepth;
}

// ---- offline rendering ---------------------------------------------------------------------

namespace
{
/// What a render's caller holds: progress and cancelling, from any thread. The render itself
/// (an Edit copy and its graph) stays on the message thread.
struct RenderProgress final : Session::RenderJob
{
    std::atomic<float> fraction{0.0f};
    std::atomic<bool> cancelled{false};
    float progress() const override { return fraction; }
    void cancel() override { cancelled = true; }
};
} // namespace

struct Session::Impl::Render final : private juce::Timer
{
    Render() { startTimer(50); }
    ~Render() override { stopTimer(); }

    std::shared_ptr<RenderProgress> state = std::make_shared<RenderProgress>();
    std::unique_ptr<te::Edit> copy;
    std::shared_ptr<te::EditRenderer::Handle> handle; // after `copy`: goes first
    std::function<void(const std::string&)> done;

private:
    void timerCallback() override
    {
        if (handle == nullptr)
            return;
        state->fraction = handle->getProgress();
        if (state->cancelled)
            handle->cancel();
    }
};

std::shared_ptr<Session::RenderJob> Session::render(const RenderRequest& request, const juce::File& destination,
                                                    std::function<void(const std::string& error)> done)
{
    auto& impl = *impl_;
    if (!(request.end > request.start) || (request.track && impl.track(*request.track) == nullptr))
        return nullptr;

    // A copy of the project: our devices' state is all in its tree; third-party plugins get
    // their live state copied over, so nothing in the open project is touched.
    te::Edit::Options options{impl.engine, impl.edit->state.createCopy(), te::ProjectItemID::createNewID(te::ProjectID{})};
    options.role = te::Edit::forRendering;
    options.numUndoLevelsToStore = 0;
    options.numAudioTracks = 0;
    if (impl.file)
        options.editFileRetriever = [f = *impl.file] { return f; };
    auto job = std::make_shared<Impl::Render>();
    job->copy = te::Edit::createEdit(std::move(options));
    if (job->copy == nullptr)
        return nullptr;
    for (auto* p : te::getAllPlugins(*job->copy, false))
        if (auto* copied = dynamic_cast<te::ExternalPlugin*>(p))
            if (auto* live = dynamic_cast<te::ExternalPlugin*>(impl.plugin(p->itemID.getRawID())))
                if (auto* from = live->getAudioPluginInstance())
                    if (auto* to = copied->getAudioPluginInstance())
                    {
                        juce::MemoryBlock state;
                        from->getStateInformation(state);
                        to->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                    }

    te::Renderer::Parameters params(*job->copy);
    params.destFile = destination;
    params.audioFormat = impl.engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 32;
    params.sampleRateForAudio = request.sampleRate;
    params.blockSizeForAudio = 512;
    params.time = {te::TimePosition::fromSeconds(request.start), te::TimePosition::fromSeconds(request.end)};
    params.canRenderInMono = false;
    params.checkNodesForAudio = false; // silence is a result too
    params.usePlugins = true;
    params.useMasterPlugins = !request.track.has_value();
    const auto tracks = te::getAllTracks(*job->copy);
    if (request.track)
    {
        // One track, with the tracks feeding its sidechains (ADR 0025): they play into it, muted
        // in the copy so they aren't heard themselves.
        const auto id = te::EditItemID::fromRawID(*request.track);
        const auto sources = te::findStemSourceTracks(*job->copy, {id});
        for (int i = 0; i < tracks.size(); ++i)
        {
            if (tracks[i]->itemID == id)
                params.tracksToDo.setBit(i);
            else if (sources.contains(tracks[i]->itemID))
            {
                params.tracksToDo.setBit(i);
                tracks[i]->setMute(true);
            }
        }
        params.tracksToProcessWhileMuted = sources;
    }
    else
        params.tracksToDo = te::toBitSet(tracks);

    job->done = std::move(done);
    std::weak_ptr<Impl::Render> weak = job;
    Impl* owner = &impl;
    job->handle = te::EditRenderer::render(params, [weak, owner](auto result) {
        const std::string error = result ? std::string() : std::string(result.error());
        juce::MessageManager::callAsync([weak, owner, error] {
            auto finished = weak.lock();
            if (finished == nullptr)
                return; // the session has gone
            auto callback = std::move(finished->done);
            std::erase(owner->renders, finished);
            if (callback)
                callback(error);
        }); // `finished`, the last reference, takes the Edit copy with it here
    });
    impl.renders.push_back(job);
    return job->state;
}

juce::AudioDeviceManager& Session::deviceManager()
{
    return impl_->engine.getDeviceManager().deviceManager;
}

double Session::cpuUsage() const
{
    return impl_->engine.getDeviceManager().getCpuUsage();
}
} // namespace fd::engine
