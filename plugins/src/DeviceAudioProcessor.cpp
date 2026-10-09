#include "DeviceAudioProcessor.h"

#include <fd/device/Realtime.h>

namespace fd::plugin
{
namespace
{
constexpr int kEditorMargin = 8;

/// Typed text as UTF-8. The CLAP wrapper hands text over as a C string that JUCE reads as
/// Latin-1, so UTF-8 ("\u221E:1") arrives one character per byte; when those bytes form valid
/// UTF-8, that is what was meant.
std::string utf8Of(const juce::String& s)
{
    std::string bytes;
    for (auto p = s.getCharPointer(); !p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c > 0xFF)
            return s.toStdString();
        bytes.push_back(static_cast<char>(c));
    }
    if (juce::CharPointer_UTF8::isValidString(bytes.data(), static_cast<int>(bytes.size())))
        return bytes;
    return s.toStdString();
}

std::unique_ptr<juce::RangedAudioParameter> makeParameter(const ParamSpec& spec)
{
    const juce::ParameterID id{juce::String(std::string(spec.id)), spec.sinceVersion};
    const juce::String name(std::string(spec.name));
    auto toText = [&spec](float v, int) { return juce::String(spec.toText(v)); };
    auto fromText = [&spec](const juce::String& s) {
        const auto v = spec.fromText(utf8Of(s));
        return v ? *v : spec.def;
    };
    const bool automatable = spec.hasFlag(ParamFlags::Automatable);

    switch (spec.kind)
    {
    case ParamKind::Toggle:
        return std::make_unique<juce::AudioParameterBool>(
            id, name, spec.def >= 0.5f,
            juce::AudioParameterBoolAttributes().withAutomatable(automatable));
    case ParamKind::Choice:
    {
        juce::StringArray choices;
        for (auto c : spec.choices)
            choices.add(juce::String(std::string(c)));
        return std::make_unique<juce::AudioParameterChoice>(
            id, name, choices, static_cast<int>(spec.def),
            juce::AudioParameterChoiceAttributes().withAutomatable(automatable));
    }
    case ParamKind::Discrete:
        return std::make_unique<juce::AudioParameterInt>(
            id, name, static_cast<int>(spec.min), static_cast<int>(spec.max), static_cast<int>(spec.def),
            juce::AudioParameterIntAttributes()
                .withAutomatable(automatable)
                .withStringFromValueFunction([&spec](int v, int) { return juce::String(spec.toText(static_cast<float>(v))); })
                .withValueFromStringFunction([fromText](const juce::String& s) { return static_cast<int>(fromText(s)); }));
    case ParamKind::Continuous:
        break;
    }

    // Same mapping as ParamSpec::toNormalized/fromNormalized, so every host agrees.
    juce::NormalisableRange<float> range(
        spec.min, spec.max, [&spec](float, float, float n) { return spec.fromNormalized(n); },
        [&spec](float, float, float v) { return spec.toNormalized(v); },
        [&spec](float, float, float v) { return spec.sanitize(v); });
    return std::make_unique<juce::AudioParameterFloat>(
        id, name, range, spec.def,
        juce::AudioParameterFloatAttributes()
            .withAutomatable(automatable)
            .withStringFromValueFunction(toText)
            .withValueFromStringFunction(fromText));
}
} // namespace

juce::AudioProcessor::BusesProperties DeviceAudioProcessor::busesFor(const DeviceInfo& info)
{
    BusesProperties props;
    if (info.buses.inputChannels > 0)
        props = props.withInput("Input", juce::AudioChannelSet::canonicalChannelSet(info.buses.inputChannels), true);
    if (info.buses.outputChannels > 0)
        props = props.withOutput("Output", juce::AudioChannelSet::canonicalChannelSet(info.buses.outputChannels), true);
    return props;
}

DeviceAudioProcessor::DeviceAudioProcessor(const DeviceFactory& factory, const ui::PanelFactory* panel)
    : juce::AudioProcessor(busesFor(factory.info())), device_(factory.create()), panel_(panel),
      name_(std::string(factory.info().name))
{
    for (const auto& spec : device_->info().params)
    {
        if (spec.hasFlag(ParamFlags::ChangesLatency))
            latencyParams_.push_back(params_.size());
        auto p = makeParameter(spec);
        params_.push_back(p.get());
        addParameter(p.release());
    }
    if (!latencyParams_.empty())
        startTimer(100); // a latency-changing setting is a configuration change (ADR 0020)
}

void DeviceAudioProcessor::updateLatency()
{
    auto& store = device_->params();
    for (auto i : latencyParams_)
        store.setNormalized(i, params_[i]->getValue()); // the same value the audio thread pulls
    if (const int latency = device_->latencySamples(); latency != getLatencySamples())
        setLatencySamples(latency);
}

bool DeviceAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (device_->info().buses.inputChannels > 0)
        return layouts.getMainInputChannelSet() == out;
    return true;
}

void DeviceAudioProcessor::prepareToPlay(double sampleRate, int maxBlockSize)
{
    pullParameters();
    device_->prepare({.sampleRate = sampleRate, .maxBlockSize = maxBlockSize});
    events_.reserve(2048);
    channelPtrs_.resize(static_cast<size_t>(juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels())));
    setLatencySamples(device_->latencySamples());
}

void DeviceAudioProcessor::pullParameters() noexcept
{
    auto& store = device_->params();
    for (size_t i = 0; i < params_.size(); ++i)
        store.setNormalized(i, params_[i]->getValue());
}

void DeviceAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    pullParameters();

    events_.clear();
    host::appendMidi(midi, events_);
    if (!producesMidi())
        midi.clear();

    const int channels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channelPtrs_.size()));
    for (int c = 0; c < channels; ++c)
        channelPtrs_[static_cast<size_t>(c)] = buffer.getWritePointer(c);

    ProcessContext ctx;
    ctx.input = {channelPtrs_.data(), juce::jmin(channels, getTotalNumInputChannels())};
    ctx.output = {channelPtrs_.data(), juce::jmin(channels, getTotalNumOutputChannels())};
    ctx.events = &events_;
    ctx.numSamples = buffer.getNumSamples();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            ctx.transport.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
                ctx.transport.tempoBpm = *bpm;
            if (auto ppq = pos->getPpqPosition())
                ctx.transport.ppqPosition = *ppq;
            if (auto s = pos->getTimeInSamples())
                ctx.transport.samplePosition = *s;
        }

    processRealtime(*device_, ctx);
}

void DeviceAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    pullParameters();
    destData = host::stateToMemory(*device_, viewState());
}

std::string DeviceAudioProcessor::viewState() const
{
    const juce::ScopedLock lock(viewLock_);
    return viewState_;
}

void DeviceAudioProcessor::setViewState(std::string state)
{
    const juce::ScopedLock lock(viewLock_);
    viewState_ = std::move(state);
}

void DeviceAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (host::stateFromMemory(*device_, data, sizeInBytes) != ApplyResult::Ok)
        return;
    setViewState(host::viewStateFromMemory(data, sizeInBytes));
    viewStateLoaded.sendChangeMessage(); // asynchronous: the editor applies it on the message thread
    const auto& store = device_->params();
    for (size_t i = 0; i < params_.size(); ++i)
        params_[i]->setValueNotifyingHost(store.getNormalized(i));
    // Tell hosts that values changed wholesale (CLAP: params.rescan, required after a state load).
    updateHostDisplay(ChangeDetails{}.withParameterInfoChanged(true));
}

void DeviceAudioProcessor::applyExtraState(const nlohmann::json& patch)
{
    ui::mergeExtraState(*device_, patch);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

juce::AudioProcessorEditor* DeviceAudioProcessor::createEditor()
{
    return new DeviceEditor(*this);
}

// ---------------------------------------------------------------------------

class DeviceEditor::Binding final : public ui::ParamBinding
{
public:
    explicit Binding(DeviceAudioProcessor& p) : processor_(p) {}

    float getPlain(size_t i) const override
    {
        return processor_.device().info().params[i].fromNormalized(processor_.parameter(i).getValue());
    }
    void setPlain(size_t i, float plain) override
    {
        processor_.parameter(i).setValueNotifyingHost(processor_.device().info().params[i].toNormalized(plain));
    }
    void beginGesture(size_t i) override { processor_.parameter(i).beginChangeGesture(); }
    void endGesture(size_t i) override { processor_.parameter(i).endChangeGesture(); }
    void applyExtraState(const nlohmann::json& patch) override { processor_.applyExtraState(patch); }

private:
    DeviceAudioProcessor& processor_;
};

DeviceEditor::DeviceEditor(DeviceAudioProcessor& p) : juce::AudioProcessorEditor(p), processor_(p)
{
    setLookAndFeel(&lnf_);
    binding_ = std::make_unique<Binding>(p);
    context_ = std::make_unique<ui::DeviceUiContext>(
        ui::DeviceUiContext{p.device().info(), *binding_, &p.device().analysis(), &p.device()});
    panel_ = p.panelFactory()->create(*context_);
    panel_->setViewState(p.viewState());
    panel_->onViewChanged = [this] {
        processor_.setViewState(panel_->viewState());
        triggerAsyncUpdate(); // resize outside the mouse event (hosts resize synchronously)
    };
    viewport_.setViewedComponent(panel_.get(), false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(6);
    addAndMakeVisible(viewport_);
    p.viewStateLoaded.addChangeListener(this);
    handleAsyncUpdate();
}

DeviceEditor::~DeviceEditor()
{
    processor_.viewStateLoaded.removeChangeListener(this);
    viewport_.setViewedComponent(nullptr, false);
    panel_.reset();
    setLookAndFeel(nullptr);
}

void DeviceEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    panel_->setViewState(processor_.viewState());
    handleAsyncUpdate();
}

void DeviceEditor::handleAsyncUpdate()
{
    // The panel's width, but never wider than the screen it is on (it scrolls instead).
    const int panelWidth = panel_->preferredWidth();
    int width = panelWidth + 2 * kEditorMargin;
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = isOnDesktop() ? displays.getDisplayForRect(getScreenBounds()) : displays.getPrimaryDisplay();
    if (display != nullptr)
        width = std::min(width, static_cast<int>(display->userBounds.getWidth()) - 32);
    const bool scrolls = width < panelWidth + 2 * kEditorMargin;
    const int height = ui::Theme::current().devicePanelHeight + 2 * kEditorMargin + (scrolls ? 8 : 0);
    if (getWidth() != width || getHeight() != height)
        setSize(width, height);
    else
        resized();
}

void DeviceEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface1);
}

void DeviceEditor::resized()
{
    // Laid out from the left at whatever size the host granted.
    viewport_.setBounds(getLocalBounds().reduced(kEditorMargin, 0).withTrimmedTop(kEditorMargin));
    panel_->setSize(panel_->preferredWidth(), ui::Theme::current().devicePanelHeight);
}
} // namespace fd::plugin
