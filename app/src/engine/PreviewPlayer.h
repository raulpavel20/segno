#pragma once

#include <atomic>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <optional>

namespace fd::engine
{
/// The browser's previews (docs/design/browser.md §7): one sound at a time, added to the output
/// after the engine's (so after the master's modules), at its own volume. It's an audio callback
/// of its own on the device, so nothing it plays is ever in a recording, a bounce or an export.
class PreviewPlayer final : public juce::AudioIODeviceCallback
{
public:
    PreviewPlayer();
    ~PreviewPlayer() override;
    PreviewPlayer(const PreviewPlayer&) = delete;
    PreviewPlayer& operator=(const PreviewPlayer&) = delete;

    /// Plays a file, read ahead on a thread of its own: once, or looping from `loopFrom` seconds
    /// in (a loop in time with the song). False if it isn't audio.
    bool play(const juce::File& file, std::optional<double> loopFrom = std::nullopt);
    /// Plays a sound made here (an instrument preset's phrase), once.
    void play(juce::AudioBuffer<float> sound, double sampleRate);
    void stop();
    [[nodiscard]] bool isPlaying() const;
    /// Where it is and how long it is, in the source's seconds.
    [[nodiscard]] double position() const;
    [[nodiscard]] double length() const;
    /// The preview's volume, in dB.
    void setVolume(float db);

    /// The next `out.getNumSamples()` samples, as the device callback would write them (tests,
    /// and the device callback itself). `prepare` first, as a device start does.
    void prepare(double sampleRate, int blockSize);
    void renderNextBlock(juce::AudioBuffer<float>& out);

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void setSource(juce::PositionableAudioSource* source, double sourceRate);

    juce::AudioFormatManager formats_;
    juce::TimeSliceThread readAhead_{"Segno preview"};
    juce::AudioTransportSource transport_;
    std::unique_ptr<juce::PositionableAudioSource> source_; // what the transport plays
    juce::AudioBuffer<float> scratch_;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    std::atomic<float> gain_{0.5f};
};
} // namespace fd::engine
