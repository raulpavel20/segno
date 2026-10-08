#pragma once

#include <fd/device/Device.h>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <fd_host_juce/fd_host_juce.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace fd::plugin
{
/// Plugin adapter (docs/architecture.md): wraps any fd::Device as a JUCE AudioProcessor.
/// JUCE parameters are built from the device's ParamSpec table using the *same* normalised
/// mapping and text conversion, so hosts, presets and the DAW all agree.
class DeviceAudioProcessor final : public juce::AudioProcessor
{
public:
    DeviceAudioProcessor(const DeviceFactory& factory, const ui::PanelFactory* panel);

    // juce::AudioProcessor
    const juce::String getName() const override { return name_; }
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return panel_ != nullptr; }

    bool acceptsMidi() const override { return device_->info().buses.acceptsMidi; }
    bool producesMidi() const override { return device_->info().buses.producesMidi; }
    bool isMidiEffect() const override { return device_->info().kind == DeviceKind::MidiEffect; }
    double getTailLengthSeconds() const override { return device_->tailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    /// UI edit of non-parameter state (ADR 0017): applies it and tells the host.
    void applyExtraState(const nlohmann::json& patch);
    void setStateInformation(const void* data, int sizeInBytes) override;

    // For the editor.
    [[nodiscard]] Device& device() noexcept { return *device_; }
    [[nodiscard]] const ui::PanelFactory* panelFactory() const noexcept { return panel_; }
    [[nodiscard]] juce::RangedAudioParameter& parameter(size_t index) const noexcept { return *params_[index]; }

private:
    static BusesProperties busesFor(const DeviceInfo& info);
    void pullParameters() noexcept;

    std::unique_ptr<Device> device_;
    const ui::PanelFactory* panel_;
    juce::String name_;
    std::vector<juce::RangedAudioParameter*> params_; // owned by AudioProcessor
    EventList events_;
    std::vector<float*> channelPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceAudioProcessor)
};

/// Editor: the device's DevicePanel, the same one the DAW uses.
class DeviceEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DeviceEditor(DeviceAudioProcessor& processor);
    ~DeviceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Binding;

    ui::LookAndFeel lnf_;
    std::unique_ptr<Binding> binding_;
    std::unique_ptr<ui::DeviceUiContext> context_;
    std::unique_ptr<ui::DevicePanel> panel_;
};
} // namespace fd::plugin
