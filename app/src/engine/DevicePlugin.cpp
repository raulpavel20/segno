#include "DevicePlugin.h"

#include "Modulator.h"
#include "PadInlet.h"
#include "SongKey.h"
#include "SpecParameter.h"

#include <fd/device/Realtime.h>
#include <fd/device/State.h>
#include <fd_host_juce/fd_host_juce.h>
#include <set>

namespace fd::engine
{
namespace
{
const juce::Identifier kExtraStateId{"fdExtra"};
// Parameter values in the plugin's state are normalised (ADR 0028). Projects saved before then
// hold plain values, converted when the module is loaded.
const juce::Identifier kNormalisedId{"fdNormalised"};
// The track a MIDI effect listens to (midi-effects.md §5.3), as its item id.
const juce::Identifier& kListenId = DevicePlugin::listenId;

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

/// Keeps the module's copy of the song's key (ADR 0057): every change to the pitch sequence,
/// undo and redo included, publishes it again.
struct DevicePlugin::KeyWatch final : juce::ValueTree::Listener
{
    explicit KeyWatch(DevicePlugin& p) : plugin(p), state(songKeyState(p.edit))
    {
        state.addListener(this);
        update();
    }
    ~KeyWatch() override { state.removeListener(this); }

    void update()
    {
        plugin.songKey_.store(packKey(keyInfoOf(songKeyOf(plugin.edit))), std::memory_order_relaxed);
    }
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { update(); }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { update(); }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { update(); }
    void valueTreeRedirected(juce::ValueTree&) override { update(); }

    DevicePlugin& plugin;
    juce::ValueTree state;
};

juce::String DevicePlugin::typeNameFor(std::string_view deviceId)
{
    return "fd." + juce::String(std::string(deviceId));
}

namespace
{
/// What tracktion keeps on every plugin's state (te::Plugin, the plugin list and manager): a
/// parameter of ours of the same id would overwrite it.
bool reservedProperty(std::string_view id)
{
    static const std::set<std::string_view> reserved{
        "type",    "id",           "enabled",           "process",
        "frozen",  "customName",   "quickParamName",    "windowX",
        "windowY", "windowLocked", "sidechainSourceID", "masterPluginID"};
    return reserved.contains(id);
}
constexpr std::string_view kParamPrefix = "fdParam.";
} // namespace

juce::Identifier DevicePlugin::propertyFor(std::string_view paramId)
{
    return juce::String(reservedProperty(paramId) ? std::string(kParamPrefix) + std::string(paramId)
                                                  : std::string(paramId));
}

juce::String DevicePlugin::paramIdOf(const juce::Identifier& property)
{
    const auto name = property.toString();
    const juce::String prefix{std::string(kParamPrefix)};
    return name.startsWith(prefix) && reservedProperty(name.substring(prefix.length()).toStdString())
               ? name.substring(prefix.length())
               : name;
}

DevicePlugin::DevicePlugin(te::PluginCreationInfo info, const DeviceFactory& factory)
    : te::Plugin(info), device_(factory.create()), name_(std::string(factory.info().name)),
      type_(typeNameFor(factory.info().id))
{
    auto* um = getUndoManager();
    // A module saved before ADR 0028 holds plain values: convert them once (not an edit).
    if (!static_cast<bool>(state.getProperty(kNormalisedId, false)))
    {
        for (const auto& spec : device_->info().params)
            if (const auto id = propertyFor(spec.id); state.hasProperty(id))
                state.setProperty(id, spec.toNormalized(static_cast<float>(static_cast<double>(state[id]))), nullptr);
        state.setProperty(kNormalisedId, true, nullptr);
    }
    for (const auto& spec : device_->info().params)
    {
        // How you listen (ParamFlags::Transient) starts from its default, and never makes an
        // undo step.
        const auto id = propertyFor(spec.id);
        const bool transient = spec.hasFlag(ParamFlags::Transient);
        if (transient)
            state.removeProperty(id, nullptr);
        // Every other value is stored explicitly (its default too), so undo restores a value rather
        // than removing one, and following it afterwards writes nothing new (it would clear redo).
        else if (!state.hasProperty(id))
            state.setProperty(id, spec.toNormalized(spec.def), nullptr);
        auto value = std::make_unique<juce::CachedValue<float>>();
        value->referTo(state, id, transient ? nullptr : um, spec.toNormalized(spec.def));
        auto* param = new SpecParameter(spec, *this);
        addAutomatableParameter(*param);
        param->attachToCurrentValue(*value);
        params_.emplace_back(param);
        values_.push_back(std::move(value));
    }
    pulled_.assign(params_.size(), -1.0f);

    // A device recreated from a saved project gets its non-parameter state back (ADR 0017).
    if (state.hasProperty(kExtraStateId))
        applyExtraFromState(state[kExtraStateId].toString());

    keyWatch_ = std::make_unique<KeyWatch>(*this);
    listening_.store(getNoteSourceID().isValid(), std::memory_order_relaxed);
}

DevicePlugin::~DevicePlugin()
{
    keyWatch_.reset();
    notifyListenersOfDeletion();
    for (auto& p : params_)
        p->detachFromCurrentValue();
}

void DevicePlugin::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate_ = info.sampleRate;
    const auto specs = device_->info().params;
    for (size_t i = 0; i < params_.size(); ++i)
    {
        pulled_[i] = params_[i]->getCurrentValue();
        device_->params().set(i, specs[i].fromNormalized(pulled_[i]));
    }
    device_->prepare({.sampleRate = info.sampleRate, .maxBlockSize = info.blockSizeSamples});
    events_.reserve(2048);
    outEvents_.reserve(2048);
    listen_.reserve(1024);
    heard_.resize(1024);
    heardCount_ = 0;
    channelPtrs_.resize(8);
    sidechainPtrs_.resize(2);
    // Instruments render here and are added to the track's audio (ADR 0019).
    if (isInstrument())
        instrumentOut_.setSize(2, std::max(info.blockSizeSamples, 8192));
}

void DevicePlugin::reset()
{
    device_->reset();
}

te::Plugin::BusLayout DevicePlugin::getBusses() const
{
    const auto& buses = device_->info().buses;
    // A MIDI effect passes the track's audio by untouched (ADR 0074).
    if (isMidiEffect())
        return BusLayout::singleStereoInOut();
    if (buses.inputChannels == 0)
        return BusLayout::singleInOut(te::ChannelConfiguration::none(), te::ChannelConfiguration::stereo());
    auto layout = BusLayout::singleStereoInOut();
    if (buses.sidechainChannels > 0)
        layout.inputs.push_back(te::ChannelConfiguration::stereo());
    return layout;
}

void DevicePlugin::getChannelNames(juce::StringArray* ins, juce::StringArray* outs)
{
    te::Plugin::getChannelNames(ins, outs);
    if (ins != nullptr && device_->info().buses.inputChannels > 0 && device_->info().buses.sidechainChannels > 0)
        ins->addArray({"Sidechain Left", "Sidechain Right"});
}

double DevicePlugin::getLatencySeconds()
{
    return sampleRate_ > 0.0 ? device_->latencySamples() / sampleRate_ : 0.0;
}

void DevicePlugin::applyToBuffer(const te::PluginRenderContext& fc)
{
    if (!isEnabled() || (fc.destBuffer == nullptr && !isMidiEffect()))
        return;

    auto& store = device_->params();
    const auto specs = device_->info().params;
    for (size_t i = 0; i < params_.size(); ++i)
        if (const float n = params_[i]->getCurrentValue(); n != pulled_[i]) // includes automation and modifiers
        {
            pulled_[i] = n;
            store.set(i, specs[i].fromNormalized(n));
        }

    if (isMidiEffect())
    {
        processNotes(fc);
        return;
    }

    // With a sidechain source, tracktion hands over the main channels and then the source's
    // (ADR 0025); otherwise just the main ones.
    const bool sidechained = getSidechainSourceID().isValid() && device_->info().buses.sidechainChannels > 0 &&
                             fc.destBuffer->getNumChannels() >= 4;
    const int channels =
        juce::jmin(fc.destBuffer->getNumChannels(), sidechained ? 2 : static_cast<int>(channelPtrs_.size()));
    // An instrument adds its sound to the track's audio rather than replacing it (ADR 0019):
    // it renders into a scratch buffer that is mixed in afterwards.
    const bool adds = isInstrument() && fc.bufferNumSamples <= instrumentOut_.getNumSamples();
    jassert(!isInstrument() || adds); // blocks are never longer than prepared
    for (int c = 0; c < channels; ++c)
        channelPtrs_[static_cast<size_t>(c)] = adds ? instrumentOut_.getWritePointer(std::min(c, 1))
                                                    : fc.destBuffer->getWritePointer(c, fc.bufferStartSample);

    convertMidi(fc);

    ProcessContext ctx;
    ctx.input = {channelPtrs_.data(), device_->info().buses.inputChannels > 0 ? channels : 0};
    if (sidechained)
    {
        for (int c = 0; c < 2; ++c)
            sidechainPtrs_[static_cast<size_t>(c)] = fc.destBuffer->getWritePointer(2 + c, fc.bufferStartSample);
        ctx.sidechain = {sidechainPtrs_.data(), 2};
    }
    ctx.output = {channelPtrs_.data(), std::min(channels, adds ? 2 : channels)};
    ctx.events = &events_;
    ctx.numSamples = fc.bufferNumSamples;
    tempoPos_.set(fc.editTime.getStart());
    const auto timeSig = tempoPos_.getTimeSignature();
    ctx.transport = {.tempoBpm = tempoPos_.getTempo(),
                     .ppqPosition = tempoPos_.getBeats().inBeats(),
                     .samplePosition = te::toSamples(fc.editTime.getStart(), sampleRate_),
                     .timeSigNumerator = timeSig.numerator,
                     .timeSigDenominator = timeSig.denominator,
                     .playing = fc.isPlaying,
                     .key = unpackKey(songKey_.load(std::memory_order_relaxed))};
    processRealtime(*device_, ctx);

    if (adds)
        for (int c = 0; c < channels; ++c)
            fc.destBuffer->addFrom(c, fc.bufferStartSample, instrumentOut_, std::min(c, ctx.output.numChannels - 1), 0,
                                   fc.bufferNumSamples);
}

te::EditItemID DevicePlugin::getNoteSourceID() const
{
    return isMidiEffect() ? te::EditItemID::fromVar(state.getProperty(kListenId)) : te::EditItemID();
}

void DevicePlugin::setListenTo(te::EditItemID track, juce::UndoManager* um)
{
    if (track.isValid())
        state.setProperty(kListenId, track.toVar(), um);
    else
        state.removeProperty(kListenId, um);
    // valueTreePropertyChanged follows (edits, undo and redo): the flag, and playback rebuilt so
    // the listened track sends its notes.
}

void DevicePlugin::setSourceNotes(const te::MidiMessageArray& notes, te::TimeRange block)
{
    heardCount_ = 0;
    const double start = block.getStart().inSeconds();
    for (const auto& m : notes)
    {
        if (heardCount_ >= heard_.size())
            break;
        if (const auto e = host::toEvent(m, 0))
            heard_[heardCount_++] = {.time = start + m.getTimeStamp(), .event = *e};
    }
}

void DevicePlugin::processNotes(const te::PluginRenderContext& fc) noexcept
{
    convertMidi(fc);
    // The song stopping ends what the module started (its ledger), as a jump does.
    if (wasPlaying_ && !fc.isPlaying)
    {
        events_.push({.sampleOffset = 0, .type = EventType::AllNotesOff});
        events_.sortByTime();
    }
    wasPlaying_ = fc.isPlaying;

    // The song: the newest view handed over, kept until the next.
    if (auto fresh = songView_.takeNew(); fresh.isSet())
    {
        if (song_.isSet())
            songView_.retire(song_);
        song_ = fresh;
    }

    // The listened track's notes in this piece of the block (tracktion may split a block when a
    // parameter moves); a stopped transport's block has no length, and takes them all.
    listen_.clear();
    const double t0 = fc.editTime.getStart().inSeconds(), t1 = fc.editTime.getEnd().inSeconds();
    const int last = std::max(0, fc.bufferNumSamples - 1);
    for (std::size_t i = 0; i < heardCount_; ++i)
        if (const auto& h = heard_[i]; t1 <= t0 || (h.time >= t0 && h.time < t1))
        {
            auto e = h.event;
            e.sampleOffset = juce::jlimit(0, last, juce::roundToInt((h.time - t0) * sampleRate_));
            listen_.push(e);
        }

    ProcessContext ctx;
    ctx.events = &events_;
    outEvents_.clear();
    ctx.outEvents = &outEvents_;
    ctx.numSamples = fc.bufferNumSamples;
    tempoPos_.set(fc.editTime.getStart());
    const auto timeSig = tempoPos_.getTimeSignature();
    ctx.transport = {.tempoBpm = tempoPos_.getTempo(),
                     .ppqPosition = tempoPos_.getBeats().inBeats(),
                     .samplePosition = te::toSamples(fc.editTime.getStart(), sampleRate_),
                     .timeSigNumerator = timeSig.numerator,
                     .timeSigDenominator = timeSig.denominator,
                     .playing = fc.isPlaying,
                     .key = unpackKey(songKey_.load(std::memory_order_relaxed))};
    ctx.song = song_.get();
    ctx.listen = listening_.load(std::memory_order_relaxed) ? &listen_ : nullptr;
    processRealtime(*device_, ctx);

    // Its notes replace the ones it was given; a jump's "all notes off" goes on as it came.
    auto* midi = fc.bufferForMidiMessages;
    if (midi == nullptr)
        return;
    const bool allNotesOff = midi->isAllNotesOff;
    midi->clear();
    midi->isAllNotesOff = allNotesOff;
    outEvents_.sortByTime();
    for (const auto& e : outEvents_)
        midi->addMidiMessage(host::toMidiMessage(e), (e.sampleOffset + fc.bufferStartSample) / sampleRate_,
                             midiSourceID_);
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
    const bool normalised = v.getProperty(kNormalisedId, false);
    const auto specs = device_->info().params;
    for (size_t i = 0; i < values_.size(); ++i)
        if (auto& value = *values_[i]; v.hasProperty(value.getPropertyID()))
        {
            const auto stored = static_cast<float>(static_cast<double>(v[value.getPropertyID()]));
            value = normalised ? stored : specs[i].toNormalized(stored);
        }
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

void DevicePlugin::followStoredValues()
{
    for (size_t i = 0; i < params_.size(); ++i)
        if (params_[i]->getCurrentBaseValue() != values_[i]->get())
            params_[i]->updateFromAttachedValue();
}

void DevicePlugin::setStoredValue(size_t index, float normalised)
{
    values_[index]->setValue(normalised, getUndoManager());
    params_[index]->updateFromAttachedValue();
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
    if (tree == state && id == te::IDs::sidechainSourceID) // a sidechain source came or went (ADR 0025)
        edit.restartPlayback();
    if (tree == state && id == kListenId) // what a MIDI effect listens to (midi-effects.md §5.3)
    {
        listening_.store(getNoteSourceID().isValid(), std::memory_order_relaxed);
        edit.restartPlayback();
    }
    if (tree == state)
        for (size_t i = 0; i < device_->info().params.size(); ++i)
            if (const auto& spec = device_->info().params[i];
                spec.hasFlag(ParamFlags::ChangesLatency) && id.toString() == juce::String(std::string(spec.id)))
            {
                // A latency-changing setting (ADR 0020): the device learns it now, and the
                // playback graph is rebuilt around its new latency. Read from the tree: on undo
                // the cached value may not have caught up yet.
                device_->params().set(
                    i, spec.fromNormalized(static_cast<float>(static_cast<double>(tree.getProperty(id, spec.toNormalized(spec.def))))));
                edit.restartPlayback();
            }
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

KeyInfo DevicePlugin::songKey() const noexcept
{
    return unpackKey(songKey_.load(std::memory_order_relaxed));
}

void registerDevices(te::Engine& engine)
{
    for (const auto* factory : allDevices())
        registerDevice(engine, *factory);
    Modulator::registerType(); // the modulators: modifiers of our own type (modulators.md §22)
    PadInlet::registerType(engine); // a Drum Kit pad's first plugin (drum-kit.md §15)
}

void registerDevice(te::Engine& engine, const DeviceFactory& factory)
{
    engine.getPluginManager().registerBuiltInType(std::make_unique<DeviceBuiltInType>(factory));
}
} // namespace fd::engine
