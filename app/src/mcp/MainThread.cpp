#include "mcp/MainThread.h"

#include <juce_events/juce_events.h>

#include <condition_variable>
#include <exception>
#include <mutex>

namespace fd::mcp
{
struct MainThread::Shared
{
    std::atomic<bool> open{true};
};

namespace
{
struct Job
{
    std::mutex mutex;
    std::condition_variable doneChanged;
    bool done = false;
    bool ran = false;
    std::exception_ptr error;
};
} // namespace

MainThread::MainThread() : shared_(std::make_shared<Shared>()) {}

MainThread::~MainThread()
{
    shared_->open = false;
}

bool MainThread::isOpen() const noexcept
{
    return shared_->open;
}

void MainThread::close()
{
    shared_->open = false;
}

void MainThread::reopen()
{
    shared_ = std::make_shared<Shared>();
}

bool MainThread::run(const std::function<void()>& work)
{
    if (!shared_->open)
        return false;
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        work();
        return true;
    }
    auto job = std::make_shared<Job>();
    // `work` lives on this thread's stack: the job only touches it while this call waits (a
    // closed MainThread never runs it, and close() happens on the message thread).
    const auto* w = &work;
    const bool posted = juce::MessageManager::callAsync([job, shared = shared_, w] {
        bool ran = false;
        std::exception_ptr error;
        if (shared->open)
        {
            try
            {
                (*w)();
            }
            catch (...)
            {
                error = std::current_exception();
            }
            ran = true;
        }
        {
            std::lock_guard lock(job->mutex);
            job->done = true;
            job->ran = ran;
            job->error = error;
        }
        job->doneChanged.notify_all();
    });
    if (!posted)
        return false;
    std::unique_lock lock(job->mutex);
    while (!job->done)
    {
        if (!shared_->open)
            return false;
        job->doneChanged.wait_for(lock, std::chrono::milliseconds(20));
    }
    if (job->error)
        std::rethrow_exception(job->error);
    return job->ran;
}
} // namespace fd::mcp
