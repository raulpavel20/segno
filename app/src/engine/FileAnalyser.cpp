#include "engine/FileAnalyser.h"

#include "model/Browser.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace fd::engine
{
namespace
{
/// The root note a file's loop metadata gives (§8.2): a WAV's acid chunk, an AIFF's, when it's set.
std::optional<int> metadataRoot(const juce::File& file)
{
    thread_local const auto formats = []
    {
        auto m = std::make_unique<juce::AudioFormatManager>();
        m->registerBasicFormats();
        return m;
    }();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats->createReaderFor(file));
    if (reader == nullptr)
        return std::nullopt;
    const auto& meta = reader->metadataValues;
    if (meta.getValue(juce::WavAudioFormat::acidRootSet, "0").getIntValue() != 0)
        return meta.getValue(juce::WavAudioFormat::acidRootNote, "0").getIntValue();
    if (meta.getValue(juce::AiffAudioFormat::appleRootSet, "0").getIntValue() != 0)
        return meta.getValue(juce::AiffAudioFormat::appleKey, "0").getIntValue();
    return std::nullopt;
}

/// What's found in a file, its key from its metadata and name as well as its sound.
std::shared_ptr<const model::FileAnalysis> analyse(const std::string& path,
                                                   const std::atomic<bool>* stop = nullptr)
{
    auto found = model::analyseFile(path, nullptr, stop);
    if (!found)
        return nullptr;
    const juce::File file(path);
    found->key = model::fileKey(metadataRoot(file),
                                model::keyFromName(file.getFileNameWithoutExtension().toStdString()),
                                &found->pitchClasses);
    return std::make_shared<const model::FileAnalysis>(std::move(*found));
}
} // namespace

FileAnalyser::FileAnalyser() : juce::Thread("Segno file analysis")
{
    startThread(juce::Thread::Priority::low);
}

FileAnalyser::~FileAnalyser()
{
    alive_->store(false);
    stopping_.store(true); // a long file's analysis stops where it is
    signalThreadShouldExit();
    notify();
    stopThread(10000);
}

std::shared_ptr<const model::FileAnalysis> FileAnalyser::get(const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    const auto modified = file.getLastModificationTime();
    {
        std::lock_guard lock(mutex_);
        auto& e = entries_[path];
        // Changed on disk since: found again.
        if (e.modified != modified && !e.queued)
        {
            e = Entry{modified, nullptr, false, false};
        }
        if (e.found != nullptr || e.failed)
            return e.found;
        if (e.queued)
            return nullptr;
        e.queued = true;
        queue_.push_back(path);
        ++pending_;
    }
    notify();
    return nullptr;
}

std::shared_ptr<const model::FileAnalysis> FileAnalyser::peek(const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(path);
    return it != entries_.end() && it->second.modified == file.getLastModificationTime() ? it->second.found
                                                                                         : nullptr;
}

std::shared_ptr<const model::FileAnalysis> FileAnalyser::now(const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    const auto modified = file.getLastModificationTime();
    {
        std::lock_guard lock(mutex_);
        if (const auto it = entries_.find(path); it != entries_.end() && it->second.modified == modified &&
                                                 (it->second.found != nullptr || it->second.failed))
            return it->second.found;
    }
    const auto shared = analyse(path);
    std::lock_guard lock(mutex_);
    auto& e = entries_[path];
    if (!e.queued) // the background's, when it's on its way, lands as well
    {
        e.modified = modified;
        e.found = shared;
        e.failed = shared == nullptr;
    }
    return shared;
}

void FileAnalyser::waitUntilIdle()
{
    while (pending_.load() > 0)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        else
            juce::Thread::sleep(5);
    }
}

void FileAnalyser::run()
{
    while (!threadShouldExit())
    {
        std::string path;
        {
            std::lock_guard lock(mutex_);
            if (!queue_.empty())
            {
                path = queue_.front();
                queue_.pop_front();
            }
        }
        if (path.empty())
        {
            wait(-1);
            continue;
        }
        const auto shared = analyse(path, &stopping_);
        if (stopping_.load())
            break;
        {
            std::lock_guard lock(mutex_);
            auto& e = entries_[path];
            e.queued = false;
            e.found = shared;
            e.failed = shared == nullptr;
        }
        juce::MessageManager::callAsync(
            [this, alive = alive_, path]
            {
                if (!alive->load())
                    return;
                --pending_;
                if (onFound)
                    onFound(path);
            });
    }
}
} // namespace fd::engine
