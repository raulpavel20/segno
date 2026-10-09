#pragma once

#include <fd/device/Device.h>
#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>
#include <tracktion_engine/tracktion_engine.h>

namespace fd::engine
{
namespace te = tracktion;

/// DAW adapter (docs/architecture.md): one tracktion_engine plugin type that wraps any
/// fd::Device, so every device runs natively in the DAW, in the docked device chain.
///
/// Parameters become te::AutomatableParameters (automation, modifiers and macros for free) with
/// the same normalised mapping as ParamSpec. Their plain values are pulled into the device's
/// ParamStore before every block. All tracktion API use stays in app/src/engine (ADR 0004).
class DevicePlugin final : public te::Plugin
{
public:
    /// Plugin type name for a device: "fd.<device-id>".
    static juce::String typeNameFor(std::string_view deviceId);

    DevicePlugin(te::PluginCreationInfo info, const DeviceFactory& factory);
    ~DevicePlugin() override;

    juce::String getName() const override { return name_; }
    juce::String getPluginType() override { return type_; }
    juce::String getSelectableDescription() override { return name_; }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& context) override;

    BusLayout getBusses() const override;
    /// A device with a sidechain bus lists its channels after the main ones, which is how
    /// tracktion routes a sidechain source to it (ADR 0025).
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override;
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool isSynth() override { return isInstrument(); }
    bool takesMidiInput() override { return device_->info().buses.acceptsMidi; }
    bool producesAudioWhenNoAudioInput() override { return isInstrument(); }
    double getLatencySeconds() override;
    double getTailLength() const override { return device_->tailSeconds(); }

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;
    void flushPluginStateToValueTree() override;
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id) override;

    /// UI edit of non-parameter state (ADR 0017): recorded in the plugin's state with the Edit's
    /// undo manager; the device follows the state (including undo and redo).
    void applyExtraState(const nlohmann::json& patch);

    [[nodiscard]] Device& device() noexcept { return *device_; }
    [[nodiscard]] te::AutomatableParameter& parameter(size_t index) const noexcept { return *params_[index]; }

private:
    [[nodiscard]] bool isInstrument() const noexcept { return device_->info().kind == DeviceKind::Instrument; }
    void convertMidi(const te::PluginRenderContext& fc) noexcept;
    void applyExtraFromState(const juce::String& json);

    std::unique_ptr<Device> device_;
    juce::String name_, type_;
    std::vector<te::AutomatableParameter::Ptr> params_;
    std::vector<std::unique_ptr<juce::CachedValue<float>>> values_;
    std::vector<float*> channelPtrs_, sidechainPtrs_;
    EventList events_;
    juce::AudioBuffer<float> instrumentOut_; // instruments render here, then add (ADR 0019)
    te::tempo::Sequence::Position tempoPos_{te::createPosition(edit.tempoSequence)};
    double sampleRate_ = 48000.0;
    juce::String appliedExtra_; // the extra-state JSON the device currently reflects

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DevicePlugin)
};

/// Registers every device in fd::allDevices() as a built-in plugin type.
void registerDevices(te::Engine& engine);

/// UI binding for a native device: edits go through AutomatableParameter (automation
/// recording and undo work).
class DevicePluginBinding final : public ui::ParamBinding
{
public:
    explicit DevicePluginBinding(DevicePlugin& plugin) : plugin_(plugin) {}

    float getPlain(size_t i) const override { return plugin_.parameter(i).getCurrentValue(); }
    void setPlain(size_t i, float plain) override { plugin_.parameter(i).setParameter(plain, juce::sendNotification); }
    void beginGesture(size_t i) override { plugin_.parameter(i).parameterChangeGestureBegin(); }
    void endGesture(size_t i) override { plugin_.parameter(i).parameterChangeGestureEnd(); }
    void applyExtraState(const nlohmann::json& patch) override { plugin_.applyExtraState(patch); }

private:
    DevicePlugin& plugin_;
};
} // namespace fd::engine
