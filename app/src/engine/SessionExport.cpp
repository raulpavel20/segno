// Export (M2 step C8, ADR 0039, docs/design/export.md): the mix or stems, rendered offline on a
// copy of the project, then measured, normalised, dithered and written in the background.

#include "engine/EngineBehaviour.h"
#include "engine/SessionImpl.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace fd::engine
{
namespace
{
/// What one file of an export renders.
struct Target
{
    enum class Kind { Mix, Track, Return };
    Kind kind = Kind::Mix;
    std::uint64_t track = 0;
    std::string name; // the track's, for its stem's file name
};

/// What finishing a rendered file came to (on the export's thread).
struct Finished
{
    Session::ExportedFile file;
    std::string error;
};

/// The rendered file, as the export writes it (§4, §5): its tail kept until silent, normalised,
/// on the format's grid, under a unique name in `folder`. Runs off the message thread.
Finished finish(const juce::File& rendered, const model::ExportSpan& span, const model::ExportSettings& settings,
                const juce::File& folder, const std::string& fileName, const std::atomic<bool>& cancelled)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered));
    if (reader == nullptr)
        return {.error = "The export couldn't be read back."};
    const double rate = reader->sampleRate;
    const int channels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> audio(channels, static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
    reader.reset();

    // The tail: on until it falls silent (§3).
    constexpr int block = 256;
    model::BlockLevels levels{.blockSeconds = block / rate};
    for (int pos = 0; pos < audio.getNumSamples(); pos += block)
    {
        const int n = std::min(block, audio.getNumSamples() - pos);
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c)
            peak = std::max(peak, audio.getMagnitude(c, pos, n));
        levels.peaks.push_back(peak);
    }
    const int frames = std::min(audio.getNumSamples(), static_cast<int>(std::llround(model::exportLength(span, levels) * rate)));
    if (cancelled)
        return {};

    auto measure = [&] {
        std::vector<std::vector<float>> data(static_cast<size_t>(channels));
        for (int c = 0; c < channels; ++c)
            data[static_cast<size_t>(c)].assign(audio.getReadPointer(c), audio.getReadPointer(c) + frames);
        return model::measureAudio(data, rate);
    };
    // Normalising: a gain, the true peak first (§4).
    Finished out;
    if (settings.normalise != model::ExportNormalise::Off)
    {
        const auto before = measure();
        const auto gain = model::normaliseGain(settings.normalise, settings.loudnessTarget, before.truePeakDb, before.integratedLufs);
        audio.applyGain(0, frames, static_cast<float>(std::pow(10.0, gain.db / 20.0)));
        out.file.ceilingHeld = gain.ceilingHeld;
    }
    // On the file's grid, dithered below 32-bit float.
    for (int c = 0; c < channels; ++c)
    {
        model::Quantiser quantise(settings.bitDepth, settings.dither, static_cast<std::uint32_t>(c + 1));
        auto* data = audio.getWritePointer(c);
        for (int i = 0; i < frames; ++i)
            data[i] = quantise(data[i]);
    }
    if (cancelled)
        return {};
    out.file.measured = measure();

    // Written beside its place, then moved into it, so a file is only ever whole (§7).
    if (!folder.createDirectory())
        return {.error = "Can't make the folder " + folder.getFullPathName().toStdString() + "."};
    auto* format = formats.findFormatForFileExtension(juce::String(model::extensionFor(settings.format)));
    if (format == nullptr)
        return {.error = "This build can't write " + model::formatName(settings.format) + " files."};
    const auto partial = folder.getNonexistentChildFile(".export", ".part", false);
    {
        std::unique_ptr<juce::OutputStream> stream(partial.createOutputStream());
        if (stream == nullptr)
            return {.error = "Can't write to " + folder.getFullPathName().toStdString() + "."};
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(channels).withBitsPerSample(settings.bitDepth);
        if (settings.bitDepth == 32)
            options = options.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = format->createWriterFor(stream, options);
        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(audio, 0, frames))
        {
            writer.reset();
            partial.deleteFile();
            return {.error = "The export couldn't be written (is the disk full?)."};
        }
    }
    const auto name = model::uniqueFileName(fileName, [&](const std::string& n) { return folder.getChildFile(juce::String::fromUTF8(n.c_str())).exists(); });
    out.file.file = folder.getChildFile(juce::String::fromUTF8(name.c_str()));
    if (!partial.moveFileTo(out.file.file))
    {
        partial.deleteFile();
        return {.error = "The export couldn't be moved into " + folder.getFullPathName().toStdString() + "."};
    }
    return out;
}
} // namespace

struct Session::Impl::Export final : std::enable_shared_from_this<Export>, private juce::Timer
{
    /// What callers hold: progress and cancelling, from any thread.
    struct Progress final : Session::RenderJob
    {
        std::atomic<float> fraction{0.0f};
        std::atomic<bool> cancelled{false};
        float progress() const override { return fraction; }
        void cancel() override { cancelled = true; }
    };

    explicit Export(Impl& i) : impl(i) { startTimer(50); }
    ~Export() override
    {
        stopTimer();
        temp.deleteFile();
    }

    Impl& impl;
    model::ExportSettings settings;
    model::ExportSpan span;
    double rate = 48000.0;
    juce::File folder;
    std::string base, bars;
    std::vector<Target> targets;
    std::vector<ExportedFile> files;
    std::function<void(const ExportResult&)> done;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    std::shared_ptr<Session::RenderJob> rendering;
    juce::File temp;
    bool over = false;

    /// Renders the next file; false if it couldn't start.
    bool renderNext()
    {
        temp = juce::File::createTempFile(".wav");
        std::weak_ptr<Export> weak = weak_from_this();
        rendering = impl.renderCopy([&](te::Edit& copy, te::Renderer::Parameters& params) { return setUp(copy, params, targets[files.size()]); },
                                    [weak](const std::string& error) {
                                        if (auto self = weak.lock())
                                            self->rendered(error);
                                    });
        return rendering != nullptr;
    }

    /// The copy plays what the target is (§2): the mix; a track through its modules and fader
    /// (and, through the returns and master, as heard in the mix); a return fed by the tracks
    /// sending to it, which are muted so only the return is heard.
    bool setUp(te::Edit& copy, te::Renderer::Parameters& params, const Target& target) const
    {
        params.destFile = temp;
        params.sampleRateForAudio = rate;
        params.time = {te::TimePosition::fromSeconds(span.start),
                       te::TimePosition::fromSeconds(span.end + (span.tail ? model::kBounceTailSeconds : 0.0))};
        for (auto* t : te::getAudioTracks(copy)) // solo is for listening: an export plays every track
            t->setSolo(false);
        const auto all = te::getAllTracks(copy);
        switch (target.kind)
        {
        case Target::Kind::Mix:
            params.tracksToDo = te::toBitSet(all);
            params.useMasterPlugins = true;
            return true;
        case Target::Kind::Track:
            renderOneTrack(copy, params, target.track);
            params.useMasterPlugins = settings.throughReturns;
            if (settings.throughReturns)
                for (int i = 0; i < all.size(); ++i)
                    if (isReturn(*all[i]))
                        params.tracksToDo.setBit(i);
            return true;
        case Target::Kind::Return:
        {
            auto* back = te::findTrackForID(copy, te::EditItemID::fromRawID(target.track));
            if (back == nullptr)
                return false;
            int bus = -1;
            if (auto* audio = dynamic_cast<te::AudioTrack*>(back))
                for (auto* p : audio->pluginList)
                    if (auto* r = dynamic_cast<te::AuxReturnPlugin*>(p))
                        bus = r->busNumber;
            copy.state.setProperty(sendsWhileMutedId, true, nullptr);
            for (int i = 0; i < all.size(); ++i)
            {
                if (all[i] == back)
                {
                    params.tracksToDo.setBit(i);
                    continue;
                }
                auto* audio = dynamic_cast<te::AudioTrack*>(all[i]);
                if (audio == nullptr || audio->isMuted(false) || isReturn(*audio))
                    continue;
                for (auto* p : audio->pluginList)
                    if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p); send != nullptr && send->getBusNumber() == bus)
                    {
                        params.tracksToDo.setBit(i);
                        params.tracksToProcessWhileMuted.add(audio->itemID);
                        audio->setMute(true);
                        break;
                    }
            }
            params.useMasterPlugins = false;
            return true;
        }
        }
        return false;
    }

    void rendered(const std::string& error)
    {
        rendering.reset();
        if (progress->cancelled)
            return end({.cancelled = true});
        if (!error.empty())
            return end({.error = "The export didn't finish: " + error});
        // Finished off the message thread; then the next file.
        const auto& target = targets[files.size()];
        const auto name = model::exportFileName(base, target.kind == Target::Kind::Mix ? std::string() : target.name, bars, settings.format);
        std::weak_ptr<Export> weak = weak_from_this();
        if (!impl.exportThread)
            impl.exportThread = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}.withThreadName("Export").withNumberOfThreads(1));
        impl.exportThread->addJob([weak, audio = temp, range = span, options = settings, into = folder, name, state = progress,
                                   track = target.name] {
            auto outcome = finish(audio, range, options, into, name, state->cancelled);
            outcome.file.track = track;
            juce::MessageManager::callAsync([weak, ready = std::move(outcome)]() mutable {
                if (auto self = weak.lock())
                    self->finished(std::move(ready));
            });
        });
    }

    void finished(Finished result)
    {
        temp.deleteFile();
        if (progress->cancelled)
        {
            result.file.file.deleteFile();
            return end({.cancelled = true});
        }
        if (!result.error.empty())
            return end({.error = result.error});
        files.push_back(std::move(result.file));
        if (files.size() < targets.size())
        {
            if (!renderNext())
                end({.error = "The export couldn't start its next file."});
            return;
        }
        end({.files = files});
    }

    /// Done, one way or another: a cancelled or failed export leaves no file (§7).
    void end(ExportResult result)
    {
        if (over)
            return;
        over = true;
        stopTimer();
        if (!result.cancelled && result.error.empty())
            progress->fraction = 1.0f;
        else
            for (const auto& f : files)
                f.file.deleteFile();
        auto self = shared_from_this(); // until the callback has run
        if (impl.exporting == self)
            impl.exporting.reset();
        if (done)
            done(result);
    }

private:
    void timerCallback() override
    {
        // Each file's share: rendering most of it, finishing the rest.
        const float each = 1.0f / static_cast<float>(std::max<size_t>(1, targets.size()));
        const float now = rendering ? rendering->progress() * 0.85f : 0.9f;
        progress->fraction = each * (static_cast<float>(files.size()) + now);
        if (progress->cancelled && rendering) // a file being finished notices by itself
            rendering->cancel();
    }
};

// ---- Session -------------------------------------------------------------------------------

juce::File Session::exportsFolder()
{
    return impl_->projectFolder().getChildFile("Exports");
}

juce::File Session::exportFolder(const model::ExportSettings& settings)
{
    if (settings.folder.empty())
        return exportsFolder();
    const auto path = juce::String::fromUTF8(settings.folder.c_str());
    return juce::File::isAbsolutePath(path) ? juce::File(path) : impl_->projectFolder().getChildFile(path);
}

std::string Session::projectName() const
{
    return impl_->file ? impl_->file->getFileNameWithoutExtension().toStdString() : std::string("Untitled");
}

bool Session::isExporting() const
{
    return impl_->exporting != nullptr;
}

std::string Session::exportRefusal(const model::ExportSettings& settings, const std::vector<std::uint64_t>& tracks)
{
    if (impl_->exporting)
        return "An export is running: wait for it, or cancel it.";
    std::string why;
    const auto [loopStart, loopEnd] = loopRange();
    const auto loop = loopEnd > loopStart ? std::optional(std::pair{loopStart, loopEnd}) : std::nullopt;
    if (!model::exportSpan(*snapshot(), settings.range, loop, &why))
        return why;
    if (settings.what == model::ExportWhat::SelectedTracks)
    {
        const bool any = std::any_of(tracks.begin(), tracks.end(), [&](auto id) {
            const auto* t = snapshot()->channel(id);
            return t != nullptr && t->role != model::TrackInfo::Role::Master;
        });
        if (!any)
            return "Select the tracks to export as stems first.";
    }
    return {};
}

std::shared_ptr<Session::RenderJob> Session::exportAudio(const model::ExportSettings& asked, const std::vector<std::uint64_t>& tracks,
                                                         std::function<void(const ExportResult&)> done)
{
    updateSnapshotNow();
    if (!exportRefusal(asked, tracks).empty())
        return nullptr;
    auto& impl = *impl_;
    auto job = std::make_shared<Impl::Export>(impl);
    job->settings = model::validated(asked);
    const auto [loopStart, loopEnd] = loopRange();
    const auto loop = loopEnd > loopStart ? std::optional(std::pair{loopStart, loopEnd}) : std::nullopt;
    job->span = *model::exportSpan(*snapshot(), job->settings.range, loop);
    job->rate = job->settings.sampleRate > 0.0 ? job->settings.sampleRate : sampleRate();
    job->folder = exportFolder(job->settings);
    job->base = job->settings.name.empty() ? projectName() : job->settings.name;
    if (job->settings.range == model::ExportRange::Loop)
        job->bars = model::loopBars(snapshot()->tempo, job->span.start, job->span.end);
    using K = Target::Kind;
    const auto& s = *snapshot();
    switch (job->settings.what)
    {
    case model::ExportWhat::Mix: job->targets.push_back({.kind = K::Mix}); break;
    case model::ExportWhat::SelectedTracks:
    case model::ExportWhat::AllTracks:
        // The tracks, then the return tracks (each a stem of its own), in the arrangement's order.
        for (const auto* list : {&s.tracks, &s.returns})
            for (const auto& t : *list)
                if (job->settings.what == model::ExportWhat::AllTracks || std::find(tracks.begin(), tracks.end(), t.id) != tracks.end())
                    job->targets.push_back({.kind = list == &s.returns ? K::Return : K::Track, .track = t.id, .name = t.name});
        break;
    }
    if (job->targets.empty())
        return nullptr;
    job->done = std::move(done);
    impl.exporting = job;
    if (!job->renderNext())
    {
        impl.exporting.reset();
        return nullptr;
    }
    return job->progress;
}
} // namespace fd::engine
