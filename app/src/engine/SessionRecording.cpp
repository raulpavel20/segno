// Recording (M2 step C5, ADRs 0030 and 0031, docs/design/recording.md): inputs per track,
// arming, monitoring, Record with count-in (recording inside the loop brace while looping), and
// project folders for the recordings.

#include "engine/SessionImpl.h"

#include "engine/EngineBehaviour.h"

namespace fd::engine
{
namespace
{
std::string monitorName(model::Monitor m)
{
    return m == model::Monitor::In ? "in" : m == model::Monitor::Off ? "off" : "auto";
}

bool isMidi(const te::InputDevice& d)
{
    return d.getDeviceType() == te::InputDevice::physicalMidiDevice || d.getDeviceType() == te::InputDevice::virtualMidiDevice;
}

/// Whether a track's MIDI input choice takes notes from `device` ("all": every device, merged
/// through tracktion's "All MIDI Ins", and the computer keyboard).
bool midiTakes(const std::string& choice, const te::InputDevice& device)
{
    const auto name = device.getName().toStdString();
    if (choice.empty())
        return false;
    if (choice == "all")
        return device.getDeviceType() == te::InputDevice::virtualMidiDevice;
    if (choice == "keyboard")
        return name == Session::computerKeyboardName;
    return device.getDeviceType() == te::InputDevice::physicalMidiDevice && name == choice;
}
} // namespace

// ---- inputs --------------------------------------------------------------------------------

model::InputInfo Session::Impl::inputOf(const te::AudioTrack& t) const
{
    model::InputInfo in;
    in.audio = t.state[audioInId].toString().toStdString();
    in.midi = t.state.getProperty(midiInId, "all").toString().toStdString();
    in.channel = std::clamp(static_cast<int>(t.state[midiChannelId]), 0, 16);
    const auto monitor = t.state[monitorId].toString();
    in.monitor = monitor == "in" ? model::Monitor::In : monitor == "off" ? model::Monitor::Off : model::Monitor::Auto;
    const auto records = t.state[recordsId].toString();
    if (records == "midi")
        in.records = model::RecordKind::Midi;
    else if (records == "audio")
        in.records = model::RecordKind::Audio;
    in.armed = t.state[armedId];
    in.softArmed = !in.armed && midiTarget == t.itemID.getRawID() && !anyArmed();
    return in;
}

bool Session::Impl::anyArmed() const
{
    for (auto* t : tracks())
        if (t->state[armedId])
            return true;
    return false;
}

void Session::Impl::applyInputs()
{
    if (edit == nullptr || edit->getTransport().isRecording())
        return;
    bool rebuild = false;
    const auto all = tracks();
    for (auto* instance : edit->getAllInputDevices())
    {
        const auto& device = instance->getInputDevice();
        const bool wave = device.getDeviceType() == te::InputDevice::waveDevice;
        if (!wave && !isMidi(device))
            continue; // a track's output as an input (later, §15)
        std::vector<te::EditItemID> wanted;
        for (auto* t : all)
        {
            const auto in = inputOf(*t);
            if (wave ? in.audio != device.getName().toStdString() : !midiTakes(in.midi, device))
                continue;
            const bool hasInstrument = std::any_of(t->pluginList.begin(), t->pluginList.end(),
                                                   [](auto* p) { return p->isSynth(); });
            const auto kind = model::recordKind(in, hasInstrument);
            const bool records = in.isArmed() && (wave ? kind == model::RecordKind::Audio : kind == model::RecordKind::Midi);
            const bool hears = model::hearsInput(in) && (wave || hasInstrument);
            if (!records && !hears)
                continue;
            wanted.push_back(t->itemID);
            if (!instance->getTargets().contains(t->itemID))
            {
                (void)instance->setTarget(t->itemID, false, nullptr);
                rebuild = true;
            }
            for (auto* dest : instance->destinations)
                if (dest->targetID == t->itemID)
                {
                    const juce::String monitor = hears ? "on" : "off";
                    const int channel = wave ? 0 : in.channel;
                    if (dest->state["fdMonitor"].toString() != monitor || static_cast<int>(dest->state["fdChannel"]) != channel)
                    {
                        dest->state.setProperty("fdMonitor", monitor, nullptr);
                        dest->state.setProperty("fdChannel", channel, nullptr);
                        rebuild = true;
                    }
                }
            if (instance->isRecordingEnabled(t->itemID) != records)
                instance->setRecordingEnabled(t->itemID, records);
        }
        for (auto id : instance->getTargets())
            if (std::find(wanted.begin(), wanted.end(), id) == wanted.end())
            {
                (void)instance->removeTarget(id, nullptr);
                rebuild = true;
            }
    }
    if (rebuild)
        edit->restartPlayback(); // monitoring and channels are read when the graph is built
}

// ---- project folders (ADR 0030) ------------------------------------------------------------

juce::File Session::Impl::projectFolder()
{
    if (file)
        return file->getParentDirectory();
    if (!unsavedFolder)
    {
        auto folder = unsavedRoot.getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"));
        for (int n = 2; folder.exists(); ++n)
            folder = unsavedRoot.getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + " " + juce::String(n));
        unsavedFolder = folder;
    }
    return *unsavedFolder;
}

juce::File Session::Impl::recordingFile(te::Track& t, const juce::String& extension)
{
    const auto folder = projectFolder().getChildFile("Recordings");
    (void)folder.createDirectory();
    const auto name = juce::File::createLegalFileName(t.getName());
    const auto ext = extension.startsWithChar('.') ? extension : "." + extension;
    for (int n = 1;; ++n)
        if (auto f = folder.getChildFile(name + " " + juce::String(n) + ext); !f.exists())
        {
            liveFiles[t.itemID.getRawID()] = f; // drawn while it records
            return f;
        }
}

void Session::Impl::moveToProject(const juce::File& to)
{
    // Every file a source refers to, resolved where the project was.
    std::vector<std::pair<juce::ValueTree, juce::File>> sources;
    std::function<void(juce::ValueTree)> collect = [&](juce::ValueTree v) {
        if (v.hasProperty(te::IDs::source))
            if (auto f = te::SourceFileReference::findFileFromString(*edit, v[te::IDs::source].toString()); f != juce::File())
                sources.emplace_back(v, f);
        for (auto child : v)
            collect(child);
    };
    collect(edit->state);

    // Recordings made before the first save move into the project's folder.
    std::map<juce::String, juce::File> moved;
    if (!file && unsavedFolder)
    {
        const auto from = unsavedFolder->getChildFile("Recordings");
        const auto into = to.getParentDirectory().getChildFile("Recordings");
        if (from.isDirectory() && from != into)
        {
            (void)into.createDirectory();
            for (const auto& f : from.findChildFiles(juce::File::findFiles, false))
            {
                auto dest = into.getChildFile(f.getFileName());
                for (int n = 2; dest.exists(); ++n)
                    dest = into.getChildFile(f.getFileNameWithoutExtension() + " " + juce::String(n) + f.getFileExtension());
                if (f.moveFileTo(dest))
                    moved[f.getFullPathName()] = dest;
            }
        }
        unsavedFolder.reset();
    }

    // Files in the project's folder are referred to relative to it, so the folder moves as a
    // whole; files elsewhere keep their full path. (tracktion resolves a relative path from the
    // project file's folder, but would write it from the file itself, one level off.)
    edit->editFileRetriever = [to] { return to; };
    const auto folder = to.getParentDirectory();
    for (auto& [tree, f] : sources)
    {
        const auto it = moved.find(f.getFullPathName());
        const auto now = it != moved.end() ? it->second : f;
        const auto path = now.isAChildOf(folder) ? now.getRelativePathFrom(folder) : now.getFullPathName();
        if (tree[te::IDs::source].toString() != path)
            tree.setProperty(te::IDs::source, path, nullptr);
    }
}

// ---- recording passes ----------------------------------------------------------------------

void Session::Impl::beginPass(const juce::String& name)
{
    // A pass is one undo step, however many tracks and controls it records (§5, automation §8);
    // a held control's gesture step becomes the pass, so its first move is part of it.
    if (recordingPass != nullptr)
        return;
    auto& um = edit->getUndoManager();
    if (groupDepth > 0)
        um.setCurrentTransactionName(name);
    else
        um.beginNewTransaction(name);
    recordingPass = std::make_unique<te::Edit::UndoTransactionInhibitor>(*edit);
}

void Session::Impl::recordingAboutToStart(te::InputDeviceInstance&, te::EditItemID target)
{
    // From the playhead when Record was pressed (or now, punching in), or while looping the brace's
    // start.
    auto start = recordFrom.value_or(edit->getTransport().getPosition().inSeconds());
    if (edit->recordingPunchInOut)
        start = std::max(start, edit->getTransport().getLoopRange().getStart().inSeconds());
    liveStarts[target.getRawID()] = start;
}

void Session::Impl::recordingFinished(te::InputDeviceInstance&, te::EditItemID target,
                                      const juce::ReferenceCountedArray<te::Clip>& clips)
{
    // Clips are named after their track (§5); a pass becomes take lanes, the main lane playing the
    // newest complete lap (ADR 0031). An overdub merges into the clip that was there instead.
    auto* t = track(target.getRawID());
    for (auto* c : clips)
        if (c != nullptr && t != nullptr)
            c->setName(t->getName());
    const bool overdub = edit->state[overdubId] && std::any_of(clips.begin(), clips.end(), [](auto* c) {
                             return dynamic_cast<te::MidiClip*>(c) != nullptr;
                         });
    if (t != nullptr && !overdub)
    {
        const auto placed = importTakes(*t, clips);
        recordedClips.insert(recordedClips.end(), placed.begin(), placed.end());
    }
    else
        for (auto* c : clips)
            if (c != nullptr)
                recordedClips.push_back(c->itemID.getRawID());
    liveStarts.erase(target.getRawID());
    liveFiles.erase(target.getRawID());
}

// ---- Session -------------------------------------------------------------------------------

std::vector<Session::InputChoice> Session::audioInputs() const
{
    std::vector<InputChoice> out;
    for (auto* d : impl_->engine.getDeviceManager().getWaveInputDevices())
        if (d->isEnabled())
            out.push_back({d->getName().toStdString(), d->getName().toStdString()});
    return out;
}

std::vector<Session::InputChoice> Session::midiInputs() const
{
    std::vector<InputChoice> out{{"all", "All MIDI inputs"}, {"keyboard", computerKeyboardName}};
    for (auto& d : impl_->engine.getDeviceManager().getMidiInDevices())
        if (d->getDeviceType() == te::InputDevice::physicalMidiDevice)
            out.push_back({d->getName().toStdString(), d->getName().toStdString()});
    return out;
}

namespace
{
template <typename Value>
void setTrackProperty(Session::Impl& impl, std::uint64_t trackId, const juce::Identifier& id, const Value& value)
{
    auto* t = impl.track(trackId);
    if (t == nullptr || t->state[id] == juce::var(value))
        return;
    impl.beginEdit("Track input");
    t->state.setProperty(id, value, &impl.edit->getUndoManager());
    impl.applyInputs();
}
} // namespace

void Session::setAudioInput(std::uint64_t trackId, const std::string& input)
{
    setTrackProperty(*impl_, trackId, Impl::audioInId, juce::String(input));
}

void Session::setMidiInput(std::uint64_t trackId, const std::string& input)
{
    setTrackProperty(*impl_, trackId, Impl::midiInId, juce::String(input));
}

void Session::setMidiChannel(std::uint64_t trackId, int channel)
{
    setTrackProperty(*impl_, trackId, Impl::midiChannelId, std::clamp(channel, 0, 16));
}

void Session::setMonitor(std::uint64_t trackId, model::Monitor monitor)
{
    setTrackProperty(*impl_, trackId, Impl::monitorId, juce::String(monitorName(monitor)));
}

void Session::setRecordKind(std::uint64_t trackId, std::optional<model::RecordKind> kind)
{
    setTrackProperty(*impl_, trackId, Impl::recordsId,
                     juce::String(!kind ? "" : *kind == model::RecordKind::Audio ? "audio" : "midi"));
}

void Session::setArmed(std::uint64_t trackId, bool armed, bool exclusive)
{
    // A listening state, like solo: saved, never an undo step (§4).
    for (auto* t : impl_->tracks())
    {
        const bool want = t->itemID.getRawID() == trackId ? armed : (exclusive ? false : static_cast<bool>(t->state[Impl::armedId]));
        if (static_cast<bool>(t->state[Impl::armedId]) != want)
            t->state.setProperty(Impl::armedId, want, nullptr);
    }
    if (impl_->edit->getTransport().isRecording())
    {
        // Arming while recording punches that track in or out.
        for (auto* instance : impl_->edit->getAllInputDevices())
            for (auto* t : impl_->tracks())
                if (instance->getTargets().contains(t->itemID))
                    instance->setRecordingEnabled(t->itemID, impl_->inputOf(*t).isArmed());
        return;
    }
    impl_->applyInputs();
}

void Session::record()
{
    auto& transport = impl_->edit->getTransport();
    if (transport.isRecording())
    {
        transport.stopRecording(false); // punch out, keep playing
        return;
    }
    (void)impl_->projectFolder().getChildFile("Recordings").createDirectory();
    for (auto& d : impl_->engine.getDeviceManager().getMidiInDevices())
    {
        d->mergeRecordings = midiOverdub(); // Overdub merges laps; New takes makes a clip per lap
        d->replaceExistingClips = false;    // Segno replaces through take lanes (ADR 0031)
    }
    impl_->applyAutomationMode();
    impl_->applyInputs();
    impl_->beginPass("Record");
    // Looping is punching (recording.md §5): only the brace records. tracktion's loop recording
    // starts at the brace; from a playhead before it, playback rolls in from the playhead, a lead-in
    // that's heard but not recorded (the loop-record-lead-in patch).
    impl_->edit->recordingPunchInOut = false; // no separate punch (projects before C5b's review)
    if (transport.isPlaying())
        impl_->recordFrom.reset();
    else
        impl_->recordFrom = isLooping() ? loopRange().first : position();
    // Capture MIDI: the pass records what's played; what came before is used up.
    impl_->drainTap();
    impl_->consumedUntil = impl_->tap.now();
    if (!transport.isPlaying())
    {
        impl_->playStartedAt = impl_->consumedUntil;
        impl_->wasPlaying = true;
    }
    transport.record(false, true); // with nothing armed it still plays (and records automation)
}

std::optional<double> Session::recordingFrom() const
{
    return impl_->recordFrom;
}

std::vector<Session::LiveRecording> Session::liveRecordings() const
{
    std::vector<LiveRecording> out;
    if (!isRecording())
        return out;
    for (const auto& [track, start] : impl_->liveStarts)
        out.push_back({track, start, impl_->liveFiles.contains(track)});
    return out;
}

void Session::drawLiveWaveform(juce::Graphics& g, std::uint64_t track, juce::Rectangle<int> area, double seconds) const
{
    const auto it = impl_->liveFiles.find(track);
    if (it == impl_->liveFiles.end() || seconds <= 0.0)
        return;
    auto thumb = impl_->engine.getRecordingThumbnailManager().getThumbnailFor(it->second);
    if (thumb != nullptr && thumb->thumb != nullptr)
        thumb->thumb->drawChannels(g, area, 0.0, seconds, 1.0f);
}

void Session::recordUntil(double seconds)
{
    const double from = position();
    record();
    impl_->stopAt.seconds = std::max(0.0, seconds - from);
    impl_->stopAt.startedAt.reset();
    impl_->stopAt.startTimer(5);
}

void Session::Impl::StopAt::timerCallback()
{
    auto& transport = impl.edit->getTransport();
    if (!transport.isPlaying())
    {
        stopTimer();
        return;
    }
    auto* context = impl.edit->getCurrentPlaybackContext();
    const double unlooped = context != nullptr ? context->getUnloopedPosition().inSeconds() : transport.getPosition().inSeconds();
    if (!startedAt)
    {
        if (impl.recordFrom && transport.getPosition().inSeconds() < *impl.recordFrom - 1e-3)
            return; // still counting in
        startedAt = unlooped;
    }
    if (unlooped - *startedAt >= seconds)
    {
        stopTimer();
        transport.stop(false, false);
    }
}

bool Session::isRecording() const
{
    return impl_->edit->getTransport().isRecording();
}

void Session::setCountIn(model::CountIn countIn)
{
    using C = te::Edit::CountIn;
    switch (countIn)
    {
    case model::CountIn::Off: impl_->edit->setCountInMode(C::none); break;
    case model::CountIn::OneBeat: impl_->edit->setCountInMode(C::oneBeat); break;
    case model::CountIn::TwoBeats: impl_->edit->setCountInMode(C::twoBeat); break;
    case model::CountIn::OneBar: impl_->edit->setCountInMode(C::oneBar); break;
    case model::CountIn::TwoBars: impl_->edit->setCountInMode(C::twoBar); break;
    }
}

model::CountIn Session::countIn() const
{
    using C = te::Edit::CountIn;
    switch (impl_->edit->getCountInMode())
    {
    case C::none: return model::CountIn::Off;
    case C::oneBeat: return model::CountIn::OneBeat;
    case C::twoBeat: return model::CountIn::TwoBeats;
    case C::oneBar: return model::CountIn::OneBar;
    case C::twoBar: return model::CountIn::TwoBars;
    }
    return model::CountIn::Off;
}


void Session::setMidiOverdub(bool on)
{
    impl_->edit->state.setProperty(Impl::overdubId, on, nullptr);
}

bool Session::midiOverdub() const
{
    return impl_->edit->state[Impl::overdubId];
}

void Session::setRecordingOffsets(double audioMs, double midiMs)
{
    audioMs = std::clamp(audioMs, -500.0, 500.0);
    midiMs = std::clamp(midiMs, -500.0, 500.0);
    for (auto* d : impl_->engine.getDeviceManager().getWaveInputDevices())
        d->setRecordAdjustmentMs(audioMs);
    for (auto& d : impl_->engine.getDeviceManager().getMidiInDevices())
        d->setManualAdjustmentMs(midiMs);
}

std::pair<double, double> Session::recordingOffsets() const
{
    auto& dm = impl_->engine.getDeviceManager();
    const auto waves = dm.getWaveInputDevices();
    const auto midis = dm.getMidiInDevices();
    return {waves.empty() ? 0.0 : waves.front()->getRecordAdjustmentMs(),
            midis.empty() ? 0.0 : midis.front()->getManualAdjustmentMs()};
}

juce::File Session::recordingsFolder() const
{
    return impl_->projectFolder().getChildFile("Recordings");
}

void Session::setUnsavedRoot(const juce::File& folder)
{
    impl_->unsavedRoot = folder;
    impl_->unsavedFolder.reset();
}
} // namespace fd::engine
