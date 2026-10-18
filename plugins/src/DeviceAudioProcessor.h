#pragma once

#include <array>
#include <atomic>
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
class DeviceAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    /// `presets`: the device's factory presets (ADR 0044).
    DeviceAudioProcessor(const DeviceFactory& factory, const ui::PanelFactory* panel,
                         std::span<const FactoryPresetFile> presets = {});
    ~DeviceAudioProcessor() override;

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

    // The host's program list: Default settings and the factory presets (presets.md §12).
    int getNumPrograms() override;
    int getCurrentProgram() override;
    /// Loads a program unless it's the one loaded (some hosts set the program again after a
    /// state load).
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    /// UI edit of non-parameter state (ADR 0017): applies it and tells the host.
    void applyExtraState(const nlohmann::json& patch);
    void setStateInformation(const void* data, int sizeInBytes) override;

    /// The editor panel's view state (ADR 0022): kept with the plugin state, outside the
    /// device state, so presets ignore it. Any thread.
    [[nodiscard]] std::string viewState() const;
    void setViewState(std::string state);
    /// Tells an open editor that a loaded state brought another view state (message thread).
    juce::ChangeBroadcaster viewStateLoaded;

    /// Presets (ADR 0044): the header's, kept beside the device state like the view. Message
    /// thread.
    [[nodiscard]] PresetHost& presets() noexcept;

    /// Whether the device listens to the host's sidechain input (ADR 0025): the header's
    /// Off/External picker, kept in the plugin state. Any thread.
    [[nodiscard]] bool sidechainExternal() const noexcept { return sidechainExternal_.load(); }
    void setSidechainExternal(bool external);

    // For the editor.
    [[nodiscard]] Device& device() noexcept { return *device_; }
    [[nodiscard]] const ui::PanelFactory* panelFactory() const noexcept { return panel_; }
    [[nodiscard]] juce::RangedAudioParameter& parameter(size_t index) const noexcept { return *params_[index]; }

    /// Re-reads the device's latency after a latency-changing setting moved (ADR 0020) and
    /// tells the host. Message thread; also polled by a timer.
    void updateLatency();

    /// A note held on the device from its editor (an editor's Preview, ADR 0053), velocity 0 to
    /// let it go: played from the next block on, beside the host's MIDI. Message thread; the
    /// audio thread takes it without locking.
    void playNote(int note, float velocity);

private:
    class Presets;
    friend class Presets;
    static BusesProperties busesFor(const DeviceInfo& info);
    /// The program list: Default settings, then the factory presets.
    [[nodiscard]] std::vector<const Preset*> programs();
    void pullParameters() noexcept;
    void timerCallback() override { updateLatency(); }

    std::unique_ptr<Device> device_;
    const ui::PanelFactory* panel_;
    juce::String name_;
    std::vector<juce::RangedAudioParameter*> params_; // owned by AudioProcessor
    std::vector<size_t> latencyParams_;               // ParamFlags::ChangesLatency (ADR 0020)
    EventList events_, outEvents_;
    // Notes from the editor, waiting for the audio thread (one writer, one reader).
    static constexpr int kQueuedNotes = 32;
    juce::AbstractFifo noteFifo_{kQueuedNotes};
    std::array<Event, kQueuedNotes> notes_{};
    std::vector<float*> channelPtrs_, sidechainPtrs_;
    std::atomic<bool> sidechainExternal_{false};
    mutable juce::CriticalSection viewLock_; // state calls can come from any thread
    std::string viewState_;
    std::string presetMemo_; // the PresetMemo's JSON, under viewLock_
    std::unique_ptr<Presets> presets_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceAudioProcessor)
};

/// Editor: the device's DevicePanel, the same one the DAW uses. Its size follows the panel:
/// collapsing a section asks the host for a narrower window. Wider than the screen, it scrolls.
class DeviceEditor final : public juce::AudioProcessorEditor, public juce::AsyncUpdater, private juce::ChangeListener
{
public:
    explicit DeviceEditor(DeviceAudioProcessor& processor);
    ~DeviceEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    [[nodiscard]] ui::DevicePanel& panel() noexcept { return *panel_; }
    /// How tall it is beside the panel while the preset list needs room below the header.
    [[nodiscard]] int popoverRoom() const noexcept { return popoverRoom_; }
    /// The module's editor while it's open (ADR 0053): under the panel, the window grown for it.
    [[nodiscard]] ui::ModuleEditor* moduleEditor() const noexcept { return moduleEditor_.get(); }
    [[nodiscard]] ui::PanelHeader& editorHeader() noexcept { return editorHeader_; }
    void closeModuleEditor();

private:
    class Binding;
    class Sidechain;
    void handleAsyncUpdate() override;                              // resize to the panel (and the editor)
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // a state load brought a view
    void showModuleEditor(std::unique_ptr<ui::ModuleEditor> editor);

    DeviceAudioProcessor& processor_;
    ui::LookAndFeel lnf_;
    std::unique_ptr<Binding> binding_;
    std::unique_ptr<Sidechain> sidechain_;
    std::unique_ptr<ui::TapFeed> notes_; // a MIDI effect's note display (no song in a plugin host)
    std::unique_ptr<ui::DeviceUiContext> context_;
    std::unique_ptr<ui::DevicePanel> panel_;
    juce::Viewport viewport_;
    int popoverRoom_ = 0; // px below the header the preset list asked for (0: none)
    ui::PanelHeader editorHeader_;
    std::unique_ptr<ui::ModuleEditor> moduleEditor_;
};
} // namespace fd::plugin
