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
/// Every preset this plugin offers (ADR 0044): its module's factory set and yours, in the folder
/// the DAW reads too. One per plugin binary, shared by its instances.
PresetLibrary& libraryOf(std::span<const FactoryPresetFile> factory)
{
    static PresetLibrary presets(
        factory, std::filesystem::path(ui::defaultPresetsFolder().getFullPathName().toStdString()));
    return presets;
}
} // namespace

/// The plugin's side of presets: the state through the host's parameters (each load a gesture,
/// so hosts record it as a hand on the controls), the memo beside the device state. Plugins have
/// no undo of their own: Escape after browsing puts back the state from before it.
class DeviceAudioProcessor::Presets final : public PresetHost
{
public:
    Presets(DeviceAudioProcessor& p, std::span<const FactoryPresetFile> factory) : p_(p), factory_(factory) {}

    PresetLibrary& library() override { return libraryOf(factory_); }

    DeviceState state() const override
    {
        const auto& info = p_.device_->info();
        DeviceState s;
        s.deviceId = std::string(info.id);
        s.stateVersion = info.stateVersion;
        for (size_t i = 0; i < info.params.size(); ++i)
            if (!info.params[i].hasFlag(ParamFlags::Transient))
                s.params[std::string(info.params[i].id)] =
                    info.params[i].fromNormalized(p_.params_[i]->getValue());
        p_.device_->writeExtraState(s.extra);
        return s;
    }

    DeviceState migrated(DeviceState s) const override
    {
        if (s.stateVersion < p_.device_->info().stateVersion)
            p_.device_->migrateState(s);
        return s;
    }

    PresetMemo memo() const override
    {
        const juce::ScopedLock lock(p_.viewLock_);
        return p_.presetMemo_.empty()
                   ? PresetMemo{}
                   : PresetMemo::fromJson(nlohmann::json::parse(p_.presetMemo_, nullptr, false));
    }

    void apply(const DeviceState& state, const PresetMemo& memo, const std::string&) override
    {
        const auto& info = p_.device_->info();
        for (size_t i = 0; i < info.params.size(); ++i)
        {
            const auto& spec = info.params[i];
            const auto it = state.params.find(std::string(spec.id));
            const float normalised =
                spec.toNormalized(spec.sanitize(it != state.params.end() ? it->second : spec.def));
            if (auto* param = p_.params_[i]; std::abs(param->getValue() - normalised) > 1e-7f)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(normalised);
                param->endChangeGesture();
            }
        }
        nlohmann::json extra = nlohmann::json::object();
        p_.device_->writeExtraState(extra);
        if (const auto patch = extraPatch(info, extra, state.extra); !patch.empty())
            p_.applyExtraState(patch);
        setMemo(memo);
    }

    void setMemo(const PresetMemo& memo) override
    {
        {
            const juce::ScopedLock lock(p_.viewLock_);
            p_.presetMemo_ = memo.empty() ? std::string() : memo.toJson().dump();
        }
        p_.updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true).withProgramChanged(true));
    }

    void beginBrowsing() override
    {
        before_ = state();
        memoBefore_ = memo();
    }

    void endBrowsing(bool keep) override
    {
        if (auto before = std::exchange(before_, std::nullopt); before && !keep)
            apply(*before, memoBefore_, {});
    }

    bool trash(const std::filesystem::path& file) override
    {
        return juce::File(juce::String::fromUTF8(file.string().c_str())).moveToTrash();
    }

    void reveal(const std::filesystem::path& folder) override
    {
        const juce::File f(juce::String::fromUTF8(folder.string().c_str()));
        if (!f.isDirectory())
            f.createDirectory();
        f.startAsProcess();
    }

private:
    DeviceAudioProcessor& p_;
    std::span<const FactoryPresetFile> factory_;
    std::optional<DeviceState> before_;
    PresetMemo memoBefore_;
};

juce::AudioProcessor::BusesProperties DeviceAudioProcessor::busesFor(const DeviceInfo& info)
{
    BusesProperties props;
    if (info.buses.inputChannels > 0)
        props = props.withInput("Input", juce::AudioChannelSet::canonicalChannelSet(info.buses.inputChannels), true);
    if (info.buses.outputChannels > 0)
        props = props.withOutput("Output", juce::AudioChannelSet::canonicalChannelSet(info.buses.outputChannels), true);
    // An auxiliary input, inactive until the host routes something to it (ADR 0025).
    if (info.buses.sidechainChannels > 0)
        props = props.withInput("Sidechain", juce::AudioChannelSet::canonicalChannelSet(info.buses.sidechainChannels),
                                false);
    return props;
}

DeviceAudioProcessor::DeviceAudioProcessor(const DeviceFactory& factory, const ui::PanelFactory* panel,
                                           std::span<const FactoryPresetFile> presets)
    : juce::AudioProcessor(busesFor(factory.info())), device_(factory.create()), panel_(panel),
      name_(std::string(factory.info().name)), presets_(std::make_unique<Presets>(*this, presets))
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
    // A new instance starts from your default, if you set one (a host's saved state replaces it).
    if (const auto start = PresetController::startingPoint(device_->info(), presets_->library()))
        presets_->apply(presets_->migrated(start->first), start->second, {});
}

DeviceAudioProcessor::~DeviceAudioProcessor() = default;

PresetHost& DeviceAudioProcessor::presets() noexcept
{
    return *presets_;
}

std::vector<const Preset*> DeviceAudioProcessor::programs()
{
    auto all = presets_->library().presetsFor(device_->info());
    std::erase_if(all, [](const Preset* p)
                  { return p->source != Preset::Source::Builtin && p->source != Preset::Source::Factory; });
    return all;
}

int DeviceAudioProcessor::getNumPrograms()
{
    return static_cast<int>(programs().size());
}

int DeviceAudioProcessor::getCurrentProgram()
{
    const auto key = presets_->memo().preset;
    const auto list = programs();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->key() == key)
            return static_cast<int>(i);
    return 0;
}

void DeviceAudioProcessor::setCurrentProgram(int index)
{
    const auto list = programs();
    if (index < 0 || index >= static_cast<int>(list.size()) || index == getCurrentProgram())
        return;
    PresetController(device_->info(), *presets_).load(*list[static_cast<size_t>(index)]);
}

const juce::String DeviceAudioProcessor::getProgramName(int index)
{
    const auto list = programs();
    return index >= 0 && index < static_cast<int>(list.size())
               ? juce::String::fromUTF8(list[static_cast<size_t>(index)]->name.c_str())
               : juce::String();
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
    if (device_->info().buses.inputChannels > 0 && layouts.getMainInputChannelSet() != out)
        return false;
    if (layouts.inputBuses.size() > 1) // the sidechain: off, mono or stereo
    {
        const auto& sc = layouts.inputBuses.getReference(1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void DeviceAudioProcessor::prepareToPlay(double sampleRate, int maxBlockSize)
{
    pullParameters();
    device_->prepare({.sampleRate = sampleRate, .maxBlockSize = maxBlockSize});
    events_.reserve(2048);
    channelPtrs_.resize(static_cast<size_t>(juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels())));
    sidechainPtrs_.resize(static_cast<size_t>(std::max(1, device_->info().buses.sidechainChannels)));
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
    // Notes from the editor's Preview, at the block's start.
    if (const auto queued = noteFifo_.read(noteFifo_.getNumReady());
        queued.blockSize1 + queued.blockSize2 > 0)
    {
        queued.forEach([this](int i) { events_.push(notes_[static_cast<size_t>(i)]); });
        events_.sortByTime();
    }
    if (!producesMidi())
        midi.clear();

    const int channels = juce::jmin(buffer.getNumChannels(), static_cast<int>(channelPtrs_.size()));
    for (int c = 0; c < channels; ++c)
        channelPtrs_[static_cast<size_t>(c)] = buffer.getWritePointer(c);

    ProcessContext ctx;
    ctx.input = {channelPtrs_.data(), juce::jmin(channels, getMainBusNumInputChannels())};
    ctx.output = {channelPtrs_.data(), juce::jmin(channels, getMainBusNumOutputChannels())};
    // The sidechain, when the picker says External and the host provides the input (ADR 0025).
    if (sidechainExternal_.load(std::memory_order_relaxed) && getBusCount(true) > 1)
        if (const auto* bus = getBus(true, 1); bus != nullptr && bus->isEnabled())
        {
            const int first = bus->getChannelIndexInProcessBlockBuffer(0);
            const int n = juce::jmin(bus->getNumberOfChannels(), static_cast<int>(sidechainPtrs_.size()),
                                     buffer.getNumChannels() - first);
            for (int c = 0; c < n; ++c)
                sidechainPtrs_[static_cast<size_t>(c)] = buffer.getWritePointer(first + c);
            ctx.sidechain = {sidechainPtrs_.data(), juce::jmax(0, n)};
        }
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
    std::string memo;
    {
        const juce::ScopedLock lock(viewLock_);
        memo = presetMemo_;
    }
    destData = host::stateToMemory(
        *device_, {.view = viewState(), .sidechainExternal = sidechainExternal(), .preset = memo});
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
    const auto extras = host::extrasFromMemory(data, sizeInBytes);
    setViewState(extras.view);
    {
        const juce::ScopedLock lock(viewLock_);
        presetMemo_ = extras.preset;
    }
    sidechainExternal_.store(extras.sidechainExternal);
    viewStateLoaded.sendChangeMessage(); // asynchronous: the editor applies it on the message thread
    const auto& store = device_->params();
    for (size_t i = 0; i < params_.size(); ++i)
        params_[i]->setValueNotifyingHost(store.getNormalized(i));
    // Tell hosts that values changed wholesale (CLAP: params.rescan, required after a state load).
    updateHostDisplay(ChangeDetails{}.withParameterInfoChanged(true));
}

void DeviceAudioProcessor::setSidechainExternal(bool external)
{
    if (sidechainExternal_.exchange(external) != external)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); // the project changed
}

void DeviceAudioProcessor::playNote(int note, float velocity)
{
    Event e;
    e.type = velocity > 0.0f ? EventType::NoteOn : EventType::NoteOff;
    e.key = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
    e.value = std::clamp(velocity, 0.0f, 1.0f);
    noteFifo_.write(1).forEach([this, &e](int i) { notes_[static_cast<size_t>(i)] = e; });
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

/// The header's sidechain picker in a plugin: Off, or the host's auxiliary input (ADR 0025).
class DeviceEditor::Sidechain final : public ui::SidechainRouting
{
public:
    explicit Sidechain(DeviceAudioProcessor& p) : processor_(p) {}

    std::vector<std::string> sources() const override { return {"Off", "External"}; }
    int selected() const override { return processor_.sidechainExternal() ? 1 : 0; }
    void select(int index) override { processor_.setSidechainExternal(index == 1); }

private:
    DeviceAudioProcessor& processor_;
};

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
    sidechain_ = std::make_unique<Sidechain>(p);
    context_ = std::make_unique<ui::DeviceUiContext>(
        ui::DeviceUiContext{p.device().info(), *binding_, &p.device().analysis(), &p.device(),
                            sidechain_.get(), &p.presets(), [this](int height)
                            {
                                // The preset list needs room below the header: the window grows
                                // while it's open.
                                popoverRoom_ = height;
                                handleAsyncUpdate();
                            }});
    // The module's editor (ADR 0053): below the panel, the window grown for it; its Preview
    // held on the device.
    context_->showEditor = [this](std::unique_ptr<ui::ModuleEditor> editor)
    { showModuleEditor(std::move(editor)); };
    context_->playNote = [this](int note, float velocity) { processor_.playNote(note, velocity); };
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
    closeModuleEditor(); // before the panel and the context it edits through
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

void DeviceEditor::showModuleEditor(std::unique_ptr<ui::ModuleEditor> editor)
{
    closeModuleEditor();
    moduleEditor_ = std::move(editor);
    editorHeader_.setTitle(moduleEditor_->tabName());
    editorHeader_.setContext(moduleEditor_->title());
    for (auto* c : moduleEditor_->commands())
        editorHeader_.addCommand(*c);
    moduleEditor_->onTitleChanged = [this] { editorHeader_.setContext(moduleEditor_->title()); };
    moduleEditor_->onDone = [this]
    {
        closeModuleEditor();
        triggerAsyncUpdate(); // shrink back, outside the click
    };
    addAndMakeVisible(editorHeader_);
    addAndMakeVisible(*moduleEditor_);
    triggerAsyncUpdate(); // grow, outside the mouse event (hosts resize synchronously)
}

void DeviceEditor::closeModuleEditor()
{
    if (moduleEditor_ == nullptr)
        return;
    moduleEditor_->closing();
    for (auto* c : moduleEditor_->commands())
        editorHeader_.removeCommand(*c);
    removeChildComponent(&editorHeader_);
    const auto closing = std::move(moduleEditor_);
    removeChildComponent(closing.get());
}

void DeviceEditor::handleAsyncUpdate()
{
    // The panel's width, but never wider than the screen it is on (it scrolls instead).
    const int panelWidth =
        std::max(panel_->preferredWidth(), moduleEditor_ != nullptr ? moduleEditor_->minimumWidth() : 0);
    int width = panelWidth + 2 * kEditorMargin;
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = isOnDesktop() ? displays.getDisplayForRect(getScreenBounds()) : displays.getPrimaryDisplay();
    if (display != nullptr)
        width = std::min(width, static_cast<int>(display->userBounds.getWidth()) - 32);
    const bool scrolls = width < panelWidth + 2 * kEditorMargin;
    const auto& t = ui::Theme::current();
    // Under the panel, the module's editor: its header and its least height.
    const int below = moduleEditor_ != nullptr
                          ? t.shell.panelHeaderHeight + moduleEditor_->minimumHeight() + kEditorMargin
                          : 0;
    const int height =
        std::max(t.devicePanelHeight + 2 * kEditorMargin + (scrolls ? 8 : 0) + below,
                 popoverRoom_ > 0 ? kEditorMargin + t.deviceHeaderHeight + popoverRoom_ + kEditorMargin : 0);
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
    const auto& t = ui::Theme::current();
    auto r = getLocalBounds().reduced(kEditorMargin, 0).withTrimmedTop(kEditorMargin);
    if (moduleEditor_ != nullptr)
    {
        // The editor's header and the editor at the bottom, the panel above them.
        auto below =
            r.removeFromBottom(t.shell.panelHeaderHeight + moduleEditor_->minimumHeight() + kEditorMargin);
        below.removeFromBottom(kEditorMargin);
        // The header across the window, as the DAW's detail panel's is across the panel.
        editorHeader_.setBounds(
            below.removeFromTop(t.shell.panelHeaderHeight).withX(0).withWidth(getWidth()));
        moduleEditor_->setBounds(below);
    }
    viewport_.setBounds(r);
    panel_->setSize(panel_->preferredWidth(), t.devicePanelHeight);
}
} // namespace fd::plugin
