#include "engine/FileIndexer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace fd::engine
{
namespace
{
std::string text(const juce::File& f)
{
    return f.getFullPathName().toStdString();
}

bool hidden(const juce::File& f)
{
    return f.getFileName().startsWithChar('.');
}
} // namespace

FileIndexer::FileIndexer() : juce::Thread("Segno file index")
{
    startThread(juce::Thread::Priority::low);
}

FileIndexer::~FileIndexer()
{
    alive_->store(false);
    signalThreadShouldExit();
    notify();
    stopThread(10000);
}

void FileIndexer::setIndexFile(const juce::File& file)
{
    indexFile_ = file;
    if (file != juce::File() && file.existsAsFile())
        index_ = model::browserIndexFromJson(file.loadFileAsString().toStdString());
}

void FileIndexer::setPlaces(std::vector<juce::File> places, std::vector<juce::File> excluded)
{
    places_ = std::move(places);
    {
        const std::lock_guard lock(mutex_);
        excluded_ = std::move(excluded);
    }
    bool dropped = false;
    for (auto it = index_.places.begin(); it != index_.places.end();)
    {
        const bool listed = std::any_of(places_.begin(), places_.end(),
                                        [&](const juce::File& p) { return text(p) == it->first; });
        if (listed)
            ++it;
        else
        {
            it = index_.places.erase(it);
            dropped = true;
        }
    }
    if (dropped)
    {
        save();
        if (onChanged)
            onChanged();
    }
    refresh();
}

void FileIndexer::refresh(const juce::File& folder)
{
    {
        const std::lock_guard lock(mutex_);
        for (const auto& place : places_)
            if (folder == juce::File() || folder == place || folder.isAChildOf(place))
                if (std::find(work_.begin(), work_.end(), place) == work_.end())
                {
                    work_.push_back(place);
                    known_[text(place)] = index_.places.count(text(place)) != 0
                                              ? index_.places.at(text(place))
                                              : std::vector<model::IndexedFile>{};
                }
        if (!work_.empty())
            busy_ = true;
    }
    notify();
}

void FileIndexer::waitUntilIdle()
{
    while (isIndexing())
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

std::optional<model::IndexedFile> FileIndexer::read(const juce::File& file)
{
    const auto kind = model::fileKind(text(file));
    if (!kind || !file.existsAsFile())
        return std::nullopt;
    model::IndexedFile f;
    f.path = text(file);
    f.kind = *kind;
    f.size = file.getSize();
    f.modified = file.getLastModificationTime().toMilliseconds();
    if (*kind != model::BrowserKind::Sample)
        return f;
    thread_local const auto formats = []
    {
        auto m = std::make_unique<juce::AudioFormatManager>();
        m->registerBasicFormats();
        return m;
    }();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats->createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return f; // listed, without what it couldn't read
    f.sampleRate = reader->sampleRate;
    f.channels = static_cast<int>(reader->numChannels);
    f.seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    // A loop's tempo: WAV's acid chunk, AIFF's beat count, else the name.
    const auto& meta = reader->metadataValues;
    if (const double t = meta.getValue("acid tempo", "0").getDoubleValue(); t >= 30.0 && t <= 400.0)
        f.tempo = t;
    else if (const int beats = meta.getValue("apple beats", "0").getIntValue(); beats > 0 && f.seconds > 0.0)
        f.tempo = beats * 60.0 / f.seconds;
    if (f.tempo == 0.0)
        f.tempo = model::tempoFromName(file.getFileNameWithoutExtension().toStdString());
    return f;
}

void FileIndexer::run()
{
    while (!threadShouldExit())
    {
        juce::File place;
        std::vector<model::IndexedFile> known;
        std::vector<juce::File> excluded;
        {
            const std::lock_guard lock(mutex_);
            if (!work_.empty())
            {
                place = work_.front();
                work_.erase(work_.begin());
                known = std::move(known_[text(place)]);
                known_.erase(text(place));
                excluded = excluded_;
            }
            else
                busy_ = false;
        }
        if (place == juce::File())
        {
            wait(-1);
            continue;
        }
        std::map<std::string, const model::IndexedFile*> before;
        for (const auto& f : known)
            before[f.path] = &f;
        std::vector<model::IndexedFile> files;
        std::vector<juce::File> folders{place};
        while (!folders.empty() && !threadShouldExit())
        {
            const auto folder = folders.back();
            folders.pop_back();
            if (std::any_of(excluded.begin(), excluded.end(),
                            [&](const juce::File& e) { return folder == e; }))
                continue;
            for (const auto& entry :
                 juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories))
            {
                const auto& f = entry.getFile();
                if (hidden(f) || entry.isHidden())
                    continue;
                if (entry.isDirectory())
                {
                    folders.push_back(f);
                    continue;
                }
                if (!model::fileKind(text(f)))
                    continue;
                const auto it = before.find(text(f));
                if (it != before.end() && it->second->size == entry.getFileSize() &&
                    it->second->modified == entry.getModificationTime().toMilliseconds())
                    files.push_back(*it->second); // unchanged: not read again
                else if (auto read = FileIndexer::read(f))
                    files.push_back(std::move(*read));
            }
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        if (!threadShouldExit())
            deliver(text(place), std::move(files));
    }
}

void FileIndexer::deliver(const std::string& place, std::vector<model::IndexedFile> files)
{
    ++pending_; // until it arrives, the work isn't done
    juce::MessageManager::callAsync(
        [this, alive = alive_, place, found = std::move(files)]() mutable
        {
            if (!alive->load())
                return;
            --pending_;
            const bool listed = std::any_of(places_.begin(), places_.end(),
                                            [&](const juce::File& p) { return text(p) == place; });
            if (listed && (index_.places.count(place) == 0 || index_.places.at(place) != found))
            {
                index_.places[place] = std::move(found);
                save();
                if (onChanged)
                    onChanged();
            }
        });
}

void FileIndexer::save() const
{
    if (indexFile_ == juce::File())
        return;
    (void)indexFile_.getParentDirectory().createDirectory();
    const auto temp = indexFile_.getSiblingFile(indexFile_.getFileName() + ".tmp");
    if (temp.replaceWithText(model::toJson(index_)))
        (void)temp.moveFileTo(indexFile_);
}
} // namespace fd::engine
