#pragma once

#include "model/RoundTrip.h"

#include <atomic>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace fd::engine
{
/// Measures the round trip (recording.md §22.2): tracktion's test signal (noise with spikes) played
/// at -12 dBFS on one of the audio device's output channels, and found on one of its input channels,
/// three times; then judged (model::judgeRoundTrip). A callback beside the engine's, so playback goes
/// on. Each try's search runs on a thread of its own. Message thread; gone, it stops.
class RoundTrip final : private juce::AudioIODeviceCallback, private juce::Timer
{
public:
    /// `output` and `input` index the device's channels in use. `progress` hears each try start
    /// (1, 2, 3); `done` the result, once.
    RoundTrip(juce::AudioDeviceManager& devices, int output, int input, std::function<void(int)> progress,
              std::function<void(model::RoundTripResult)> done);
    ~RoundTrip() override;

    static constexpr int tries = 3;
    static constexpr float level = 0.25f; // -12 dBFS

private:
    void startTry();
    void tryFound(std::optional<int> samples, float peak);

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void timerCallback() override;

    juce::AudioDeviceManager& devices_;
    const int output_, input_;
    std::function<void(int)> progress_;
    std::function<void(model::RoundTripResult)> done_;
    juce::AudioBuffer<float> sound_, recorded_; // written by the audio thread only while running_
    std::atomic<bool> running_{false};
    std::atomic<int> position_{0}; // samples played and recorded in this try
    model::RoundTripTries found_;
    double sampleRate_ = 0.0;
    int reported_ = 0; // the device's input and output latency, in samples
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
} // namespace fd::engine
