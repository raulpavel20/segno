// The browser in the engine (M3 C2, ADR 0046, docs/design/browser.md): its places and index, what
// it finds, previews, putting an instrument in place of another, loading a Sampler, and keeping
// a project's files together.

#include "Sampler.h"
#include "engine/DevicePlugin.h"
#include "engine/PresetPreview.h"
#include "engine/SessionImpl.h"

#include <cmath>
#include <fd/audiofile/AudioFile.h>
#include <fd/device/Library.h>
#include <fd/device/Registry.h>

namespace fd::engine
{
namespace
{
juce::File fileOf(const std::string& path)
{
    return juce::File(juce::String::fromUTF8(path.c_str()));
}

std::string text(const juce::File& f)
{
    return f.getFullPathName().toStdString();
}
} // namespace

// ---- places and the index ------------------------------------------------------------------

void Session::Impl::updateBrowserPlaces()
{
    browserStarted = true;
    std::vector<juce::File> places;
    const auto add = [&places](const juce::File& f)
    {
        if (f != juce::File() && f.isDirectory() &&
            std::find(places.begin(), places.end(), f) == places.end())
            places.push_back(f);
    };
    if (file)
        add(file->getParentDirectory());
    else if (unsavedFolder)
        add(*unsavedFolder);
    // What ships (ADR 0055), then yours.
    if (const auto library = fd::library::root(); !library.empty())
        add(fileOf(library.string()));
    (void)segnoFolder.getChildFile("Samples").createDirectory(); // your own sounds (§9)
    (void)segnoFolder.getChildFile("Wavetables").createDirectory(); // your tables (table-editor.md §7)
    add(segnoFolder);
    for (const auto& p : browserPlaces)
        add(fileOf(p));
    // The Segno folder's presets are browsed in their modules, its unsaved projects as the project.
    indexer.setPlaces(
        places, {segnoFolder.getChildFile("Presets"), segnoFolder.getChildFile("Unsaved"), presetsFolder});
}

void Session::setBrowserIndexFile(const juce::File& file)
{
    impl_->indexer.setIndexFile(file);
}

void Session::setBrowserPlaces(const std::vector<std::string>& folders)
{
    if (folders == impl_->browserPlaces && impl_->browserStarted)
        return;
    impl_->browserPlaces = folders;
    impl_->updateBrowserPlaces();
}

std::vector<juce::File> Session::browserPlaces() const
{
    std::vector<juce::File> out;
    for (const auto& p : impl_->browserPlaces)
        out.push_back(fileOf(p));
    return out;
}

juce::File Session::libraryFolder() const
{
    const auto root = fd::library::root();
    const auto folder = root.empty() ? juce::File() : fileOf(root.string());
    return folder.isDirectory() ? folder : juce::File();
}

void Session::setLibraryFolder(const juce::File& folder)
{
    if (folder == impl_->libraryChoice)
        return;
    impl_->libraryChoice = folder;
    fd::library::setRoot(folder == juce::File()
                             ? std::filesystem::path()
                             : std::filesystem::path(folder.getFullPathName().toStdString()));
    if (impl_->browserStarted)
        impl_->updateBrowserPlaces();
}

juce::File Session::segnoFolder() const
{
    return impl_->segnoFolder;
}

void Session::setSegnoFolder(const juce::File& folder)
{
    if (folder == impl_->segnoFolder && impl_->browserStarted)
        return;
    impl_->segnoFolder = folder;
    impl_->updateBrowserPlaces();
}

std::optional<juce::File> Session::projectFolderOnDisk() const
{
    if (impl_->file)
        return impl_->file->getParentDirectory();
    if (impl_->unsavedFolder && impl_->unsavedFolder->isDirectory())
        return *impl_->unsavedFolder;
    return std::nullopt;
}

const model::BrowserIndexFile& Session::browserIndex() const
{
    return impl_->indexer.index();
}

bool Session::isIndexing() const
{
    return impl_->indexer.isIndexing();
}

void Session::refreshBrowser(const juce::File& folder)
{
    if (folder == juce::File())
        impl_->updateBrowserPlaces(); // the project's folder may have come or moved
    else
        impl_->indexer.refresh(folder);
}

void Session::waitForBrowserIndex()
{
    impl_->indexer.waitUntilIdle();
}

std::vector<model::BrowserItem> Session::browserItems(const std::set<std::string>& favourites)
{
    std::vector<model::BrowserItem> items;
    auto& library = presetLibrary();
    // Our modules and their presets, then the plugins.
    for (const auto& d : availableDevices())
    {
        if (!d.native)
        {
            items.push_back({"plugin:" + d.key,
                             model::BrowserKind::Plugin,
                             d.name,
                             d.category,
                             {},
                             d.instrument ? "instrument" : "effect",
                             favourites.contains("plugin:" + d.key)});
            continue;
        }
        items.push_back({"module:" + d.key,
                         model::BrowserKind::Module,
                         d.name,
                         d.category,
                         {},
                         {},
                         favourites.contains("module:" + d.key)});
        const auto* factory = findDevice(d.key.substr(3));
        if (factory == nullptr)
            continue;
        const auto& info = factory->info();
        for (const auto* p : library.presetsFor(info))
        {
            if (p->source == Preset::Source::Builtin)
                continue;
            auto words = p->description + " " + p->author + " " + p->folder;
            items.push_back({"preset:" + d.key + "|" + p->key(), model::BrowserKind::Preset,
                             p->source == Preset::Source::Default ? "Your default" : p->name, d.name, p->tags,
                             std::move(words), library.isFavourite(info.id, p->key())});
        }
    }
    // The files of the places, and their folders; the Library's say so ("Library · Acoustic").
    std::set<std::string> folders;
    const auto shipped = libraryFolder();
    const auto from = [&shipped](const juce::File& folder)
    {
        if (shipped == juce::File() || (folder != shipped && !folder.isAChildOf(shipped)))
            return folder.getFileName().toStdString();
        return folder == shipped ? std::string("Library") : "Library · " + folder.getFileName().toStdString();
    };
    for (const auto& [place, files] : browserIndex().places)
    {
        const auto root = fileOf(place);
        for (const auto& f : files)
        {
            const auto file = fileOf(f.path);
            auto parent = file.getParentDirectory();
            const auto id = "file:" + f.path;
            items.push_back({id,
                             f.kind,
                             file.getFileName().toStdString(),
                             from(parent),
                             {},
                             file.getRelativePathFrom(root).toStdString(),
                             favourites.contains(id)});
            for (; parent != root && parent.isAChildOf(root); parent = parent.getParentDirectory())
                folders.insert(text(parent));
        }
    }
    for (const auto& path : folders)
    {
        const auto folder = fileOf(path);
        items.push_back({"folder:" + path,
                         model::BrowserKind::Folder,
                         folder.getFileName().toStdString(),
                         from(folder.getParentDirectory()),
                         {},
                         {},
                         favourites.contains("folder:" + path)});
    }
    return items;
}

// ---- previews ------------------------------------------------------------------------------

std::optional<Session::Preview> Session::previewFile(const juce::File& file)
{
    stopPreview();
    const auto read = FileIndexer::read(file);
    if (!read || read->kind != model::BrowserKind::Sample || read->sampleRate <= 0.0)
        return std::nullopt;
    Preview p{read->seconds, read->sampleRate, read->channels, read->tempo, false};
    // In time (§7): while the song plays, a loop of a whole number of beats at the song's tempo
    // (two or more) starts at once, from the point that lines up with the song's bar, and loops.
    std::optional<double> loopFrom;
    if (isPlaying() && read->seconds > 0.0)
    {
        const auto tempo = snapshot()->tempo;
        const double beatSeconds = 60.0 / tempo.bpm;
        const double beats = read->seconds / beatSeconds;
        const double whole = std::round(beats);
        if (whole >= 2.0 && std::abs(beats - whole) * beatSeconds <= std::max(0.01, 0.005 * read->seconds))
        {
            const double songBeats = position() / beatSeconds;
            loopFrom = std::fmod(songBeats, whole) * read->seconds / whole;
            p.inTime = true;
        }
    }
    if (!impl_->preview.play(file, loopFrom))
        return std::nullopt;
    return p;
}

bool Session::previewPreset(const std::string& deviceId, const std::string& presetKey)
{
    stopPreview();
    const auto* factory = findDevice(deviceId);
    if (factory == nullptr)
        return false;
    const auto& info = factory->info();
    const auto* preset = presetLibrary().find(info, presetKey);
    if (preset == nullptr || presetTooNew(info, *preset))
        return false;
    const double rate = sampleRate() > 0.0 ? sampleRate() : 48000.0;
    auto sound = renderPresetPhrase(info, preset->state, preset->tags, rate);
    if (sound.getNumSamples() == 0)
        return false;
    impl_->preview.play(std::move(sound), rate);
    return true;
}

bool Session::previewMultisample(const juce::File& file)
{
    stopPreview();
    const auto* factory = findDevice("sampler");
    if (factory == nullptr)
        return false;
    // Its own Sampler with the file and what it sets; the phrase is played through another made
    // from that state, which finds the samples already decoded while this one holds them.
    auto device = factory->create();
    device->readExtraState({{"sample.path", file.getFullPathName().toStdString()}});
    const auto* sampler = dynamic_cast<const devices::Sampler*>(device.get());
    const auto map = sampler != nullptr ? sampler->map() : nullptr;
    if (map == nullptr || map->zones.empty())
        return false;
    for (const auto& [param, value] : devices::Sampler::settingsFor(*map))
        device->params().set(param, value);
    const double rate = sampleRate() > 0.0 ? sampleRate() : 48000.0;
    auto sound = renderPresetPhrase(factory->info(), captureState(*device), {}, rate);
    if (sound.getNumSamples() == 0)
        return false;
    impl_->preview.play(std::move(sound), rate);
    return true;
}

bool Session::previewMidiFile(const juce::File& file, std::uint64_t trackId)
{
    stopPreview();
    if (trackId == 0 && impl_->midiTarget)
        trackId = *impl_->midiTarget;
    auto* track = impl_->track(trackId);
    if (track == nullptr || !instrumentOf(trackId))
        return false;
    juce::FileInputStream in(file);
    juce::MidiFile midi;
    if (!in.openedOk() || !midi.readFrom(in) || midi.getTimeFormat() <= 0)
        return false;
    // Notes in beats, played at the song's tempo, as a drop would place them.
    const double beatSeconds = 60.0 / snapshot()->tempo.bpm;
    const double ticks = midi.getTimeFormat();
    auto& events = impl_->midiPreview.events;
    events.clear();
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* e : *midi.getTrack(t))
            if (e->message.isNoteOnOrOff())
                events.emplace_back(e->message.getTimeStamp() / ticks * beatSeconds, e->message);
    std::stable_sort(events.begin(), events.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    // Up to 16 bars: a preview, not the whole song.
    const double limit = 16.0 * snapshot()->tempo.beatsPerBar() * beatSeconds;
    std::erase_if(events, [limit](const auto& e) { return e.first > limit && e.second.isNoteOn(); });
    if (std::none_of(events.begin(), events.end(), [](const auto& e) { return e.second.isNoteOn(); }))
        return false;
    impl_->midiPreview.track = trackId;
    impl_->midiPreview.next = 0;
    impl_->midiPreview.started = juce::Time::getMillisecondCounterHiRes() * 0.001;
    impl_->midiPreview.startTimer(2);
    return true;
}

void Session::Impl::MidiPreview::timerCallback()
{
    auto* t = impl.track(track);
    if (t == nullptr)
    {
        stop();
        return;
    }
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001 - started;
    for (; next < events.size() && events[next].first <= now; ++next)
        t->injectLiveMidiMessage(events[next].second, {});
    if (next >= events.size())
        stop();
}

void Session::Impl::MidiPreview::stop()
{
    if (!isTimerRunning() && events.empty())
        return;
    stopTimer();
    if (auto* t = impl.edit != nullptr ? impl.track(track) : nullptr)
        for (int channel = 1; channel <= 16; ++channel)
            t->injectLiveMidiMessage(juce::MidiMessage::allNotesOff(channel), {});
    events.clear();
    next = 0;
}

void Session::stopPreview()
{
    impl_->preview.stop();
    impl_->midiPreview.stop();
}

bool Session::isPreviewing() const
{
    return impl_->preview.isPlaying() || impl_->midiPreview.isTimerRunning();
}

double Session::previewPosition() const
{
    return impl_->preview.position();
}

double Session::previewLength() const
{
    return impl_->preview.length();
}

void Session::setPreviewVolume(float db)
{
    impl_->preview.setVolume(db);
}

PreviewPlayer& Session::previewPlayer()
{
    return impl_->preview;
}

// ---- putting things in ---------------------------------------------------------------------

std::optional<model::DeviceSlot> Session::instrumentOf(std::uint64_t trackId) const
{
    if (const auto* t = snapshot()->channel(trackId))
        for (const auto& d : t->devices)
            if (d.instrument)
                return d;
    return std::nullopt;
}

std::uint64_t Session::replaceInstrument(std::uint64_t trackId, const std::string& key)
{
    auto* list = impl_->plugins(trackId);
    if (list == nullptr)
        return 0;
    flushSnapshot();
    const auto old = instrumentOf(trackId);
    int index = 0;
    if (old)
    {
        const auto chain = Impl::chain(*list);
        for (size_t i = 0; i < chain.size(); ++i)
            if (chain[i]->itemID.getRawID() == old->id)
                index = static_cast<int>(i);
    }
    const auto available = availableDevices();
    const auto it =
        std::find_if(available.begin(), available.end(), [&](const auto& d) { return d.key == key; });
    const std::string name = it != available.end() ? it->name : key;
    const EditGroup group(*this, old ? "Replace " + old->name + " with " + name : "Add " + name);
    if (old)
        removeDevice(old->id);
    return addDevice(trackId, key, index);
}

std::optional<Session::LoadedSample> Session::loadSample(std::uint64_t deviceId, const juce::File& file,
                                                         std::string* error)
{
    using devices::SampleMap;
    auto handle = openDevice(deviceId);
    if (handle == nullptr || handle->deviceId() != "sampler")
    {
        if (error != nullptr)
            *error = "isn't a Sampler";
        return std::nullopt;
    }
    // Read before it's put in, so a file that can't play changes nothing. Held until the Sampler
    // has it: one decode for both (the samples' cache).
    const auto path = file.getFullPathName().toStdString();
    std::string why;
    std::shared_ptr<const SampleMap> map;
    if (SampleMap::isSfz(path))
    {
        map = SampleMap::loadSfz(path, &why);
        if (map != nullptr && map->zones.empty())
            why = "it has no zones the Sampler can play";
    }
    else if (auto data = devices::SampleData::load(path, &why))
        map = SampleMap::one(std::move(data));
    if (map == nullptr || map->zones.empty())
    {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    }
    const EditGroup group(*this, "Load sample " + file.getFileNameWithoutExtension().toStdString());
    auto& context = handle->context();
    context.params.applyExtraState({{"sample.path", fd::library::reference(path)}}); // ADR 0055
    // What the file sets on the Sampler's controls (SPEC, S6).
    for (const auto& [param, value] : devices::Sampler::settingsFor(*map))
    {
        context.params.beginGesture(param);
        context.params.setPlain(param, value);
        context.params.endGesture(param);
    }
    LoadedSample loaded;
    loaded.seconds = map->longestSeconds();
    if (map->single && map->zones[0].sample != nullptr)
        loaded.rootKey = map->zones[0].sample->rootKey;
    loaded.zones = static_cast<int>(map->zones.size());
    loaded.missing = map->missing;
    loaded.ignored = map->ignored;
    return loaded;
}

// ---- keeping a project's files together (§12) ----------------------------------------------

Session::Collected Session::collectFiles()
{
    Collected out;
    if (!impl_->file)
        return out;
    const auto folder = impl_->file->getParentDirectory();
    const auto samples = folder.getChildFile("Samples");
    std::map<juce::String, juce::File> copied; // one copy per outside file
    std::function<void(juce::ValueTree)> visit = [&](juce::ValueTree v)
    {
        if (v.hasProperty(te::IDs::source))
        {
            const auto f =
                te::SourceFileReference::findFileFromString(*impl_->edit, v[te::IDs::source].toString());
            if (f.existsAsFile() && !f.isAChildOf(folder))
            {
                auto it = copied.find(f.getFullPathName());
                if (it == copied.end())
                {
                    (void)samples.createDirectory();
                    auto dest = samples.getChildFile(f.getFileName());
                    for (int n = 2; dest.exists(); ++n)
                        dest = samples.getChildFile(f.getFileNameWithoutExtension() + " " + juce::String(n) +
                                                    f.getFileExtension());
                    const bool ok = f.copyFileTo(dest);
                    if (ok)
                        ++out.copied;
                    else
                        out.failed.push_back(text(f));
                    it = copied.emplace(f.getFullPathName(), ok ? dest : juce::File()).first;
                }
                if (it->second != juce::File())
                    v.setProperty(te::IDs::source, it->second.getRelativePathFrom(folder),
                                  nullptr); // not an edit
            }
        }
        for (auto child : v)
            visit(child);
    };
    visit(impl_->edit->state);
    impl_->rebuildSnapshot();
    return out;
}

std::vector<juce::File> Session::unusedFiles() const
{
    std::vector<juce::File> out;
    if (isRecording())
        return out;
    const auto folder = projectFolderOnDisk();
    if (!folder)
        return out;
    for (const auto* sub : {"Recordings", "Bounces", "Samples"})
        for (const auto& f : folder->getChildFile(sub).findChildFiles(juce::File::findFiles, true))
            if (model::fileKind(text(f)) == model::BrowserKind::Sample &&
                !impl_->usedSources.contains(text(f)))
                out.push_back(f);
    std::sort(out.begin(), out.end());
    return out;
}

int Session::removeUnusedFiles(const std::vector<juce::File>& files,
                               const std::function<bool(const juce::File&)>& remove)
{
    const auto unused = unusedFiles();
    int removed = 0;
    for (const auto& f : files)
        if (std::find(unused.begin(), unused.end(), f) != unused.end() &&
            (remove ? remove(f) : f.moveToTrash()))
            ++removed;
    if (removed > 0)
        impl_->indexer.refresh(files.front());
    return removed;
}
} // namespace fd::engine
