#include "engine/PreviewPlayer.h"

namespace fd::engine
{
PreviewPlayer::PreviewPlayer()
{
    formats_.registerBasicFormats();
    readAhead_.startThread(juce::Thread::Priority::normal);
    transport_.prepareToPlay(blockSize_, sampleRate_);
}

PreviewPlayer::~PreviewPlayer()
{
    transport_.setSource(nullptr);
    readAhead_.stopThread(2000);
}

void PreviewPlayer::setSource(juce::PositionableAudioSource* source, double sourceRate)
{
    // The transport drops the old source under its lock before the old one is freed.
    transport_.setSource(source, source != nullptr ? 32768 : 0, source != nullptr ? &readAhead_ : nullptr,
                         sourceRate, 2);
}

bool PreviewPlayer::play(const juce::File& file, std::optional<double> loopFrom)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return false;
    const double rate = reader->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    source->setLooping(loopFrom.has_value());
    setSource(source.get(), rate);
    source_ = std::move(source);
    transport_.setPosition(loopFrom.value_or(0.0));
    transport_.start();
    return true;
}

void PreviewPlayer::play(juce::AudioBuffer<float> sound, double sampleRate)
{
    auto source = std::make_unique<juce::MemoryAudioSource>(sound, true, false);
    setSource(source.get(), sampleRate);
    source_ = std::move(source);
    transport_.setPosition(0.0);
    transport_.start();
}

void PreviewPlayer::stop()
{
    transport_.stop();
    setSource(nullptr, 0.0);
    source_.reset();
}

bool PreviewPlayer::isPlaying() const
{
    return transport_.isPlaying();
}

double PreviewPlayer::position() const
{
    return transport_.getCurrentPosition();
}

double PreviewPlayer::length() const
{
    return transport_.getLengthInSeconds();
}

void PreviewPlayer::setVolume(float db)
{
    gain_ = juce::Decibels::decibelsToGain(db);
}

void PreviewPlayer::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, blockSize);
    scratch_.setSize(2, std::max(blockSize_, 8192)); // room for a device that sends more than it said
    transport_.prepareToPlay(blockSize_, sampleRate_);
}

void PreviewPlayer::renderNextBlock(juce::AudioBuffer<float>& out)
{
    out.clear();
    if (!transport_.isPlaying())
        return;
    transport_.getNextAudioBlock(juce::AudioSourceChannelInfo(out));
    out.applyGain(gain_.load());
}

void PreviewPlayer::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs,
                                                     int numOutputs, int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // The device manager adds every callback's output: write ours, silence when there's nothing.
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear(outputs[c], numSamples);
    if (numOutputs == 0 || !transport_.isPlaying())
        return;
    if (scratch_.getNumSamples() < numSamples)
        return; // a block larger than the device said (never, but no allocation here)
    juce::AudioBuffer<float> block(scratch_.getArrayOfWritePointers(), 2, numSamples);
    renderNextBlock(block);
    // Stereo onto the first two outputs; a mono device gets both sides.
    for (int c = 0; c < std::min(numOutputs, 2); ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::copy(outputs[c], block.getReadPointer(c), numSamples);
    if (numOutputs == 1 && outputs[0] != nullptr)
        juce::FloatVectorOperations::add(outputs[0], block.getReadPointer(1), numSamples);
}

void PreviewPlayer::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void PreviewPlayer::audioDeviceStopped()
{
    transport_.releaseResources();
}
} // namespace fd::engine
