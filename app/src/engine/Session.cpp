#include "Granular.h"
#include "engine/ClipFades.h"
#include "engine/ClipNotes.h"
#include "engine/DevicePlugin.h"
#include "engine/EngineBehaviour.h"
#include "engine/PluginScan.h"
#include "engine/SessionImpl.h"
#include "engine/SongKey.h"
#include "model/ModuleFacts.h"
#include "model/NoteEditing.h"
#include "model/Racks.h"
#include "model/SongHarmony.h"
#include "model/TrackEdits.h"

#include <atomic>
#include <fd/audiofile/AudioFile.h>
#include <fd/device/Registry.h>

namespace fd::engine
{
namespace
{
const juce::Identifier& kColourId = Session::Impl::colourPropertyId; // index into the clip palette
const juce::Identifier& kRoleId = Session::Impl::rolePropertyId;     // "return" on a return track (ADR 0026)
// The project format: 2 since mixer levels sit on Segno's taper (ADR 0028), 3 since automation
// lanes are rows of their own (ADR 0029). Older projects are converted when opened.
const juce::Identifier kFormatId{"fdFormat"};
constexpr int kFormat = 4;

/// Number of colours in ui::Theme::clipColours; snapshots carry indices modulo this.
constexpr int kPaletteSize = Session::Impl::paletteSize;

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
/// the module, by name. A Follower's Listen (modulators.md §12.4) says "Here" for none.
class SessionSidechain final : public ui::SidechainRouting
{
public:
    SessionSidechain(Session& session, std::uint64_t deviceId, std::string none = "None")
        : session_(session), deviceId_(deviceId), none_(std::move(none))
    {
    }

    std::vector<std::string> sources() const override
    {
        choices_ = session_.sidechainSources(deviceId_);
        const auto current = source();
        if (current && std::find(choices_.begin(), choices_.end(), *current) == choices_.end())
            choices_.push_back(*current);
        std::vector<std::string> names{none_};
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
            if (const auto* d = model::findDevice(t.devices, deviceId_)) // in a rack too
                return d->sidechainSource;
        return std::nullopt;
    }

    Session& session_;
    std::uint64_t deviceId_;
    std::string none_; // the first choice: no sidechain ("None"), or where it sits ("Here")
    mutable std::vector<std::uint64_t> choices_;
};

/// A module's parameters in the DAW, with its automation (a control's menu, ADR 0028).
class SessionDeviceBinding final : public DevicePluginBinding
{
public:
    SessionDeviceBinding(Session& session, DevicePlugin& plugin) : DevicePluginBinding(plugin), session_(session) {}

    void beginEdit(const juce::String& name) override { session_.impl().beginEdit(name); }

    void addAutomationItems(size_t i, juce::PopupMenu& menu) override
    {
        if (const auto t = target(i); t && session_.automationMenu)
            session_.automationMenu(*t, menu);
    }

    // In a rack (racks.md §4): set by its macro, lit and dimmed by where the pointer is.
    [[nodiscard]] std::optional<MacroMapping> macroMapping(size_t i) const override
    {
        const auto* rack = holder();
        if (rack == nullptr)
            return std::nullopt;
        int macro = -1;
        const auto* m = model::mappingOf(*rack->rack, id(), std::string(spec(i).id), &macro);
        if (m == nullptr)
            return std::nullopt;
        const auto& pointer = session_.macroPointer();
        return MacroMapping{
            juce::String::fromUTF8(rack->rack->macros[static_cast<size_t>(macro)].name.c_str()), m->from,
            m->to, pointer.rack == rack->id && pointer.macro >= 0 && pointer.macro != macro};
    }
    [[nodiscard]] MacroDrag macroDrag(size_t i) const override
    {
        const auto& pointer = session_.macroPointer();
        if (!pointer.dragging)
            return {};
        const auto* rack = holder();
        if (rack == nullptr || rack->id != pointer.rack || !canAutomate(i))
            return {};
        return {true, pointer.module == id() && pointer.param == spec(i).id};
    }
    void addMacroItems(size_t i, juce::PopupMenu& menu) override
    {
        if (holder() != nullptr && session_.macroMenu)
            session_.macroMenu(id(), std::string(spec(i).id), menu);
    }

    // Moved by its track's modulators (modulators.md §7.4).
    [[nodiscard]] std::optional<Modulated> modulated(size_t i) const override
    {
        return session_.impl().modulatedLook(plugin_.parameter(i));
    }
    [[nodiscard]] MacroDrag modulatorDrag(size_t i) const override
    {
        return session_.impl().modulatorDragLook(plugin_.parameter(i));
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
    std::optional<Session::ParamTarget> target(size_t i) const
    {
        return session_.moduleParameter(plugin_.itemID.getRawID(), std::string(spec(i).id));
    }
    [[nodiscard]] std::uint64_t id() const { return plugin_.itemID.getRawID(); }
    /// The rack holding the module, as the snapshot has it (null: none).
    const model::DeviceSlot* holder() const
    {
        snapshot_ = session_.snapshot(); // keeps what's returned alive
        for (const auto* list : {&snapshot_->tracks, &snapshot_->returns})
            for (const auto& t : *list)
                if (const auto* rack = model::rackHolding(t.devices, id()))
                    return rack;
        return model::rackHolding(snapshot_->master.devices, id());
    }

    Session& session_;
    mutable std::shared_ptr<const model::Snapshot> snapshot_;
    RouteDrag ring_;
};

class NativeDeviceHandle final : public DeviceHandle
{
public:
    NativeDeviceHandle(Session& session, DevicePlugin& plugin)
        : plugin_(&plugin), native_(plugin), binding_(session, plugin),
          sidechain_(session, plugin.itemID.getRawID()),
          presets_(session.presetHost(plugin.itemID.getRawID())),
          context_{plugin.device().info(), binding_,    &plugin.device().analysis(),
                   &plugin.device(),       &sidechain_, presets_.get()}
    {
        // An editor's Preview (ADR 0053): a note held on the module's track, live, as the
        // browser's previews play.
        context_.playNote = [this, &session](int note, float velocity)
        {
            if (auto* track = session.impl().trackOf(*plugin_)) // its rack's track, in one
                track->injectLiveMidiMessage(
                    velocity > 0.0f ? juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, note), velocity)
                                    : juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, note)),
                    {});
        };
        context_.segnoFolder = [&session] { return session.segnoFolder(); };
    }
    ui::DeviceUiContext& context() override { return context_; }
    std::string_view deviceId() const override { return native_.device().info().id; }

private:
    te::Plugin::Ptr plugin_; // keeps the device alive while its panel exists
    DevicePlugin& native_;
    SessionDeviceBinding binding_;
    SessionSidechain sidechain_;
    std::unique_ptr<PresetHost> presets_;
    ui::DeviceUiContext context_;
};

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
    // Its own, given when it was made (ADR 0043); the track's only for a clip made outside Segno.
    clip.colour = static_cast<int>(c.state.getProperty(kColourId, trackColour));
    clip.fromTake = static_cast<std::uint64_t>(static_cast<juce::int64>(c.state["fdTake"])); // ADR 0031
    clip.muted = c.isMuted(); // ADR 0035
    if (auto* midi = dynamic_cast<te::MidiClip*>(&c))
    {
        // Notes count beats from the content start (the clip's start minus its offset).
        clip.contentStartBeat = c.edit.tempoSequence.toBeats(pos.getStart() - pos.getOffset()).inBeats();
        for (auto* n : midi->getSequence().getNotes())
            clip.notes.push_back(noteOf(*n));
        std::sort(clip.notes.begin(), clip.notes.end(), model::noteOrder);
        clip.controllers = controllerLanesOf(*midi); // piano-roll.md §11
        clip.chanceSeed = chanceSeedOf(*midi);       // §10
        clip.chanceKept = keptChanceOf(c).has_value(); // §26
    }
    if (auto* audio = dynamic_cast<te::AudioClipBase*>(&c))
    {
        clip.sourcePath = audio->getOriginalFile().getFullPathName().toStdString();
        clip.sound = soundOf(*audio); // ADR 0063
        // Warping (P6b): while it warps, its content counts the song's time, its file laid out in
        // beats.
        const auto map = contentMapOf(*audio);
        clip.warp = warpOf(*audio);
        clip.pitch = pitchOf(audio->state);
        clip.fileLength = map.fileLength;
        clip.beatsPerSecond = map.beatsPerSecond;
        clip.sourceLength = map.fileLength > 0.0 ? map.contentLength() : 0.0;
        // The format of the person's file: a reversed clip's forward one, not our copy (ADR 0063).
        const auto file =
            c.state.hasProperty(soundIds::forward)
                ? te::SourceFileReference::findFileFromString(c.edit, c.state[soundIds::forward].toString())
                : audio->getOriginalFile();
        const auto info = te::AudioFile(c.edit.engine, file).getInfo();
        clip.fileChannels = info.numChannels;
        clip.fileSampleRate = info.sampleRate;
        clip.fileBitDepth = info.isFloatingPoint ? 0 : info.bitsPerSample;
    }
    clip.loop = loopOf(c.state, c.edit.tempoSequence); // ADR 0048
    if (clip.loop.on() && clip.kind == model::ClipInfo::Kind::Midi) // the zero the clip view shows
        clip.contentStartBeat = c.edit.tempoSequence.toBeats(te::TimePosition::fromSeconds(model::clipViewOrigin(clip))).inBeats();
    return clip;
}

model::TrackInfo::Kind kindOf(te::AudioTrack& t)
{
    for (auto* p : t.pluginList)
        if (Session::Impl::playsNotes(*p))
            return model::TrackInfo::Kind::Instrument;
    return model::TrackInfo::Kind::Audio;
}
} // namespace

model::ClipInfo clipInfoOf(te::Clip& clip)
{
    return clipInfo(clip, 0);
}

model::ClipLoop loopOf(const juce::ValueTree& clip, const te::TempoSequence& tempo)
{
    const auto seconds = [&](double beats) { return tempo.toTime(te::BeatPosition::fromBeats(beats)).inSeconds(); };
    // A MIDI clip, and an audio clip that follows the tempo, loop in beats (ADR 0063).
    if (clip.hasType(te::IDs::MIDICLIP) || static_cast<bool>(clip[te::IDs::autoTempo]))
    {
        const double start = clip[te::IDs::loopStartBeats], length = clip[te::IDs::loopLengthBeats];
        return length > 0.0 ? model::ClipLoop{seconds(start), seconds(start + length) - seconds(start)} : model::ClipLoop{};
    }
    const double length = clip[te::IDs::loopLength];
    return length > 0.0 ? model::ClipLoop{clip[te::IDs::loopStart], length} : model::ClipLoop{};
}

void writeLoop(te::Clip& clip, const model::ClipLoop& loop, juce::UndoManager* um)
{
    // A MIDI clip, and an audio clip that follows the tempo, loop in beats (ADR 0063).
    auto* audio = dynamic_cast<te::AudioClipBase*>(&clip);
    if (dynamic_cast<te::MidiClip*>(&clip) != nullptr || (audio != nullptr && audio->getAutoTempo()))
    {
        const auto& tempo = clip.edit.tempoSequence;
        const auto beats = [&](double seconds) { return tempo.toBeats(te::TimePosition::fromSeconds(seconds)); };
        if (loop.on())
            clip.setLoopRangeBeats({beats(loop.start), beats(loop.end())});
        else if (audio == nullptr)
            clip.setLoopRangeBeats({});
        else
        {
            clip.state.setProperty(te::IDs::loopStartBeats, 0.0, um);
            clip.state.setProperty(te::IDs::loopLengthBeats, 0.0, um);
        }
        return;
    }
    if (loop.on())
    {
        if (audio == nullptr)
            return;
        // In whole samples of the file, as the engine wraps it: the seams drawn and faded are then
        // exactly where it wraps, lap after lap.
        const double rate = audio->getAudioFile().getSampleRate();
        const auto whole = [rate](double seconds) { return rate > 0.0 ? std::round(seconds * rate) / rate : seconds; };
        audio->setLoopRange({te::TimePosition::fromSeconds(whole(loop.start)), te::TimeDuration::fromSeconds(whole(loop.length))});
        return;
    }
    // Written directly: the engine's own way does nothing for a clip whose file is missing.
    clip.state.setProperty(te::IDs::loopStart, 0.0, um);
    clip.state.setProperty(te::IDs::loopLength, 0.0, um);
}

void placeClip(te::Clip& clip, double start, double length, double offset)
{
    const auto loop = loopOf(clip.state, clip.edit.tempoSequence);
    clip.setPosition({{te::TimePosition::fromSeconds(start), te::TimePosition::fromSeconds(start + length)},
                      te::TimeDuration::fromSeconds(model::phaseOf(offset, loop))});
}

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
    // Recorded audio goes to this project's folder (ADR 0030).
    setRecordingFiles(engine, [this](te::Track& t, const juce::String& extension) {
        return edit != nullptr && &t.edit == edit.get() ? recordingFile(t, extension) : juce::File();
    });
    // Beats starts pieces at a clip's transients (audio-editor.md §7.4), once they're found.
    setTransientSource(engine, [this](te::AudioClipBase& c, bool forRendering)
                       { return transientsOf(c, forRendering); });
    analyser.onFound = [this](const std::string& path) { analysisFound(path); };
    // The browser's previews play after the engine's output (browser.md §7).
    engine.getDeviceManager().deviceManager.addAudioCallback(&preview);
    tap.setMapper(&midiMapper); // MIDI learn's moves aren't captured as music
    midiDrain.startTimer(16);
    indexer.onChanged = [this]
    {
        if (owner.onBrowserIndexChanged)
            owner.onBrowserIndexChanged();
    };
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

std::unique_ptr<ui::SidechainRouting>
Session::Impl::sidechainRouting(Session& session, std::uint64_t deviceId, std::string none)
{
    return std::make_unique<SessionSidechain>(session, deviceId, std::move(none));
}

void Session::Impl::attach(std::unique_ptr<te::Edit> e)
{
    edit = std::move(e);
    chancePass = chancePassOf(*edit); // its clips' chance rolls (piano-roll.md §26)
    usedSources.clear(); // a new project's history
    if (browserStarted)
        updateBrowserPlaces(); // its folder is the Project place
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
    loadTakes(format < 4); // ADR 0032: takes belong to clips
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
    edit->getTransport().addListener(this);                // recorded clips (recording.md §5)
}

void Session::Impl::detach()
{
    if (edit == nullptr)
        return;
    cancelPendingUpdate();
    endMidiGestures(true); // a controller's move ends with its project
    midiMap.learning.reset();
    midiMap.pending.reset();
    midiMap.follow.clear();
    midiMapper.setLearning(false);
    endRecordingPass();
    lastTouched.reset();
    edit->getTransport().removeListener(this);
    edit->getTransport().removeChangeListener(this);
    edit->getParameterChangeHandler().removeListener(*this);
    edit->state.removeListener(this);
    edit->getTransport().stop(false, true);
    edit.reset();
    unsavedFolder.reset(); // a new project records into a new temporary folder
}

Session::Impl::~Impl()
{
    engine.getDeviceManager().deviceManager.removeAudioCallback(&preview);
    indexer.onChanged = nullptr;
    midiPreview.stop(); // while the Edit is still here
    bounces.clear(); // nothing lands any more
    renders.clear(); // cancels them (their Edit copies go before the engine)
    engine.getDeviceManager().removeChangeListener(this);
    tapDrain.stopTimer();
    setRecordingFiles(engine, {});
    setTransientSource(engine, {});
    analyser.onFound = nullptr;
    detach();
}

std::vector<double> Session::Impl::transientsOf(te::AudioClipBase& c, bool now)
{
    auto info = model::ClipInfo{};
    info.analysis = now ? analyser.now(forwardFileOf(c)) : analyser.get(forwardFileOf(c));
    info.warp = warpOf(c);
    info.transientsAdded = timesOf(c.state[warpIds::transientsAdded]);
    info.transientsRemoved = timesOf(c.state[warpIds::transientsRemoved]);
    return model::transientsOf(info);
}

void Session::Impl::analysisFound(const std::string& path)
{
    if (edit == nullptr)
        return;
    bool beats = false;
    for (auto* t : te::getAudioTracks(*edit))
        for (auto* c : t->getClips())
            if (auto* audio = dynamic_cast<te::AudioClipBase*>(c))
                if (forwardFileOf(*audio).getFullPathName().toStdString() == path)
                    beats = beats || (audio->getAutoTempo() && warpOf(*audio).mode == model::WarpMode::Beats);
    if (beats)
        edit->restartPlayback();
    triggerAsyncUpdate();
}

void Session::Impl::midiDevicesChanged()
{
    if (edit == nullptr)
        return;
    for (auto& in : engine.getDeviceManager().getMidiInDevices())
    {
        in->updateRetrospectiveBufferLength(0.0); // Capture MIDI has its own (MidiTap)
        if (!in->isEnabled())
            in->setEnabled(true);
        if (in->getMonitorMode() != te::InputDevice::MonitorMode::on)
            in->setMonitorMode(te::InputDevice::MonitorMode::on);
    }
    edit->getTransport().ensureContextAllocated(true); // instances for new inputs
    midiMapper.syncDevices();                          // before the tap, which asks it what it takes
    tap.syncDevices();
    if (!tapDrain.isTimerRunning())
        tapDrain.startTimer(10);
    applyInputs();
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
    s->keys.key = songKeyOf(*edit);

    // The module chain of a channel, as the views show it; a rack with its chains (racks.md §11).
    std::function<model::DeviceSlot(te::Plugin&)> slotOf = [&](te::Plugin& plugin)
    {
        auto* p = &plugin;
        model::DeviceSlot slot;
        slot.id = p->itemID.getRawID();
        slot.name = p->getName().toStdString();
        slot.enabled = p->isEnabled();
        slot.instrument = playsNotes(*p);
        if (auto* rack = dynamic_cast<te::RackInstance*>(p))
        {
            slot.kind = model::DeviceSlot::Kind::Native;
            slot.deviceId = "rack";
            if (rack->type != nullptr)
                slot.name = rack->type->rackName.get().toStdString();
            if (rack->type != nullptr && isKit(*rack->type)) // drum-kit.md §14
            {
                slot.deviceId = "drum-kit";
                slot.name = "Drum Kit";
            }
            slot.rack = rackInfo(*rack, slotOf);
        }
        else if (auto* native = dynamic_cast<DevicePlugin*>(p))
        {
            slot.kind = model::DeviceSlot::Kind::Native;
            slot.deviceId = std::string(native->device().info().id);
            slot.midiEffect = native->device().info().kind == DeviceKind::MidiEffect;
            slot.takesSidechain = takesSidechain(*p);
            if (slot.takesSidechain && p->getSidechainSourceID().isValid())
                slot.sidechainSource = p->getSidechainSourceID().getRawID();
            if (const auto* granular = dynamic_cast<const devices::Granular*>(&native->device()))
                slot.rollKept = granular->rollKept();
        }
        else
        {
            slot.kind = model::DeviceSlot::Kind::External;
            if (auto* external = dynamic_cast<te::ExternalPlugin*>(p))
                if (auto* instance = external->getAudioPluginInstance())
                    slot.midiEffect = instance->isMidiEffect();
        }
        return slot;
    };
    refreshModulatorRoutes(); // their copies, after whatever changed (a load, undo, a route)
    auto devicesOf = [&](te::PluginList& list, model::TrackInfo& info, te::Track* track)
    {
        const auto items = track != nullptr ? chainItems(*track) : std::vector<ChainItem>{};
        if (track == nullptr)
            for (auto* p : chain(list))
            {
                auto slot = slotOf(*p);
                if (slot.instrument && info.instrument.empty())
                    info.instrument = slot.name;
                info.devices.push_back(std::move(slot));
            }
        for (const auto& item : items)
        {
            if (item.modulator != nullptr)
            {
                info.devices.push_back(modulatorSlot(*item.modulator));
                continue;
            }
            auto slot = slotOf(*item.plugin);
            if (slot.instrument && info.instrument.empty())
                info.instrument = slot.name;
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
            if (c->state.hasProperty(auditionId)) // an audition's copy: not the project (ADR 0038)
                continue;
            auto clip = clipInfo(*c, info.colour);
            if (auto* audio = dynamic_cast<te::AudioClipBase*>(c))
            {
                // What's found in its file (P6b), once something asked for it (the editor showing
                // it, Beats playing it, a loop placed): read here, never started.
                clip.analysis = analyser.peek(forwardFileOf(*audio));
                clip.transientsAdded = timesOf(c->state[warpIds::transientsAdded]);
                clip.transientsRemoved = timesOf(c->state[warpIds::transientsRemoved]);
                // Its key (P6c): set by hand or pinned, else what's found.
                const auto stored = storedKeyOf(c->state);
                clip.key = stored ? *stored : (clip.analysis != nullptr ? clip.analysis->key : std::nullopt);
                clip.keyFound = !stored || static_cast<bool>(c->state.getProperty(keyIds::found, false));
                clip.followKey = followsKey(c->state);
                clip.followSteps = followStepsOf(*audio);
            }
            if (audition && std::find(audition->muted.begin(), audition->muted.end(), clip.id) != audition->muted.end())
                clip.muted = false; // muted only while its take is auditioned
            info.clips.push_back(std::move(clip));
        }
        if (audition && audition->track == info.id)
            info.audition = audition->take;
        std::sort(info.clips.begin(), info.clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        fillFades(*t, info.clips);
        devicesOf(t->pluginList, info, t);
        info.mix = mixOf(t->pluginList);
        if (!isReturn(*t))
        {
            info.input = inputOf(*t);
            info.stacks = stackInfos(*t, info.colour);
            for (const auto& stack : info.stacks)
            {
                for (auto& c : info.clips)
                    if (std::find(stack.parts.begin(), stack.parts.end(), c.id) != stack.parts.end())
                        c.stack = stack.id;
            }
        }
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

    s->edgeFades = edgeFadesOn(*edit);
    // MIDI learn (midi-learn.md §6): the project's mappings whose targets are there (one whose
    // module was deleted comes back with its undo), then the app's.
    for (auto& m : projectMidiMappings())
        if (owner.canMapMidi(m.target))
            s->midiMappings.push_back(std::move(m));
    s->midiMappings.insert(s->midiMappings.end(), midiMap.app.begin(), midiMap.app.end());

    // The song's sections (ADR 0049): the arranger track's clips, by start.
    edit->ensureArrangerTrack();
    if (auto* arranger = edit->getArrangerTrack())
    {
        for (auto* c : arranger->getClips())
        {
            const auto at = c->getPosition().time;
            s->sections.push_back({.id = c->itemID.getRawID(),
                                   .name = c->getName().toStdString(),
                                   .colour = static_cast<int>(c->state.getProperty(kColourId, 0)),
                                   .start = at.getStart().inSeconds(),
                                   .end = at.getEnd().inSeconds()});
        }
        std::sort(s->sections.begin(), s->sections.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
    }
    // The song's harmony, read from its notes, as the MIDI effects follow it (midi-effects.md §5.1).
    s->songChords = model::songHarmony(*s);
    auto& master = s->master;
    master.id = masterId();
    master.role = model::TrackInfo::Role::Master;
    master.name = "Master";
    devicesOf(edit->getMasterPluginList(), master, edit->getMasterTrack()); // its modulators too
    master.mix = mixOf(edit->getMasterPluginList());
    s->automationLanes = laneInfos();
    // Every file a clip or take uses, kept while the project is open (Remove unused files).
    for (const auto& t : s->tracks)
    {
        for (const auto& c : t.clips)
            if (!c.sourcePath.empty())
                usedSources.insert(c.sourcePath);
        for (const auto& stack : t.stacks)
            for (const auto& take : stack.takes)
                for (const auto& c : take.clips)
                    if (!c.sourcePath.empty())
                        usedSources.insert(c.sourcePath);
    }
    snapshot = std::move(s);
    publishMidiMapped(); // the inputs take what's mapped now
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

std::vector<std::string> Session::Impl::trackNames() const
{
    std::vector<std::string> names;
    for (auto* t : te::getAudioTracks(*edit))
        names.push_back(t->getName().toStdString());
    return names;
}

te::AudioTrack* Session::Impl::insertTrack()
{
    // After the last track and before the return tracks (ADR 0026).
    const auto existing = tracks();
    const auto name = model::nextTrackName(trackNames());
    const int colour = nextRowColour(); // tracks and lanes take turns through the palette
    auto track = edit->insertNewAudioTrack(te::TrackInsertPoint(nullptr, existing.empty() ? nullptr : existing.back()),
                                           nullptr);
    if (track == nullptr)
        return nullptr;
    track->state.setProperty(kColourId, colour, &edit->getUndoManager());
    track->setName(juce::String::fromUTF8(name.c_str()));
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
    // plugins at the end. A MIDI effect goes before the instrument (midi-effects.md §11).
    const auto devices = chain(list);
    if (isNoteEffect(*plugin))
        index = model::placeInChain(roles(devices), index, model::ChainRole::MidiEffect);
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
            placeClip(*c, cut.start, cut.length, cut.offset);
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
    settle(track, &edit->getUndoManager()); // takes follow (ADR 0032): a cut-off tail is a plain clip
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
    // A module in a rack (racks.md): its own id, as any module's.
    for (int i = 0; i < edit->getRackList().size(); ++i)
        if (auto rack = edit->getRackList().getRackType(i))
            for (auto* p : rack->getPlugins())
                if (p->itemID.getRawID() == id)
                    return p;
    return nullptr;
}

te::Clip* Session::Impl::clip(std::uint64_t id) const
{
    return te::findClipForID(*edit, te::EditItemID::fromRawID(id));
}

std::string Session::Impl::loopRefusal(te::Clip& c)
{
    // The clip and, if it has one, its stack's takes: what model::whyCantLoop looks at.
    model::TrackInfo track;
    auto info = clipInfo(c, 0);
    if (const auto stack = stackOf(c); stack.isValid())
    {
        model::TakeStackInfo s{.id = 1};
        for (int i = 0; i < stack.getNumChildren(); ++i)
            s.takes.push_back({.id = static_cast<std::uint64_t>(i + 1)});
        track.stacks.push_back(s);
        info.stack = 1;
    }
    return model::whyCantLoop(info, track);
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
    if (!isPlaying()) // Capture MIDI: what's played from now on is in the song
    {
        impl_->drainTap();
        impl_->playStartedAt = impl_->tap.now();
        impl_->wasPlaying = true;
        impl_->startPasses(); // a new pass: fresh dice for what doesn't keep its roll
    }
    impl_->edit->getTransport().play(false);
}

void Session::stop()
{
    impl_->drainTap(); // what was played keeps its place in the song (Capture MIDI)
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
    if (isPlaying())
        impl_->drainTap(); // before the jump: what was played keeps its place (Capture MIDI)
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
    auto* track = impl_->insertTrack();
    if (track == nullptr)
        return 0;
    if (!name.empty())
        track->setName(juce::String(name));
    return track->itemID.getRawID();
}

std::uint64_t Session::insertMidiFile(std::uint64_t trackId, const juce::File& file, double seconds)
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
    return clip->itemID.getRawID();
}

std::uint64_t Session::createMidiClip(std::uint64_t trackId, double seconds, double lengthSeconds)
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

    // The rest pair up in time order: a moved note keeps its other properties (its colour).
    std::sort(added.begin(), added.end(), model::noteOrder);
    std::sort(changed.begin(), changed.end(), [](auto* a, auto* b) { return model::noteOrder(noteOf(*a), noteOf(*b)); });
    const auto paired = std::min(added.size(), changed.size());
    for (size_t i = 0; i < paired; ++i)
    {
        const auto& to = added[i];
        changed[i]->setStartAndLength(te::BeatPosition::fromBeats(to.start), te::BeatDuration::fromBeats(to.length), undo);
        changed[i]->setNoteNumber(to.pitch, undo);
        changed[i]->setVelocity(midiVelocity(to.velocity), undo);
        changed[i]->setMute(to.muted, undo);
        writeNoteExtras(*changed[i], to, undo);
    }
    for (size_t i = paired; i < changed.size(); ++i)
        list.removeNote(*changed[i], undo);
    for (size_t i = paired; i < added.size(); ++i)
        if (auto* note = list.addNote(added[i].pitch, te::BeatPosition::fromBeats(added[i].start),
                                      te::BeatDuration::fromBeats(added[i].length),
                                      midiVelocity(added[i].velocity), 0, undo))
        {
            if (added[i].muted)
                note->setMute(true, undo);
            writeNoteExtras(*note, added[i], undo);
        }

    // Notes past the clip's end lengthen it to the end of that bar when nothing is in the way
    // (B15), in the same undo step; in a looped clip, notes written past its loop's end lengthen the
    // loop instead (ADR 0048).
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
        if (const auto info = clipInfo(*midi, 0); info.loop.on())
        {
            if (const auto length = model::lengthenedLoop(info, added, tempo))
                writeLoop(*midi, {info.loop.start, *length}, undo);
        }
        else if (const auto end = model::lengthenedClipEnd(clipInfo(*midi, 0), clips, notes, tempo))
        {
            const auto pos = midi->getPosition();
            midi->setPosition({{pos.getStart(), te::TimePosition::fromSeconds(*end)}, pos.getOffset()});
        }
    }
}

void Session::setControllers(std::uint64_t clipId, const std::vector<model::ControllerLane>& lanes,
                             const std::string& undoName)
{
    auto* midi = dynamic_cast<te::MidiClip*>(impl_->clip(clipId));
    if (midi == nullptr || lanes.empty())
        return;
    impl_->beginEdit(undoName);
    writeControllerLanes(*midi, lanes, &impl_->edit->getUndoManager());
}

bool Session::keepChance(std::uint64_t clipId, bool keep)
{
    auto* midi = dynamic_cast<te::MidiClip*>(impl_->clip(clipId));
    if (midi == nullptr || keptChanceOf(*midi).has_value() == keep)
        return false;
    impl_->beginEdit(keep ? "Keep chance as played" : "Let chance roll");
    impl_->keepChance(*midi, keep);
    return true;
}

void Session::Impl::keepChance(te::MidiClip& clip, bool keep)
{
    auto* um = &edit->getUndoManager();
    if (!keep)
    {
        clip.state.removeProperty(chanceIds::keptRoll, um);
        clip.state.removeProperty(chanceIds::keptLap, um);
        return;
    }
    // What it was heard with: the lap playing, or the last one played; never heard, this pass's
    // first lap (what it plays next).
    const auto heard = chanceHeardOf(*edit, clip.itemID.getRawID());
    const bool played = heard->heard.load();
    const auto roll = played ? heard->roll.load() : chancePass->roll.load();
    const int lap = played ? heard->lap.load() : 0;
    clip.state.setProperty(chanceIds::keptRoll, juce::String(static_cast<juce::int64>(roll)), um);
    clip.state.setProperty(chanceIds::keptLap, lap, um);
}

std::uint32_t Session::rerollChance(std::uint64_t clipId, std::optional<std::uint32_t> seed)
{
    auto* midi = dynamic_cast<te::MidiClip*>(impl_->clip(clipId));
    if (midi == nullptr)
        return 0;
    auto next = seed.value_or(0);
    while (next == 0 || (!seed && next == chanceSeedOf(*midi))) // a new one, never "none"
        next = static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt());
    impl_->beginEdit("Re-roll chance");
    midi->state.setProperty(noteIds::seed, static_cast<juce::int64>(next), &impl_->edit->getUndoManager());
    return next;
}

void Session::setMidiInputTarget(std::optional<std::uint64_t> trackId)
{
    if (impl_->midiTarget == trackId)
        return;
    impl_->midiTarget = trackId;
    impl_->applyInputs();       // the selection softly arms it (recording.md §4)
    impl_->triggerAsyncUpdate(); // the header shows it
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
                impl_->tap.pushKeyboard(message); // Capture MIDI hears it, whatever it plays
                keyboard->handleIncomingMidiMessage(message, te::MPESourceID{1});
            }
}

std::optional<Session::ClipSound> Session::clipSound(std::uint64_t clipId, std::optional<double> from,
                                                     std::optional<double> to) const
{
    const auto snapshot = this->snapshot();
    const model::ClipInfo* clip = nullptr;
    for (const auto& t : snapshot->tracks)
        for (const auto& c : t.clips)
            if (c.id == clipId)
                clip = &c;
    if (clip == nullptr || clip->kind != model::ClipInfo::Kind::Audio || clip->sourcePath.empty())
        return std::nullopt;
    const auto audio = audiofile::decode(clip->sourcePath);
    if (!audio || audio->numFrames() < 2 || audio->sampleRate <= 0.0)
        return std::nullopt;
    // The stretch of the source the clip plays, once.
    const double start = std::clamp(from.value_or(clip->start), clip->start, clip->end());
    const double end = std::clamp(to.value_or(clip->end()), start, clip->end());
    const auto first = static_cast<int>(std::floor((clip->offset + start - clip->start) * audio->sampleRate));
    const auto last = static_cast<int>(std::ceil((clip->offset + end - clip->start) * audio->sampleRate));
    const int a = std::clamp(first, 0, audio->numFrames()), b = std::clamp(last, a, audio->numFrames());
    if (b - a < 2)
        return std::nullopt;
    ClipSound out;
    out.sampleRate = audio->sampleRate;
    out.name = clip->name;
    out.samples.assign(static_cast<size_t>(b - a), 0.0f);
    for (const auto& ch : audio->channels)
        for (int i = a; i < b; ++i)
            out.samples[static_cast<size_t>(i - a)] +=
                ch[static_cast<size_t>(i)] / static_cast<float>(audio->numChannels());
    return out;
}

std::uint64_t Session::insertAudioFile(std::uint64_t trackId, const juce::File& file, double seconds)
{
    auto* track = impl_->track(trackId);
    const te::AudioFile audio(impl_->engine, file);
    if (track == nullptr || !audio.isValid())
        return 0;
    EditGroup group(*this, "Insert audio file");
    impl_->beginEdit("Insert audio file");
    const auto start = te::TimePosition::fromSeconds(std::max(0.0, seconds));
    auto clip = track->insertWaveClip(file.getFileNameWithoutExtension(), file,
                                      {{start, te::TimeDuration::fromSeconds(audio.getLength())}, {}}, false);
    if (clip == nullptr)
        return 0;
    // The engine would stretch a file with loop metadata to the tempo by itself: placed at its own
    // speed first (clip-loops.md §4).
    if (clip->getAutoTempo() || clip->getAutoPitch())
    {
        clip->setAutoTempo(false);
        clip->setAutoPitch(false);
        clip->setPosition({{start, te::TimeDuration::fromSeconds(audio.getLength())}, {}});
    }
    const auto id = clip->itemID.getRawID();
    impl_->keepFirstTempo(*clip);
    // A loop plays in time (audio-editor.md §7.2): Warp on at its tempo, in the mode its sound
    // chooses. Anything else plays at its file's speed.
    if (impl_->loopsFollowTempo || impl_->loopsFollowKey)
        if (const auto loop = impl_->loopTempo(*clip))
        {
            if (impl_->loopsFollowTempo)
                setWarp({id}, {.on = true, .mode = loop->second, .tempo = loop->first}, "Insert audio file");
            // And in key, when asked (§8.3): a loop with a key follows the song's.
            if (impl_->loopsFollowKey)
                setWarp({id}, {.followKey = true}, "Insert audio file");
        }
    const double end = impl_->clip(id) != nullptr ? impl_->clip(id)->getPosition().getEnd().inSeconds()
                                                  : start.inSeconds() + audio.getLength();
    impl_->clearUnder(*track, start.inSeconds(), end, {id});
    return id;
}

void Session::applyClipEdits(const std::vector<model::ClipEdit>& asked, const std::string& undoName)
{
    if (asked.empty())
        return;
    // A clip and its takes move as one (ADR 0032): its other parts follow, its takes shift.
    std::vector<model::ClipPlace> places;
    std::set<te::AudioTrack*> involved;
    for (const auto& e : asked)
    {
        if (auto* c = impl_->clip(e.clip))
            if (auto* source = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                involved.insert(source);
        if (auto* target = impl_->track(e.track))
            involved.insert(target);
    }
    for (auto* t : involved)
    {
        const auto p = impl_->places(*t);
        places.insert(places.end(), p.begin(), p.end());
    }
    const auto grouped = model::groupEdits(places, asked);
    const auto& edits = grouped.edits;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(undoName);
    for (const auto& m : grouped.moves) // while their clips are still where they were
        if (auto* target = impl_->track(m.toTrack))
            impl_->moveStack(m.stack, *target, m.delta, um);
    for (const auto& trim : grouped.trims)
        if (auto [track, stack] = impl_->findStack(trim.stack); track != nullptr)
        {
            if (trim.start)
                stack.setProperty(Impl::fromId, *trim.start, um);
            if (trim.end)
                stack.setProperty(Impl::toId, *trim.end, um);
        }
    for (const auto& e : edits)
    {
        auto* c = impl_->clip(e.clip);
        auto* target = impl_->track(e.track);
        if (c == nullptr || target == nullptr)
            continue;
        if (c->getClipTrack() != target)
            c->moveTo(*target);
        if (e.loop) // ADR 0048: before the placement, whose offset is then the phase in it
            writeLoop(*c, *e.loop, um);
        placeClip(*c, e.start, e.length, e.offset);
    }
    // No overlaps (ADR 0019): the placed clips cut what they cover, in the same undo step; a clip's
    // parts clear the gaps between them too.
    std::set<std::uint64_t> placed;
    for (const auto& e : edits)
        placed.insert(e.clip);
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<double, double>> spans; // (track, stack or clip)
    for (const auto& e : edits)
    {
        std::uint64_t stack = 0;
        for (const auto& p : places)
            if (p.clip == e.clip)
                stack = p.stack;
        const auto key = std::pair{e.track, stack != 0 ? stack : e.clip};
        auto [it, fresh] = spans.try_emplace(key, e.start, e.start + e.length);
        if (!fresh)
            it->second = {std::min(it->second.first, e.start), std::max(it->second.second, e.start + e.length)};
    }
    for (const auto& [key, span] : spans)
        if (auto* target = impl_->track(key.first))
            impl_->clearUnder(*target, span.first, span.second, placed);
    for (auto* t : involved)
        impl_->settle(*t, um);
}

std::string Session::setClipsLooping(const std::vector<std::uint64_t>& clips, bool on)
{
    std::string refused;
    std::vector<std::pair<te::Clip*, model::LoopPlacement>> changes;
    for (const auto id : clips)
    {
        auto* c = impl_->clip(id);
        if (c == nullptr || loopOf(c->state, c->edit.tempoSequence).on() == on)
            continue;
        if (on)
            if (auto why = impl_->loopRefusal(*c); !why.empty())
            {
                if (refused.empty())
                    refused = std::move(why);
                continue;
            }
        const auto info = clipInfo(*c, 0);
        changes.emplace_back(c, on ? model::loopOn(info) : model::loopOff(info));
    }
    if (changes.empty())
        return refused;
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(on ? "Loop clips" : "Stop looping clips");
    std::set<te::AudioTrack*> tracks;
    for (auto& [c, placed] : changes)
    {
        if (on && impl_->stackOf(*c).isValid()) // one take (more can't loop): it leaves its stack
            c->state.removeProperty(Impl::fromTakeId, um);
        writeLoop(*c, placed.loop, um);
        placeClip(*c, placed.start, placed.length, placed.offset);
        if (auto* t = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
            tracks.insert(t);
    }
    for (auto* t : tracks)
        impl_->settle(*t, um);
    return refused;
}

std::string Session::setClipLoop(std::uint64_t clipId, const model::ClipLoop& loop, const std::string& undoName)
{
    auto* c = impl_->clip(clipId);
    if (c == nullptr || !loop.on())
        return {};
    const bool wasOn = loopOf(c->state, c->edit.tempoSequence).on();
    if (!wasOn)
        if (auto why = impl_->loopRefusal(*c); !why.empty())
            return why;
    const auto placed = model::withLoop(clipInfo(*c, 0), loop);
    if (wasOn && placed == model::placementOf(clipInfo(*c, 0)))
        return {};
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit(undoName);
    if (!wasOn && impl_->stackOf(*c).isValid())
        c->state.removeProperty(Impl::fromTakeId, um);
    writeLoop(*c, placed.loop, um);
    placeClip(*c, placed.start, placed.length, placed.offset);
    if (auto* t = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
        impl_->settle(*t, um);
    return {};
}

std::vector<std::uint64_t> Session::duplicateClips(const std::vector<model::ClipEdit>& asked)
{
    std::vector<std::uint64_t> created;
    if (asked.empty())
        return created;
    // A clip's copy brings a copy of its takes (ADR 0032), once for all of its parts.
    std::vector<model::ClipPlace> places;
    std::set<te::AudioTrack*> sources;
    for (const auto& p : asked)
        if (auto* c = impl_->clip(p.clip))
            if (auto* source = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                sources.insert(source);
    for (auto* t : sources)
    {
        const auto p = impl_->places(*t);
        places.insert(places.end(), p.begin(), p.end());
    }
    const auto [placements, copies] = model::groupPlacements(places, asked);
    auto* um = &impl_->edit->getUndoManager();
    impl_->beginEdit("Duplicate clips");
    std::map<std::uint64_t, std::uint64_t> takeClips; // a take clip -> its copy's (per stack copy)
    std::vector<std::tuple<std::uint64_t, std::uint64_t, double, std::map<std::uint64_t, std::uint64_t>>> copied;
    for (const auto& copy : copies)
        if (auto [track, stack] = impl_->findStack(copy.stack); track != nullptr)
            if (auto* target = impl_->track(copy.toTrack))
            {
                std::map<std::uint64_t, std::uint64_t> map;
                impl_->copyStack(stack, *target, copy.delta, map, um);
                copied.emplace_back(copy.stack, copy.toTrack, copy.delta, std::move(map));
            }
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
            // The copy plays from the copied takes.
            const auto from = static_cast<std::uint64_t>(static_cast<juce::int64>(copy->state[Impl::fromTakeId]));
            if (from != 0)
            {
                const double delta = p.start - source->getPosition().getStart().inSeconds();
                std::uint64_t to = 0;
                for (const auto& [stack, track, d, map] : copied)
                    if (track == p.track && std::abs(d - delta) < 1e-9)
                        if (const auto it = map.find(from); it != map.end())
                            to = it->second;
                if (to != 0)
                    copy->state.setProperty(Impl::fromTakeId, static_cast<juce::int64>(to), um);
                else
                    copy->state.removeProperty(Impl::fromTakeId, um);
            }
            created.push_back(copy->itemID.getRawID());
        }
    }
    const std::set<std::uint64_t> placed(created.begin(), created.end());
    for (const auto& p : placements)
        if (auto* target = impl_->track(p.track))
            impl_->clearUnder(*target, p.start, p.start + p.length, placed);
    return created;
}

void Session::deleteClips(const std::vector<std::uint64_t>& asked)
{
    if (asked.empty())
        return;
    // A clip goes with all of its parts and its takes (ADR 0032; undo brings them back).
    std::vector<model::ClipPlace> places;
    std::set<te::AudioTrack*> tracks;
    for (auto id : asked)
        if (auto* c = impl_->clip(id))
            if (auto* t = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                tracks.insert(t);
    for (auto* t : tracks)
    {
        const auto p = impl_->places(*t);
        places.insert(places.end(), p.begin(), p.end());
    }
    impl_->beginEdit("Delete clips");
    for (auto id : model::withParts(places, asked))
        if (auto* c = impl_->clip(id))
            c->removeFromParent();
    for (auto* t : tracks)
        impl_->settle(*t, &impl_->edit->getUndoManager());
}

std::vector<std::uint64_t> Session::splitClips(const std::vector<std::uint64_t>& ids, double seconds)
{
    std::vector<std::uint64_t> created;
    const auto at = te::TimePosition::fromSeconds(seconds);
    // A clip's takes split with it (ADR 0032): the right halves become the right clip's takes.
    std::set<te::AudioTrack*> tracks;
    for (auto id : ids)
        if (auto* c = impl_->clip(id))
            if (auto* t = dynamic_cast<te::AudioTrack*>(c->getClipTrack()))
                tracks.insert(t);
    auto* um = &impl_->edit->getUndoManager();
    bool begun = false;
    for (auto* track : tracks)
    {
        const auto plan = model::splitPlan(impl_->places(*track), impl_->stackRanges(*track), ids, seconds);
        if (plan.clips.empty() && plan.stacks.empty())
            continue;
        if (!std::exchange(begun, true))
            impl_->beginEdit("Split clips");
        for (auto id : plan.clips)
            if (auto* c = impl_->clip(id))
                if (auto* right = track->splitClip(*c, at))
                    created.push_back(right->itemID.getRawID());
        for (auto id : plan.stacks)
            if (auto stack = impl_->stackTree(*track, id); stack.isValid())
                impl_->splitStack(*track, stack, seconds, um);
        impl_->settle(*track, um);
    }
    return created;
}

std::vector<AvailableDevice> Session::availableDevices() const
{
    std::vector<AvailableDevice> out;
    for (const auto* f : fd::allDevices())
    {
        const bool instrument = f->info().kind == DeviceKind::Instrument;
        const bool midiEffect = f->info().kind == DeviceKind::MidiEffect;
        out.push_back({.key = "fd:" + std::string(f->info().id),
                       .name = std::string(f->info().name),
                       .category = instrument   ? "Instruments"
                                   : midiEffect ? "MIDI effects"
                                                : "Audio effects",
                       .instrument = instrument,
                       .midiEffect = midiEffect});
    }
    // The Drum Kit, after our instruments (drum-kit.md, ADR 0064): a rack underneath, the DAW's.
    out.push_back({.key = "fd:drum-kit", .name = "Drum Kit", .category = "Instruments", .instrument = true});
    for (const auto kind : model::allModulators) // modulators.md §14: after the audio effects
    {
        const auto& info = model::modulatorInfo(kind);
        out.push_back({.key = "fd:" + std::string(info.id),
                       .name = std::string(info.name),
                       .category = "Modulators",
                       .modulator = true});
    }
    for (const auto& d : impl_->engine.getPluginManager().knownPluginList.getTypes())
        out.push_back({"ext:" + d.createIdentifierString().toStdString(), d.name.toStdString(),
                       d.manufacturerName.isNotEmpty() ? d.manufacturerName.toStdString() : "Plugins", false, d.isInstrument});
    return out;
}

const DeviceInfo* Session::nativeInfo(std::string_view key)
{
    if (!key.starts_with("fd:"))
        return nullptr;
    const auto id = key.substr(3);
    if (const auto kind = model::modulatorKind(id))
        return &model::modulatorInfo(*kind);
    if (id == drumKit().id)
        return &drumKit();
    const auto* factory = fd::findDevice(id);
    return factory != nullptr ? &factory->info() : nullptr;
}

std::uint64_t Session::addDevice(std::uint64_t trackId, const std::string& key, int index)
{
    if (key.starts_with("fd:"))
        if (const auto kind = model::modulatorKind(std::string_view(key).substr(3)))
            return addModulator(trackId, *kind, index);
    if (key == "fd:drum-kit")
        return addDrumKit(trackId, index); // drum-kit.md §4
    auto* list = impl_->plugins(trackId); // any channel: tracks, returns, the master
    if (list == nullptr)
        return 0;
    impl_->beginEdit("Add module");
    // Along a track's chain, its modulators count among its places (modulators.md §6).
    auto* track = impl_->chainTrack(trackId); // the master's too
    const auto items = track != nullptr ? Impl::chainItems(*track) : std::vector<Impl::ChainItem>{};
    const bool modulated =
        std::any_of(items.begin(), items.end(), [](const auto& item) { return item.modulator != nullptr; });
    auto* plugin = impl_->insertDevice(*list, key, modulated ? -1 : index);
    if (modulated && plugin != nullptr)
    {
        auto order = Impl::chainItems(*track);
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [plugin](const auto& item) { return item.plugin == plugin; }),
                    order.end());
        if (Impl::isNoteEffect(*plugin))
            index = model::placeInChain(Impl::roles(order), index, model::ChainRole::MidiEffect);
        const auto at = index < 0 ? order.size() : std::min(order.size(), static_cast<size_t>(index));
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), Impl::ChainItem{plugin, nullptr});
        impl_->arrangeChain(*track, order);
    }
    // A new module starts from your default, if you set one (ADR 0044), in the same step.
    if (auto* native = dynamic_cast<DevicePlugin*>(plugin))
        if (const auto start = PresetController::startingPoint(native->device().info(), presetLibrary()))
        {
            auto state = start->first;
            if (state.stateVersion < native->device().info().stateVersion)
                native->device().migrateState(state);
            impl_->applyModuleState(*native, state, start->second);
        }
    return plugin != nullptr ? plugin->itemID.getRawID() : 0;
}

void Session::removeDevice(std::uint64_t deviceId)
{
    if (auto* m = impl_->modulator(deviceId))
    {
        // A modulator: its routes and its parameters' lanes go with it.
        impl_->beginEdit("Remove " + m->getName());
        for (const auto& r : impl_->routesOf(*m))
            r.param.param->removeModifier(*r.assignment);
        impl_->dropLanesOf(deviceId);
        auto* rack = impl_->rackOf(*m);
        te::Modifier::Ptr keep(m);
        m->remove();
        if (rack != nullptr) // its rack's chain no longer taps into it
            Impl::wireRack(*rack, &impl_->edit->getUndoManager());
        impl_->refreshModulatorRoutes();
        return;
    }
    // One step, whatever goes with it.
    EditGroup group(*this, "Remove module");
    if (auto* p = impl_->plugin(deviceId); p != nullptr && !p->isInRack())
        if (auto* track = impl_->chainTrackOf(*p))
        {
            // The modulators before it stay where they are: placed before what comes after it.
            auto order = Impl::chainItems(*track);
            const auto before = order.size();
            order.erase(std::remove_if(order.begin(), order.end(),
                                       [p](const auto& item) { return item.plugin == p; }),
                        order.end());
            if (order.size() != before && std::any_of(order.begin(), order.end(), [](const auto& item)
                                                      { return item.modulator != nullptr; }))
                impl_->arrangeChain(*track, order);
        }
    if (auto* rack = impl_->rackInstance(deviceId))
    {
        impl_->beginEdit("Remove module");
        impl_->removeRack(*rack); // its type, its modules and their lanes with it
        return;
    }
    if (auto* p = impl_->plugin(deviceId))
    {
        auto* holder = impl_->rackHolding(*p);
        const te::RackType::Ptr rack = holder != nullptr ? holder->type : nullptr;
        impl_->beginEdit("Remove module");
        // Its parameters leave their lanes in the same step (ADR 0029).
        const auto prefix = "m" + std::to_string(deviceId) + "/";
        for (auto lane : impl_->lanesTree())
            for (int i = lane.getNumChildren(); --i >= 0;)
                if (lane.getChild(i).hasType(Impl::assignId) &&
                    lane.getChild(i)[Impl::keyId].toString().startsWith(juce::String(prefix)))
                    lane.removeChild(i, &impl_->edit->getUndoManager());
        // In a rack's chain with modulators: they stay where they sit (§14).
        std::optional<std::pair<int, std::vector<Impl::ChainItem>>> order;
        if (rack != nullptr)
        {
            const auto chains = Impl::rackChains(*rack);
            for (size_t c = 0; c < chains.size(); ++c)
                if (!chains[c].modulators.empty() &&
                    std::find(chains[c].modules.begin(), chains[c].modules.end(), p) !=
                        chains[c].modules.end())
                {
                    std::vector<Impl::ChainItem> items;
                    for (const auto& item : chains[c].items)
                        if (item.plugin != p)
                            items.push_back(item);
                    order.emplace(static_cast<int>(c), std::move(items));
                }
        }
        p->removeFromParent();
        if (order)
            impl_->arrangeRackChain(*rack, order->first, order->second);
        else if (rack != nullptr)
            Impl::wireRack(*rack, &impl_->edit->getUndoManager()); // its chain closes up
        if (rack != nullptr && Impl::isKit(*rack))
            impl_->prunePads(*rack); // a pad with nothing left on it is empty (drum-kit.md §4)
    }
}

void Session::moveDevice(std::uint64_t deviceId, int index)
{
    // Along a track's chain with modulators (modulators.md §6): every item where it was but this.
    // In a rack's chain, along it.
    if (auto* m = impl_->modulator(deviceId); m != nullptr && impl_->rackOf(*m) != nullptr)
    {
        if (auto place = placeOf(deviceId))
        {
            place->index = index >= place->index ? index + 1 : index;
            moveDevice(deviceId, *place);
        }
        return;
    }
    {
        auto* m = impl_->modulator(deviceId);
        auto* moving = impl_->plugin(deviceId);
        te::Track* track = m != nullptr                               ? impl_->trackOf(*m)
                           : moving != nullptr && !moving->isInRack() ? impl_->chainTrackOf(*moving)
                                                                      : nullptr;
        if (track != nullptr)
        {
            auto order = Impl::chainItems(*track);
            if (m != nullptr || std::any_of(order.begin(), order.end(),
                                            [](const auto& item) { return item.modulator != nullptr; }))
            {
                const auto from = std::find_if(order.begin(), order.end(), [deviceId](const auto& item)
                                               { return item.id() == deviceId; });
                if (from == order.end())
                    return;
                const auto item = *from;
                const auto was = static_cast<int>(from - order.begin());
                order.erase(from);
                index = std::clamp(index, 0, static_cast<int>(order.size()));
                if (item.plugin != nullptr && Impl::isNoteEffect(*item.plugin)) // before the instrument
                    index = model::placeInChain(Impl::roles(order), index, model::ChainRole::MidiEffect);
                if (index == was)
                    return;
                order.insert(order.begin() + index, item);
                impl_->beginEdit(m != nullptr ? "Move " + m->getName() : juce::String("Move module"));
                impl_->arrangeChain(*track, order);
                return;
            }
        }
    }
    auto* p = impl_->plugin(deviceId);
    if (p != nullptr && p->isInRack()) // along its rack's chain: to where it ends up
    {
        if (auto place = placeOf(deviceId))
        {
            place->index = index >= place->index ? index + 1 : index;
            moveDevice(deviceId, *place);
        }
        return;
    }
    auto* list = p != nullptr ? p->getOwnerList() : nullptr;
    if (list == nullptr)
        return;
    const auto devices = Impl::chain(*list);
    index = std::clamp(index, 0, static_cast<int>(devices.size()) - 1);
    if (Impl::isNoteEffect(*p)) // before the instrument (midi-effects.md §11)
        index = model::placeInChain(Impl::roles(devices, p), index, model::ChainRole::MidiEffect);
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
    auto* m = p == nullptr ? impl_->modulator(deviceId) : nullptr;
    te::Track* owner = p != nullptr ? p->getOwnerTrack() : m != nullptr ? impl_->trackOf(*m) : nullptr;
    if (owner == nullptr || (p != nullptr && !takesSidechain(*p)) ||
        (m != nullptr && (m->kind() != model::ModulatorKind::Follower || impl_->rackOf(*m) != nullptr)))
        return {}; // a Follower in a rack listens where it sits
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
        // A Follower's listening: its track hears the one it listens to (modulators.md §12.4).
        if (auto* list = t.getModifierList())
            for (auto& q : list->getModifiers())
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
    // A Follower's Listen (modulators.md §12.4): one undo step; the graph is rebuilt.
    if (auto* m = impl_->modulator(deviceId))
    {
        if (m->kind() != model::ModulatorKind::Follower || (trackId && impl_->rackOf(*m) != nullptr))
            return false;
        te::EditItemID source;
        if (trackId)
        {
            const auto allowed = sidechainSources(deviceId);
            if (std::find(allowed.begin(), allowed.end(), *trackId) == allowed.end())
                return false;
            source = impl_->track(*trackId)->itemID;
        }
        if (m->getSidechainSourceID() == source)
            return true;
        impl_->beginEdit("Set Listen");
        if (source.isValid())
            m->state.setProperty(Modulator::listenId, source.toVar(), &impl_->edit->getUndoManager());
        else
            m->state.removeProperty(Modulator::listenId, &impl_->edit->getUndoManager());
        return true;
    }
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
constexpr int kMaxReturns = Session::Impl::maxReturns;
} // namespace

std::uint64_t Session::addReturnTrack(const std::string& name)
{
    auto& edit = *impl_->edit;
    const auto existing = impl_->returns();
    const int bus = impl_->freeBus();
    if (bus >= kMaxReturns)
        return 0;

    impl_->beginEdit("Add return track");
    auto* um = &edit.getUndoManager();
    auto track = edit.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(edit), nullptr);
    if (track == nullptr)
        return 0;
    const auto title = juce::String::fromUTF8(name.empty() ? model::nextReturnName(impl_->trackNames()).c_str() : name.c_str());
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
    if (auto* m = impl_->modulator(deviceId))
    {
        // Off, it moves nothing (tracktion leaves a disabled modifier's routes out).
        if (m->enabledParam != nullptr && (m->enabledParam->getCurrentValue() >= 0.5f) != enabled)
        {
            impl_->beginEdit("Modulator on/off");
            m->enabledParam->setParameter(enabled ? 1.0f : 0.0f, juce::sendNotificationSync);
        }
        return;
    }
    if (auto* p = impl_->plugin(deviceId); p != nullptr && p->isEnabled() != enabled)
    {
        impl_->beginEdit("Module on/off");
        p->setEnabled(enabled);
    }
}

std::string Session::moduleType(std::uint64_t deviceId) const
{
    if (auto* m = impl_->modulator(deviceId))
        return std::string(m->info().name);
    auto* p = impl_->plugin(deviceId);
    if (p == nullptr)
        return {};
    if (auto* native = dynamic_cast<DevicePlugin*>(p))
        return std::string(native->device().info().name);
    if (auto* external = dynamic_cast<te::ExternalPlugin*>(p))
    {
        const auto& d = external->desc;
        auto text = d.name;
        for (const auto& part : {d.manufacturerName, d.pluginFormatName})
            if (part.isNotEmpty())
                text << juce::String::fromUTF8(" \xc2\xb7 ") << part;
        return text.toStdString();
    }
    if (Impl::isKit(p)) // drum-kit.md §8: what it holds
    {
        size_t pads = 0;
        if (auto* type = Impl::rackTypeOf(p))
            for (const auto& c : Impl::rackChains(*type))
                pads += c.inlet != nullptr ? 1 : 0;
        return "Drum Kit \xc2\xb7 " + std::to_string(pads) + (pads == 1 ? " pad" : " pads");
    }
    if (dynamic_cast<te::RackInstance*>(p) != nullptr)
    {
        // Its chains, as the snapshot has them.
        size_t chains = 0;
        const auto s = snapshot();
        const auto look = [&](const model::TrackInfo& channel)
        {
            if (const auto facts = model::moduleFacts(channel, {}, deviceId))
                chains = static_cast<size_t>(facts->chains);
        };
        for (const auto& t : s->tracks)
            look(t);
        for (const auto& r : s->returns)
            look(r);
        look(s->master);
        return "Rack \xc2\xb7 " + std::to_string(chains) + (chains == 1 ? " chain" : " chains");
    }
    return p->getName().toStdString();
}

double Session::moduleLatencyMs(std::uint64_t deviceId) const
{
    auto* p = impl_->plugin(deviceId);
    if (p == nullptr)
        return 0.0;
    if (auto* native = dynamic_cast<DevicePlugin*>(p))
    {
        // Ours: from its settings, at the rate it runs at (or the audio device's, before it has run).
        const double rate = native->sampleRate() > 0.0 ? native->sampleRate()
                                                       : impl_->engine.getDeviceManager().getSampleRate();
        return rate > 0.0 ? 1000.0 * native->device().latencySamples() / rate : 0.0;
    }
    return 1000.0 * p->getLatencySeconds();
}

namespace
{
const juce::Identifier kDelayToldId{"fdDelayTold"}; // the project was told a module delays its track
} // namespace

std::optional<Session::DelayNotice> Session::untoldDelay(double minMs) const
{
    if (static_cast<bool>(impl_->edit->state.getProperty(kDelayToldId, false)))
        return std::nullopt;
    const auto first = [&](te::PluginList& list, const juce::String& track) -> std::optional<DelayNotice>
    {
        for (auto* p : Impl::chain(list))
        {
            std::vector<te::Plugin*> modules{p};
            if (auto* rack = Impl::rackTypeOf(p)) // the modules in a rack too
                modules = Impl::rackModules(*rack);
            for (auto* m : modules)
                if (const double ms = moduleLatencyMs(m->itemID.getRawID()); ms >= minMs)
                    return DelayNotice{m->getName().toStdString(), track.toStdString(), ms};
        }
        return std::nullopt;
    };
    for (auto* t : te::getAudioTracks(*impl_->edit))
        if (auto notice = first(t->pluginList, t->getName()))
            return notice;
    return first(impl_->edit->getMasterPluginList(), "Master");
}

void Session::markDelayTold()
{
    impl_->edit->state.setProperty(kDelayToldId, true, nullptr);
}

std::string Session::deviceViewState(std::uint64_t deviceId) const
{
    if (auto* p = impl_->plugin(deviceId))
        return p->state.getProperty(Impl::panelViewId).toString().toStdString();
    if (auto* m = impl_->modulator(deviceId)) // a modulator's panel, as a module's
        return m->state.getProperty(Impl::panelViewId).toString().toStdString();
    return {};
}

void Session::setDeviceViewState(std::uint64_t deviceId, const std::string& json)
{
    auto* p = impl_->plugin(deviceId);
    auto* m = p == nullptr ? impl_->modulator(deviceId) : nullptr;
    if (p == nullptr && m == nullptr)
        return;
    auto state = p != nullptr ? p->state : m->state;
    if (json.empty())
        state.removeProperty(Impl::panelViewId, nullptr);
    else
        state.setProperty(Impl::panelViewId, juce::String(json), nullptr); // no undo manager
    impl_->listeners.call([deviceId](Listener& l) { l.deviceViewChanged(deviceId); });
}

model::GridSettings Session::grid(GridEditor editor) const
{
    return impl_->grids[static_cast<size_t>(editor)];
}

void Session::setGrid(GridEditor editor, const model::GridSettings& grid)
{
    auto& g = impl_->grids[static_cast<size_t>(editor)];
    if (g == grid)
        return;
    g = grid;
    impl_->listeners.call([](Listener& l) { l.gridChanged(); });
}

std::string Session::gridStep(GridEditor editor) const
{
    return impl_->gridSteps[static_cast<size_t>(editor)];
}

void Session::setGridStep(GridEditor editor, std::string step)
{
    impl_->gridSteps[static_cast<size_t>(editor)] = std::move(step);
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

void Session::scanPlugins(std::function<void(const juce::String&)> onProgress,
                          std::function<void(ScanResult)> onDone)
{
    if (isScanning())
        return;
    impl_->scanner = std::make_unique<PluginScan>(
        impl_->engine, PluginScan::Options{.folders = impl_->pluginFolders}, std::move(onProgress),
        [done = std::move(onDone)](PluginScan::Result r)
        {
            if (done)
                done({r.found, r.skipped});
        });
}

bool Session::isScanning() const
{
    return impl_->scanner != nullptr && !impl_->scanner->isDone();
}

juce::String Session::scanningNow() const
{
    return isScanning() ? impl_->scanner->current() : juce::String();
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
    impl_->stopAudition();
    impl_->detach();
    impl_->file = std::move(file);
    impl_->grids = {model::GridSettings::arrangement(), model::GridSettings::pianoRoll()};
    impl_->gridSteps = {};
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
    impl_->stopAudition(); // never saved
    auto& edit = *impl_->edit;
    (void)file.getParentDirectory().createDirectory();
    impl_->moveToProject(file); // recordings into its folder, paths relative to it (ADR 0030)
    if (viewStateProvider)
        edit.state.setProperty(kViewStateId, juce::String(viewStateProvider()), nullptr);
    edit.flushState(); // device states (and our devices' extra state) into the tree
    if (!te::EditFileOperations(edit).writeToFile(file, false))
        return false;
    const bool moved = impl_->file != file;
    impl_->file = file;
    edit.resetChangedStatus();
    impl_->dirty = false;
    if (moved && impl_->browserStarted)
        impl_->updateBrowserPlaces(); // the project's folder is a place
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

namespace
{
/// Undo and redo restore our modules' and modulators' stored values; their parameters follow
/// (DevicePlugin, Modulator).
void followStoredValues(te::Edit& edit)
{
    for (auto* p : te::getAllPlugins(edit, false))
        if (auto* native = dynamic_cast<DevicePlugin*>(p))
            native->followStoredValues();
    for (auto* t : te::getAudioTracks(edit))
        if (auto* list = t->getModifierList())
            for (auto* m : list->getModifiers())
                if (auto* mod = dynamic_cast<Modulator*>(m))
                    mod->followStoredValues();
}
} // namespace

void Session::undo()
{
    impl_->stopAudition();
    impl_->edit->getUndoManager().undo();
    followStoredValues(*impl_->edit);
}

void Session::redo()
{
    impl_->stopAudition();
    impl_->edit->getUndoManager().redo();
    followStoredValues(*impl_->edit);
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
    impl.keyStepOpen = false;
    impl.endMidiMovesForEdit();
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

std::shared_ptr<Session::RenderJob> Session::Impl::renderCopy(const std::function<bool(te::Edit&, te::Renderer::Parameters&)>& setup,
                                                              std::function<void(const std::string&)> done)
{
    stopAudition(); // a render hears the project as it is
    // A copy of the project: our devices' state is all in its tree; third-party plugins get
    // their live state copied over, so nothing in the open project is touched.
    te::Edit::Options options{engine, edit->state.createCopy(), te::ProjectItemID::createNewID(te::ProjectID{})};
    options.role = te::Edit::forRendering;
    options.numUndoLevelsToStore = 0;
    options.numAudioTracks = 0;
    if (file)
        options.editFileRetriever = [f = *file] { return f; };
    auto job = std::make_shared<Render>();
    job->copy = te::Edit::createEdit(std::move(options));
    if (job->copy == nullptr)
        return nullptr;
    for (auto* p : te::getAllPlugins(*job->copy, false))
        if (auto* copied = dynamic_cast<te::ExternalPlugin*>(p))
            if (auto* live = dynamic_cast<te::ExternalPlugin*>(plugin(p->itemID.getRawID())))
                if (auto* from = live->getAudioPluginInstance())
                    if (auto* to = copied->getAudioPluginInstance())
                    {
                        juce::MemoryBlock state;
                        from->getStateInformation(state);
                        to->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                    }

    te::Renderer::Parameters params(*job->copy);
    params.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 32;
    params.blockSizeForAudio = 512;
    params.canRenderInMono = false;
    params.checkNodesForAudio = false; // silence is a result too
    params.usePlugins = true;
    if (!setup(*job->copy, params))
        return nullptr;

    job->done = std::move(done);
    std::weak_ptr<Render> weak = job;
    Impl* self = this;
    job->handle = te::EditRenderer::render(params, [weak, self](auto result) {
        const std::string error = result ? std::string() : std::string(result.error());
        juce::MessageManager::callAsync([weak, self, error] {
            auto finished = weak.lock();
            if (finished == nullptr)
                return; // the session has gone
            auto callback = std::move(finished->done);
            std::erase(self->renders, finished);
            if (callback)
                callback(error);
        }); // `finished`, the last reference, takes the Edit copy with it here
    });
    renders.push_back(job);
    return job->state;
}

void Session::Impl::renderOneTrack(te::Edit& copy, te::Renderer::Parameters& params, std::uint64_t trackId)
{
    // One track, with the tracks feeding its sidechains (ADR 0025): they play into it, muted in
    // the copy so they aren't heard themselves.
    const auto tracks = te::getAllTracks(copy);
    const auto id = te::EditItemID::fromRawID(trackId);
    const auto sources = te::findStemSourceTracks(copy, {id});
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

std::shared_ptr<Session::RenderJob> Session::render(const RenderRequest& request, const juce::File& destination,
                                                    std::function<void(const std::string& error)> done)
{
    auto& impl = *impl_;
    if (!(request.end > request.start) || (request.track && impl.track(*request.track) == nullptr))
        return nullptr;
    return impl.renderCopy(
        [&](te::Edit& copy, te::Renderer::Parameters& params) {
            params.destFile = destination;
            params.sampleRateForAudio = request.sampleRate;
            params.time = {te::TimePosition::fromSeconds(request.start), te::TimePosition::fromSeconds(request.end)};
            params.useMasterPlugins = !request.track.has_value();
            if (request.track)
                impl.renderOneTrack(copy, params, *request.track);
            else
                params.tracksToDo = te::toBitSet(te::getAllTracks(copy));
            return true;
        },
        std::move(done));
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
