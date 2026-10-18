// Export (M2 step C8, ADR 0039, docs/design/export.md): the mix or stems, rendered offline on a
// copy of the project, then measured, normalised, dithered and written in the background.

#include "engine/EngineBehaviour.h"
#include "engine/Mp3Writer.h"
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

/// What finishing a rendered file came to (on the export's thread): its files, one a format
/// (export.md §17.2).
struct Finished
{
    std::vector<Session::ExportedFile> files;
    std::string error;
};

/// The quality index JUCE's writers take for a lossy format's bit rate (§17.1): Vorbis's quality
/// (an index of tenths), Opus's list of rates after "Auto".
int qualityIndexFor(model::ExportFormat format, int kbps)
{
    if (format == model::ExportFormat::Ogg)
        return kbps <= 128 ? 4 : kbps <= 192 ? 6 : kbps <= 256 ? 8 : 9;
    constexpr int opus[] = {64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 510};
    for (int i = 0; i < static_cast<int>(std::size(opus)); ++i)
        if (opus[i] >= kbps)
            return i + 1;
    return 0;
}

/// The rendered file, as the export writes it (§4, §5): its tail kept until silent, normalised,
/// then each format's file (§17.2): PCM on its grid, dithered below 32-bit float; MP3, Ogg and
/// Opus encoded from the float render as it is, and measured decoded back, so the report has the
/// peaks listeners get (§17.1). Each under a unique name in `folder`. Off the message thread.
Finished finish(const juce::File& rendered, const model::ExportSpan& span,
                const model::ExportSettings& settings, const juce::File& folder,
                const std::vector<std::string>& fileNames, const std::atomic<bool>& cancelled)
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

    const auto measure = [](const juce::AudioBuffer<float>& data, int length, double at)
    {
        std::vector<std::vector<float>> channelsOf(static_cast<size_t>(data.getNumChannels()));
        for (int c = 0; c < data.getNumChannels(); ++c)
            channelsOf[static_cast<size_t>(c)].assign(data.getReadPointer(c),
                                                      data.getReadPointer(c) + length);
        return model::measureAudio(channelsOf, at);
    };
    // Normalising: a gain, the true peak first (§4), once for every format.
    bool ceilingHeld = false;
    if (settings.normalise != model::ExportNormalise::Off)
    {
        const auto before = measure(audio, frames, rate);
        const auto gain = model::normaliseGain(settings.normalise, settings.loudnessTarget, before.truePeakDb, before.integratedLufs);
        audio.applyGain(0, frames, static_cast<float>(std::pow(10.0, gain.db / 20.0)));
        ceilingHeld = gain.ceilingHeld;
    }
    if (!folder.createDirectory())
        return {.error = "Can't make the folder " + folder.getFullPathName().toStdString() + "."};

    Finished out;
    const auto fail = [&out](std::string why)
    {
        for (const auto& f : out.files)
            f.file.deleteFile();
        return Finished{.error = std::move(why)};
    };
    const auto files = model::exportFormats(settings);
    for (size_t k = 0; k < files.size() && k < fileNames.size(); ++k)
    {
        if (cancelled)
            return fail({});
        const auto& f = files[k];
        Session::ExportedFile file;
        file.ceilingHeld = ceilingHeld;
        const auto title =
            juce::File(juce::String::fromUTF8(fileNames[k].c_str())).getFileNameWithoutExtension();
        // Written beside its place, then moved into it, so a file is only ever whole (§7).
        const auto partial = folder.getNonexistentChildFile(".export", ".part", false);
        if (f.format == model::ExportFormat::Mp3)
        {
            std::string error;
            if (!writeMp3(partial, audio, frames, rate, f.kbps, title, error))
            {
                partial.deleteFile();
                return fail(error);
            }
        }
        else
        {
            auto* format = formats.findFormatForFileExtension(juce::String(model::extensionFor(f.format)));
            if (format == nullptr)
                return fail("This build can't write " + model::formatName(f.format) + " files.");
            // PCM on the file's grid, dithered below 32-bit float; a lossy format takes the floats.
            const bool lossy = model::isLossy(f.format);
            juce::AudioBuffer<float> data(channels, frames);
            for (int c = 0; c < channels; ++c)
            {
                data.copyFrom(c, 0, audio, c, 0, frames);
                if (lossy)
                    continue;
                model::Quantiser quantise(f.bitDepth, settings.dither, static_cast<std::uint32_t>(c + 1));
                auto* d = data.getWritePointer(c);
                for (int i = 0; i < frames; ++i)
                    d[i] = quantise(d[i]);
            }
            if (!lossy)
                file.measured = measure(data, frames, rate);
            std::unique_ptr<juce::OutputStream> stream(partial.createOutputStream());
            if (stream == nullptr)
                return fail("Can't write to " + folder.getFullPathName().toStdString() + ".");
            auto options = juce::AudioFormatWriterOptions{}
                               .withSampleRate(rate)
                               .withNumChannels(channels)
                               .withBitsPerSample(lossy ? 32 : f.bitDepth);
            if (lossy)
                options = options.withQualityOptionIndex(qualityIndexFor(f.format, f.kbps))
                              .withMetadataValues({{juce::OggVorbisAudioFormat::id3title, title},
                                                   {juce::OggVorbisAudioFormat::encoderName, "Segno"}});
            else if (f.bitDepth == 32)
                options =
                    options.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            auto writer = format->createWriterFor(stream, options);
            if (writer == nullptr || !writer->writeFromAudioSampleBuffer(data, 0, frames))
            {
                writer.reset();
                partial.deleteFile();
                return fail("The export couldn't be written (is the disk full?).");
            }
        }
        // A lossy file is measured as listeners hear it: decoded back.
        if (model::isLossy(f.format))
        {
            std::unique_ptr<juce::AudioFormatReader> back(
                formats.findFormatForFileExtension(juce::String(model::extensionFor(f.format)))
                    ->createReaderFor(new juce::FileInputStream(partial), true));
            if (back == nullptr)
            {
                partial.deleteFile();
                return fail("The " + model::formatName(f.format) + " file couldn't be read back.");
            }
            juce::AudioBuffer<float> decoded(static_cast<int>(back->numChannels),
                                             static_cast<int>(back->lengthInSamples));
            back->read(&decoded, 0, decoded.getNumSamples(), 0, true, true);
            file.measured = measure(decoded, decoded.getNumSamples(), back->sampleRate);
        }
        const auto name = model::uniqueFileName(
            fileNames[k], [&](const std::string& n)
            { return folder.getChildFile(juce::String::fromUTF8(n.c_str())).exists(); });
        file.file = folder.getChildFile(juce::String::fromUTF8(name.c_str()));
        if (!partial.moveFileTo(file.file))
        {
            partial.deleteFile();
            return fail("The export couldn't be moved into " + folder.getFullPathName().toStdString() + ".");
        }
        out.files.push_back(std::move(file));
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
    size_t done_ = 0; // targets finished: each may write several files (§17.2)
    bool over = false;

    /// Renders the next file; false if it couldn't start.
    bool renderNext()
    {
        temp = juce::File::createTempFile(".wav");
        std::weak_ptr<Export> weak = weak_from_this();
        rendering = impl.renderCopy([&](te::Edit& copy, te::Renderer::Parameters& params)
                                    { return setUp(copy, params, targets[done_]); },
                                    [weak](const std::string& error)
                                    {
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
        // Solo is for listening: an export plays every track and group.
        for (auto* t : te::getAudioTracks(copy))
            t->setSolo(false);
        for (auto* t : te::getAllTracks(copy))
            if (auto* group = dynamic_cast<te::FolderTrack*>(t))
                group->setSolo(false);
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
        const auto& target = targets[done_];
        std::vector<std::string> names;
        for (const auto& f : model::exportFormats(settings))
            names.push_back(model::exportFileName(
                base, target.kind == Target::Kind::Mix ? std::string() : target.name, bars, f.format));
        std::weak_ptr<Export> weak = weak_from_this();
        if (!impl.exportThread)
            impl.exportThread = std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}.withThreadName("Export").withNumberOfThreads(1));
        impl.exportThread->addJob(
            [weak, audio = temp, range = span, options = settings, into = folder, names, state = progress,
             track = target.name]
            {
                auto outcome = finish(audio, range, options, into, names, state->cancelled);
                for (auto& f : outcome.files)
                    f.track = track;
                juce::MessageManager::callAsync(
                    [weak, ready = std::move(outcome)]() mutable
                    {
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
            for (const auto& f : result.files)
                f.file.deleteFile();
            return end({.cancelled = true});
        }
        if (!result.error.empty())
            return end({.error = result.error});
        for (auto& f : result.files)
            files.push_back(std::move(f));
        if (++done_ < targets.size())
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
        progress->fraction = each * (static_cast<float>(done_) + now);
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

namespace
{
const juce::Identifier kExportId{"fdExport"};
} // namespace

model::ExportSettings Session::exportSettings() const
{
    // A project that hasn't exported starts from the defaults (the last export's, ADR 0045).
    if (!impl_->edit->state.hasProperty(kExportId))
        return impl_->exportDefaults;
    return model::exportSettingsFromJson(impl_->edit->state.getProperty(kExportId).toString().toStdString());
}

void Session::setExportSettings(const model::ExportSettings& settings)
{
    // Kept with the project, as the view is: never an undo step or an unsaved change (as solo).
    auto& edit = *impl_->edit;
    const auto text = juce::String(model::toJson(model::validated(settings)));
    if (edit.state.getProperty(kExportId).toString() == text)
        return;
    const bool wasChanged = edit.hasChangedSinceSaved();
    const bool wasDirty = impl_->dirty;
    edit.state.setProperty(kExportId, text, nullptr);
    if (!wasChanged)
        edit.resetChangedStatus();
    impl_->dirty = wasDirty;
}

std::string Session::projectName() const
{
    return impl_->file ? impl_->file->getFileNameWithoutExtension().toStdString() : std::string("Untitled");
}

bool Session::isExporting() const
{
    return impl_->exporting != nullptr;
}

namespace
{
std::optional<std::pair<double, double>> spanOf(std::optional<model::TimeSpan> span)
{
    return span ? std::optional(std::pair{span->start, span->end}) : std::nullopt;
}
} // namespace

std::string Session::exportRefusal(const model::ExportSettings& settings,
                                   const std::vector<std::uint64_t>& tracks,
                                   std::optional<model::TimeSpan> selection)
{
    if (impl_->exporting)
        return "An export is running: wait for it, or cancel it.";
    std::string why;
    const auto [loopStart, loopEnd] = loopRange();
    const auto loop = loopEnd > loopStart ? std::optional(std::pair{loopStart, loopEnd}) : std::nullopt;
    if (!model::exportSpan(*snapshot(), settings.range, loop, spanOf(selection), &why))
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

std::shared_ptr<Session::RenderJob> Session::exportAudio(const model::ExportSettings& asked,
                                                         const std::vector<std::uint64_t>& tracks,
                                                         std::function<void(const ExportResult&)> done,
                                                         std::optional<model::TimeSpan> selection)
{
    updateSnapshotNow();
    if (!exportRefusal(asked, tracks, selection).empty())
        return nullptr;
    auto& impl = *impl_;
    auto job = std::make_shared<Impl::Export>(impl);
    job->settings = model::validated(asked);
    const auto [loopStart, loopEnd] = loopRange();
    const auto loop = loopEnd > loopStart ? std::optional(std::pair{loopStart, loopEnd}) : std::nullopt;
    job->span = *model::exportSpan(*snapshot(), job->settings.range, loop, spanOf(selection));
    job->rate = job->settings.sampleRate > 0.0 ? job->settings.sampleRate : sampleRate();
    job->folder = exportFolder(job->settings);
    job->base = job->settings.name.empty() ? projectName() : job->settings.name;
    if (job->settings.range != model::ExportRange::Song)
        job->bars = model::loopBars(snapshot()->tempo, job->span.start, job->span.end);
    using K = Target::Kind;
    const auto& s = *snapshot();
    switch (job->settings.what)
    {
    case model::ExportWhat::Mix: job->targets.push_back({.kind = K::Mix}); break;
    case model::ExportWhat::SelectedTracks:
    case model::ExportWhat::AllTracks:
        // The tracks and groups, then the return tracks (each a stem of its own), in the
        // arrangement's order: a group whole, through its modules and fader (groups.md §11).
        for (const auto id : model::stemChannels(s, job->settings, tracks))
            if (const auto* t = s.channel(id))
                job->targets.push_back(
                    {.kind = t->role == model::TrackInfo::Role::Return ? K::Return : K::Track,
                     .track = id,
                     .name = t->name});
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
