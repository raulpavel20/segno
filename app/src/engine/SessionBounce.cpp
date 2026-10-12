// Bounce (M2 step C7, ADR 0035, docs/design/bounce.md): clips rendered offline on a copy of the
// project, run by run, then landing as one undo step: in place with the source kept as a take, or
// on a new track with the source muted.

#include "engine/SessionImpl.h"

#include "engine/DevicePlugin.h"
#include "model/TrackEdits.h"

#include <cmath>

namespace fd::engine
{
namespace
{
const juce::Identifier kColourId{"fdColour"};

te::TimePosition at(double seconds)
{
    return te::TimePosition::fromSeconds(seconds);
}

/// A bounce lands only on clips that are still what it rendered.
bool sameClip(const model::ClipInfo& a, const model::ClipInfo& b)
{
    return a.kind == b.kind && a.start == b.start && a.length == b.length && a.offset == b.offset && a.notes == b.notes &&
           a.sourcePath == b.sourcePath && a.muted == b.muted && a.fromTake == b.fromTake && a.loop == b.loop;
}

/// The modules a run renders (ADR 0019): in place, a MIDI clip's chain up to its instrument and
/// none for audio; to a new track, the whole chain.
std::vector<model::DeviceSlot> renderedModules(const model::TrackInfo& track, model::ClipInfo::Kind kind, model::BounceMode mode)
{
    if (mode == model::BounceMode::NewTrack)
        return track.devices;
    std::vector<model::DeviceSlot> out;
    if (kind == model::ClipInfo::Kind::Audio)
        return out;
    for (const auto& d : track.devices)
    {
        out.push_back(d);
        if (d.instrument)
            break;
    }
    return out;
}

bool sameModules(const std::vector<model::DeviceSlot>& a, const std::vector<model::DeviceSlot>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const auto& x, const auto& y) {
        return x.id == y.id && x.enabled == y.enabled && x.sidechainSource == y.sidechainSource;
    });
}

const model::ClipInfo* snapshotClip(const model::Snapshot& s, std::uint64_t id)
{
    for (const auto& t : s.tracks)
        for (const auto& c : t.clips)
            if (c.id == id)
                return &c;
    return nullptr;
}

/// A bounce file: `name.wav` in `folder`, numbered when taken.
juce::File bounceFile(const juce::File& folder, const std::string& name)
{
    const auto base = juce::File::createLegalFileName(juce::String::fromUTF8(name.c_str()));
    auto f = folder.getChildFile(base + ".wav");
    for (int n = 2; f.exists(); ++n)
        f = folder.getChildFile(base + " " + juce::String(n) + ".wav");
    return f;
}
} // namespace

struct Session::Impl::Bounce final : std::enable_shared_from_this<Bounce>, Session::Listener, private juce::Timer
{
    /// What callers hold: progress and cancelling, from any thread.
    struct Progress final : Session::RenderJob
    {
        std::atomic<float> fraction{0.0f};
        std::atomic<bool> cancelled{false};
        float progress() const override { return fraction; }
        void cancel() override { cancelled = true; }
    };
    /// A rendered run: its file and where each of its pieces ends.
    struct RunFile
    {
        juce::File file;
        std::vector<model::BounceEnd> ends;
    };

    Bounce(Impl& i, model::BounceMode m) : impl(i), mode(m), editAtStart(i.edit.get())
    {
        impl.owner.addListener(this);
        startTimer(50);
    }

    ~Bounce() override
    {
        stopTimer();
        impl.owner.removeListener(this);
        temp.deleteFile();
        if (!landed) // nothing ever referred to them
            for (const auto& f : files)
                f.file.deleteFile();
    }

    Impl& impl;
    const model::BounceMode mode;
    te::Edit* const editAtStart;
    std::vector<model::BounceRun> runs;
    std::string undoName;
    std::function<void(const BounceResult&)> done;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    const double startedAt = juce::Time::getMillisecondCounterHiRes();
    /// What it renders, as it was when it started.
    std::vector<model::ClipInfo> sources;
    std::map<std::uint64_t, std::vector<model::DeviceSlot>> modules; // per run, by index
    std::set<std::uint64_t> watched;
    bool moduleChanged = false;
    std::vector<RunFile> files;
    std::shared_ptr<Session::RenderJob> rendering;
    juce::File temp;
    bool landed = false, finished = false, waiting = false;

    [[nodiscard]] std::vector<std::uint64_t> clips() const
    {
        std::vector<std::uint64_t> out;
        for (const auto& run : runs)
            for (const auto& piece : run.pieces)
                out.insert(out.end(), piece.parts.begin(), piece.parts.end());
        return out;
    }

    /// Renders the next run; false if it couldn't start.
    bool renderNext()
    {
        const auto& run = runs[files.size()];
        temp = juce::File::createTempFile(".wav");
        std::weak_ptr<Bounce> weak = weak_from_this();
        rendering = impl.renderCopy([&](te::Edit& copy, te::Renderer::Parameters& params) { return setUp(copy, params, run); },
                                    [weak](const std::string& error) {
                                        if (auto self = weak.lock())
                                            self->rendered(error);
                                    });
        return rendering != nullptr;
    }

    /// The copy plays only the run's clips, through what the bounce renders (§3).
    bool setUp(te::Edit& copy, te::Renderer::Parameters& params, const model::BounceRun& run) const
    {
        auto* track = dynamic_cast<te::AudioTrack*>(te::findTrackForID(copy, te::EditItemID::fromRawID(run.track)));
        if (track == nullptr)
            return false;
        params.destFile = temp;
        params.sampleRateForAudio = impl.owner.sampleRate();
        params.time = {at(run.start), at(run.renderEnd)};
        params.useMasterPlugins = false;
        for (const auto& piece : run.pieces)
            for (const auto id : piece.parts)
                if (auto* c = te::findClipForID(copy, te::EditItemID::fromRawID(id)))
                    params.allowedClips.add(c);
        // Heard whatever the mixer's mute and solo say.
        for (auto* t : te::getAudioTracks(copy))
            t->setSolo(false);
        track->setMute(false);
        if (mode == model::BounceMode::NewTrack)
        {
            // The whole chain before the fader: the mixer's own plugins (volume and pan, sends) off.
            for (auto* p : track->pluginList)
                if (dynamic_cast<te::VolumeAndPanPlugin*>(p) != nullptr || dynamic_cast<te::AuxSendPlugin*>(p) != nullptr)
                    p->setEnabled(false);
            renderOneTrack(copy, params, run.track);
            return true;
        }
        params.tracksToDo = te::toBitSet(juce::Array<te::Track*>{track});
        if (run.kind == model::ClipInfo::Kind::Audio)
            params.usePlugins = false; // the clip as it enters the chain
        else
        {
            // Up to and including the instrument: everything after it is off in the copy.
            bool after = false;
            for (auto* p : track->pluginList)
            {
                if (after)
                    p->setEnabled(false);
                after = after || p->isSynth();
            }
        }
        return true;
    }

    void rendered(const std::string& error)
    {
        rendering.reset();
        if (progress->cancelled)
            return finish({.error = "Cancelled.", .cancelled = true});
        if (!error.empty())
            return finish({.error = "The bounce didn't finish: " + error});
        const auto& run = runs[files.size()];
        auto& formats = impl.engine.getAudioFileFormatManager();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.readFormatManager.createReaderFor(temp));
        if (reader == nullptr)
            return finish({.error = "The bounce couldn't be read back."});

        // Its level per block, for the tails (§7).
        const double rate = reader->sampleRate;
        constexpr int block = 256;
        model::BlockLevels levels{.blockSeconds = block / rate};
        const auto channels = static_cast<int>(reader->numChannels);
        juce::AudioBuffer<float> buffer(channels, block);
        for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += block)
        {
            const auto n = static_cast<int>(std::min<juce::int64>(block, reader->lengthInSamples - pos));
            reader->read(&buffer, 0, n, pos, true, true);
            float peak = 0.0f;
            for (int c = 0; c < channels; ++c)
                peak = std::max(peak, buffer.getMagnitude(c, 0, n));
            levels.peaks.push_back(peak);
        }
        RunFile out;
        for (const auto& piece : run.pieces)
            out.ends.push_back(model::bounceEnd(run, piece, levels, mode));

        // The file keeps the whole tail, even where a clip cuts it (§7).
        const auto kept = std::min<juce::int64>(reader->lengthInSamples,
                                                static_cast<juce::int64>(std::ceil(model::keptLength(run, levels, mode) * rate)));
        auto folder = impl.bouncesFolder();
        (void)folder.createDirectory();
        const auto* track = impl.owner.snapshot()->channel(run.track);
        out.file = bounceFile(folder, model::bounceFileName(track != nullptr ? track->name : std::string("Track"),
                                                            run.pieces.front().name));
        {
            std::unique_ptr<juce::OutputStream> stream(out.file.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer;
            if (stream != nullptr)
                writer = formats.getWavFormat()->createWriterFor(
                    stream, juce::AudioFormatWriterOptions{}
                                .withSampleRate(rate)
                                .withNumChannels(static_cast<int>(reader->numChannels))
                                .withBitsPerSample(32)
                                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (writer == nullptr)
            {
                out.file.deleteFile();
                return finish({.error = "The bounce couldn't be written to " + folder.getFullPathName().toStdString() + "."});
            }
            if (!writer->writeFromAudioReader(*reader, 0, kept))
            {
                writer.reset();
                out.file.deleteFile();
                return finish({.error = "The bounce couldn't be written."});
            }
        }
        reader.reset();
        temp.deleteFile();
        files.push_back(std::move(out));
        if (files.size() < runs.size())
        {
            if (!renderNext())
                finish({.error = "The bounce couldn't start."});
            return;
        }
        land();
    }

    void land()
    {
        // Never in the middle of a recording pass: it lands once the pass has ended.
        if (impl.owner.isRecording())
        {
            waiting = true;
            return;
        }
        waiting = false;
        if (impl.edit.get() != editAtStart)
            return finish({.error = "The project was closed while it was bouncing."});
        impl.owner.flushSnapshot();
        const auto snap = impl.owner.snapshot();
        for (const auto& c : sources)
            if (const auto* now = snapshotClip(*snap, c.id); now == nullptr || !sameClip(*now, c))
                return finish({.error = c.name + " changed while it was bouncing, so nothing was bounced."});
        for (size_t r = 0; r < runs.size(); ++r)
        {
            const auto* track = snap->channel(runs[r].track);
            if (moduleChanged || track == nullptr || !sameModules(renderedModules(*track, runs[r].kind, mode), modules[r]))
                return finish({.error = "A module changed while " + runs[r].pieces.front().name +
                                        " was bouncing, so nothing was bounced."});
        }
        impl.beginEdit(juce::String(undoName));
        auto result = mode == model::BounceMode::InPlace ? landInPlace(*snap) : landOnNewTracks();
        landed = true;
        finish(std::move(result));
    }

    /// In place (§4): the source becomes a take of the bounced clip, which plays the bounce.
    BounceResult landInPlace(const model::Snapshot& snap)
    {
        BounceResult result;
        auto* um = &impl.edit->getUndoManager();
        for (size_t r = 0; r < runs.size(); ++r)
        {
            const auto& run = runs[r];
            auto* track = impl.track(run.track);
            const auto* info = snap.channel(run.track);
            if (track == nullptr || info == nullptr)
                continue;
            for (size_t p = 0; p < run.pieces.size(); ++p)
            {
                const auto& piece = run.pieces[p];
                const auto& end = files[r].ends[p];
                std::vector<te::Clip*> parts;
                for (const auto id : piece.parts)
                    if (auto* c = impl.clip(id))
                        parts.push_back(c);
                if (parts.empty())
                    continue;
                const auto colour = parts.front()->state[kColourId];
                // A clip playing one of its takes keeps it there; a plain clip or a comp is kept
                // as "Before bounce".
                auto stack = impl.stackOf(*parts.front());
                bool keepSource = true;
                if (stack.isValid())
                {
                    if (const auto* s = info->stack(idOf(stack)))
                        keepSource = !model::playingTake(*s, info->clips).has_value();
                }
                else
                    stack = impl.makeStack(*track, piece.start, end.end, um);
                if (keepSource)
                {
                    std::vector<juce::ValueTree> states;
                    for (auto* c : parts)
                        states.push_back(c->state);
                    impl.addTake(stack, model::kBeforeBounceName, states, um);
                }
                std::vector<model::TakeInfo> names;
                for (auto take : stack)
                    names.push_back({.name = take[te::IDs::name].toString().toStdString()});
                for (auto* c : parts)
                    c->removeFromParent();

                auto clip = track->insertWaveClip(juce::String::fromUTF8(piece.name.c_str()), files[r].file,
                                                  {{at(piece.start), at(end.end)}, te::TimeDuration::fromSeconds(piece.start - run.start)},
                                                  false);
                if (clip == nullptr)
                    continue;
                if (!colour.isVoid())
                    clip->state.setProperty(kColourId, colour, um);
                if (end.cut)
                    clip->setFadeOut(te::TimeDuration::fromSeconds(model::kBounceCutFadeSeconds));
                auto take = impl.addTake(stack, model::bounceTakeName(names), {clip->state}, um);
                clip->state.setProperty(fromTakeId, static_cast<juce::int64>(idOf(take.getChild(0))), um);
                stack.setProperty(fromId, std::min(piece.start, static_cast<double>(stack[fromId])), um);
                stack.setProperty(toId, std::max(end.end, static_cast<double>(stack[toId])), um);
                result.clips.push_back(clip->itemID.getRawID());
                if (end.cut && end.audible)
                    result.cutTails.push_back({piece.name, piece.limitName});
            }
            impl.settle(*track, um);
        }
        return result;
    }

    /// To new tracks (§5): one per source track, right after it, with its mix and lanes; the
    /// source clips muted.
    BounceResult landOnNewTracks()
    {
        BounceResult result;
        std::map<std::uint64_t, te::AudioTrack*> made;
        for (size_t r = 0; r < runs.size(); ++r)
        {
            const auto& run = runs[r];
            auto* source = impl.track(run.track);
            if (source == nullptr)
                continue;
            auto*& target = made[run.track];
            if (target == nullptr)
            {
                target = newTrackFor(*source);
                if (target == nullptr)
                    continue;
                result.tracks.push_back(target->itemID.getRawID());
            }
            for (size_t p = 0; p < run.pieces.size(); ++p)
            {
                const auto& piece = run.pieces[p];
                const auto& end = files[r].ends[p];
                juce::var colour;
                for (const auto id : piece.parts)
                    if (auto* c = impl.clip(id))
                    {
                        if (colour.isVoid())
                            colour = c->state[kColourId];
                        c->setMuted(true);
                    }
                auto clip = target->insertWaveClip(juce::String::fromUTF8(piece.name.c_str()), files[r].file,
                                                   {{at(piece.start), at(end.end)}, te::TimeDuration::fromSeconds(piece.start - run.start)},
                                                   false);
                if (clip == nullptr)
                    continue;
                if (!colour.isVoid())
                    clip->state.setProperty(kColourId, colour, &impl.edit->getUndoManager());
                result.clips.push_back(clip->itemID.getRawID());
            }
        }
        return result;
    }

    /// A track right after `source`, named after it, in its colour, with its volume, pan and
    /// sends (and their lanes) and no modules.
    te::AudioTrack* newTrackFor(te::AudioTrack& source)
    {
        auto* um = &impl.edit->getUndoManager();
        auto copy = impl.edit->copyTrack(&source, te::TrackInsertPoint(nullptr, &source));
        auto* track = dynamic_cast<te::AudioTrack*>(copy.get());
        if (track == nullptr)
            return nullptr;
        const auto clips = track->getClips();
        for (int i = clips.size(); --i >= 0;)
            clips[i]->removeFromParent();
        if (auto takes = track->state.getChildWithName(takesId); takes.isValid())
            track->state.removeChild(takes, um);
        for (auto* p : chain(track->pluginList)) // their automation goes with them
            p->deleteFromParent();
        // The mixer's curves are cleared in place: removing their trees would leave the copied
        // parameters writing to curves no longer in the project.
        for (auto* p : track->pluginList)
            for (auto* param : p->getAutomatableParameters())
                if (param->getCurve().getNumPoints() > 0)
                    param->getCurve().clear(um);
        for (const auto& id : {audioInId, midiInId, midiChannelId, monitorId, recordsId, armedId})
            track->state.removeProperty(id, um);
        track->setName(juce::String::fromUTF8(
            model::uniqueName(model::bounceTrackName(source.getName().toStdString()), impl.trackNames()).c_str()));
        // The lanes driving the source's mix drive the bounce's too (ADR 0029).
        std::vector<std::string> keys{"volume", "pan"};
        for (auto* r : impl.returns())
            keys.push_back("send/" + std::to_string(r->itemID.getRawID()));
        for (const auto& key : keys)
            if (auto assign = impl.assignTree({source.itemID.getRawID(), key}); assign.isValid())
                impl.assignTo(assign.getParent(), {track->itemID.getRawID(), key}, rangeOf(assign), um);
        return track;
    }

    void finish(BounceResult result)
    {
        if (finished)
            return;
        finished = true;
        stopTimer();
        progress->fraction = 1.0f;
        auto self = shared_from_this(); // until the callback has run
        auto callback = std::move(done);
        std::erase(impl.bounces, self);
        if (callback)
            callback(result);
    }

    void deviceStateChanged(std::uint64_t id) override
    {
        if (watched.contains(id))
            moduleChanged = true;
    }

private:
    void timerCallback() override
    {
        if (progress->cancelled)
        {
            if (rendering != nullptr)
                rendering->cancel(); // finishes when the render says so
            else
                finish({.error = "Cancelled.", .cancelled = true});
            return;
        }
        const float current = rendering != nullptr ? rendering->progress() : 0.0f;
        progress->fraction = (static_cast<float>(files.size()) + current) / static_cast<float>(std::max<size_t>(1, runs.size()));
        if (waiting)
            land();
    }
};

juce::File Session::Impl::bouncesFolder()
{
    return projectFolder().getChildFile("Bounces");
}

// ---- Session -------------------------------------------------------------------------------

std::string Session::bounceRefusal(const std::vector<std::uint64_t>& clips, model::BounceMode mode)
{
    flushSnapshot();
    const auto snapshot = this->snapshot();
    for (const auto& t : snapshot->tracks)
        for (const auto& c : t.clips)
            if (std::find(clips.begin(), clips.end(), c.id) != clips.end())
                if (const auto refusal = model::bounceRefusal(t, c, mode); refusal != model::BounceRefusal::None)
                    return model::refusalText(refusal);
    return {};
}

std::shared_ptr<Session::RenderJob> Session::bounce(const std::vector<std::uint64_t>& clips, model::BounceMode mode,
                                                    std::function<void(const BounceResult&)> done, const std::string& undoName)
{
    auto& impl = *impl_;
    flushSnapshot();
    const auto snapshot = this->snapshot();
    auto job = std::make_shared<Impl::Bounce>(impl, mode);
    job->runs = model::bounceRuns(*snapshot, {clips.begin(), clips.end()}, mode);
    if (job->runs.empty())
        return nullptr;
    job->undoName = !undoName.empty() ? undoName : mode == model::BounceMode::InPlace ? "Bounce in place" : "Bounce to new track";
    job->done = std::move(done);
    for (const auto id : job->clips())
        if (const auto* c = snapshotClip(*snapshot, id))
            job->sources.push_back(*c);
    for (size_t r = 0; r < job->runs.size(); ++r)
        if (const auto* track = snapshot->channel(job->runs[r].track))
        {
            job->modules[r] = renderedModules(*track, job->runs[r].kind, mode);
            for (const auto& d : job->modules[r])
                job->watched.insert(d.id);
        }
    impl.bounces.push_back(job);
    if (!job->renderNext())
    {
        std::erase(impl.bounces, job);
        return nullptr;
    }
    return job->progress;
}

std::vector<Session::Bouncing> Session::bouncing() const
{
    std::vector<Bouncing> out;
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& b : impl_->bounces)
        for (const auto id : b->clips())
            out.push_back({id, b->progress->fraction, (now - b->startedAt) / 1000.0});
    return out;
}

bool Session::cancelBounces()
{
    for (const auto& b : impl_->bounces)
        b->progress->cancel();
    return !impl_->bounces.empty();
}

void Session::setClipsMuted(const std::vector<std::uint64_t>& asked, bool muted)
{
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
    const auto ids = model::withParts(places, asked);
    if (ids.empty())
        return;
    impl_->beginEdit(muted ? "Mute clips" : "Unmute clips");
    for (auto id : ids)
        if (auto* c = impl_->clip(id); c != nullptr && c->isMuted() != muted)
            c->setMuted(muted);
}

double Session::sampleRate() const
{
    const double rate = impl_->engine.getDeviceManager().getSampleRate();
    return rate > 0.0 ? rate : 48000.0;
}

juce::File Session::bouncesFolder() const
{
    return impl_->bouncesFolder();
}
} // namespace fd::engine
