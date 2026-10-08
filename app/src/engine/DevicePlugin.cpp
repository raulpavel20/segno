#include "DevicePlugin.h"

#include <fd/device/Realtime.h>
#include <fd/device/State.h>
#include <fd_host_juce/fd_host_juce.h>

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

    // A device recreated from a saved project gets its non-parameter state back (ADR 0017).
    if (state.hasProperty(kExtraStateId))
        applyExtraFromState(state[kExtraStateId].toString());
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

    convertMidi(fc);

    ProcessContext ctx;
    ctx.input = {channelPtrs_.data(), device_->info().buses.inputChannels > 0 ? channels : 0};
    ctx.output = {channelPtrs_.data(), channels};
    ctx.events = &events_;
    ctx.numSamples = fc.bufferNumSamples;
    tempoPos_.set(fc.editTime.getStart());
    const auto timeSig = tempoPos_.getTimeSignature();
    ctx.transport = {.tempoBpm = tempoPos_.getTempo(),
                     .ppqPosition = tempoPos_.getBeats().inBeats(),
                     .samplePosition = te::toSamples(fc.editTime.getStart(), sampleRate_),
                     .timeSigNumerator = timeSig.numerator,
                     .timeSigDenominator = timeSig.denominator,
                     .playing = fc.isPlaying};
    processRealtime(*device_, ctx);
}

void DevicePlugin::convertMidi(const te::PluginRenderContext& fc) noexcept
{
    events_.clear();
    const auto* midi = fc.bufferForMidiMessages;
    if (midi == nullptr || !device_->info().buses.acceptsMidi)
        return;

    if (midi->isAllNotesOff)
        events_.push({.sampleOffset = 0, .type = EventType::AllNotesOff});

    // Timestamps are seconds from the start of destBuffer (as tracktion's own synths read them);
    // events are clamped into this block so none is dropped by rounding.
    const int last = fc.bufferNumSamples - 1;
    for (const auto& m : *midi)
    {
        const int offset = juce::jlimit(0, juce::jmax(0, last),
                                        juce::roundToInt(m.getTimeStamp() * sampleRate_) - fc.bufferStartSample);
        if (const auto e = host::toEvent(m, offset))
            events_.push(*e);
    }
    events_.sortByTime();
}

void DevicePlugin::restorePluginStateFromValueTree(const juce::ValueTree& v)
{
    for (auto& value : values_)
        if (v.hasProperty(value->getPropertyID()))
            *value = static_cast<float>(v[value->getPropertyID()]);
    for (auto& p : params_)
        p->updateFromAttachedValue();

    if (v.hasProperty(kExtraStateId))
        applyExtraFromState(v[kExtraStateId].toString());
}

void DevicePlugin::flushPluginStateToValueTree()
{
    te::Plugin::flushPluginStateToValueTree();
    nlohmann::json extra = nlohmann::json::object();
    device_->writeExtraState(extra);
    if (!extra.empty())
    {
        appliedExtra_ = juce::String(extra.dump()); // already what the device holds
        state.setProperty(kExtraStateId, appliedExtra_, nullptr);
    }
}

void DevicePlugin::applyExtraState(const nlohmann::json& patch)
{
    nlohmann::json extra = nlohmann::json::object();
    device_->writeExtraState(extra);
    extra.merge_patch(patch);
    state.setProperty(kExtraStateId, juce::String(extra.dump()), getUndoManager()); // -> listener
}

void DevicePlugin::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == state && id == kExtraStateId) // edits, undo and redo
        applyExtraFromState(state.getProperty(kExtraStateId, "{}").toString());
    te::Plugin::valueTreePropertyChanged(tree, id);
}

void DevicePlugin::applyExtraFromState(const juce::String& json)
{
    if (json == appliedExtra_)
        return;
    appliedExtra_ = json;
    const auto parsed = nlohmann::json::parse(json.toStdString(), nullptr, false);
    device_->readExtraState(parsed.is_object() ? parsed : nlohmann::json::object());
}

void registerDevices(te::Engine& engine)
{
    for (const auto* factory : allDevices())
        engine.getPluginManager().registerBuiltInType(std::make_unique<DeviceBuiltInType>(*factory));
}
} // namespace fd::engine
