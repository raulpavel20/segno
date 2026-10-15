#include "engine/RoundTrip.h"

#include <tracktion_engine/tracktion_engine.h>
// tracktion's round-trip test: its signal and the search for it coming back.
#include <tracktion_engine/testing/tracktion_RoundTripLatency.h>

namespace fd::engine
{
namespace tu = tracktion::test_utilities;

RoundTrip::RoundTrip(juce::AudioDeviceManager& devices, int output, int input,
                     std::function<void(int)> progress, std::function<void(model::RoundTripResult)> done)
    : devices_(devices), output_(output), input_(input), progress_(std::move(progress)),
      done_(std::move(done))
{
    sound_ = tu::createSyncTestSound();
    sound_.applyGain(level);
    devices_.addAudioCallback(this); // audioDeviceAboutToStart: the rate and the reported latency
    // Made once, long enough for the signal and a round trip of a second: the audio thread writes
    // into it, so it's never made again while a try may run.
    recorded_.setSize(1, sound_.getNumSamples() + static_cast<int>(std::max(sampleRate_, 48000.0)));
    startTry();
}

RoundTrip::~RoundTrip()
{
    *alive_ = false;
    stopTimer();
    devices_.removeAudioCallback(this);
}

void RoundTrip::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate_ = device->getCurrentSampleRate();
    reported_ = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
}

void RoundTrip::startTry()
{
    recorded_.clear();
    position_ = 0;
    running_ = true;
    startTimer(50);
    if (progress_)
        progress_(static_cast<int>(found_.samples.size()) + 1);
}

void RoundTrip::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                                 float* const* outputs, int numOutputs, int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear(outputs[c], numSamples);
    if (!running_.load(std::memory_order_acquire))
        return;
    const int at = position_.load(std::memory_order_relaxed);
    const int length = recorded_.getNumSamples();
    const int n = std::min(numSamples, length - at);
    if (n <= 0)
        return;
    if (input_ < numInputs && inputs[input_] != nullptr)
        juce::FloatVectorOperations::copy(recorded_.getWritePointer(0, at), inputs[input_], n);
    if (output_ < numOutputs && outputs[output_] != nullptr)
    {
        const int played = std::clamp(sound_.getNumSamples() - at, 0, n);
        if (played > 0)
            juce::FloatVectorOperations::copy(outputs[output_], sound_.getReadPointer(0, at), played);
    }
    position_.store(at + n, std::memory_order_release);
}

void RoundTrip::timerCallback()
{
    if (position_.load(std::memory_order_acquire) < recorded_.getNumSamples())
        return;
    stopTimer();
    running_ = false;
    // The search takes a moment: on a thread of its own, with copies, back here when found.
    std::weak_ptr<bool> alive = alive_;
    juce::Thread::launch(
        [this, alive, sound = sound_, recorded = recorded_]
        {
            const auto samples = tu::findSyncDeltaSamples(sound, recorded);
            const float peak = recorded.getMagnitude(0, 0, recorded.getNumSamples());
            juce::MessageManager::callAsync(
                [this, alive, samples, peak]
                {
                    if (const auto a = alive.lock(); a != nullptr && *a)
                        tryFound(samples, peak);
                });
        });
}

void RoundTrip::tryFound(std::optional<int> samples, float peak)
{
    found_.samples.push_back(samples);
    found_.inputPeak = std::max(found_.inputPeak, peak);
    // Too loud or nothing back: no use trying again.
    if (static_cast<int>(found_.samples.size()) < tries && samples.has_value() &&
        peak < model::kRoundTripTooLoud)
    {
        startTry();
        return;
    }
    devices_.removeAudioCallback(this); // finished: nothing more to play
    auto done = std::move(done_);
    if (done)
        done(model::judgeRoundTrip(found_, reported_, sampleRate_));
}
} // namespace fd::engine
