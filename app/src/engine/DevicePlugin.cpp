#include "DevicePlugin.h"

#include <fd/device/Realtime.h>
#include <fd/device/State.h>

namespace fd::engine
{
namespace
{
const juce::Identifier kExtraStateId{"fdExtra"};

class DeviceBuiltInType final : public te::PluginManager::BuiltInType
{
public:
    explicit DeviceBuiltInType(const DeviceFactory& factory)
        : BuiltInType(DevicePlugin::typeNameFor(factory.info().id)), factory_(factory)
    {
    }

    te::Plugin::Ptr create(te::PluginCreationInfo info) override { return new DevicePlugin(info, factory_); }

private:
    const DeviceFactory& factory_;
};
} // namespace

juce::String DevicePlugin::typeNameFor(std::string_view deviceId)
{
    return "fd." + juce::String(std::string(deviceId));
}

DevicePlugin::DevicePlugin(te::PluginCreationInfo info, const DeviceFactory& factory)
    : te::Plugin(info), device_(factory.create()), name_(std::string(factory.info().name)),
      type_(typeNameFor(factory.info().id))
{
    auto* um = getUndoManager();
    for (const auto& spec : device_->info().params)
    {
        const juce::String id(std::string(spec.id));
        auto value = std::make_unique<juce::CachedValue<float>>();
        value->referTo(state, juce::Identifier(id), um, spec.def);

        // Same normalised mapping and text as ParamSpec (and the plugin adapter).
        juce::NormalisableRange<float> range(
            spec.min, spec.max, [&spec](float, float, float n) { return spec.fromNormalized(n); },
            [&spec](float, float, float v) { return spec.toNormalized(v); },
            [&spec](float, float, float v) { return spec.sanitize(v); });
        auto* param = addParam(
            id, juce::String(std::string(spec.name)), range,
            [&spec](float v) { return juce::String(spec.toText(v)); },
            [&spec](const juce::String& s) {
                const auto v = spec.fromText(s.toStdString());
                return v ? *v : spec.def;
            });
        param->attachToCurrentValue(*value);
        params_.emplace_back(param);
        values_.push_back(std::move(value));
    }
}

DevicePlugin::~DevicePlugin()
{
    notifyListenersOfDeletion();
    for (auto& p : params_)
        p->detachFromCurrentValue();
}

void DevicePlugin::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate_ = info.sampleRate;
    for (size_t i = 0; i < params_.size(); ++i)
        device_->params().set(i, params_[i]->getCurrentValue());
    device_->prepare({.sampleRate = info.sampleRate, .maxBlockSize = info.blockSizeSamples});
    events_.reserve(2048);
    channelPtrs_.resize(8);
}

void DevicePlugin::reset()
{
    device_->reset();
}

te::Plugin::BusLayout DevicePlugin::getBusses() const
{
    const auto& buses = device_->info().buses;
    if (buses.inputChannels == 0)
        return BusLayout::singleInOut(te::ChannelConfiguration::none(), te::ChannelConfiguration::stereo());
    return BusLayout::singleStereoInOut();
}

double DevicePlugin::getLatencySeconds()
{
    return sampleRate_ > 0.0 ? device_->latencySamples() / sampleRate_ : 0.0;
}

void DevicePlugin::applyToBuffer(const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || !isEnabled())
        return;

    auto& store = device_->params();
    for (size_t i = 0; i < params_.size(); ++i)
        store.set(i, params_[i]->getCurrentValue()); // includes automation and modifiers

    const int channels = juce::jmin(fc.destBuffer->getNumChannels(), static_cast<int>(channelPtrs_.size()));
    for (int c = 0; c < channels; ++c)
        channelPtrs_[static_cast<size_t>(c)] = fc.destBuffer->getWritePointer(c, fc.bufferStartSample);

    events_.clear(); // MIDI for instruments arrives with the first instrument (M1)

    ProcessContext ctx;
    ctx.input = {channelPtrs_.data(), channels};
    ctx.output = {channelPtrs_.data(), channels};
    ctx.events = &events_;
    ctx.numSamples = fc.bufferNumSamples;
    ctx.transport.playing = fc.isPlaying;
    processRealtime(*device_, ctx);
}

void DevicePlugin::restorePluginStateFromValueTree(const juce::ValueTree& v)
{
    for (auto& value : values_)
        if (v.hasProperty(value->getPropertyID()))
            *value = static_cast<float>(v[value->getPropertyID()]);
    for (auto& p : params_)
        p->updateFromAttachedValue();

    if (v.hasProperty(kExtraStateId))
    {
        const auto parsed = nlohmann::json::parse(v[kExtraStateId].toString().toStdString(), nullptr, false);
        if (parsed.is_object())
            device_->readExtraState(parsed);
    }
}

void DevicePlugin::flushPluginStateToValueTree()
{
    te::Plugin::flushPluginStateToValueTree();
    nlohmann::json extra = nlohmann::json::object();
    device_->writeExtraState(extra);
    if (!extra.empty())
        state.setProperty(kExtraStateId, juce::String(extra.dump()), nullptr);
}

void registerDevices(te::Engine& engine)
{
    for (const auto* factory : allDevices())
        engine.getPluginManager().registerBuiltInType(std::make_unique<DeviceBuiltInType>(*factory));
}
} // namespace fd::engine
