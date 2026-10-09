#include "engine/SessionImpl.h"

#include "engine/DevicePlugin.h"
#include "model/NoteEditing.h"

#include <fd/device/Registry.h>

namespace fd::engine
{
namespace
{
const juce::Identifier kColourId{"fdColour"}; // index into the clip palette

/// Number of colours in ui::Theme::clipColours; snapshots carry indices modulo this.
constexpr int kPaletteSize = 16;

/// tracktion's own mixer plugins on every track: part of the mixer, not the device chain.
bool isMixerPlugin(const te::Plugin* p)
{
    return dynamic_cast<const te::VolumeAndPanPlugin*>(p) != nullptr || dynamic_cast<const te::LevelMeterPlugin*>(p) != nullptr;
}

/// UI access to one of our devices (the panel's binding through the engine).
class NativeDeviceHandle final : public DeviceHandle
{
public:
    explicit NativeDeviceHandle(DevicePlugin& plugin)
        : plugin_(&plugin), native_(plugin), binding_(plugin),
          context_{plugin.device().info(), binding_, &plugin.device().analysis(), &plugin.device()}
    {
    }
    ui::DeviceUiContext& context() override { return context_; }
    std::string_view deviceId() const override { return native_.device().info().id; }

private:
    te::Plugin::Ptr plugin_; // keeps the device alive while its panel exists
    DevicePlugin& native_;
    DevicePluginBinding binding_;
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
                               .getChildFile("FossDAW")
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
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    dirty = false;
    midiTarget.reset();
    midiDevicesChanged(); // playback context and input routing for this edit
    rebuildSnapshot();
    edit->state.addListener(this);
}

void Session::Impl::detach()
{
    if (edit == nullptr)
        return;
    cancelPendingUpdate();
    edit->state.removeListener(this);
    edit->getTransport().stop(false, true);
    edit.reset();
}

Session::Impl::~Impl()
{
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

    int index = 0;
    for (auto* t : te::getAudioTracks(*edit))
    {
        model::TrackInfo info;
        info.id = t->itemID.getRawID();
        info.kind = kindOf(*t);
        info.name = t->getName().toStdString();
        info.colour = static_cast<int>(t->state.getProperty(kColourId, index * 3 % kPaletteSize));
        for (auto* c : t->getClips())
        {
            auto clip = clipInfo(*c, info.colour);
            info.clips.push_back(std::move(clip));
        }
        std::sort(info.clips.begin(), info.clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        for (auto* p : chain(*t))
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
            }
            else
                slot.kind = model::DeviceSlot::Kind::External;
            info.devices.push_back(std::move(slot));
        }
        s->tracks.push_back(std::move(info));
        ++index;
    }
    snapshot = std::move(s);
    if (owner.onSnapshotChanged)
        owner.onSnapshotChanged();
}

te::AudioTrack* Session::Impl::track(std::uint64_t id) const
{
    for (auto* t : te::getAudioTracks(*edit))
        if (t->itemID.getRawID() == id)
            return t;
    return nullptr;
}

te::AudioTrack* Session::Impl::insertTrack(const juce::String& name)
{
    const int count = te::getAudioTracks(*edit).size();
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr);
    if (track == nullptr)
        return nullptr;
    track->setName(juce::String(count + 1) + " " + name);
    track->state.setProperty(kColourId, count * 3 % kPaletteSize, &edit->getUndoManager());
    return track.get();
}

te::Plugin* Session::Impl::insertDevice(te::AudioTrack& track, const std::string& key, int index)
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
    const auto devices = chain(track);
    int at = track.pluginList.size();
    if (index >= 0 && index < static_cast<int>(devices.size()))
        at = track.pluginList.indexOf(devices[static_cast<size_t>(index)]);
    else
        for (int i = 0; i < track.pluginList.size(); ++i)
            if (isMixerPlugin(track.pluginList[i]))
            {
                at = i;
                break;
            }
    track.pluginList.insertPlugin(plugin, at, nullptr);
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
    const auto devices = chain(track);
    if (std::any_of(devices.begin(), devices.end(), [](auto* p) { return p->isSynth(); }))
        return false;
    return insertDevice(track, "fd:subtractive-synth", 0) != nullptr;
}

std::vector<te::Plugin*> Session::Impl::chain(te::AudioTrack& track)
{
    std::vector<te::Plugin*> out;
    for (auto* p : track.pluginList)
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

void Session::play()
{
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
        impl_->beginEdit();
        tempo->setBpm(std::clamp(bpm, 20.0, 999.0));
    }
}

void Session::setTimeSignature(int numerator, int denominator)
{
    if (auto* sig = impl_->edit->tempoSequence.getTimeSig(0))
    {
        impl_->beginEdit();
        sig->setStringTimeSig(juce::String(std::clamp(numerator, 1, 32)) + "/" + juce::String(denominator));
    }
}

std::uint64_t Session::addTrack()
{
    impl_->beginEdit();
    auto* track = impl_->insertTrack("Track");
    return track != nullptr ? track->itemID.getRawID() : 0;
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

    impl_->beginEdit();
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
    impl_->beginEdit();
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
    impl_->beginEdit();

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
    impl_->beginEdit();
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
    impl_->beginEdit();
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
    impl_->beginEdit();
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
    impl_->beginEdit();
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
            impl_->beginEdit();
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
    auto* track = impl_->track(trackId);
    if (track == nullptr)
        return 0;
    impl_->beginEdit();
    auto* plugin = impl_->insertDevice(*track, key, index);
    return plugin != nullptr ? plugin->itemID.getRawID() : 0;
}

void Session::removeDevice(std::uint64_t deviceId)
{
    if (auto* p = impl_->plugin(deviceId))
    {
        impl_->beginEdit();
        p->removeFromParent();
    }
}

void Session::moveDevice(std::uint64_t deviceId, int index)
{
    auto* p = impl_->plugin(deviceId);
    auto* track = p != nullptr ? dynamic_cast<te::AudioTrack*>(p->getOwnerTrack()) : nullptr;
    if (track == nullptr)
        return;
    const auto devices = Impl::chain(*track);
    index = std::clamp(index, 0, static_cast<int>(devices.size()) - 1);
    auto* target = devices[static_cast<size_t>(index)];
    if (target == p)
        return;
    auto parent = p->state.getParent();
    impl_->beginEdit();
    parent.moveChild(parent.indexOf(p->state), parent.indexOf(target->state), &impl_->edit->getUndoManager());
}

void Session::setDeviceEnabled(std::uint64_t deviceId, bool enabled)
{
    if (auto* p = impl_->plugin(deviceId); p != nullptr && p->isEnabled() != enabled)
    {
        impl_->beginEdit();
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
}

std::unique_ptr<DeviceHandle> Session::openDevice(std::uint64_t deviceId)
{
    if (auto* native = dynamic_cast<DevicePlugin*>(impl_->plugin(deviceId)))
        return std::make_unique<NativeDeviceHandle>(*native);
    return nullptr;
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

juce::AudioDeviceManager& Session::deviceManager()
{
    return impl_->engine.getDeviceManager().deviceManager;
}

double Session::cpuUsage() const
{
    return impl_->engine.getDeviceManager().getCpuUsage();
}
} // namespace fd::engine
