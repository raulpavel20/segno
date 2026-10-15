#pragma once

// An audio device for tests with a cable from its first output to its first input (measuring the
// round trip, recording.md §22.2). Unlike tracktion's hosted device, its inputs and outputs are
// separate buffers, as an interface's are, so every callback on it reads the input. The engine
// plays through it as through an interface; a test steps it a block at a time.

#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace fd::testing
{
class LoopDevice final : public juce::AudioIODevice
{
public:
    static constexpr double rate = 48000.0;
    static constexpr int block = 480;

    LoopDevice() : juce::AudioIODevice("Loop", "Loop") {}

    /// The cable: output 1 comes back into input 1 this many samples later, `gain` times as loud
    /// (none: nothing comes back).
    std::optional<int> cable;
    float gain = 1.0f;
    /// The latency the device reports (each way).
    int reportedIn = 0, reportedOut = 0;

    /// One block through: the cable's return in, the callbacks run, output 1 kept for the cable.
    void step()
    {
        juce::AudioBuffer<float> in(2, block), out(2, block);
        in.clear();
        out.clear();
        if (cable)
            for (int i = 0; i < block; ++i)
                if (const auto at = static_cast<std::int64_t>(sent_.size()) + i - *cable;
                    at >= 0 && at < static_cast<std::int64_t>(sent_.size()))
                    in.setSample(0, i, gain * sent_[static_cast<size_t>(at)]);
        if (callback_ != nullptr)
            callback_->audioDeviceIOCallbackWithContext(in.getArrayOfReadPointers(), 2,
                                                        out.getArrayOfWritePointers(), 2, block, {});
        for (int i = 0; i < block; ++i)
            sent_.push_back(out.getSample(0, i));
    }

    juce::StringArray getOutputChannelNames() override { return {"Out 1", "Out 2"}; }
    juce::StringArray getInputChannelNames() override { return {"In 1", "In 2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {rate}; }
    juce::Array<int> getAvailableBufferSizes() override { return {block}; }
    int getDefaultBufferSize() override { return block; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback* callback) override
    {
        callback_ = callback;
        if (callback_ != nullptr)
            callback_->audioDeviceAboutToStart(this);
    }
    void stop() override
    {
        if (callback_ != nullptr)
            callback_->audioDeviceStopped();
        callback_ = nullptr;
    }
    bool isPlaying() override { return callback_ != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 3; }
    int getOutputLatencyInSamples() override { return reportedOut; }
    int getInputLatencyInSamples() override { return reportedIn; }
    bool hasControlPanel() const override { return false; }
    bool showControlPanel() override { return false; }
    bool setAudioPreprocessingEnabled(bool) override { return false; }

private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    std::vector<float> sent_;
};

class LoopDeviceType final : public juce::AudioIODeviceType
{
public:
    LoopDeviceType() : juce::AudioIODeviceType("Loop") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Loop"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override
    {
        device = new LoopDevice();
        return device;
    }
    LoopDevice* device = nullptr; // the device manager owns it
};

/// Makes the loop device the engine's interface, with two inputs and two outputs; returns it.
inline LoopDevice& useLoopDevice(tracktion::Engine& engine)
{
    auto& dm = engine.getDeviceManager();
    auto type = std::make_unique<LoopDeviceType>();
    auto* loop = type.get();
    dm.deviceManager.addAudioDeviceType(std::move(type));
    dm.deviceManager.setCurrentAudioDeviceType("Loop", true);
    dm.initialise(2, 2);
    jassert(loop->device != nullptr);
    return *loop->device;
}
} // namespace fd::testing
